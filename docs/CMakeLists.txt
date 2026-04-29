# docs/CMakeLists.txt — optional Doxygen target.
find_package(Doxygen QUIET)
if(NOT DOXYGEN_FOUND)
    message(STATUS "Doxygen not found; the 'docs' target will be unavailable")
    return()
endif()

set(DOXYGEN_OUTPUT_DIR "${CMAKE_BINARY_DIR}/docs")
set(DOXYFILE_IN  "${CMAKE_CURRENT_SOURCE_DIR}/Doxyfile.in")
set(DOXYFILE_OUT "${CMAKE_CURRENT_BINARY_DIR}/Doxyfile")

configure_file("${DOXYFILE_IN}" "${DOXYFILE_OUT}" @ONLY)

file(MAKE_DIRECTORY "${DOXYGEN_OUTPUT_DIR}")

add_custom_target(docs
    COMMAND ${DOXYGEN_EXECUTABLE} "${DOXYFILE_OUT}"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    COMMENT "Generating API documentation with Doxygen -> ${DOXYGEN_OUTPUT_DIR}/html"
    VERBATIM)
