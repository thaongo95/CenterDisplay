cmake_minimum_required(VERSION 3.16)

project(centerDisplay LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Quick Qml)

qt_standard_project_setup()

set(GSTREAMER_ROOT
    "C:/Program Files/gstreamer/1.0/mingw_x86_64"
)

qt_add_executable(centerDisplay
    main.cpp
    GstVideoReceiver.cpp
    GstVideoReceiver.h
    VideoItem.cpp
    VideoItem.h
)

qt_add_qml_module(centerDisplay
    URI centerDisplay
    VERSION 1.0

    QML_FILES
        Main.qml
)

target_include_directories(centerDisplay PRIVATE

    "${GSTREAMER_ROOT}/include"
    "${GSTREAMER_ROOT}/include/gstreamer-1.0"
    "${GSTREAMER_ROOT}/include/glib-2.0"
    "${GSTREAMER_ROOT}/lib/glib-2.0/include"

)


target_link_directories(centerDisplay PRIVATE

    "${GSTREAMER_ROOT}/lib"

)


target_link_libraries(centerDisplay PRIVATE

    Qt6::Quick
    Qt6::Qml
    gstreamer-1.0
    gstbase-1.0
    gstvideo-1.0
    gstgl-1.0
    gstapp-1.0
    gobject-2.0
    glib-2.0

)