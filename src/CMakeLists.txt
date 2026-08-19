add_library(cimbarpunk_core INTERFACE)
target_include_directories(cimbarpunk_core INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(cimbarpunk_core INTERFACE Qt6::Core Qt6::Gui)
target_sources(cimbarpunk_core INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/capture/ICaptureSource.h
    ${CMAKE_CURRENT_SOURCE_DIR}/core/SessionTypes.h
    ${CMAKE_CURRENT_SOURCE_DIR}/core/Version.h
    ${CMAKE_CURRENT_SOURCE_DIR}/decoder/IDecoder.h
    ${CMAKE_CURRENT_SOURCE_DIR}/output/IOutputStore.h
    ${CMAKE_CURRENT_SOURCE_DIR}/pipeline/IFrameProcessor.h
    ${CMAKE_CURRENT_SOURCE_DIR}/selection/ScreenIdentity.h
    ${CMAKE_CURRENT_SOURCE_DIR}/selection/SelectionModel.h
)

add_library(cimbarpunk_selection STATIC
    selection/ScreenIdentity.cpp
    selection/ScreenIdentity.h
    selection/SelectionModel.cpp
    selection/SelectionModel.h
    selection/SelectionOverlayController.cpp
    selection/SelectionOverlayController.h
)
target_include_directories(cimbarpunk_selection PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(cimbarpunk_selection PUBLIC Qt6::Core Qt6::Gui Qt6::Quick Qt6::Qml)
set_source_files_properties(selection/qml/SelectionOverlay.qml PROPERTIES
    QT_RESOURCE_ALIAS SelectionOverlay.qml
)
qt_add_resources(cimbarpunk_selection selection_overlay_qml
    PREFIX /cimbarpunk/qml
    FILES selection/qml/SelectionOverlay.qml
)
target_link_libraries(cimbarpunk_core INTERFACE cimbarpunk_selection)

add_library(cimbarpunk_capture STATIC
    capture/QtScreenCaptureSource.cpp
    capture/QtScreenCaptureSource.h
)
target_include_directories(cimbarpunk_capture PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(cimbarpunk_capture PUBLIC Qt6::Core Qt6::Gui Qt6::Multimedia)
target_link_libraries(cimbarpunk_core INTERFACE cimbarpunk_capture)

add_library(cimbarpunk_settings STATIC
    settings/SettingsStore.cpp
    settings/SettingsStore.h
)
target_include_directories(cimbarpunk_settings PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(cimbarpunk_settings PUBLIC Qt6::Core Qt6::Gui)
target_link_libraries(cimbarpunk_core INTERFACE cimbarpunk_settings)

add_library(cimbarpunk_output STATIC
    output/LibcimbarPayloadWriter.cpp
    output/LibcimbarPayloadWriter.h
    output/OutputStore.cpp
    output/OutputStore.h
)
target_include_directories(cimbarpunk_output
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
    PRIVATE
        ${CMAKE_SOURCE_DIR}/external/libcimbar/src/lib
        ${CMAKE_SOURCE_DIR}/external/libcimbar/src/third_party_lib
)
target_link_libraries(cimbarpunk_output PUBLIC cimbarpunk_settings Qt6::Core PRIVATE zstd)

add_library(cimbarpunk_decoder STATIC
    decoder/CimbarDecoderAdapter.cpp
    decoder/CimbarDecoderAdapter.h
)
target_include_directories(cimbarpunk_decoder
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
    PRIVATE
        ${OpenCV_INCLUDE_DIRS}
        ${PROJECT_SOURCE_DIR}/external/libcimbar/src/lib
        ${PROJECT_SOURCE_DIR}/external/libcimbar/src/third_party_lib
)
target_compile_definitions(cimbarpunk_decoder PRIVATE ZSTD_STATIC_LINKING_ONLY)
target_link_libraries(cimbarpunk_decoder
    PUBLIC Qt6::Core Qt6::Gui
    PRIVATE
        cimb_translator
        extractor
        correct_static
        wirehair
        zstd
        ${OpenCV_LIBS}
)
target_link_libraries(cimbarpunk_core INTERFACE cimbarpunk_decoder)

add_library(cimbarpunk_diagnostics STATIC
    diagnostics/RotatingLogger.cpp
    diagnostics/RotatingLogger.h
)
target_include_directories(cimbarpunk_diagnostics PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(cimbarpunk_diagnostics PUBLIC Qt6::Core Qt6::Gui)
target_link_libraries(cimbarpunk_core INTERFACE cimbarpunk_diagnostics)

add_library(cimbarpunk_pipeline STATIC
    pipeline/DecodeWorker.cpp
    pipeline/DecodeWorker.h
    pipeline/FramePipeline.cpp
    pipeline/FramePipeline.h
    pipeline/LatestFrameMailbox.cpp
    pipeline/LatestFrameMailbox.h
)
target_include_directories(cimbarpunk_pipeline PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(cimbarpunk_pipeline PUBLIC cimbarpunk_selection cimbarpunk_diagnostics Qt6::Core Qt6::Gui)
target_link_libraries(cimbarpunk_core INTERFACE cimbarpunk_pipeline)

add_library(cimbarpunk_session STATIC
    session/CaptureSession.cpp
    session/CaptureSession.h
)
target_include_directories(cimbarpunk_session PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(cimbarpunk_session PUBLIC
    cimbarpunk_settings
    cimbarpunk_pipeline
    Qt6::Core
    Qt6::Gui
)
target_link_libraries(cimbarpunk_core INTERFACE cimbarpunk_session)

add_library(cimbarpunk_tray STATIC
    tray/TrayController.cpp
    tray/TrayController.h
)
target_include_directories(cimbarpunk_tray PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(cimbarpunk_tray PUBLIC cimbarpunk_settings Qt6::Core Qt6::Gui Qt6::Widgets)
set_source_files_properties(${PROJECT_SOURCE_DIR}/resources/icons/tray.svg PROPERTIES
    QT_RESOURCE_ALIAS icons/tray.svg
)
qt_add_resources(cimbarpunk_tray tray_icons
    PREFIX /cimbarpunk
    FILES ${PROJECT_SOURCE_DIR}/resources/icons/tray.svg
)

add_library(cimbarpunk_app STATIC
    app/AppRuntime.cpp
    app/AppRuntime.h
)
target_include_directories(cimbarpunk_app PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(cimbarpunk_app PRIVATE cimbarpunk_core)
target_link_libraries(cimbarpunk_app PUBLIC
    cimbarpunk_capture
    cimbarpunk_decoder
    cimbarpunk_diagnostics
    cimbarpunk_output
    cimbarpunk_pipeline
    cimbarpunk_selection
    cimbarpunk_session
    cimbarpunk_settings
    cimbarpunk_tray
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
)

qt_add_executable(cimbarpunk app/main.cpp)
target_link_libraries(cimbarpunk PRIVATE cimbarpunk_app Qt6::Core Qt6::Gui Qt6::Widgets)
set_target_properties(cimbarpunk PROPERTIES WIN32_EXECUTABLE TRUE)
