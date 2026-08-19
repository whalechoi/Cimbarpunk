add_library(cimbarpunk_core INTERFACE)
target_include_directories(cimbarpunk_core INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_sources(cimbarpunk_core INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/core/Version.h)
