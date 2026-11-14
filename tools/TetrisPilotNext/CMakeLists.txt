cmake_minimum_required(VERSION 3.20)
project(TetrisPilotNext LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(tetris_pilot_core STATIC
    src/solver.cpp
    src/session_state.cpp
)
target_include_directories(tetris_pilot_core PUBLIC include)
if(MSVC)
    target_compile_options(tetris_pilot_core PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(tetris_pilot_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(WIN32)
    target_sources(tetris_pilot_core PRIVATE src/win32/process_inventory.cpp)
    target_include_directories(tetris_pilot_core PUBLIC platform/win32)
    target_link_libraries(tetris_pilot_core PUBLIC user32)
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(tetris_pilot_core_tests tests/core_tests.cpp)
    target_link_libraries(tetris_pilot_core_tests PRIVATE tetris_pilot_core)
    add_test(NAME tetris_pilot_core_tests COMMAND tetris_pilot_core_tests)
endif()
