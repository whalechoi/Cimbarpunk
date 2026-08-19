qt_add_executable(tst_version unit/tst_version.cpp)
target_link_libraries(tst_version PRIVATE cimbarpunk_core Qt6::Core Qt6::Test)

add_test(NAME version COMMAND tst_version)
set_tests_properties(version PROPERTIES
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
)

add_custom_target(cimbarpunk_tests DEPENDS tst_version)
