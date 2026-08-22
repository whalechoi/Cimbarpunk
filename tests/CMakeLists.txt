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

qt_add_executable(tst_decode_worker unit/tst_decode_worker.cpp)
target_include_directories(tst_decode_worker PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(tst_decode_worker PRIVATE cimbarpunk_core Qt6::Core Qt6::Gui Qt6::Test)

add_test(NAME decode_worker COMMAND tst_decode_worker)
set_tests_properties(decode_worker PROPERTIES
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
)

qt_add_executable(tst_qt_screen_capture_source unit/tst_qt_screen_capture_source.cpp)
target_link_libraries(tst_qt_screen_capture_source PRIVATE
    cimbarpunk_core
    Qt6::Core
    Qt6::Gui
    Qt6::Multimedia
    Qt6::Test
)

add_test(NAME qt_screen_capture_source COMMAND tst_qt_screen_capture_source)
set_tests_properties(qt_screen_capture_source PROPERTIES
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
)

if(WIN32)
    qt_add_executable(tst_windows_screen_capture integration/tst_windows_screen_capture.cpp)
    target_link_libraries(tst_windows_screen_capture PRIVATE
        cimbarpunk_core
        Qt6::Core
        Qt6::Gui
        Qt6::Multimedia
        Qt6::Test
        Qt6::Widgets
    )

    add_test(NAME windows_screen_capture COMMAND tst_windows_screen_capture)
    set_tests_properties(windows_screen_capture PROPERTIES
        ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
    )
endif()

if(UNIX AND NOT APPLE)
    qt_add_executable(tst_linux_screen_capture integration/tst_linux_screen_capture.cpp)
    target_link_libraries(tst_linux_screen_capture PRIVATE
        cimbarpunk_core
        cimbarpunk_selection
        Qt6::Core
        Qt6::Gui
        Qt6::Multimedia
        Qt6::Test
        Qt6::Widgets
    )

    add_test(NAME linux_screen_capture COMMAND tst_linux_screen_capture)
    set_tests_properties(linux_screen_capture PROPERTIES
        LABELS linux_gui
        TIMEOUT 60
    )
endif()

qt_add_executable(tst_selection_overlay_controller unit/tst_selection_overlay_controller.cpp)
target_link_libraries(tst_selection_overlay_controller PRIVATE
    cimbarpunk_core
    Qt6::Core
    Qt6::Gui
    Qt6::Quick
    Qt6::Qml
    Qt6::Test
)

add_test(NAME selection_overlay COMMAND tst_selection_overlay_controller)
set_tests_properties(selection_overlay PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software"
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
)
if(WIN32)
    file(TO_CMAKE_PATH "$ENV{WINDIR}/Fonts" CIMBARPUNK_TEST_FONT_DIRECTORY)
    set_property(TEST selection_overlay APPEND PROPERTY
        ENVIRONMENT "QT_QPA_FONTDIR=${CIMBARPUNK_TEST_FONT_DIRECTORY}"
    )
endif()

qt_add_executable(tst_tray_controller unit/tst_tray_controller.cpp)
target_link_libraries(tst_tray_controller PRIVATE
    cimbarpunk_tray
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Test
)

add_test(NAME tray_controller COMMAND tst_tray_controller)
set_tests_properties(tray_controller PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
)

qt_add_executable(tst_app_runtime integration/tst_app_runtime.cpp)
target_link_libraries(tst_app_runtime PRIVATE
    cimbarpunk_app
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Test
)

add_test(NAME app_runtime COMMAND tst_app_runtime)
set_tests_properties(app_runtime PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software"
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

qt_add_executable(tst_session
    integration/tst_session.cpp
    fakes/FakeCaptureSource.h
    fakes/FakeFrameProcessor.h
)
target_include_directories(tst_session PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(tst_session PRIVATE cimbarpunk_core Qt6::Core Qt6::Gui Qt6::Test)

add_test(NAME session COMMAND tst_session)
set_tests_properties(session PROPERTIES
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
)

add_custom_target(cimbarpunk_tests DEPENDS tst_version tst_contracts tst_selection_model tst_settings_store tst_rotating_logger tst_output_store tst_latest_frame_mailbox tst_frame_pipeline tst_decode_worker tst_qt_screen_capture_source tst_selection_overlay_controller tst_tray_controller tst_app_runtime tst_decoder tst_session)
if(WIN32)
    add_dependencies(cimbarpunk_tests tst_windows_screen_capture)
endif()
if(UNIX AND NOT APPLE)
    add_dependencies(cimbarpunk_tests tst_linux_screen_capture)
endif()

add_subdirectory(manual/frame_player)

if(WIN32)
    find_program(CIMBARPUNK_PWSH NAMES pwsh powershell REQUIRED)
    add_test(NAME windows_build_helpers
        COMMAND "${CIMBARPUNK_PWSH}" -NoProfile -File
            "${CMAKE_CURRENT_SOURCE_DIR}/scripts/tst_windows_build_helpers.ps1"
            -ModulePath "${PROJECT_SOURCE_DIR}/scripts/WindowsBuildHelpers.psm1"
            -ToolPath "${CMAKE_COMMAND}"
            -VcpkgRoot "${Z_VCPKG_ROOT_DIR}"
            -WrongRepository "${PROJECT_SOURCE_DIR}"
    )
endif()

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
