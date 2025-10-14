cmake_minimum_required(VERSION 3.20 FATAL_ERROR)

project(ThreadSafeContainer VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Threads REQUIRED)

# Header-only library
add_library(tsc INTERFACE)
add_library(tsc::tsc ALIAS tsc)

target_include_directories(tsc INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(tsc INTERFACE Threads::Threads)

target_compile_features(tsc INTERFACE cxx_std_17)

# Add compiler warnings
target_compile_options(tsc INTERFACE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
)

# Installation
include(GNUInstallDirs)

install(TARGETS tsc
    EXPORT tscTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

install(EXPORT tscTargets
    FILE tscTargets.cmake
    NAMESPACE tsc::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/tsc
)

# Create config file
include(CMakePackageConfigHelpers)
write_basic_package_version_file(
    "tscConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/tscConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/tscConfig.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/tsc
)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/tscConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/tscConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/tsc
)
