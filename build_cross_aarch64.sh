#!/bin/bash
set -e

# 获取脚本所在的绝对路径
ROOT_PWD=$(cd "$(dirname "$0")" && pwd)

# 设置交叉编译器路径 (请根据您的实际路径修改)
GCC_COMPILER_PATH="/home/sugarong/gcc-arm-10.3-2021.07-x86_64-aarch64-none-linux-gnu/bin/aarch64-none-linux-gnu"

# 设置构建目录
BUILD_DIR=${ROOT_PWD}/build_aarch64

# 设置部署目录
DEPLOY_DIR=${BUILD_DIR}/deploy

echo "=== YOLOv8 RK3588 交叉编译和部署脚本 ==="
echo "项目路径: ${ROOT_PWD}"
echo "构建目录: ${BUILD_DIR}"
echo "部署目录: ${DEPLOY_DIR}"
echo "交叉编译器: ${GCC_COMPILER_PATH}"
echo ""

# 清理或创建构建目录
if [ -d "${BUILD_DIR}" ]; then
  echo "清理现有构建目录: ${BUILD_DIR}"
  rm -rf "${BUILD_DIR}"
fi
mkdir -p "${BUILD_DIR}"

echo "进入构建目录: ${BUILD_DIR}"
cd "${BUILD_DIR}"

echo "配置CMake for aarch64..."
cmake .. \
    -DCMAKE_SYSTEM_NAME=Linux \
    -DCMAKE_SYSTEM_PROCESSOR=aarch64 \
    -DCMAKE_C_COMPILER=${GCC_COMPILER_PATH}-gcc \
    -DCMAKE_CXX_COMPILER=${GCC_COMPILER_PATH}-g++ \
    -DCMAKE_FIND_ROOT_PATH_MODE_PROGRAM=NEVER \
    -DCMAKE_FIND_ROOT_PATH_MODE_LIBRARY=ONLY \
    -DCMAKE_FIND_ROOT_PATH_MODE_INCLUDE=ONLY \
    -DCMAKE_FIND_ROOT_PATH_MODE_PACKAGE=ONLY \
    -DTARGET_SOC=RK3588

if [ $? -ne 0 ]; then
    echo "❌ CMake配置失败！"
    exit 1
fi

echo "开始编译项目..."
make -j$(nproc)

if [ $? -ne 0 ]; then
    echo "❌ 编译失败！"
    exit 1
fi

echo "生成部署包..."
make install

if [ $? -ne 0 ]; then
    echo "❌ 安装失败！"
    exit 1
fi

# 检查部署包是否生成成功
if [ -d "${DEPLOY_DIR}" ]; then
    echo "✅ 部署包生成成功！"
    echo ""
    echo "=== 部署包结构 ==="
    ls -la "${DEPLOY_DIR}"
    echo ""
    
    # 显示部署包大小
    DEPLOY_SIZE=$(du -sh "${DEPLOY_DIR}" | cut -f1)
    echo "部署包大小: ${DEPLOY_SIZE}"
    
    # 创建压缩包
    echo "创建压缩包..."
    cd "${BUILD_DIR}"
    ARCHIVE_NAME="yolov8_rk3588_deploy_$(date +%Y%m%d_%H%M%S).tar.gz"
    tar -czf "${ARCHIVE_NAME}" deploy/
    
    if [ $? -eq 0 ]; then
        ARCHIVE_SIZE=$(du -sh "${ARCHIVE_NAME}" | cut -f1)
        echo "✅ 压缩包创建成功: ${BUILD_DIR}/${ARCHIVE_NAME}"
        echo "压缩包大小: ${ARCHIVE_SIZE}"
        echo ""
        echo "=== 部署说明 ==="
        echo "1. 将压缩包传输到RK3588设备:"
        echo "   scp ${BUILD_DIR}/${ARCHIVE_NAME} user@rk3588-device:/path/to/destination/"
        echo ""
        echo "2. 在RK3588设备上解压:"
        echo "   tar -xzf ${ARCHIVE_NAME}"
        echo "   cd deploy"
        echo ""
        echo "3. 运行程序:"
        echo "   # 检测单张图像"
        echo "   ./run.sh input/frame_00000.png"
        echo ""
        echo "   # 检测图像文件夹"
        echo "   ./run.sh input/ output/"
        echo ""
        echo "   # 直接运行可执行文件"
        echo "   export LD_LIBRARY_PATH=\$PWD/lib:\$LD_LIBRARY_PATH"
        echo "   ./bin/rknn_yolov8_demo <图像路径> [输出路径]"
    else
        echo "❌ 压缩包创建失败！"
    fi
else
    echo "❌ 部署包生成失败！"
    exit 1
fi

cd "${ROOT_PWD}"
echo ""
echo "🎉 交叉编译和部署包生成完成！"
echo "构建目录: ${BUILD_DIR}"
echo "部署包: ${DEPLOY_DIR}"
echo "压缩包: ${BUILD_DIR}/${ARCHIVE_NAME}"