cmake_minimum_required(VERSION 3.25)
project(Cimbarpunk VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)

include(CTest)

find_package(Qt6 6.8.4 EXACT REQUIRED COMPONENTS Core Gui Widgets Quick Qml Multimedia Svg Test)
find_package(OpenCV 4 REQUIRED COMPONENTS core imgproc imgcodecs photo calib3d)

qt_standard_project_setup(REQUIRES 6.8)

set(DISABLE_TESTS ON CACHE BOOL "Disable libcimbar tests in the parent build" FORCE)

add_subdirectory(external/libcimbar EXCLUDE_FROM_ALL)
add_subdirectory(src)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
