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

add_custom_target(cimbarpunk_tests DEPENDS tst_version tst_contracts tst_selection_model)
