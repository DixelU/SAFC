find_package(Threads REQUIRED)

add_executable(safc-app-workers-tests
    app_workers_tests.cpp
    app_workers_test_submission.cpp
    app_workers_test_shutdown.cpp)
target_include_directories(safc-app-workers-tests PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../../utility/include")
target_compile_features(safc-app-workers-tests PRIVATE cxx_std_23)
target_link_libraries(safc-app-workers-tests PRIVATE Threads::Threads)
if(MSVC)
    target_compile_options(safc-app-workers-tests PRIVATE /EHsc)
endif()

add_test(NAME safc-app-workers COMMAND safc-app-workers-tests)
set_tests_properties(safc-app-workers PROPERTIES TIMEOUT 15)
