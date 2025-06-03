#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <math.h>
#include <sys/time.h>


#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_THREAD_LOCALS
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#include "stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include "turbojpeg.h"

#include "image_utils.h"
#include "file_utils.h"

static const char* filter_image_names[] = {
    "jpg",
    "jpeg",
    "JPG",
    "JPEG",
    "png",
    "PNG",
    "data",
    NULL
};

static const char* subsampName[TJ_NUMSAMP] = {"4:4:4", "4:2:2", "4:2:0", "Grayscale", "4:4:0", "4:1:1"};

static const char* colorspaceName[TJ_NUMCS] = {"RGB", "YCbCr", "GRAY", "CMYK", "YCCK"};

static int image_file_filter(const struct dirent *entry)
{
    const char ** filter;

    for (filter = filter_image_names; *filter; ++filter) {
        if(strstr(entry->d_name, *filter) != NULL) {
            return 1;
        }
    }
    return 0;
}








static int write_image_jpeg(const char* path, int quality, const image_buffer_t* image)
{
    int ret;
    int jpegSubsamp = TJSAMP_422;
    unsigned char* jpegBuf = NULL;
    unsigned long jpegSize = 0;
    int flags = 0;

    const unsigned char* data = image->virt_addr;
    int width = image->width;
    int height = image->height;
    int pixelFormat = TJPF_RGB;

	tjhandle handle = tjInitCompress();

    if (image->format == IMAGE_FORMAT_RGB888) {
        ret = tjCompress2(handle, data, width, 0, height, pixelFormat, &jpegBuf, &jpegSize, jpegSubsamp, quality, flags);
    } else {
        printf("write_image_jpeg: pixel format %d not support\n", image->format);
        return -1;
    }

    if (jpegBuf != NULL && jpegSize > 0) {
        write_data_to_file(path, (const char*)jpegBuf, jpegSize);
        tjFree(jpegBuf);
    }
    tjDestroy(handle);

	return 0;
}



static int read_image_stb(const char* path, image_buffer_t* image)  
{  
    int w, h, c;  
    unsigned char* pixeldata = stbi_load(path, &w, &h, &c, STBI_default);  
    if (!pixeldata) {  
        printf("error: read image %s fail\n", path);  
        return -1;  
    }  
  
    int size = w * h * c;  
  
    // 设置图像数据  
    if (image->virt_addr != NULL) {  
        memcpy(image->virt_addr, pixeldata, size);  
        stbi_image_free(pixeldata);  
    } else {  
        image->virt_addr = pixeldata;  
    }  
    image->width = w;  
    image->height = h;  
    if (c == 4) {  
        image->format = IMAGE_FORMAT_RGBA8888;  
    } else if (c == 1) {  
        image->format = IMAGE_FORMAT_GRAY8;  
    } else {  
        image->format = IMAGE_FORMAT_RGB888;  
    }  
    return 0;  
}  

int read_image(const char* path, image_buffer_t* image)
{
    return read_image_stb(path, image);  

}



static int crop_and_scale_image_c(int channel, unsigned char *src, int src_width, int src_height,
                                    int crop_x, int crop_y, int crop_width, int crop_height,
                                    unsigned char *dst, int dst_width, int dst_height,
                                    int dst_box_x, int dst_box_y, int dst_box_width, int dst_box_height) {
    if (dst == NULL) {
        printf("dst buffer is null\n");
        return -1;
    }

    float x_ratio = (float)crop_width / (float)dst_box_width;
    float y_ratio = (float)crop_height / (float)dst_box_height;


    // 从原图指定区域取数据，双线性缩放到目标指定区域
    for (int dst_y = dst_box_y; dst_y < dst_box_y + dst_box_height; dst_y++) {
        for (int dst_x = dst_box_x; dst_x < dst_box_x + dst_box_width; dst_x++) {
            int dst_x_offset = dst_x - dst_box_x;
            int dst_y_offset = dst_y - dst_box_y;

            int src_x = (int)(dst_x_offset * x_ratio) + crop_x;
            int src_y = (int)(dst_y_offset * y_ratio) + crop_y;

            float x_diff = (dst_x_offset * x_ratio) - (src_x - crop_x);
            float y_diff = (dst_y_offset * y_ratio) - (src_y - crop_y);

            int index1 = src_y * src_width * channel + src_x * channel;
            int index2 = index1 + src_width * channel;    // down
            if (src_y == src_height - 1) {
                // 如果到图像最下边缘，变成选择上面的像素
                index2 = index1 - src_width * channel;
            }
            int index3 = index1 + 1 * channel;            // right
            int index4 = index2 + 1 * channel;            // down right
            if (src_x == src_width - 1) {
                // 如果到图像最右边缘，变成选择左边的像素
                index3 = index1 - 1 * channel;
                index4 = index2 - 1 * channel;
            }


            for (int c = 0; c < channel; c++) {
                unsigned char A = src[index1+c];
                unsigned char B = src[index3+c];
                unsigned char C = src[index2+c];
                unsigned char D = src[index4+c];

                unsigned char pixel = (unsigned char)(
                    A * (1 - x_diff) * (1 - y_diff) +
                    B * x_diff * (1 - y_diff) +
                    C * y_diff * (1 - x_diff) +
                    D * x_diff * y_diff
                );

                dst[(dst_y * dst_width  + dst_x) * channel + c] = pixel;
            }
        }
    }

    return 0;
}

static int crop_and_scale_image_yuv420sp(unsigned char *src, int src_width, int src_height,
                                    int crop_x, int crop_y, int crop_width, int crop_height,
                                    unsigned char *dst, int dst_width, int dst_height,
                                    int dst_box_x, int dst_box_y, int dst_box_width, int dst_box_height) {

    unsigned char* src_y = src;
    unsigned char* src_uv = src + src_width * src_height;

    unsigned char* dst_y = dst;
    unsigned char* dst_uv = dst + dst_width * dst_height;

    crop_and_scale_image_c(1, src_y, src_width, src_height, crop_x, crop_y, crop_width, crop_height,
        dst_y, dst_width, dst_height, dst_box_x, dst_box_y, dst_box_width, dst_box_height);
    
    crop_and_scale_image_c(2, src_uv, src_width / 2, src_height / 2, crop_x / 2, crop_y / 2, crop_width / 2, crop_height / 2,
        dst_uv, dst_width / 2, dst_height / 2, dst_box_x, dst_box_y, dst_box_width, dst_box_height);

    return 0;
}

static int convert_image_cpu(image_buffer_t *src, image_buffer_t *dst, image_rect_t *src_box, image_rect_t *dst_box, char color) {
    int ret;
    if (dst->virt_addr == NULL) {
        return -1;
    }
    if (src->virt_addr == NULL) {
        return -1;
    }
    if (src->format != dst->format) {
        return -1;
    }

    int src_box_x = 0;
    int src_box_y = 0;
    int src_box_w = src->width;
    int src_box_h = src->height;
    if (src_box != NULL) {
        src_box_x = src_box->left;
        src_box_y = src_box->top;
        src_box_w = src_box->right - src_box->left + 1;
        src_box_h = src_box->bottom - src_box->top + 1;
    }
    int dst_box_x = 0;
    int dst_box_y = 0;
    int dst_box_w = dst->width;
    int dst_box_h = dst->height;
    if (dst_box != NULL) {
        dst_box_x = dst_box->left;
        dst_box_y = dst_box->top;
        dst_box_w = dst_box->right - dst_box->left + 1;
        dst_box_h = dst_box->bottom - dst_box->top + 1;
    }

    // fill pad color
    if (dst_box_w != dst->width || dst_box_h != dst->height) {
        int dst_size = get_image_size(dst);
        memset(dst->virt_addr, color, dst_size);
    }

    int need_release_dst_buffer = 0;
    int reti = 0;
    if (src->format == IMAGE_FORMAT_RGB888) {
        reti = crop_and_scale_image_c(3, src->virt_addr, src->width, src->height,
            src_box_x, src_box_y, src_box_w, src_box_h,
            dst->virt_addr, dst->width, dst->height,
            dst_box_x, dst_box_y, dst_box_w, dst_box_h);
    } else if (src->format == IMAGE_FORMAT_RGBA8888) {
        reti = crop_and_scale_image_c(4, src->virt_addr, src->width, src->height,
            src_box_x, src_box_y, src_box_w, src_box_h,
            dst->virt_addr, dst->width, dst->height,
            dst_box_x, dst_box_y, dst_box_w, dst_box_h);
    } else if (src->format == IMAGE_FORMAT_GRAY8) {
        reti = crop_and_scale_image_c(1, src->virt_addr, src->width, src->height,
            src_box_x, src_box_y, src_box_w, src_box_h,
            dst->virt_addr, dst->width, dst->height,
            dst_box_x, dst_box_y, dst_box_w, dst_box_h);
    } else if (src->format == IMAGE_FORMAT_YUV420SP_NV12 || src->format == IMAGE_FORMAT_YUV420SP_NV21) {
        reti = crop_and_scale_image_yuv420sp(src->virt_addr, src->width, src->height,
            src_box_x, src_box_y, src_box_w, src_box_h,
            dst->virt_addr, dst->width, dst->height,
            dst_box_x, dst_box_y, dst_box_w, dst_box_h);
    } else {
        printf("no support format %d\n", src->format);
    }
    if (reti != 0) {
        printf("convert_image_cpu fail %d\n", reti);
        return -1;
    }
    printf("finish\n");
    return 0;
}

// get_rga_fmt函数已移除，不再使用RGA硬件加速

int get_image_size(image_buffer_t* image)
{
    if (image == NULL) {
        return 0;
    }
    switch (image->format)
    {
    case IMAGE_FORMAT_GRAY8:
        return image->width * image->height;
    case IMAGE_FORMAT_RGB888:
        return image->width * image->height * 3;    
    case IMAGE_FORMAT_RGBA8888:
        return image->width * image->height * 4;
    case IMAGE_FORMAT_YUV420SP_NV12:
    case IMAGE_FORMAT_YUV420SP_NV21:
        return image->width * image->height * 3 / 2;
    default:
        break;
    }
}

/**
 * @brief 图像转换函数（仅使用CPU处理）
 * @param src_img 源图像缓冲区
 * @param dst_img 目标图像缓冲区
 * @param src_box 源图像裁剪区域（可为NULL表示整个图像）
 * @param dst_box 目标图像放置区域（可为NULL表示整个图像）
 * @param color 填充颜色
 * @return 成功返回0，失败返回-1
 * 
 * 功能说明：
 * 1. 统一使用CPU进行图像处理，确保稳定性
 * 2. 支持多种图像格式转换和缩放
 * 3. 支持图像裁剪和区域放置
 */
int convert_image(image_buffer_t* src_img, image_buffer_t* dst_img, image_rect_t* src_box, image_rect_t* dst_box, char color)
{
    printf("Converting image using CPU processing\n");
    printf("src width=%d height=%d fmt=0x%x virAddr=0x%p fd=%d\n",
        src_img->width, src_img->height, src_img->format, src_img->virt_addr, src_img->fd);
    printf("dst width=%d height=%d fmt=0x%x virAddr=0x%p fd=%d\n",
        dst_img->width, dst_img->height, dst_img->format, dst_img->virt_addr, dst_img->fd);
    if (src_box != NULL) {
        printf("src_box=(%d %d %d %d)\n", src_box->left, src_box->top, src_box->right, src_box->bottom);
    }
    if (dst_box != NULL) {
        printf("dst_box=(%d %d %d %d)\n", dst_box->left, dst_box->top, dst_box->right, dst_box->bottom);
    }
    printf("color=0x%x\n", color);

    // 直接使用CPU处理，不再尝试RGA
    return convert_image_cpu(src_img, dst_img, src_box, dst_box, color);
}

/**
 * @brief 带letterbox的图像转换（仅使用CPU和普通内存）
 * @param src_image 源图像缓冲区
 * @param dst_image 目标图像缓冲区
 * @param letterbox letterbox参数结构体，用于记录缩放和填充信息
 * @param color 填充颜色
 * @return 成功返回0，失败返回-1
 * 
 * 功能说明：
 * 1. 计算letterbox缩放参数，保持图像宽高比
 * 2. 使用普通内存分配目标图像缓冲区
 * 3. 调用CPU图像处理函数进行转换
 * 4. 正确设置letterbox参数供后处理使用
 */
int convert_image_with_letterbox(image_buffer_t* src_image, image_buffer_t* dst_image, letterbox_t* letterbox, char color)
{
    int ret = 0;
    int src_w = src_image->width;
    int src_h = src_image->height;
    int dst_w = dst_image->width;
    int dst_h = dst_image->height;
    int resize_w = dst_w;
    int resize_h = dst_h;

    int _left_offset = 0;
    int _top_offset = 0;
    float scale = 1.0;

    // 设置源图像区域（整个图像）
    image_rect_t src_box;
    src_box.left = 0;
    src_box.top = 0;
    src_box.right = src_image->width - 1;
    src_box.bottom = src_image->height - 1;

    // 计算letterbox缩放参数
    float _scale_w = (float)dst_w / src_w;
    float _scale_h = (float)dst_h / src_h;
    
    if(_scale_w < _scale_h) {
        // 宽度是限制因素
        scale = _scale_w;
        resize_h = (int)(src_h * scale);
        int padding_h = dst_h - resize_h;
        _top_offset = padding_h / 2;
    } else {
        // 高度是限制因素
        scale = _scale_h;
        resize_w = (int)(src_w * scale);
        int padding_w = dst_w - resize_w;
        _left_offset = padding_w / 2;
    }
    
    // 设置letterbox参数（重要：用于后处理坐标转换）
    letterbox->scale = scale;
    letterbox->x_pad = _left_offset;
    letterbox->y_pad = _top_offset;
    
    printf("Letterbox params: scale=%.3f, x_pad=%d, y_pad=%d\n", 
           scale, _left_offset, _top_offset);
    
    // 设置目标图像区域
    image_rect_t dst_box;
    dst_box.left = _left_offset;
    dst_box.top = _top_offset;
    dst_box.right = _left_offset + resize_w - 1;
    dst_box.bottom = _top_offset + resize_h - 1;
    
    // 分配目标图像内存（使用普通内存）
    if (dst_image->virt_addr == NULL && dst_image->fd <= 0) {
        int dst_size = get_image_size(dst_image);
        dst_image->virt_addr = (unsigned char*)malloc(dst_size);
        dst_image->fd = 0;  // 标记为普通内存
        
        if (dst_image->virt_addr == NULL) {
            printf("Error: Memory allocation failed for destination image size %d\n", dst_size);
            return -1;
        }
        
        printf("Allocated destination image buffer: %d bytes\n", dst_size);
    }
    
    // 使用CPU进行图像转换
    ret = convert_image(src_image, dst_image, &src_box, &dst_box, color);
    return ret;
}

/**
 * @brief 释放图像缓冲区内存（仅支持普通内存）
 * @param image 图像缓冲区结构体指针
 * 
 * 功能说明：
 * 1. 统一使用普通内存，简化内存管理
 * 2. 释放malloc分配的内存
 * 3. 重置缓冲区指针和标志
 */
void free_image_buffer(image_buffer_t* image) {
    if (image->virt_addr != NULL) {
        // 统一使用普通内存，直接释放
        free(image->virt_addr);
        printf("Image buffer freed (regular memory)\n");
        
        // 重置缓冲区信息
        image->virt_addr = NULL;
        image->fd = 0;
    }
}