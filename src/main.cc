/**
 * @file main.cc
 * @brief YOLOv8目标检测程序主文件
 * @description 基于RKNN推理引擎的YOLOv8目标检测应用，支持批量处理图像文件夹
 * @author sugarong
 * @date 2024
 */

// 标准C库头文件
#include <stdint.h>     // 定义标准整数类型，如uint8_t, uint32_t等
#include <stdio.h>      // 标准输入输出函数，如printf, scanf等
#include <stdlib.h>     // 标准库函数，如malloc, free, exit等
#include <string.h>     // 字符串处理函数，如memcpy, strcmp等

// 项目自定义头文件
#include "yolov8.h"        // YOLOv8模型推理相关函数
#include "image_utils.h"   // 图像处理工具函数
#include "file_utils.h"    // 文件操作工具函数
#include "image_drawing.h" // 图像绘制函数（画框、文字等）

// C++标准库头文件
#include <string>       // C++字符串类std::string
#include <chrono>       // C++时间库（本代码中未使用）
#include <vector>       // C++动态数组容器（本代码中未使用）
#include <iostream>     // C++输入输出流

// POSIX系统调用头文件（Linux/Unix系统）
#include <dirent.h>     // 目录操作函数，如opendir, readdir等
#include <sys/types.h>  // 系统数据类型定义
#include <sys/stat.h>   // 文件状态信息结构体和相关宏定义 - 新添加
#include <unistd.h>     // POSIX操作系统API
#include <cstring>      // C字符串函数的C++版本

// OpenCV计算机视觉库
#include <opencv2/opencv.hpp>  // OpenCV主头文件

/**
 * @brief 使用OpenCV读取图像文件
 * @param path 图像文件路径
 * @param image 输出的图像缓冲区结构体指针
 * @return 成功返回0，失败返回-1
 * 
 * 功能说明：
 * 1. 使用OpenCV的cv::imread读取图像
 * 2. 根据图像通道数设置格式（RGBA/RGB/GRAY）
 * 3. 分配内存并复制图像数据到image_buffer_t结构体
 * 4. 如果是4通道RGBA图像，转换为3通道RGB
 */
#include "../utils/image_utils.h"
// DMA分配器已移除，统一使用普通内存
ee// #include "../3rdparty/allocator/dma/dma_alloc.h"

/**
 * @brief 使用OpenCV读取图像文件并转换为image_buffer_t格式
 * @param path 图像文件路径
 * @param image 输出的图像缓冲区结构体指针
 * @return 成功返回0，失败返回-1
 * 
 * 功能说明：
 * 1. 使用OpenCV读取各种格式的图像文件
 * 2. 自动处理RGBA到RGB的转换
 * 3. 使用普通内存分配，避免DMA相关问题
 * 4. 设置正确的图像格式和尺寸信息
 */
int read_image_opencv(const char* path, image_buffer_t* image) {
    // 使用OpenCV读取图像
    cv::Mat cv_img = cv::imread(path, cv::IMREAD_COLOR);
    if (cv_img.empty()) {
        printf("Error: Cannot read image from %s\n", path);
        return -1;
    }
    
    // 设置图像基本信息
    image->width = cv_img.cols;
    image->height = cv_img.rows;
    int channels = cv_img.channels();
    
    // 根据通道数设置图像格式
    if (channels == 3) {
        image->format = IMAGE_FORMAT_RGB888;
        // OpenCV默认是BGR，转换为RGB
        cv::cvtColor(cv_img, cv_img, cv::COLOR_BGR2RGB);
    } else if (channels == 4) {
        image->format = IMAGE_FORMAT_RGBA8888;
        cv::cvtColor(cv_img, cv_img, cv::COLOR_BGRA2RGBA);
    } else if (channels == 1) {
        image->format = IMAGE_FORMAT_GRAY8;
    } else {
        printf("Error: Unsupported image format with %d channels\n", channels);
        return -1;
    }
    
    // 计算图像数据大小
    int size = cv_img.total() * cv_img.elemSize();
    image->size = size;
    
    // 使用普通内存分配
    image->virt_addr = (unsigned char*)malloc(size);
    image->fd = 0;  // 标记为普通内存
    
    if (image->virt_addr == NULL) {
        printf("Error: Memory allocation failed for image size %d\n", size);
        return -1;
    }
    
    // 复制图像数据
    memcpy(image->virt_addr, cv_img.data, size);
    
    printf("Image loaded: %dx%d, %d channels, %d bytes\n", 
           image->width, image->height, channels, size);
    
    return 0;
}

/**
 * @brief 使用OpenCV保存图像文件
 * @param path 输出图像文件路径
 * @param img 输入的图像缓冲区结构体指针
 * @return 成功返回0，失败返回-1
 * 
 * 功能说明：
 * 1. 根据image_buffer_t格式确定OpenCV图像类型
 * 2. 创建cv::Mat对象包装现有图像数据
 * 3. 使用cv::imwrite保存图像
 */
int write_image(const char* path, const image_buffer_t* img) {  
    int width = img->width;  
    int height = img->height;  
    
    // 三元运算符链：根据图像格式确定通道数
    int channels = (img->format == IMAGE_FORMAT_RGB888) ? 3 :   
                   (img->format == IMAGE_FORMAT_GRAY8) ? 1 :   
                   4; // 默认4通道
    
    void* data = img->virt_addr;  
  
    // cv::Mat构造函数：使用现有内存数据创建Mat对象
    // 参数：高度, 宽度, 数据类型, 数据指针
    // CV_8UC(channels): 8位无符号整数，channels个通道
    cv::Mat cv_img(height, width, CV_8UC(channels), data);  
  
    // 处理BGR到RGB的转换（OpenCV默认使用BGR格式）
    if (channels == 3 && img->format != IMAGE_FORMAT_RGB888) {
        cv::Mat rgb_img;  
        cv::cvtColor(cv_img, rgb_img, cv::COLOR_BGR2RGB);  
        bool success = cv::imwrite(path, rgb_img);  
        return success ? 0 : -1;  
    }  
  
    // cv::imwrite: OpenCV图像保存函数
    bool success = cv::imwrite(path, cv_img);  
    return success ? 0 : -1;
}

/**
 * @brief 从文件路径中提取不带扩展名的文件名
 * @param path 完整文件路径
 * @return 不带扩展名的文件名
 * 
 * 功能说明：
 * 1. 找到路径中最后一个路径分隔符（/或\）
 * 2. 提取文件名部分
 * 3. 去除文件扩展名
 */
std::string extractFileNameWithoutExtension(const std::string& path) 
{  
    // std::string::find_last_of: 从后向前查找指定字符集中任一字符的位置
    // "/\\"表示查找'/'或'\\'字符
    auto pos = path.find_last_of("/\\");  
    
    // std::string::npos: 表示"未找到"的特殊值
    // 三元运算符：如果未找到分隔符，使用整个路径；否则提取分隔符后的部分
    // std::string::substr(pos+1): 从位置pos+1开始提取子字符串到末尾
    std::string filename = (pos == std::string::npos) ? path : path.substr(pos + 1);  
      
    // 查找并去除文件扩展名（最后一个'.'之后的部分）
    pos = filename.find_last_of(".");  
    if (pos != std::string::npos) {  
        // std::string::substr(0, pos): 从开始提取到位置pos（不包含pos）
        filename = filename.substr(0, pos);  
    }  
      
    return filename;  
}

/**
 * @brief 处理文件夹中的所有图像文件
 * @param folderPath 输入图像文件夹路径
 * @param rknn_app_ctx RKNN应用上下文指针
 * @param outputFolderPath 输出图像文件夹路径
 * 
 * 功能说明：
 * 1. 遍历指定文件夹中的所有文件
 * 2. 筛选图像文件（.jpg, .jpeg, .png）
 * 3. 对每个图像文件进行YOLOv8推理
 * 4. 在图像上绘制检测结果（边界框和标签）
 * 5. 保存处理后的图像
 */
void processImagesInFolder(const std::string& folderPath, rknn_app_context_t* rknn_app_ctx, const std::string& outputFolderPath) 
{  
    // opendir: POSIX函数，打开目录流
    // DIR*: 目录流指针类型
    DIR *dir = opendir(folderPath.c_str());  
    if (dir == nullptr) {  
        perror("opendir");  // perror: 打印系统错误信息
        return;  
    }  
  
    // struct dirent: 目录项结构体，包含文件名等信息
    struct dirent *entry;  
    
    // readdir: 读取目录中的下一个文件项
    // 返回nullptr表示已读取完所有文件
    while ((entry = readdir(dir)) != nullptr) 
    {  
        std::string fileName = entry->d_name;  // d_name: 文件名成员
        std::string fullPath = folderPath + "/" + fileName;  
        
        // 检查文件扩展名：使用strcmp比较字符串
        // fileName.c_str(): 将std::string转换为C风格字符串（const char*）
        // fileName.size(): 获取字符串长度
        // strcmp: C函数，比较两个字符串，相等返回0
        if ((fileName.size() >= 4 && strcmp(fileName.c_str() + fileName.size() - 4, ".jpg") == 0) ||  
            (fileName.size() >= 5 && strcmp(fileName.c_str() + fileName.size() - 5, ".jpeg") == 0) ||  
            (fileName.size() >= 4 && strcmp(fileName.c_str() + fileName.size() - 4, ".png") == 0)) {  
  
            // 构造输出文件名：原文件名_out.png
            std::string outputFileName = outputFolderPath + "/" + extractFileNameWithoutExtension(fullPath) + "_out.png";  
  
            int ret;  
            image_buffer_t src_image;  
            // memset: 将内存块设置为指定值（这里设置为0）
            // 语法：memset(内存地址, 设置值, 字节数)
            memset(&src_image, 0, sizeof(image_buffer_t));  
 
            // 读取图像文件
            ret = read_image_opencv(fullPath.c_str(), &src_image); 
  
            if (ret != 0) {  
                printf("read image fail! ret=%d image_path=%s\n", ret, fullPath.c_str());  
                continue;  // 跳过当前循环，处理下一个文件
            }  
  
            // object_detect_result_list: 目标检测结果列表结构体
            object_detect_result_list od_results;  
            
            // 执行YOLOv8模型推理
            ret = inference_yolov8_model(rknn_app_ctx, &src_image, &od_results);  
            if (ret != 0) {  
                printf("inference_yolov8_model fail! ret=%d\n", ret);  
                // 释放已分配的图像内存
                if (src_image.virt_addr != NULL) {  
                    free_image_buffer(&src_image);  
                }  
                continue;  
            } 

            // 在处理检测结果的部分（约第404-415行）
            if (ret != 0) {
                printf("inference_yolov8_model fail! ret=%d\n", ret);
            } else {
                printf("\n=== 检测结果 ===\n");
                if (od_results.count == 0) {
                    printf("未检测到目标\n");
                } else {
                    printf("检测到 %d 个目标:\n", od_results.count);
                    // 绘制检测结果
                    char text[256];
                    for (int i = 0; i < od_results.count; i++) {
                        object_detect_result *det_result = &(od_results.results[i]);
                        
                        // 改进的输出格式
                        printf("目标 %d:\n", i + 1);
                        printf("  类别名称: %s\n", coco_cls_to_name(det_result->cls_id));
                        printf("  置信度: %.1f%%\n", det_result->prop * 100);
                        printf("  位置: (%d, %d, %d, %d)\n", 
                               det_result->box.left, det_result->box.top,
                               det_result->box.right, det_result->box.bottom);
                        printf("\n");
                        
                        int x1 = det_result->box.left;
                        int y1 = det_result->box.top;
                        int x2 = det_result->box.right;
                        int y2 = det_result->box.bottom;
                        
                        draw_rectangle(&src_image, x1, y1, x2 - x1, y2 - y1, COLOR_BLUE, 3);
                        sprintf(text, "%s %.1f%%", coco_cls_to_name(det_result->cls_id), det_result->prop * 100);
                        draw_text(&src_image, text, x1, y1 - 20, COLOR_RED, 10);
                    }
                }
                
                // 保存处理后的图像
                write_image(outputFileName.c_str(), &src_image);
                printf("输出图像已保存到: %s\n", outputFileName.c_str());
            }
                
                // 释放图像内存
                if (src_image.virt_addr != NULL) 
                {  
                    free_image_buffer(&src_image);  
                }  
        }  
    }  
  
    // closedir: 关闭目录流
    closedir(dir);  
}   
  
/**
 * @brief 主函数 - 程序入口点
 * @param argc 命令行参数个数
 * @param argv 命令行参数数组
 * @return 程序退出码，0表示成功
 * 
 * 功能说明：
 * 1. 解析命令行参数获取输入图像路径
 * 2. 设置模型路径、输出文件夹路径
 * 3. 初始化RKNN推理环境和YOLOv8模型
 * 4. 处理单张图像或批量处理图像文件夹
 * 5. 清理资源并退出
 * 
 * 使用方法：
 * ./rknn_yolov8_demo <input_image_or_folder> [output_folder]
 * 例如：
 * ./rknn_yolov8_demo /path/to/image.jpg
 * ./rknn_yolov8_demo /path/to/image_folder
 * ./rknn_yolov8_demo /path/to/image.jpg /path/to/output
 */
int main(int argc, char **argv)  
{   
    // 检查命令行参数
    if (argc < 2) {
        printf("Usage: %s <input_image_or_folder> [output_folder]\n", argv[0]);
        printf("Examples:\n");
        printf("  %s /path/to/image.jpg\n", argv[0]);
        printf("  %s /path/to/image_folder\n", argv[0]);
        printf("  %s /path/to/image.jpg /path/to/output\n", argv[0]);
        return -1;
    }
    
    // 从命令行参数获取输入路径
    std::string inputPath = argv[1];
    
    // 设置输出路径（如果用户指定了输出路径则使用，否则使用默认路径）
    std::string outputFolder;
    if (argc >= 3) {
        outputFolder = argv[2];
    } else {
        outputFolder = "./outputimage";  // 使用相对路径
    }
    
    // ***** 配置模型路径（使用相对路径）
    const std::string modelPath = "./model/yolov8.rknn";  // 使用相对路径
    
    int ret;  // 函数返回值
    rknn_app_context_t rknn_app_ctx;  // RKNN应用上下文结构体
    memset(&rknn_app_ctx, 0, sizeof(rknn_app_context_t)); // 初始化为0

    // 初始化后处理模块
    init_post_process(); 

    // 初始化YOLOv8模型
    ret = init_yolov8_model(modelPath.c_str(), &rknn_app_ctx);  
    if (ret != 0) 
    {  
        printf("init_yolov8_model fail! ret=%d model_path=%s\n", ret, modelPath.c_str());  
        return -1;  // 返回错误码
    }      

    // 判断输入是文件还是文件夹
    struct stat path_stat;
    if (stat(inputPath.c_str(), &path_stat) != 0) {
        printf("Error: Cannot access input path: %s\n", inputPath.c_str());
        release_yolov8_model(&rknn_app_ctx);
        deinit_post_process();
        return -1;
    }
    
    if (S_ISDIR(path_stat.st_mode)) {
        // 输入是文件夹，批量处理
        printf("Processing images in folder: %s\n", inputPath.c_str());
        processImagesInFolder(inputPath, &rknn_app_ctx, outputFolder);
    } else if (S_ISREG(path_stat.st_mode)) {
        // 输入是单个文件，处理单张图像
        printf("Processing single image: %s\n", inputPath.c_str());
        
        // 检查文件扩展名
        std::string fileName = inputPath;
        if ((fileName.size() >= 4 && strcmp(fileName.c_str() + fileName.size() - 4, ".jpg") == 0) ||
            (fileName.size() >= 5 && strcmp(fileName.c_str() + fileName.size() - 5, ".jpeg") == 0) ||
            (fileName.size() >= 4 && strcmp(fileName.c_str() + fileName.size() - 4, ".png") == 0)) {
            
            // 构造输出文件名
            std::string outputFileName = outputFolder + "/" + extractFileNameWithoutExtension(inputPath) + "_out.png";
            
            image_buffer_t src_image;
            memset(&src_image, 0, sizeof(image_buffer_t));
            
            // 读取图像文件
            ret = read_image_opencv(inputPath.c_str(), &src_image);
            if (ret != 0) {
                printf("read image fail! ret=%d image_path=%s\n", ret, inputPath.c_str());
            } else {
                // 执行推理
                object_detect_result_list od_results;
                ret = inference_yolov8_model(&rknn_app_ctx, &src_image, &od_results);
                if (ret != 0) {
                    printf("inference_yolov8_model fail! ret=%d\n", ret);
                } else {
                    // 绘制检测结果
                    char text[256];
                    for (int i = 0; i < od_results.count; i++) {
                        object_detect_result *det_result = &(od_results.results[i]);
                        printf("%s @ (%d %d %d %d) %.3f\n", coco_cls_to_name(det_result->cls_id),
                               det_result->box.left, det_result->box.top,
                               det_result->box.right, det_result->box.bottom,
                               det_result->prop);
                        
                        int x1 = det_result->box.left;
                        int y1 = det_result->box.top;
                        int x2 = det_result->box.right;
                        int y2 = det_result->box.bottom;
                        
                        draw_rectangle(&src_image, x1, y1, x2 - x1, y2 - y1, COLOR_BLUE, 3);
                        sprintf(text, "%s %.1f%%", coco_cls_to_name(det_result->cls_id), det_result->prop * 100);
                        draw_text(&src_image, text, x1, y1 - 20, COLOR_RED, 10);
                    }
                    
                    // 保存处理后的图像
                    write_image(outputFileName.c_str(), &src_image);
                    printf("Output saved to: %s\n", outputFileName.c_str());
                }
                
                // 释放图像内存
                if (src_image.virt_addr != NULL) {
                    free_image_buffer(&src_image);
                }
            }
        } else {
            printf("Error: Unsupported image format. Please use .jpg, .jpeg, or .png files.\n");
        }
    } else {
        printf("Error: Input path is neither a file nor a directory: %s\n", inputPath.c_str());
    }

    // 释放YOLOv8模型资源
    ret = release_yolov8_model(&rknn_app_ctx); 
    if (ret != 0) 
    {  
        printf("release_yolov8_model fail! ret=%d\n", ret);  
    }  

    // 清理后处理模块
    deinit_post_process();  

    return 0;  // 程序正常退出
}

