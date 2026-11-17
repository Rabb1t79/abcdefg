cmake_minimum_required(VERSION 3.20)
project(TopRuntimeProbe LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "TopRuntimeProbe is a Windows-only external process reader.")
endif()
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "TopRuntimeProbe must be built as Win32/x86 to match TO-P_CHS_05.")
endif()

add_executable(TopRuntimeProbe src/main.cpp)
target_compile_features(TopRuntimeProbe PRIVATE cxx_std_17)
target_compile_definitions(TopRuntimeProbe PRIVATE
    UNICODE
    _UNICODE
    _WIN32_WINNT=0x0A00
)
target_compile_options(TopRuntimeProbe PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
    $<$<CXX_COMPILER_ID:MSVC>:/permissive->
    $<$<CXX_COMPILER_ID:MSVC>:/EHsc>
)
target_link_libraries(TopRuntimeProbe PRIVATE bcrypt)

enable_testing()
add_test(
    NAME TopRuntimeProbeBoardDecodeSelfTest
    COMMAND TopRuntimeProbe --self-test
)
