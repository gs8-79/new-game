cmake_minimum_required(VERSION 3.20)

project(mud_demos VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

add_library(mud_lib
    src/core.cpp
    src/starport.cpp
    src/island.cpp
    src/tribe.cpp
)
target_include_directories(mud_lib PUBLIC src)

if(MSVC)
    target_compile_options(mud_lib PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(mud_lib PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(mud-demos src/main.cpp)
target_link_libraries(mud-demos PRIVATE mud_lib)

if(MSVC)
    target_compile_options(mud-demos PRIVATE /W4 /permissive- /utf-8)
endif()

enable_testing()

add_executable(mud-tests
    tests/test_main.cpp
    tests/core_tests.cpp
    tests/starport_tests.cpp
    tests/island_tests.cpp
    tests/tribe_tests.cpp
)
target_link_libraries(mud-tests PRIVATE mud_lib)
target_include_directories(mud-tests PRIVATE tests)

if(MSVC)
    target_compile_options(mud-tests PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(mud-tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_test(NAME mud-tests COMMAND mud-tests)
