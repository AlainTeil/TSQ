cmake_minimum_required(VERSION 3.20 FATAL_ERROR)

project(ThreadSafeContainerProject VERSION 1.0.0 LANGUAGES CXX)

# Set C++ standard at project level
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Build library first (creates tsc::tsc target)
add_subdirectory(lib)

# Build application (can now use tsc::tsc target directly)
add_subdirectory(app)

# Enable testing at top level
enable_testing()

# Add custom target for formatting
find_program(CLANG_FORMAT clang-format)
if(CLANG_FORMAT)
    file(GLOB_RECURSE ALL_CXX_SOURCE_FILES
        ${CMAKE_SOURCE_DIR}/lib/include/tsc/*.hpp
        ${CMAKE_SOURCE_DIR}/app/src/*.cpp
    )
    add_custom_target(
        format
        COMMAND ${CLANG_FORMAT} -i ${ALL_CXX_SOURCE_FILES}
        COMMENT "Running clang-format on source files"
    )
endif()
