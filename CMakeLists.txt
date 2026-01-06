cmake_minimum_required(VERSION 3.10)
project(jetson_motion_tracker)

set(CMAKE_CXX_STANDARD 17)

# Optimize for runtime performance by default
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")

find_package(PkgConfig REQUIRED)
find_package(Threads REQUIRED)
find_package(OpenCV REQUIRED)

pkg_check_modules(GSTREAMER REQUIRED gstreamer-1.0)
pkg_check_modules(GSTREAMER_APP REQUIRED gstreamer-app-1.0)

include_directories(
    src
    src/pipeline
    src/processing
    ${OpenCV_INCLUDE_DIRS}
    ${GSTREAMER_INCLUDE_DIRS}
    ${GSTREAMER_APP_INCLUDE_DIRS}
)

add_executable(jetson_motion_tracker
    src/main.cpp
    src/pipeline/v4l2_source.cpp
    src/pipeline/nvargus_source.cpp
    src/pipeline/video_file_source.cpp
    src/pipeline/image_sequence_source.cpp
    src/processing/aruco_tracker.cpp
    src/util/csv_logger.h
)

target_link_libraries(jetson_motion_tracker
    ${OpenCV_LIBS}
    ${GSTREAMER_LIBRARIES}
    ${GSTREAMER_APP_LIBRARIES}
    Threads::Threads
)
