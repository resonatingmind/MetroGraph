cmake_minimum_required(VERSION 3.25)
project(MetroGraph VERSION 0.1.0 DESCRIPTION "Transit routing engine" LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(METROGRAPH_BUILD_TESTS "Build the unit tests" ON)
option(METROGRAPH_SANITIZE "Build everything with AddressSanitizer + UndefinedBehaviorSanitizer" OFF)
option(METROGRAPH_WERROR "Treat compiler warnings as errors (used in CI)" OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# Sanitizers are applied globally so third-party code (Catch2) is instrumented too;
# mixing instrumented and uninstrumented objects produces false positives.
if(METROGRAPH_SANITIZE)
    if(MSVC)
        message(FATAL_ERROR "METROGRAPH_SANITIZE targets GCC/Clang; use the Linux 'asan' preset or CI job.")
    endif()
    add_compile_options(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -g)
    add_link_options(-fsanitize=address,undefined)
endif()

# Warnings apply to our own targets only.
add_library(metrograph_warnings INTERFACE)
if(MSVC)
    target_compile_options(metrograph_warnings INTERFACE /W4 /permissive- /utf-8)
else()
    target_compile_options(metrograph_warnings INTERFACE -Wall -Wextra -Wpedantic -Wshadow -Wconversion)
endif()
if(METROGRAPH_WERROR)
    target_compile_options(metrograph_warnings INTERFACE $<IF:$<CXX_COMPILER_ID:MSVC>,/WX,-Werror>)
endif()

# ---- core library ---------------------------------------------------------
add_library(metrograph_core STATIC
    src/geo.cpp
    src/graph.cpp
    src/history.cpp
    src/nearest.cpp
    src/network.cpp
    src/routing.cpp
)
target_include_directories(metrograph_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(metrograph_core PRIVATE metrograph_warnings)

# ---- console application --------------------------------------------------
add_executable(metrograph
    app/console.cpp
    app/format.cpp
    app/main.cpp
)
target_link_libraries(metrograph PRIVATE metrograph_core metrograph_warnings)

# ---- tests ----------------------------------------------------------------
if(METROGRAPH_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
