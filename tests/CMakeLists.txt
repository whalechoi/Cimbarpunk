qt_add_executable(tst_version unit/tst_version.cpp)
target_link_libraries(tst_version PRIVATE cimbarpunk_core Qt6::Core Qt6::Test)

add_test(NAME version COMMAND tst_version)
set_tests_properties(version PROPERTIES
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
)

qt_add_executable(tst_contracts unit/tst_contracts.cpp)
target_link_libraries(tst_contracts PRIVATE cimbarpunk_core Qt6::Core Qt6::Test)

add_test(NAME contracts COMMAND tst_contracts)
set_tests_properties(contracts PROPERTIES
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
)

qt_add_executable(tst_selection_model unit/tst_selection_model.cpp)
target_link_libraries(tst_selection_model PRIVATE cimbarpunk_core Qt6::Core Qt6::Gui Qt6::Test)

add_test(NAME selection_model COMMAND tst_selection_model)
set_tests_properties(selection_model PROPERTIES
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
)

qt_add_executable(tst_settings_store unit/tst_settings_store.cpp)
target_link_libraries(tst_settings_store PRIVATE cimbarpunk_core Qt6::Core Qt6::Gui Qt6::Test)

add_test(NAME settings_store COMMAND tst_settings_store)
set_tests_properties(settings_store PROPERTIES
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
)

qt_add_executable(tst_rotating_logger unit/tst_rotating_logger.cpp)
target_link_libraries(tst_rotating_logger PRIVATE cimbarpunk_core Qt6::Core Qt6::Gui Qt6::Test)

add_test(NAME rotating_logger COMMAND tst_rotating_logger)
set_tests_properties(rotating_logger PROPERTIES
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
)

qt_add_executable(tst_output_store unit/tst_output_store.cpp)
target_include_directories(tst_output_store PRIVATE ${CMAKE_SOURCE_DIR}/external/libcimbar/src/third_party_lib)
target_link_libraries(tst_output_store PRIVATE cimbarpunk_core cimbarpunk_output Qt6::Core Qt6::Test zstd)

add_test(NAME output_store COMMAND tst_output_store)
set_tests_properties(output_store PROPERTIES
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
)

qt_add_executable(tst_latest_frame_mailbox unit/tst_latest_frame_mailbox.cpp)
target_link_libraries(tst_latest_frame_mailbox PRIVATE cimbarpunk_core Qt6::Core Qt6::Gui Qt6::Test)

add_test(NAME latest_frame_mailbox COMMAND tst_latest_frame_mailbox)
set_tests_properties(latest_frame_mailbox PROPERTIES
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
)

qt_add_executable(tst_frame_pipeline unit/tst_frame_pipeline.cpp)
target_link_libraries(tst_frame_pipeline PRIVATE cimbarpunk_core Qt6::Core Qt6::Gui Qt6::Test)

add_test(NAME frame_pipeline COMMAND tst_frame_pipeline)
set_tests_properties(frame_pipeline PROPERTIES
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
)

qt_add_executable(tst_decoder integration/tst_decoder.cpp)
target_compile_definitions(tst_decoder PRIVATE CIMBARPUNK_TEST_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/cimbar")
target_include_directories(tst_decoder PRIVATE
    ${CMAKE_SOURCE_DIR}/external/libcimbar/src/lib
    ${CMAKE_SOURCE_DIR}/external/libcimbar/src/third_party_lib
)
target_link_libraries(tst_decoder PRIVATE cimbarpunk_core Qt6::Core Qt6::Gui Qt6::Test)

add_test(NAME decoder COMMAND tst_decoder)
set_tests_properties(decoder PROPERTIES
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
)

add_custom_target(cimbarpunk_tests DEPENDS tst_version tst_contracts tst_selection_model tst_settings_store tst_rotating_logger tst_output_store tst_latest_frame_mailbox tst_frame_pipeline tst_decoder)

add_executable(generate_cimbar_fixture EXCLUDE_FROM_ALL tools/generate_cimbar_fixture.cpp)
target_include_directories(generate_cimbar_fixture PRIVATE
    ${OpenCV_INCLUDE_DIRS}
    ${CMAKE_SOURCE_DIR}/external/libcimbar/src/lib
    ${CMAKE_SOURCE_DIR}/external/libcimbar/src/third_party_lib
)
target_link_libraries(generate_cimbar_fixture PRIVATE
    Qt6::Core
    cimb_translator
    extractor
    correct_static
    wirehair
    zstd
    ${OpenCV_LIBS}
)
target_compile_definitions(tst_decoder PRIVATE
    CIMBARPUNK_FIXTURE_GENERATOR="$<TARGET_FILE:generate_cimbar_fixture>"
)
add_dependencies(tst_decoder generate_cimbar_fixture)
