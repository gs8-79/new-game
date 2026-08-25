cmake_minimum_required(VERSION 3.20)

project(mud_demos VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
option(TRIBE_FORMAL_ONLY "Build only the formal Tribe Dawn game" OFF)
enable_testing()

if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

if(NOT TRIBE_FORMAL_ONLY)
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
endif()

add_library(tribe_formal_lib
    formal/src/formal_types.cpp
    formal/src/content.cpp
    formal/src/battle.cpp
    formal/src/game_engine.cpp
    formal/src/save_repository.cpp
    formal/src/console_ui.cpp
)
target_include_directories(tribe_formal_lib PUBLIC formal/include)

if(MSVC)
    target_compile_options(tribe_formal_lib PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(tribe_formal_lib PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(tribe-dawn formal/src/main.cpp)
target_link_libraries(tribe-dawn PRIVATE tribe_formal_lib)

if(MSVC)
    target_compile_options(tribe-dawn PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(tribe-dawn PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(tribe-formal-tests
    tests/test_main.cpp
    formal/tests/formal_tests.cpp
)
target_link_libraries(tribe-formal-tests PRIVATE tribe_formal_lib)
target_include_directories(tribe-formal-tests PRIVATE tests)

if(MSVC)
    target_compile_options(tribe-formal-tests PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(tribe-formal-tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_test(NAME tribe-formal-tests COMMAND tribe-formal-tests)
