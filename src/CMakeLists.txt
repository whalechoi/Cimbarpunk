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
)
target_include_directories(cimbarpunk_selection PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(cimbarpunk_selection PUBLIC Qt6::Core Qt6::Gui)
target_link_libraries(cimbarpunk_core INTERFACE cimbarpunk_selection)
