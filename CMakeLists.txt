cmake_minimum_required(VERSION 3.20 FATAL_ERROR)

project(tsc
    VERSION 1.1.0
    DESCRIPTION "Header-only thread-safe bounded FIFO container"
    LANGUAGES CXX)

# ---------------------------------------------------------------------------
# Top-level options
# ---------------------------------------------------------------------------
option(TSC_BUILD_APP      "Build the stress-test demo application"   ON)
option(TSC_BUILD_EXAMPLES "Build minimal usage examples"             ON)
option(TSC_BUILD_TESTS    "Build unit tests (requires Catch2)"       ON)
option(TSC_BUILD_BENCHMARKS "Build microbenchmarks (requires Google Benchmark)" OFF)
option(TSC_WARNINGS_AS_ERRORS "Treat compile warnings as errors"     OFF)

option(ENABLE_ASAN "Enable AddressSanitizer + UndefinedBehaviorSanitizer" OFF)
option(ENABLE_TSAN "Enable ThreadSanitizer"                              OFF)

if(ENABLE_ASAN AND ENABLE_TSAN)
    message(FATAL_ERROR
        "ENABLE_ASAN and ENABLE_TSAN are mutually exclusive. "
        "Configure two separate build directories instead.")
endif()

# Default build type
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# Language baseline (C++20)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Export compile_commands.json for clang-tidy / IDEs
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ---------------------------------------------------------------------------
# Helper: project-wide compile flags applied to non-INTERFACE targets.
# ---------------------------------------------------------------------------
add_library(tsc_project_options INTERFACE)

target_compile_options(tsc_project_options INTERFACE
    $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra -Wpedantic>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
)

if(TSC_WARNINGS_AS_ERRORS)
    target_compile_options(tsc_project_options INTERFACE
        $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Werror>
        $<$<CXX_COMPILER_ID:MSVC>:/WX>
    )
endif()

if(ENABLE_ASAN)
    message(STATUS "AddressSanitizer + UBSan enabled")
    target_compile_options(tsc_project_options INTERFACE
        $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-g -O1 -fno-omit-frame-pointer -fsanitize=address -fsanitize=undefined>)
    target_link_options(tsc_project_options INTERFACE
        $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-fsanitize=address -fsanitize=undefined>)
endif()

if(ENABLE_TSAN)
    message(STATUS "ThreadSanitizer enabled")
    target_compile_options(tsc_project_options INTERFACE
        $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-g -O1 -fno-omit-frame-pointer -fsanitize=thread>)
    target_link_options(tsc_project_options INTERFACE
        $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-fsanitize=thread>)
endif()

# ---------------------------------------------------------------------------
# Library
# ---------------------------------------------------------------------------
add_subdirectory(lib)

# ---------------------------------------------------------------------------
# Test launcher: on Linux + TSan, recent Ubuntu kernels use 32-bit ASLR
# entropy that breaks TSan's shadow-memory layout. Setting
# CMAKE_CROSSCOMPILING_EMULATOR (the standard CMake hook for prepending a
# command to test/run targets) makes both `ctest` and Catch2's build-time
# `catch_discover_tests` invocations transparently go through `setarch -R`.
# ---------------------------------------------------------------------------
if(ENABLE_TSAN AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_program(SETARCH_EXECUTABLE setarch)
    if(SETARCH_EXECUTABLE)
        message(STATUS "TSan + Linux: tests will be launched via '${SETARCH_EXECUTABLE} -R'")
        set(CMAKE_CROSSCOMPILING_EMULATOR ${SETARCH_EXECUTABLE} -R
            CACHE STRING "setarch -R wrapper for TSan on Linux" FORCE)
    else()
        message(WARNING
            "ENABLE_TSAN=ON on Linux but 'setarch' was not found. "
            "Test discovery and execution may fail with "
            "'FATAL: ThreadSanitizer: unexpected memory mapping'.")
    endif()
endif()

# ---------------------------------------------------------------------------
# Testing
# ---------------------------------------------------------------------------
include(CTest)  # defines BUILD_TESTING
if(BUILD_TESTING AND TSC_BUILD_TESTS)
    add_subdirectory(tests)
endif()

# ---------------------------------------------------------------------------
# Demo application
# ---------------------------------------------------------------------------
if(TSC_BUILD_APP)
    add_subdirectory(app)
endif()

# ---------------------------------------------------------------------------
# Examples
# ---------------------------------------------------------------------------
if(TSC_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

# ---------------------------------------------------------------------------
# Benchmarks
# ---------------------------------------------------------------------------
if(TSC_BUILD_BENCHMARKS)
    add_subdirectory(benchmarks)
endif()

# ---------------------------------------------------------------------------
# Documentation (optional `docs` target if Doxygen is installed)
# ---------------------------------------------------------------------------
add_subdirectory(docs)

# ---------------------------------------------------------------------------
# Formatting target
# ---------------------------------------------------------------------------
find_program(CLANG_FORMAT clang-format)
if(CLANG_FORMAT)
    # Resolve at build time (handles newly added files without reconfigure).
    add_custom_target(format
        COMMAND ${CMAKE_COMMAND} -E echo "Running clang-format on tracked sources"
        COMMAND bash -c
            "find '${CMAKE_SOURCE_DIR}/lib/include' \
                  '${CMAKE_SOURCE_DIR}/app/src' \
                  '${CMAKE_SOURCE_DIR}/tests' \
                  '${CMAKE_SOURCE_DIR}/examples' \
                  -type f \\( -name '*.hpp' -o -name '*.cpp' -o -name '*.h' \\) \
                  -print0 | xargs -0 ${CLANG_FORMAT} -i"
        VERBATIM
        COMMENT "Formatting sources with clang-format")
endif()
