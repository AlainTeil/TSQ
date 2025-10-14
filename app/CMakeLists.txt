cmake_minimum_required(VERSION 3.20 FATAL_ERROR)

project(TSCApplication LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# The tsc::tsc target is already available from lib/CMakeLists.txt
add_executable(tsc_test src/main.cpp)
target_link_libraries(tsc_test PRIVATE tsc::tsc)

# Add compile options for better diagnostics
target_compile_options(tsc_test PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
)

# ThreadSanitizer support
option(ENABLE_TSAN "Enable ThreadSanitizer" OFF)
if(ENABLE_TSAN)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        message(STATUS "ThreadSanitizer enabled")
        target_compile_options(tsc_test PRIVATE
            -g -O1 -fsanitize=thread -fno-omit-frame-pointer)
        target_link_options(tsc_test PRIVATE -fsanitize=thread)
    else()
        message(WARNING "ThreadSanitizer not supported for this compiler")
    endif()
endif()

# AddressSanitizer support
option(ENABLE_ASAN "Enable AddressSanitizer" OFF)
if(ENABLE_ASAN)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        message(STATUS "AddressSanitizer enabled")
        target_compile_options(tsc_test PRIVATE
            -g -O1 -fsanitize=address -fno-omit-frame-pointer)
        target_link_options(tsc_test PRIVATE -fsanitize=address)
    else()
        message(WARNING "AddressSanitizer not supported for this compiler")
    endif()
endif()

# Testing
enable_testing()
add_test(NAME tsc_integration_test COMMAND tsc_test)
set_tests_properties(tsc_integration_test PROPERTIES
    TIMEOUT 10
    LABELS "integration"
)
