cmake_minimum_required(VERSION 3.20)

project(tribe_dawn VERSION 2.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
enable_testing()

set(TRIBE_LLVM_HINTS
    "$ENV{ProgramFiles}/Microsoft Visual Studio/18/Community/VC/Tools/Llvm/x64/bin"
    "C:/Program Files/Microsoft Visual Studio/18/Community/VC/Tools/Llvm/x64/bin"
)
find_program(CLANG_FORMAT_EXECUTABLE NAMES clang-format clang-format.exe HINTS ${TRIBE_LLVM_HINTS})
find_program(CLANG_TIDY_EXECUTABLE NAMES clang-tidy clang-tidy.exe HINTS ${TRIBE_LLVM_HINTS})
find_program(POWERSHELL_EXECUTABLE NAMES pwsh powershell)

if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

add_library(tribe_formal_lib
    formal/src/expansion_types.cpp
    formal/src/expansion_game.cpp
    formal/src/population_rules.cpp
    formal/src/seasonal_event_rules.cpp
    formal/src/war_rules.cpp
    formal/src/game_engine.cpp
    formal/src/save_repository.cpp
    formal/src/ending_presentation.cpp
    formal/src/console_ui.cpp
    formal/src/application.cpp
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
    formal/tests/current_game_tests.cpp
    formal/tests/map_mission_tests.cpp
    formal/tests/save_repository_tests.cpp
    formal/tests/application_workflow_tests.cpp
)
target_link_libraries(tribe-formal-tests PRIVATE tribe_formal_lib)
target_include_directories(tribe-formal-tests PRIVATE tests)

if(MSVC)
    target_compile_options(tribe-formal-tests PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(tribe-formal-tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_test(NAME tribe-formal-tests COMMAND tribe-formal-tests)

set(TRIBE_FORMAT_SOURCES
    formal/include/tribe/application.hpp
    formal/include/tribe/console_ui.hpp
    formal/include/tribe/ending_presentation.hpp
    formal/include/tribe/expansion_game.hpp
    formal/include/tribe/expansion_types.hpp
    formal/include/tribe/game_engine.hpp
    formal/include/tribe/save_repository.hpp
    formal/src/application.cpp
    formal/src/console_ui.cpp
    formal/src/ending_presentation.cpp
    formal/src/expansion_game.cpp
    formal/src/expansion_types.cpp
    formal/src/game_engine.cpp
    formal/src/main.cpp
    formal/src/population_rules.hpp
    formal/src/population_rules.cpp
    formal/src/seasonal_event_rules.hpp
    formal/src/seasonal_event_rules.cpp
    formal/src/save_repository.cpp
    formal/src/war_rules.hpp
    formal/src/war_rules.cpp
    formal/tests/application_workflow_tests.cpp
    formal/tests/current_game_tests.cpp
    formal/tests/map_mission_tests.cpp
    formal/tests/save_repository_tests.cpp
    tests/test_harness.hpp
    tests/test_main.cpp
)

if(CLANG_FORMAT_EXECUTABLE)
    add_custom_target(format
        COMMAND "${CLANG_FORMAT_EXECUTABLE}" -i ${TRIBE_FORMAT_SOURCES}
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        COMMENT "Formatting C++ sources with clang-format"
        VERBATIM
    )
    add_custom_target(format-check
        COMMAND "${CLANG_FORMAT_EXECUTABLE}" --dry-run --Werror ${TRIBE_FORMAT_SOURCES}
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        COMMENT "Checking C++ formatting with clang-format"
        VERBATIM
    )
else()
    message(STATUS "clang-format was not found; format and format-check targets are unavailable.")
endif()

if(CLANG_TIDY_EXECUTABLE AND POWERSHELL_EXECUTABLE)
    add_custom_target(tidy
        COMMAND "${POWERSHELL_EXECUTABLE}" -NoProfile -ExecutionPolicy Bypass
            -File "${CMAKE_CURRENT_SOURCE_DIR}/tools/run-clang-tidy.ps1"
            -SourceRoot "${CMAKE_CURRENT_SOURCE_DIR}"
            -ClangTidy "${CLANG_TIDY_EXECUTABLE}"
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        COMMENT "Analyzing C++ sources with clang-tidy"
        VERBATIM
    )
else()
    message(STATUS "clang-tidy or PowerShell was not found; the tidy target is unavailable.")
endif()

find_program(DOXYGEN_EXECUTABLE NAMES doxygen doxygen.exe HINTS "$ENV{DOXYGEN_ROOT}" "D:/tools/doxygen")
if(DOXYGEN_EXECUTABLE)
    add_custom_target(docs
        COMMAND "${DOXYGEN_EXECUTABLE}" Doxyfile
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        COMMENT "Generating API documentation with Doxygen"
        VERBATIM
    )
else()
    message(STATUS "Doxygen was not found; the docs target is unavailable.")
endif()
