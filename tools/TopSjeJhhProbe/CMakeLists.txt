cmake_minimum_required(VERSION 3.20)
project(TopSjeJhhProbe LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "TopSjeJhhProbe is a Windows-only TOP archive utility.")
endif()

add_executable(TopSjeJhhProbe src/main.cpp)
target_compile_features(TopSjeJhhProbe PRIVATE cxx_std_17)
target_compile_definitions(TopSjeJhhProbe PRIVATE
    UNICODE
    _UNICODE
    _WIN32_WINNT=0x0A00
)
target_compile_options(TopSjeJhhProbe PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
    $<$<CXX_COMPILER_ID:MSVC>:/permissive->
    $<$<CXX_COMPILER_ID:MSVC>:/EHsc>
)
target_link_libraries(TopSjeJhhProbe PRIVATE advapi32 bcrypt)

enable_testing()
add_test(
    NAME TopSjeJhhProbeSelfTest
    COMMAND TopSjeJhhProbe --self-test
)
