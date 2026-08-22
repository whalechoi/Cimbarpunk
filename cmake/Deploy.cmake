include_guard(GLOBAL)
include(GNUInstallDirs)

function(cimbarpunk_configure_deploy target)
    if(WIN32)
        # TARGET_RUNTIME_DLLS only covers DLL-backed CMake targets.  It omits
        # binary-only imports pulled in by those targets (for example OpenCV's
        # zlib/libpng dependencies), so scan the installed executable's full
        # runtime dependency closure instead.
        install(TARGETS ${target}
            RUNTIME_DEPENDENCIES
                DIRECTORIES
                    "$<TARGET_FILE_DIR:${target}>"
                    "$<TARGET_FILE_DIR:Qt6::Core>"
                    "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin"
                PRE_EXCLUDE_REGEXES
                    "api-ms-win-.*"
                    "ext-ms-.*"
                    # Optional Windows component imports reported by system
                    # DLLs on some Windows 11 installations. They are supplied
                    # by the OS when the corresponding component is present.
                    "^(azureattestmanager|azureattestnormal|declaredconfiguration|edpcsp|efscore|fveskybackup|hvsifiletrust|hwreqchk|lsasrv|pdmutilities|samsrv|dmenterprisediagnostics|ngcrecovery|policymanagerprecheck|wpaxholder)\\.dll$"
                    # windeployqt handles the redistributable compiler runtime;
                    # the local SDK copy in System32 must not enter our closure.
                    "^vcruntime[0-9_]*\\.dll$"
                POST_EXCLUDE_REGEXES
                    ".*[/\\\\][Ww][Ii][Nn][Dd][Oo][Ww][Ss][/\\\\][Ss][Yy][Ss][Tt][Ee][Mm]32[/\\\\].*"
            BUNDLE DESTINATION .
            RUNTIME DESTINATION .
        )
    else()
        install(TARGETS ${target}
            BUNDLE DESTINATION .
            RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
        )
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

    # Ship the authoritative full texts, not only the summary above.  The Qt
    # source provisioning scripts populate this module-by-module license tree.
    get_filename_component(qt_prefix "${Qt6_DIR}/../../.." ABSOLUTE)
    set(qt_license_root "${qt_prefix}/share/licenses/qt")
    set(qt_sbom_root "${qt_prefix}/sbom")
    if(NOT IS_DIRECTORY "${qt_license_root}")
        message(FATAL_ERROR
            "Qt license corpus is missing at ${qt_license_root}; use the documented official-source provisioner."
        )
    endif()
    if(NOT IS_DIRECTORY "${qt_sbom_root}")
        message(FATAL_ERROR "Qt SPDX corpus is missing at ${qt_sbom_root}")
    endif()
    install(DIRECTORY "${qt_license_root}/"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/licenses/cimbarpunk/qt"
    )
    install(DIRECTORY "${qt_sbom_root}/"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/licenses/cimbarpunk/qt-sbom"
        FILES_MATCHING PATTERN "*.spdx"
    )

    set(vcpkg_share_root "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/share")
    if(NOT IS_DIRECTORY "${vcpkg_share_root}")
        message(FATAL_ERROR "vcpkg share directory is missing at ${vcpkg_share_root}")
    endif()
    install(DIRECTORY "${vcpkg_share_root}/"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/licenses/cimbarpunk/vcpkg"
        FILES_MATCHING PATTERN "copyright"
    )
    install(DIRECTORY
        "${PROJECT_SOURCE_DIR}/external/libcimbar/src/third_party_lib/"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/licenses/cimbarpunk/libcimbar-vendored"
        FILES_MATCHING
            PATTERN "LICENSE"
            PATTERN "LICENSE.*"
            PATTERN "COPYING"
            PATTERN "NOTICE*"
            # base.hpp contains the full Joachim Henke Base91 BSD-3 notice,
            # which is additional to the r-lyeh wrapper's LICENSE file.
            PATTERN "base.hpp"
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
