cmake_minimum_required(VERSION 3.5 FATAL_ERROR)

project(TSC LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Threads REQUIRED)

set(SourceFiles TSCTest.cpp)
add_executable(TSCTest ${SourceFiles})

target_link_libraries(TSCTest PUBLIC Threads::Threads)

option(ENABLE_TSAN "Enable ThreadSanitizer" OFF)

if(ENABLE_TSAN)
    if(CMAKE_CXX_COMPILER_ID MATCHES GNU)
        message(STATUS "ThreadSanitizer enabled")
        target_compile_options(TSCTest PUBLIC
            -g -O1 -fsanitize=thread -fno-omit-frame-pointer -fPIC)
        target_link_libraries(TSCTest PUBLIC tsan)
    else()
        message(WARNING "ThreadSanitizer not supported for this compiler")
    endif()
endif()

enable_testing()

add_test(NAME TSCTest COMMAND $<TARGET_FILE:TSCTest>)
