add_executable(tsc_test src/main.cpp)
target_link_libraries(tsc_test PRIVATE tsc::tsc tsc_project_options)

add_test(NAME tsc_integration_test COMMAND tsc_test)
set_tests_properties(tsc_integration_test PROPERTIES
    TIMEOUT 30
    LABELS "integration")
