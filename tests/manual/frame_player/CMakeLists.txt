add_library(cimbarpunk_frame_player_lib STATIC
    FramePlayer.cpp
    FramePlayer.h
)
target_link_libraries(cimbarpunk_frame_player_lib PUBLIC
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
)

qt_add_executable(cimbarpunk_frame_player main.cpp)
target_compile_definitions(cimbarpunk_frame_player PRIVATE
    CIMBARPUNK_DEFAULT_FIXTURE_DIR="${PROJECT_SOURCE_DIR}/tests/fixtures/cimbar"
)
target_link_libraries(cimbarpunk_frame_player PRIVATE cimbarpunk_frame_player_lib)

qt_add_executable(tst_frame_player tst_frame_player.cpp)
target_link_libraries(tst_frame_player PRIVATE
    cimbarpunk_frame_player_lib
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Test
)

add_test(NAME frame_player COMMAND tst_frame_player)
set_tests_properties(frame_player PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
)

add_dependencies(cimbarpunk_tests tst_frame_player)
