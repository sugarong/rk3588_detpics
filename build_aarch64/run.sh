#!/bin/bash
# YOLOv8 目标检测程序启动脚本（支持DMA优化）

# 设置库路径
export LD_LIBRARY_PATH=$PWD/lib:$LD_LIBRARY_PATH

# 检查DMA设备权限
if [ ! -r /dev/dma_heap/system-dma32 ]; then
    echo "警告: DMA设备权限不足，将使用CPU处理"
fi

# 检查参数
if [ $# -eq 0 ]; then
    echo "使用方法:"
    echo "  检测单张图像: ./run.sh <图像路径>"
    echo "  检测图像文件夹: ./run.sh <文件夹路径> [输出文件夹]"
    echo ""
    echo "示例:"
    echo "  ./run.sh input/frame_00000.png"
    echo "  ./run.sh input/ output/"
    exit 1
fi

# 运行程序
./bin/rknn_yolov8_demo "$@"
