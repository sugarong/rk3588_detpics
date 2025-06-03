# YOLOv8 RK3588 目标检测项目

基于瑞芯微RK3588芯片的YOLOv8目标检测应用，使用RKNN推理引擎实现高效的AI推理。

## 📋 项目简介

本项目是一个完整的YOLOv8目标检测解决方案，专为RK3588开发板优化。项目支持批量图像处理，使用CPU进行图像预处理和后处理，通过RKNN推理引擎进行模型推理，实现高效的目标检测功能。

## ✨ 功能特性

- 🚀 **高性能推理**：基于RKNN推理引擎，充分利用RK3588的NPU算力
- 📁 **批量处理**：支持文件夹内多张图像的批量检测
- 🖼️ **多格式支持**：支持PNG、JPG等常见图像格式
- 🎯 **精确检测**：集成NMS非极大值抑制，提高检测精度
- 📊 **可视化输出**：自动在检测结果上绘制边界框和置信度
- 💾 **内存优化**：统一使用普通内存分配，简化内存管理

## 🛠️ 技术栈

- **推理引擎**：RKNN Runtime
- **图像处理**：OpenCV 4.x
- **编程语言**：C/C++
- **构建系统**：CMake
- **目标平台**：RK3588 (ARM64)

## 📁 项目结构

```
YOLOv8_RK3588_object_detect/
├── src/                    # 源代码目录
│   ├── main.cc            # 主程序入口
│   ├── yolov8.cc          # YOLOv8模型推理实现
│   └── postprocess.cc     # 后处理算法实现
├── include/               # 头文件目录
│   ├── yolov8.h          # YOLOv8相关声明
│   ├── postprocess.h     # 后处理相关声明
│   └── rknn_api.h        # RKNN API声明
├── utils/                 # 工具库
│   ├── image_utils.c     # 图像处理工具
│   ├── file_utils.c      # 文件操作工具
│   └── image_drawing.c   # 图像绘制工具
├── 3rdparty/             # 第三方库
│   ├── opencv/           # OpenCV库
│   └── stb_image/        # STB图像库
├── model/                # 模型文件
│   └── yolov8.rknn      # RKNN格式模型
├── inputimage/           # 输入图像目录
├── outputimage/          # 输出结果目录
├── rknn_lib/             # RKNN运行时库
│   └── librknnrt.so     # RKNN动态库
└── build_cross_aarch64.sh # 交叉编译脚本
```

## 🔧 环境要求

### 硬件要求
- RK3588开发板
- 至少2GB RAM
- 存储空间：至少1GB可用空间

### 软件要求
- Ubuntu 18.04/20.04 (开发环境)
- GCC交叉编译工具链 (aarch64-none-linux-gnu)
- CMake 3.10+
- OpenCV 4.x (已包含在3rdparty中)

## 🚀 快速开始

### 1. 环境准备

确保已安装交叉编译工具链：
```bash
# 下载并解压交叉编译工具链
wget https://developer.arm.com/-/media/Files/downloads/gnu-a/10.3-2021.07/binrel/gcc-arm-10.3-2021.07-x86_64-aarch64-none-linux-gnu.tar.xz
tar -xf gcc-arm-10.3-2021.07-x86_64-aarch64-none-linux-gnu.tar.xz
```

### 2. 编译项目

使用提供的交叉编译脚本：
```bash
# 修改脚本中的编译器路径
vim build_cross_aarch64.sh
# 将GCC_COMPILER_PATH修改为您的实际路径

# 执行编译
./build_cross_aarch64.sh
```

编译完成后，在`build_aarch64/deploy`目录下会生成完整的部署包。

### 3. 部署到开发板

将部署包传输到RK3588开发板：
```bash
# 在开发板上解压部署包
tar -xzf yolov8_rk3588_deploy_*.tar.gz
cd yolov8_rk3588_deploy/

# 设置库路径
export LD_LIBRARY_PATH=./lib:$LD_LIBRARY_PATH

# 运行程序
./rknn_yolov8_demo
```

## 📖 使用方法

### 基本用法

1. **准备输入图像**：将待检测图像放入`inputimage/`目录
2. **运行检测**：执行`./rknn_yolov8_demo`
3. **查看结果**：检测结果保存在`outputimage/`目录

### 程序参数

程序会自动处理`inputimage/`目录下的所有图像文件，支持的格式包括：
- PNG
- JPG/JPEG
- BMP

### 检测配置

可以在`include/postprocess.h`中调整检测参数：
```cpp
#define NMS_THRESH 0.45      // NMS阈值
#define BOX_THRESH 0.25      // 置信度阈值
#define OBJ_CLASS_NUM 1      // 检测类别数
```

## 🔍 核心功能说明

### 图像预处理
- 自动缩放和letterbox填充
- 格式转换（RGB/RGBA/GRAY）
- 内存对齐优化

### 模型推理
- RKNN模型加载和初始化
- 输入数据预处理
- NPU推理执行
- 输出数据获取

### 后处理
- 边界框解码
- 置信度过滤
- NMS非极大值抑制
- 坐标映射回原图

### 结果可视化
- 绘制检测框
- 显示置信度
- 保存结果图像

## 🐛 常见问题

### Q: 编译时找不到交叉编译器
A: 请检查`build_cross_aarch64.sh`中的`GCC_COMPILER_PATH`路径是否正确。

### Q: 运行时提示库文件找不到
A: 确保设置了正确的`LD_LIBRARY_PATH`：
```bash
export LD_LIBRARY_PATH=./lib:$LD_LIBRARY_PATH
```

### Q: 检测精度不理想
A: 可以调整`postprocess.h`中的阈值参数，或检查输入图像质量。

### Q: 内存不足错误
A: 确保开发板有足够的可用内存，可以减少批处理的图像数量。

## 📝 开发说明

### 代码架构

- `main.cc`：程序入口，负责图像读取和批处理流程
- `yolov8.cc`：模型管理，包括初始化、推理和释放
- `postprocess.cc`：后处理算法，包括NMS和结果解析
- `utils/`：通用工具库，提供图像处理和文件操作功能

### 内存管理

项目统一使用普通内存分配（malloc/free），已移除DMA和RGA相关代码，简化了内存管理逻辑。

### 性能优化

- 使用RKNN推理引擎充分利用NPU算力
- 优化图像预处理流程
- 减少不必要的内存拷贝

## 📄 许可证

本项目仅供学习和研究使用。

## 🤝 贡献

欢迎提交Issue和Pull Request来改进项目。

## 📞 联系方式

- 作者：sugarong

---

⚠️ **重要提示**：本项目必须在RK3588等ARM64架构的开发板上运行，不支持x86虚拟机环境！
