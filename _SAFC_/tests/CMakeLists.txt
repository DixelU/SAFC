find_package(Threads REQUIRED)

if(SAFC_BUILD_APP_TESTS)
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
endif()

if(SAFC_BUILD_PROCESSOR_TESTS)
    add_executable(safc-midi-processor-tests single_midi_processor_lean_tests.cpp)
    target_include_directories(safc-midi-processor-tests PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/../../utility/include")
    target_compile_features(safc-midi-processor-tests PRIVATE cxx_std_23)
    target_link_libraries(safc-midi-processor-tests PRIVATE
        Boost::headers GLUT::GLUT Threads::Threads)
    if(MSVC)
        target_compile_options(safc-midi-processor-tests PRIVATE /permissive /EHsc)
    endif()
    add_test(NAME safc-midi-processor COMMAND safc-midi-processor-tests
        "${CMAKE_CURRENT_BINARY_DIR}/midi-processor-test-data")
    set_tests_properties(safc-midi-processor PROPERTIES TIMEOUT 30)
endif()
