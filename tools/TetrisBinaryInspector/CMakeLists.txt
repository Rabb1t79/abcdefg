# Standalone Win32 x86 target; does not depend on the vision-player toolchain.
cmake_minimum_required(VERSION 3.20)
project(TetrisBinaryInspector LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "TetrisBinaryInspector is a Windows-only process inspector.")
endif()

add_executable(TetrisBinaryInspector src/main.cpp)
target_compile_features(TetrisBinaryInspector PRIVATE cxx_std_17)
target_compile_definitions(TetrisBinaryInspector PRIVATE
    UNICODE _UNICODE _WIN32_WINNT=0x0A00)
target_compile_options(TetrisBinaryInspector PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
    $<$<CXX_COMPILER_ID:MSVC>:/permissive->
    $<$<CXX_COMPILER_ID:MSVC>:/EHsc>)
target_link_libraries(TetrisBinaryInspector PRIVATE bcrypt)

enable_testing()
add_test(NAME TetrisBinaryInspectorSelfTest
    COMMAND TetrisBinaryInspector --self-test)
