cmake_minimum_required(VERSION 3.20)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(direct_capture_launcher LANGUAGES CXX)
endif()

if(NOT WIN32)
    message(FATAL_ERROR "The direct capture launcher requires Windows")
endif()

add_executable(ce-stream-launcher WIN32 stream_launcher.cpp)
target_compile_features(ce-stream-launcher PRIVATE cxx_std_20)
if(MINGW)
    target_link_options(ce-stream-launcher PRIVATE -municode -static)
endif()
