include_guard(GLOBAL)
include(GNUInstallDirs)

function(cimbarpunk_configure_deploy target)
    install(TARGETS ${target}
        BUNDLE DESTINATION .
        RUNTIME DESTINATION .
    )

    if(WIN32)
        install(FILES $<TARGET_RUNTIME_DLLS:${target}> DESTINATION .)
    endif()

    install(FILES
        "${PROJECT_SOURCE_DIR}/LICENSE"
        "${PROJECT_SOURCE_DIR}/THIRD_PARTY_NOTICES.md"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/licenses/cimbarpunk"
    )
    install(FILES "${PROJECT_SOURCE_DIR}/external/libcimbar/LICENSE"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/licenses/cimbarpunk"
        RENAME libcimbar-MPL-2.0.txt
    )
    install(FILES "${PROJECT_SOURCE_DIR}/resources/icons/tray.svg"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps"
        RENAME cimbarpunk.svg
    )
    install(FILES "${PROJECT_SOURCE_DIR}/src/selection/qml/SelectionOverlay.qml"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/cimbarpunk/qml"
    )

    # Windows verification invokes windeployqt explicitly after installation so
    # its plugin/QML layout is asserted in one place. Other platforms use the
    # equivalent Qt-generated install script when supported.
    if(NOT WIN32)
        qt_generate_deploy_app_script(
            TARGET ${target}
            OUTPUT_SCRIPT deploy_script
            NO_UNSUPPORTED_PLATFORM_ERROR
        )
        install(SCRIPT "${deploy_script}")
    endif()
endfunction()
