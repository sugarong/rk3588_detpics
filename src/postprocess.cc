#include "yolov8.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#include <set>
#include <vector>

// 直接在代码中定义标签信息，移除txt文件依赖
static const char* class_labels[OBJ_CLASS_NUM] = {
    "pot"  // 唯一的类别
};

inline static int clamp(float val, int min, int max) { return val > min ? (val < max ? val : max) : min; }

static char *readLine(FILE *fp, char *buffer, int *len)
{
    int ch;
    int i = 0;
    size_t buff_len = 0;

    buffer = (char *)malloc(buff_len + 1);
    if (!buffer)
        return NULL; // Out of memory

    while ((ch = fgetc(fp)) != '\n' && ch != EOF)
    {
        buff_len++;
        void *tmp = realloc(buffer, buff_len + 1);
        if (tmp == NULL)
        {
            free(buffer);
            return NULL; // Out of memory
        }
        buffer = (char *)tmp;

        buffer[i] = (char)ch;
        i++;
    }
    buffer[i] = '\0';

    *len = buff_len;

    // Detect end
    if (ch == EOF && (i == 0 || ferror(fp)))
    {
        free(buffer);
        return NULL;
    }
    return buffer;
}

static int readLines(const char *fileName, char *lines[], int max_line)
{
    FILE *file = fopen(fileName, "r");
    char *s;
    int i = 0;
    int n = 0;

    if (file == NULL)
    {
        printf("Open %s fail!\n", fileName);
        return -1;
    }

    while ((s = readLine(file, s, &n)) != NULL)
    {
        lines[i++] = s;
        if (i >= max_line)
            break;
    }
    fclose(file);
    return i;
}

static int loadLabelName(const char *locationFilename, char *label[])
{
    printf("load lable %s\n", locationFilename);
    readLines(locationFilename, label, OBJ_CLASS_NUM);
    return 0;
}

static float CalculateOverlap(float xmin0, float ymin0, float xmax0, float ymax0, float xmin1, float ymin1, float xmax1,
                              float ymax1)
{
    float w = fmax(0.f, fmin(xmax0, xmax1) - fmax(xmin0, xmin1) + 1.0);
    float h = fmax(0.f, fmin(ymax0, ymax1) - fmax(ymin0, ymin1) + 1.0);
    float i = w * h;
    float u = (xmax0 - xmin0 + 1.0) * (ymax0 - ymin0 + 1.0) + (xmax1 - xmin1 + 1.0) * (ymax1 - ymin1 + 1.0) - i;
    return u <= 0.f ? 0.f : (i / u);
}

static int nms(int validCount, std::vector<float> &outputLocations, std::vector<int> classIds, std::vector<int> &order,
               int filterId, float threshold)
{
    for (int i = 0; i < validCount; ++i)
    {
        int n = order[i];
        if (n == -1 || classIds[n] != filterId)
        {
            continue;
        }
        for (int j = i + 1; j < validCount; ++j)
        {
            int m = order[j];
            if (m == -1 || classIds[m] != filterId)
            {
                continue;
            }
            float xmin0 = outputLocations[n * 4 + 0];
            float ymin0 = outputLocations[n * 4 + 1];
            float xmax0 = outputLocations[n * 4 + 0] + outputLocations[n * 4 + 2];
            float ymax0 = outputLocations[n * 4 + 1] + outputLocations[n * 4 + 3];

            float xmin1 = outputLocations[m * 4 + 0];
            float ymin1 = outputLocations[m * 4 + 1];
            float xmax1 = outputLocations[m * 4 + 0] + outputLocations[m * 4 + 2];
            float ymax1 = outputLocations[m * 4 + 1] + outputLocations[m * 4 + 3];

            float iou = CalculateOverlap(xmin0, ymin0, xmax0, ymax0, xmin1, ymin1, xmax1, ymax1);

            if (iou > threshold)
            {
                order[j] = -1;
            }
        }
    }
    return 0;
}

static int quick_sort_indice_inverse(std::vector<float> &input, int left, int right, std::vector<int> &indices)
{
    float key;
    int key_index;
    int low = left;
    int high = right;
    if (left < right)
    {
        key_index = indices[left];
        key = input[left];
        while (low < high)
        {
            while (low < high && input[high] <= key)
            {
                high--;
            }
            input[low] = input[high];
            indices[low] = indices[high];
            while (low < high && input[low] >= key)
            {
                low++;
            }
            input[high] = input[low];
            indices[high] = indices[low];
        }
        input[low] = key;
        indices[low] = key_index;
        quick_sort_indice_inverse(input, left, low - 1, indices);
        quick_sort_indice_inverse(input, low + 1, right, indices);
    }
    return low;
}

static float sigmoid(float x) { return 1.0 / (1.0 + expf(-x)); }

static float unsigmoid(float y) { return -1.0 * logf((1.0 / y) - 1.0); }

inline static int32_t __clip(float val, float min, float max)
{
    float f = val <= min ? min : (val >= max ? max : val);
    return f;
}

static int8_t qnt_f32_to_affine(float f32, int32_t zp, float scale)
{
    float dst_val = (f32 / scale) + zp;
    int8_t res = (int8_t)__clip(dst_val, -128, 127);
    return res;
}

static uint8_t qnt_f32_to_affine_u8(float f32, int32_t zp, float scale)
{
    float dst_val = (f32 / scale) + zp;
    uint8_t res = (uint8_t)__clip(dst_val, 0, 255);
    return res;
}

static float deqnt_affine_to_f32(int8_t qnt, int32_t zp, float scale) { return ((float)qnt - (float)zp) * scale; }

static float deqnt_affine_u8_to_f32(uint8_t qnt, int32_t zp, float scale) { return ((float)qnt - (float)zp) * scale; }

static void compute_dfl(float* tensor, int dfl_len, float* box){
    for (int b=0; b<4; b++){
        float exp_t[dfl_len];
        float exp_sum=0;
        float acc_sum=0;
        for (int i=0; i< dfl_len; i++){
            exp_t[i] = exp(tensor[i+b*dfl_len]);
            exp_sum += exp_t[i];
        }
        
        for (int i=0; i< dfl_len; i++){
            acc_sum += exp_t[i]/exp_sum *i;
        }
        box[b] = acc_sum;
    }
}

static int process_u8(uint8_t *box_tensor, int32_t box_zp, float box_scale,
                      uint8_t *score_tensor, int32_t score_zp, float score_scale,
                      uint8_t *score_sum_tensor, int32_t score_sum_zp, float score_sum_scale,
                      int grid_h, int grid_w, int stride, int dfl_len,
                      std::vector<float> &boxes,
                      std::vector<float> &objProbs,
                      std::vector<int> &classId,
                      float threshold)
{
    int validCount = 0;
    int grid_len = grid_h * grid_w;
    uint8_t score_thres_u8 = qnt_f32_to_affine_u8(threshold, score_zp, score_scale);
    uint8_t score_sum_thres_u8 = qnt_f32_to_affine_u8(threshold, score_sum_zp, score_sum_scale);

    for (int i = 0; i < grid_h; i++)
    {
        for (int j = 0; j < grid_w; j++)
        {
            int offset = i * grid_w + j;
            int max_class_id = -1;

            // Use score sum to quickly filter
            if (score_sum_tensor != nullptr)
            {
                if (score_sum_tensor[offset] < score_sum_thres_u8)
                {
                    continue;
                }
            }

            uint8_t max_score = -score_zp;
            for (int c = 0; c < OBJ_CLASS_NUM; c++)
            {
                if ((score_tensor[offset] > score_thres_u8) && (score_tensor[offset] > max_score))
                {
                    max_score = score_tensor[offset];
                    max_class_id = c;
                }
                offset += grid_len;
            }

            // compute box
            if (max_score > score_thres_u8)
            {
                offset = i * grid_w + j;
                float box[4];
                float before_dfl[dfl_len * 4];
                for (int k = 0; k < dfl_len * 4; k++)
                {
                    before_dfl[k] = deqnt_affine_u8_to_f32(box_tensor[offset], box_zp, box_scale);
                    offset += grid_len;
                }
                compute_dfl(before_dfl, dfl_len, box);

                float x1, y1, x2, y2, w, h;
                x1 = (-box[0] + j + 0.5) * stride;
                y1 = (-box[1] + i + 0.5) * stride;
                x2 = (box[2] + j + 0.5) * stride;
                y2 = (box[3] + i + 0.5) * stride;
                w = x2 - x1;
                h = y2 - y1;
                boxes.push_back(x1);
                boxes.push_back(y1);
                boxes.push_back(w);
                boxes.push_back(h);

                objProbs.push_back(deqnt_affine_u8_to_f32(max_score, score_zp, score_scale));
                classId.push_back(max_class_id);
                validCount++;
            }
        }
    }
    return validCount;
}

static int process_i8(int8_t *box_tensor, int32_t box_zp, float box_scale,
                      int8_t *score_tensor, int32_t score_zp, float score_scale,
                      int8_t *score_sum_tensor, int32_t score_sum_zp, float score_sum_scale,
                      int grid_h, int grid_w, int stride, int dfl_len,
                      std::vector<float> &boxes, 
                      std::vector<float> &objProbs, 
                      std::vector<int> &classId, 
                      float threshold)
{
    int validCount = 0;
    int grid_len = grid_h * grid_w;
    int8_t score_thres_i8 = qnt_f32_to_affine(threshold, score_zp, score_scale);
    int8_t score_sum_thres_i8 = qnt_f32_to_affine(threshold, score_sum_zp, score_sum_scale);

    for (int i = 0; i < grid_h; i++)
    {
        for (int j = 0; j < grid_w; j++)
        {
            int offset = i* grid_w + j;
            int max_class_id = -1;

            // 通过 score sum 起到快速过滤的作用
            if (score_sum_tensor != nullptr){
                if (score_sum_tensor[offset] < score_sum_thres_i8){
                    continue;
                }
            }

            int8_t max_score = -score_zp;
            for (int c= 0; c< OBJ_CLASS_NUM; c++){
                if ((score_tensor[offset] > score_thres_i8) && (score_tensor[offset] > max_score))
                {
                    max_score = score_tensor[offset];
                    max_class_id = c;
                }
                offset += grid_len;
            }

            // compute box
            if (max_score> score_thres_i8){
                offset = i* grid_w + j;
                float box[4];
                float before_dfl[dfl_len*4];
                for (int k=0; k< dfl_len*4; k++){
                    before_dfl[k] = deqnt_affine_to_f32(box_tensor[offset], box_zp, box_scale);
                    offset += grid_len;
                }
                compute_dfl(before_dfl, dfl_len, box);

                float x1,y1,x2,y2,w,h;
                x1 = (-box[0] + j + 0.5)*stride;
                y1 = (-box[1] + i + 0.5)*stride;
                x2 = (box[2] + j + 0.5)*stride;
                y2 = (box[3] + i + 0.5)*stride;
                w = x2 - x1;
                h = y2 - y1;
                boxes.push_back(x1);
                boxes.push_back(y1);
                boxes.push_back(w);
                boxes.push_back(h);

                objProbs.push_back(deqnt_affine_to_f32(max_score, score_zp, score_scale));
                classId.push_back(max_class_id);
                validCount ++;
            }
        }
    }
    return validCount;
}

static int process_fp32(float *box_tensor, float *score_tensor, float *score_sum_tensor, 
                        int grid_h, int grid_w, int stride, int dfl_len,
                        std::vector<float> &boxes, 
                        std::vector<float> &objProbs, 
                        std::vector<int> &classId, 
                        float threshold)
{
    int validCount = 0;
    int grid_len = grid_h * grid_w;
    for (int i = 0; i < grid_h; i++)
    {
        for (int j = 0; j < grid_w; j++)
        {
            int offset = i* grid_w + j;
            int max_class_id = -1;

            // 通过 score sum 起到快速过滤的作用
            if (score_sum_tensor != nullptr){
                if (score_sum_tensor[offset] < threshold){
                    continue;
                }
            }

            float max_score = 0;
            for (int c= 0; c< OBJ_CLASS_NUM; c++){
                if ((score_tensor[offset] > threshold) && (score_tensor[offset] > max_score))
                {
                    max_score = score_tensor[offset];
                    max_class_id = c;
                }
                offset += grid_len;
            }

            // compute box
            if (max_score> threshold){
                offset = i* grid_w + j;
                float box[4];
                float before_dfl[dfl_len*4];
                for (int k=0; k< dfl_len*4; k++){
                    before_dfl[k] = box_tensor[offset];
                    offset += grid_len;
                }
                compute_dfl(before_dfl, dfl_len, box);

                float x1,y1,x2,y2,w,h;
                x1 = (-box[0] + j + 0.5)*stride;
                y1 = (-box[1] + i + 0.5)*stride;
                x2 = (box[2] + j + 0.5)*stride;
                y2 = (box[3] + i + 0.5)*stride;
                w = x2 - x1;
                h = y2 - y1;
                boxes.push_back(x1);
                boxes.push_back(y1);
                boxes.push_back(w);
                boxes.push_back(h);

                objProbs.push_back(max_score);
                classId.push_back(max_class_id);
                validCount ++;
            }
        }
    }
    return validCount;
}

/**
 * YOLOv8后处理主函数 - 将NPU输出转换为最终检测结果
 * @param app_ctx: 应用上下文，包含模型信息和配置
 * @param outputs: NPU推理输出的原始tensor数据
 * @param letter_box: letterbox变换参数，用于坐标还原
 * @param conf_threshold: 置信度阈值，过滤低置信度检测
 * @param nms_threshold: NMS阈值，控制重复框消除程度
 * @param od_results: 输出参数，存储最终检测结果
 * @return: 0表示成功
 */
int post_process(rknn_app_context_t *app_ctx, void *outputs, letterbox_t *letter_box, 
                 float conf_threshold, float nms_threshold, object_detect_result_list *od_results)
{
    // 【语法】强制类型转换：将void*转换为rknn_output*，获取NPU输出数据结构
    rknn_output *_outputs = (rknn_output *)outputs;
    
    // 【功能】初始化存储容器 - 使用STL容器动态管理检测结果
    std::vector<float> filterBoxes;    // 存储过滤后的边界框坐标 [x,y,w,h]
    std::vector<float> objProbs;       // 存储对应的置信度分数
    std::vector<int> classId;          // 存储对应的类别ID
    
    // 【功能】初始化局部变量
    int validCount = 0;                // 有效检测框计数器
    int stride = 0;                    // 当前层的下采样步长
    int grid_h = 0, grid_w = 0;        // 当前特征图的网格尺寸
    int model_in_w = app_ctx->model_width;   // 模型输入宽度
    int model_in_h = app_ctx->model_height;  // 模型输入高度

    // 【语法】memset函数：将od_results结构体清零初始化
    memset(od_results, 0, sizeof(object_detect_result_list));

    // 【功能】YOLOv8特征解析 - 计算DFL长度和输出分支数
    // DFL (Distribution Focal Loss) 是YOLOv8的边界框回归方法
    int dfl_len = app_ctx->output_attrs[0].dims[1] / 4;  // 每个坐标分量的分布长度
    int output_per_branch = app_ctx->io_num.n_output / 3; // 每个尺度分支的输出数量
    
    // 【功能】多尺度处理 - YOLOv8使用3个不同尺度的特征图进行检测
    for (int i = 0; i < 3; i++)
    {
        // 【功能】score_sum优化 - 可选的快速过滤机制
        void *score_sum = nullptr;
        int32_t score_sum_zp = 0;      // score_sum的量化零点
        float score_sum_scale = 1.0;   // score_sum的量化缩放因子
        
        // 【语法】条件判断：检查是否有score_sum输出（3输出模式）
        if (output_per_branch == 3) {
            // 【功能】获取第三个输出tensor（score_sum），用于快速过滤
            score_sum = _outputs[i * output_per_branch + 2].buf;
            score_sum_zp = app_ctx->output_attrs[i * output_per_branch + 2].zp;
            score_sum_scale = app_ctx->output_attrs[i * output_per_branch + 2].scale;
        }
        
        // 【功能】计算当前分支的tensor索引
        int box_idx = i * output_per_branch;      // 边界框回归tensor索引
        int score_idx = i * output_per_branch + 1; // 分类置信度tensor索引

        // 【功能】获取当前特征图的网格尺寸
        grid_h = app_ctx->output_attrs[box_idx].dims[2];  // 特征图高度
        grid_w = app_ctx->output_attrs[box_idx].dims[3];  // 特征图宽度

        // 【功能】计算下采样步长 - 用于网格坐标到图像坐标的转换
        stride = model_in_h / grid_h;  // 步长 = 输入尺寸 / 特征图尺寸

        // 【功能】根据量化类型选择处理函数
        if (app_ctx->is_quant)
        {
            // 【功能】int8量化模型处理路径
            // 【语法】强制类型转换：将void*转换为int8_t*，访问量化数据
            validCount += process_i8(
                (int8_t *)_outputs[box_idx].buf,           // 边界框tensor数据
                app_ctx->output_attrs[box_idx].zp,         // 边界框量化零点
                app_ctx->output_attrs[box_idx].scale,      // 边界框量化缩放
                (int8_t *)_outputs[score_idx].buf,         // 分类tensor数据
                app_ctx->output_attrs[score_idx].zp,       // 分类量化零点
                app_ctx->output_attrs[score_idx].scale,    // 分类量化缩放
                (int8_t *)score_sum, score_sum_zp, score_sum_scale,  // score_sum相关参数
                grid_h, grid_w, stride, dfl_len,           // 网格和DFL参数
                filterBoxes, objProbs, classId,            // 输出容器（引用传递）
                conf_threshold                             // 置信度阈值
            );
        }
        else
        {
            // 【功能】float32浮点模型处理路径
            // 【语法】强制类型转换：将void*转换为float*，访问浮点数据
            validCount += process_fp32(
                (float *)_outputs[box_idx].buf,            // 边界框tensor数据
                (float *)_outputs[score_idx].buf,          // 分类tensor数据
                (float *)score_sum,                        // score_sum数据
                grid_h, grid_w, stride, dfl_len,           // 网格和DFL参数
                filterBoxes, objProbs, classId,            // 输出容器（引用传递）
                conf_threshold                             // 置信度阈值
            );
        }
    }

    // 【功能】早期退出 - 如果没有检测到任何目标
    if (validCount <= 0)
    {
        return 0;  // 直接返回，od_results->count已经被初始化为0
    }
    
    // 【功能】准备NMS处理 - 创建索引数组用于排序和NMS
    std::vector<int> indexArray;
    // 【语法】for循环：初始化索引数组 [0, 1, 2, ..., validCount-1]
    for (int i = 0; i < validCount; ++i)
    {
        indexArray.push_back(i);  // 【语法】vector::push_back添加元素
    }
    
    // 【功能】置信度排序 - 按置信度从高到低排序
    // 【语法】函数调用：传递引用参数，原地修改objProbs和indexArray
    quick_sort_indice_inverse(objProbs, 0, validCount - 1, indexArray);

    // 【功能】获取所有检测到的类别 - 用于分类别进行NMS
    // 【语法】STL set容器：自动去重和排序
    std::set<int> class_set(std::begin(classId), std::end(classId));

    // 【功能】分类别NMS处理 - 对每个类别分别进行非极大值抑制
    // 【语法】范围for循环（C++11）：遍历set中的每个类别
    for (auto c : class_set)
    {
        // 【功能】对类别c执行NMS，移除重复检测框
        nms(validCount, filterBoxes, classId, indexArray, c, nms_threshold);
    }

    // 【功能】结果输出准备
    int last_count = 0;           // 最终输出的检测框计数
    od_results->count = 0;        // 初始化输出结果计数

    // 【功能】构建最终检测结果 - 坐标变换和格式转换
    for (int i = 0; i < validCount; ++i)
    {
        // 【功能】跳过被NMS标记为无效的检测框和超出最大数量限制的框
        // 【语法】逻辑或运算符：任一条件为真则跳过
        if (indexArray[i] == -1 || last_count >= OBJ_NUMB_MAX_SIZE)
        {
            continue;  // 【语法】continue语句：跳过当前循环迭代
        }
        
        int n = indexArray[i];  // 获取有效检测框的原始索引

        // 【功能】坐标变换 - 从模型坐标系转换到原图坐标系
        // 步骤1：移除letterbox填充
        float x1 = filterBoxes[n * 4 + 0] - letter_box->x_pad;  // 左上角x坐标
        float y1 = filterBoxes[n * 4 + 1] - letter_box->y_pad;  // 左上角y坐标
        float x2 = x1 + filterBoxes[n * 4 + 2];                 // 右下角x坐标
        float y2 = y1 + filterBoxes[n * 4 + 3];                 // 右下角y坐标
        
        int id = classId[n];           // 获取类别ID
        float obj_conf = objProbs[i];  // 获取置信度分数

        // 【功能】最终坐标计算和边界裁剪
        // 步骤2：缩放回原图尺寸并确保坐标在有效范围内
        // 【语法】强制类型转换：float转int，截断小数部分
        od_results->results[last_count].box.left = 
            (int)(clamp(x1, 0, model_in_w) / letter_box->scale);
        od_results->results[last_count].box.top = 
            (int)(clamp(y1, 0, model_in_h) / letter_box->scale);
        od_results->results[last_count].box.right = 
            (int)(clamp(x2, 0, model_in_w) / letter_box->scale);
        od_results->results[last_count].box.bottom = 
            (int)(clamp(y2, 0, model_in_h) / letter_box->scale);
        
        // 【功能】保存检测属性
        od_results->results[last_count].prop = obj_conf;  // 置信度
        od_results->results[last_count].cls_id = id;      // 类别ID
        
        last_count++;  // 【语法】后置递增：先使用后递增
    }
    
    // 【功能】设置最终结果数量
    od_results->count = last_count;
    return 0;  // 【功能】返回成功状态
}

int init_post_process()
{
    printf("Using built-in class labels: %s\n", class_labels[0]);
    return 0;
}

// 修改后的类别名称获取函数
const char *coco_cls_to_name(int cls_id)
{
    if (cls_id >= 0 && cls_id < OBJ_CLASS_NUM)
    {
        return class_labels[cls_id];
    }
    return "unknown";
}

// 简化的清理函数，不再需要释放动态分配的内存
void deinit_post_process()
{
    // 不需要释放内存，因为使用的是静态字符串常量
    printf("Post-process deinitialized\n");
}
