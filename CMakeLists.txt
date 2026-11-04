cmake_minimum_required(VERSION 3.24)
project(streambrew LANGUAGES CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GSTREAMER REQUIRED IMPORTED_TARGET gstreamer-1.0 gstreamer-app-1.0 gstreamer-video-1.0)

add_library(streambrew src/pipeline/frame_pipeline.cpp)
target_include_directories(streambrew PUBLIC src)
target_compile_features(streambrew PUBLIC cxx_std_17)
target_link_libraries(streambrew PUBLIC PkgConfig::GSTREAMER)

add_executable(main src/main.cpp)
target_link_libraries(main PRIVATE streambrew)

include(CTest)
if(BUILD_TESTING)
    add_executable(frame_pipeline_options_test tests/frame_pipeline_options_test.cpp)
    target_link_libraries(frame_pipeline_options_test PRIVATE streambrew)
    add_test(NAME frame_pipeline_options COMMAND frame_pipeline_options_test
        "${CMAKE_CURRENT_BINARY_DIR}/pipeline-options-fixture.avi"
        "${CMAKE_CURRENT_BINARY_DIR}/pipeline-options-audio-fixture.avi")
    set_tests_properties(frame_pipeline_options PROPERTIES TIMEOUT 30)
endif()
