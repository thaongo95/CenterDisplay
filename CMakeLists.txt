cmake_minimum_required(VERSION 3.16)

project(centerDisplay LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

find_package(Qt6 REQUIRED COMPONENTS Quick Qml)
qt_standard_project_setup()

qt_add_executable(centerDisplay
    main.cpp
    GstVideoReceiver.cpp GstVideoReceiver.h
    VideoItem.cpp VideoItem.h
)
qt_add_qml_module(centerDisplay
    URI centerDisplay
    VERSION 1.0
    RESOURCE_PREFIX "/"
    QML_FILES Main.qml
)
target_link_libraries(centerDisplay PRIVATE Qt6::Quick Qt6::Qml)

if(WIN32)
    set(GSTREAMER_ROOT "C:/Program Files/gstreamer/1.0/mingw_x86_64"
        CACHE PATH "Root of the matching Windows GStreamer development SDK")
    target_include_directories(centerDisplay PRIVATE
        "${GSTREAMER_ROOT}/include/gstreamer-1.0"
        "${GSTREAMER_ROOT}/include/glib-2.0"
        "${GSTREAMER_ROOT}/lib/glib-2.0/include"
    )
    target_link_directories(centerDisplay PRIVATE "${GSTREAMER_ROOT}/lib")
    target_link_libraries(centerDisplay PRIVATE
        gstreamer-1.0 gstbase-1.0 gstvideo-1.0 gstapp-1.0 gobject-2.0 glib-2.0
    )
    set_target_properties(centerDisplay PROPERTIES WIN32_EXECUTABLE TRUE)
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(GSTREAMER IMPORTED_TARGET REQUIRED
        gstreamer-1.0 gstreamer-base-1.0 gstreamer-video-1.0
        gstreamer-gl-1.0 gstreamer-app-1.0 gobject-2.0 glib-2.0
    )
    target_link_libraries(centerDisplay PRIVATE PkgConfig::GSTREAMER)
endif()

install(TARGETS centerDisplay RUNTIME DESTINATION bin)
install(FILES Main.qml DESTINATION lib/qt6/qml/centerDisplay)
