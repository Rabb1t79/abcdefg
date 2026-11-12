cmake_minimum_required(VERSION 3.20)
project(TetrisVisionPilotBenchmark LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "TetrisVisionPilot is a native Windows desktop application. Build it on Windows with MSVC.")
endif()

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_executable(TetrisVisionPilotBenchmark WIN32 src/main.cpp)
target_compile_features(TetrisVisionPilotBenchmark PRIVATE cxx_std_17)
find_package(Tesseract CONFIG REQUIRED)
target_link_libraries(TetrisVisionPilotBenchmark PRIVATE user32 gdi32 comctl32 winmm d3d11 dxgi dwmapi windowsapp d3dcompiler Tesseract::libtesseract)
add_custom_command(TARGET TetrisVisionPilotBenchmark POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/tessdata"
        "$<TARGET_FILE_DIR:TetrisVisionPilotBenchmark>/tessdata")
add_custom_command(TARGET TetrisVisionPilotBenchmark POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/src/shaders"
        "$<TARGET_FILE_DIR:TetrisVisionPilotBenchmark>/shaders")
if(MSVC)
    target_compile_options(TetrisVisionPilotBenchmark PRIVATE /W4 /utf-8 /permissive- /EHsc)
endif()
