cmake_minimum_required(VERSION 3.10)
project(my_deepstream_app)

# 设置C++标准
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(OpenCV_DIR "/home/lin/opencv-4.10.0/build")

# 查找必要的包
find_package(CUDA REQUIRED)
find_package(PkgConfig REQUIRED)
find_package(OpenCV REQUIRED)
pkg_check_modules(GST REQUIRED 
    gstreamer-1.0
    gstreamer-app-1.0
    gstreamer-video-1.0
)

# DeepStream SDK 路径（根据实际安装路径调整）
set(DEEPSTREAM_PATH "/opt/nvidia/deepstream/deepstream")
set(DEEPSTREAM_INCLUDE_PATH "${DEEPSTREAM_PATH}/sources/includes")
set(DEEPSTREAM_LIB_PATH "${DEEPSTREAM_PATH}/lib")

# 海康相机SDK路径（根据实际安装路径调整）
set(HIK_SDK_PATH "/opt/MVS")
set(HIK_INCLUDE_PATH "${HIK_SDK_PATH}/include")
set(HIK_LIB_PATH "${HIK_SDK_PATH}/lib/aarch64")

# 添加包含目录
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${GST_INCLUDE_DIRS}
    ${HIK_INCLUDE_PATH}
    ${DEEPSTREAM_INCLUDE_PATH}
    ${CUDA_INCLUDE_DIRS}
    ${OpenCV_INCLUDE_DIRS}
)

# 添加链接目录
link_directories(
    ${GST_LIBRARY_DIRS}
    ${HIK_LIB_PATH}
    ${DEEPSTREAM_LIB_PATH}
    ${CUDA_INCLUDE_DIRS}
    ${OpenCV_LIBRARIES}

)

# 添加源文件
file(GLOB SOURCES "${PROJECT_SOURCE_DIR}/*.cpp")


# 创建动态库
add_library(nvdsparsedata SHARED
    nvdsparsedata.cpp
)

# 设置动态库属性
set_target_properties(nvdsparsedata PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)

# 生成可执行文件
add_executable(${PROJECT_NAME} ${SOURCES})

# 链接库
target_link_libraries(${PROJECT_NAME}
    MvCameraControl
    ${GST_LIBRARIES}
    ${CUDA_LIBRARIES}
    ${OpenCV_LIBRARIES}
    nvdsgst_meta
    nvds_meta
    nvds_infer
    nvdsparsedata

  
)

# 设置输出目录
set_target_properties(nvdsparsedata PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
)

set_target_properties(${PROJECT_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
