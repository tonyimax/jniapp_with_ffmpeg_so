cmake_minimum_required(VERSION 3.22.1)
project("jniapp")

set(FFMPEG_DIR ${CMAKE_SOURCE_DIR}/prebuilt)
set(FFMPEG_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/include)

add_library(${CMAKE_PROJECT_NAME} SHARED
        native-lib.cpp
)

find_library(
        log-lib
        log
)

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE ${FFMPEG_INCLUDE_DIR})
target_link_directories(${CMAKE_PROJECT_NAME} PRIVATE ${FFMPEG_DIR}/${ANDROID_ABI})

target_link_libraries(${CMAKE_PROJECT_NAME}
        android
        log
        avutil
        avformat
        avcodec
        swscale
        ${log-lib}
)