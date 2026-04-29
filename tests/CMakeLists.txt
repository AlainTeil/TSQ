# Catch2 v3 (fetched if not found locally)
include(FetchContent)

find_package(Catch2 3 QUIET)
if(NOT Catch2_FOUND)
    message(STATUS "Catch2 v3 not found locally; fetching from GitHub")
    FetchContent_Declare(
        Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG        v3.5.4
    )
    FetchContent_MakeAvailable(Catch2)
    list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
endif()

include(Catch)

add_executable(tsc_unit_tests
    test_basic.cpp
    test_shutdown_drain.cpp
    test_multi_waiter_wakeup.cpp
    test_move_only.cpp
    test_timed.cpp
)

target_link_libraries(tsc_unit_tests PRIVATE
    tsc::tsc
    tsc_project_options
    Catch2::Catch2WithMain
)

# Each TEST_CASE is registered as an individual CTest test, so failures point
# at the exact case rather than a single opaque pass/fail.
catch_discover_tests(tsc_unit_tests
    PROPERTIES
        TIMEOUT 30
        LABELS "unit"
)
