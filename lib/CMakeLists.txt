# Header-only INTERFACE library. Threads is required by users at link time.
find_package(Threads REQUIRED)

add_library(tsc INTERFACE)
add_library(tsc::tsc ALIAS tsc)

target_include_directories(tsc INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(tsc INTERFACE Threads::Threads)
target_compile_features(tsc INTERFACE cxx_std_17)

# ---------------------------------------------------------------------------
# Installation / packaging
# ---------------------------------------------------------------------------
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

# INTERFACE library: no archive/library/runtime artifacts to install.
install(TARGETS tsc EXPORT tscTargets)

install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    FILES_MATCHING PATTERN "*.hpp")

install(EXPORT tscTargets
    FILE tscTargets.cmake
    NAMESPACE tsc::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/tsc)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/tscConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion)

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/tscConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/tscConfig.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/tsc)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/tscConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/tscConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/tsc)
