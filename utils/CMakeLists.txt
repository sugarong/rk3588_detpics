cmake_minimum_required(VERSION 3.10)

project(rknn_model_zoo_utils)

add_library(fileutils STATIC
    file_utils.c
)
target_include_directories(fileutils PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

add_library(imagedrawing STATIC
    image_drawing.c
)
target_include_directories(imagedrawing PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# RGA相关定义已移除，统一使用CPU处理
# if (TARGET_SOC STREQUAL "rv1106" OR TARGET_SOC STREQUAL "rk3588")
#     add_definitions(-DLIBRGA_IM2D_HANDLE)
# endif()

# DMA分配器源文件已移除
# file(GLOB DMA_SRCS ${CMAKE_CURRENT_SOURCE_DIR}/../3rdparty/allocator/dma/*.cpp)

add_library(imageutils STATIC
    image_utils.c
    # DMA源文件已移除
)

target_include_directories(imageutils PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    # DMA头文件路径已移除
    # ${CMAKE_CURRENT_SOURCE_DIR}/../3rdparty/allocator/dma
)

target_link_libraries(imageutils
    ${LIBJPEG}
    # RGA库已移除
    # ${LIBRGA}
)

target_include_directories(imageutils PUBLIC
    ${STB_INCLUDES}
    ${LIBJPEG_INCLUDES}
    # RGA头文件已移除
    # ${LIBRGA_INCLUDES}
)

add_library(audioutils STATIC
    audio_utils.c
)

target_link_libraries(audioutils
    ${LIBSNDFILE}
)

target_include_directories(audioutils PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${LIBSNDFILE_INCLUDES}
)