cmake_minimum_required(VERSION 3.20)

project(tribe_dawn VERSION 2.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
enable_testing()

if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

add_library(tribe_formal_lib
    formal/src/formal_types.cpp
    formal/src/expansion_types.cpp
    formal/src/expansion_game.cpp
    formal/src/campaign.cpp
    formal/src/campaign_migration.cpp
    formal/src/campaign_save_repository.cpp
    formal/src/ending_presentation.cpp
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
    formal/tests/baseline_gameplay_tests.cpp
    formal/tests/save_reliability_tests.cpp
    formal/tests/expansion_domain_tests.cpp
    formal/tests/expansion_game_tests.cpp
    formal/tests/campaign_tests.cpp
    formal/tests/campaign_autonomy_tests.cpp
    formal/tests/campaign_migration_tests.cpp
    formal/tests/campaign_route_tests.cpp
    formal/tests/campaign_save_tests.cpp
    formal/tests/ending_presentation_tests.cpp
)
target_link_libraries(tribe-formal-tests PRIVATE tribe_formal_lib)
target_include_directories(tribe-formal-tests PRIVATE tests)

if(MSVC)
    target_compile_options(tribe-formal-tests PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(tribe-formal-tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_test(NAME tribe-formal-tests COMMAND tribe-formal-tests)
