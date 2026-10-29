cmake_minimum_required(VERSION 3.24)
project(stream_brew LANGUAGES CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GSTREAMER REQUIRED IMPORTED_TARGET gstreamer-1.0 gstreamer-app-1.0 gstreamer-video-1.0)

add_executable(main src/main.cpp src/pipeline/frame_pipeline.cpp)
target_include_directories(main PRIVATE src)
target_compile_features(main PRIVATE cxx_std_17)
target_link_libraries(main PRIVATE PkgConfig::GSTREAMER)
