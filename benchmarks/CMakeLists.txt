# benchmarks/CMakeLists.txt
include(FetchContent)

find_package(benchmark QUIET)
if(NOT benchmark_FOUND)
    message(STATUS "Google Benchmark not found locally; fetching v1.8.3")
    set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
    set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
    set(BENCHMARK_ENABLE_GTEST_TESTS OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
        google_benchmark
        GIT_REPOSITORY https://github.com/google/benchmark.git
        GIT_TAG        v1.8.3
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(google_benchmark)
endif()

add_executable(tsc_benchmarks bench_main.cpp)
target_link_libraries(tsc_benchmarks
    PRIVATE tsc::tsc tsc_project_options benchmark::benchmark)
