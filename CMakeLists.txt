cmake_minimum_required(VERSION 3.10)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)
project(rknn_yolov8_demo)

if (ENABLE_ASAN)
	message(STATUS "BUILD WITH ADDRESS SANITIZER")
	set (CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -fno-omit-frame-pointer -fsanitize=address")
	set (CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -fno-omit-frame-pointer -fsanitize=address")
	set (CMAKE_LINKER_FLAGS_DEBUG "${CMAKE_LINKER_FLAGS_DEBUG} -fno-omit-frame-pointer -fsanitize=address")
endif ()

# 设置C++标准
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(rknpu_yolov8_file src/yolov8.cc)
set(RKNNRT_LIB_PATH ${CMAKE_CURRENT_SOURCE_DIR}/rknn_lib)  
link_directories(${RKNNRT_LIB_PATH})

# DMA分配器和RGA相关头文件已移除，统一使用CPU处理和普通内存
# include_directories(${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/allocator/dma)
# include_directories(${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/librga/include)

if (TARGET_SOC STREQUAL "rv1106" OR TARGET_SOC STREQUAL "rv1103")
    add_definitions(-DRV1106_1103)
    set(rknpu_yolov8_file rknpu2/yolov8_rv1106_1103.cc)
endif()

if(TARGET_SOC STREQUAL "rk1808" OR TARGET_SOC STREQUAL "rv1109" OR TARGET_SOC STREQUAL "rv1126")
    add_definitions(-DRKNPU1)
    set(rknpu_yolov8_file rknpu1/yolov8.cc)
endif()

# opencv
set(OpenCV_DIR ${CMAKE_SOURCE_DIR}/3rdparty/opencv/opencv-linux-aarch64/share/OpenCV)
find_package(OpenCV REQUIRED)
include_directories(${OpenCV_INCLUDE_DIRS})

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/ 3rdparty.out)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/utils/ utils.out)

set(CMAKE_INSTALL_PATH "$ORIGIN/../lib")

# DMA分配器源文件已移除
# file(GLOB DMA_SRCS ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/allocator/dma/*.cpp)

# 创建可执行文件（包含DMA源文件）
# 移除这些行（第177-183行）：
# 添加DMA分配器源文件
# set(CMAKE_CXX_STANDARD 11)
# file(GLOB SRCS ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cc)
# file(GLOB DMA_SRCS ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/allocator/dma/*.cpp)
# 
# add_executable(rknn_yolov8_demo
#     ${SRCS}
#     ${DMA_SRCS}  # 添加DMA源文件
# )

# 保持原来的add_executable定义（第32-36行）
add_executable(${PROJECT_NAME}
    src/main.cc
    src/postprocess.cc
    ${rknpu_yolov8_file}
)

# 链接库
target_link_libraries(${PROJECT_NAME}
    imageutils
    fileutils
    imagedrawing    
    ${OpenCV_LIBS} 
    rknnrt
    dl
)

if (CMAKE_SYSTEM_NAME STREQUAL "Android")
    target_link_libraries(${PROJECT_NAME}
    log
)
endif()

message(STATUS "!!!!!!!!!!!CMAKE_SYSTEM_NAME: ${CMAKE_SYSTEM_NAME}")
if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(THREADS_PREFER_PTHREAD_FLAG ON)
    find_package(Threads REQUIRED)
    target_link_libraries(${PROJECT_NAME} Threads::Threads)
endif()

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    # DMA和RGA头文件路径已移除
    # ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/allocator/dma
    # ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/librga/include
    ${LIBRKNNRT_INCLUDES}
)

# ========== 优化的安装配置 ==========
# 设置安装前缀
if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set(CMAKE_INSTALL_PREFIX "${CMAKE_BINARY_DIR}/deploy" CACHE PATH "Installation Directory" FORCE)
endif()

# 或者直接强制设置
set(CMAKE_INSTALL_PREFIX "${CMAKE_BINARY_DIR}/deploy" CACHE PATH "Installation Directory" FORCE)

# 1. 安装可执行文件到bin目录
install(TARGETS ${PROJECT_NAME} 
    RUNTIME DESTINATION bin
    COMPONENT Runtime
)

# 2. 安装模型文件到model目录（已移除标签文件安装）
# 安装所有.rknn模型文件
file(GLOB RKNN_FILES "${CMAKE_SOURCE_DIR}/model/*.rknn")
if(RKNN_FILES)
    install(FILES ${RKNN_FILES} 
        DESTINATION model
        COMPONENT Runtime
    )
else()
    message(WARNING "No .rknn files found in model directory")
endif()

# 3. 创建输出目录
install(CODE "file(MAKE_DIRECTORY \"${CMAKE_INSTALL_PREFIX}/output\")" 
    COMPONENT Runtime
)

# 4. 安装测试图像（可选）
file(GLOB INPUT_IMAGES "${CMAKE_SOURCE_DIR}/inputimage/*.png" "${CMAKE_SOURCE_DIR}/inputimage/*.jpg")
if(INPUT_IMAGES)
    install(FILES ${INPUT_IMAGES} 
        DESTINATION input
        COMPONENT Runtime
    )
else()
    message(STATUS "No input images found, creating empty input directory")
    install(CODE "file(MAKE_DIRECTORY \"${CMAKE_INSTALL_PREFIX}/input\")" 
        COMPONENT Runtime
    )
endif()

# 5. 安装运行时库
# 安装RKNN运行时库
file(GLOB RKNN_LIBS "${CMAKE_SOURCE_DIR}/rknn_lib/*.so*")
if(RKNN_LIBS)
    install(FILES ${RKNN_LIBS} 
        DESTINATION lib
        COMPONENT Runtime
    )
endif()

# 安装OpenCV库（如果是静态链接的话可以跳过）
if(OpenCV_LIBS)
    foreach(lib ${OpenCV_LIBS})
        if(EXISTS ${lib})
            get_filename_component(lib_name ${lib} NAME)
            if(lib_name MATCHES "\.so")
                install(FILES ${lib} 
                    DESTINATION lib
                    COMPONENT Runtime
                )
            endif()
        endif()
    endforeach()
endif()

# 6. 创建启动脚本
set(RUN_SCRIPT_CONTENT 
"#!/bin/bash
# YOLOv8 目标检测程序启动脚本（支持DMA优化）

# 设置库路径
export LD_LIBRARY_PATH=\$PWD/lib:\$LD_LIBRARY_PATH

# 检查DMA设备权限
if [ ! -r /dev/dma_heap/system-dma32 ]; then
    echo \"警告: DMA设备权限不足，将使用CPU处理\"
fi

# 检查参数
if [ \$# -eq 0 ]; then
    echo \"使用方法:\"
    echo \"  检测单张图像: ./run.sh <图像路径>\"
    echo \"  检测图像文件夹: ./run.sh <文件夹路径> [输出文件夹]\"
    echo \"\"
    echo \"示例:\"
    echo \"  ./run.sh input/frame_00000.png\"
    echo \"  ./run.sh input/ output/\"
    exit 1
fi

# 运行程序
./bin/rknn_yolov8_demo \"\$@\"
")

# 写入启动脚本
file(WRITE "${CMAKE_BINARY_DIR}/run.sh" "${RUN_SCRIPT_CONTENT}")
install(FILES "${CMAKE_BINARY_DIR}/run.sh" 
    DESTINATION .
    PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE
    COMPONENT Runtime
)

# 打印构建信息
message(STATUS "=== 构建配置信息 ===")
message(STATUS "项目名称: ${PROJECT_NAME}")
message(STATUS "C++标准: ${CMAKE_CXX_STANDARD}")
message(STATUS "目标平台: ${TARGET_SOC}")
message(STATUS "DMA支持: 已启用")
message(STATUS "安装路径: ${CMAKE_INSTALL_PREFIX}")
message(STATUS "========================")

