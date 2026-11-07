cmake_minimum_required(VERSION 3.20)
project(TetrisVisionPilot LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "TetrisVisionPilot is a native Windows desktop application. Build it on Windows with MSVC.")
endif()

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_executable(TetrisVisionPilot WIN32 src/main.cpp)
target_compile_features(TetrisVisionPilot PRIVATE cxx_std_17)
target_link_libraries(TetrisVisionPilot PRIVATE user32 gdi32 comctl32)
if(MSVC)
    target_compile_options(TetrisVisionPilot PRIVATE /W4 /utf-8 /permissive- /EHsc)
endif()
