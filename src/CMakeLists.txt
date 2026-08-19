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
)
