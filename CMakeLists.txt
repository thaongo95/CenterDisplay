cmake_minimum_required(VERSION 3.16)

project(centerDisplay LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

find_package(Qt6 REQUIRED COMPONENTS Quick Qml Network Location Positioning Sql)
qt_standard_project_setup()

qt_add_executable(centerDisplay
    main.cpp
    RescueStore.cpp RescueStore.h
    TelemetryModel.cpp TelemetryModel.h
    AppSettings.cpp AppSettings.h
    GstVideoReceiver.cpp GstVideoReceiver.h
    VideoItem.cpp VideoItem.h
)
qt_add_qml_module(centerDisplay
    URI centerDisplay
    VERSION 1.0
    RESOURCE_PREFIX "/"
    QML_FILES Main.qml MapPanel.qml RescuePanel.qml
)
target_link_libraries(centerDisplay PRIVATE Qt6::Quick Qt6::Qml Qt6::Network Qt6::Location Qt6::Positioning Qt6::Sql)

if(WIN32)
    set(GSTREAMER_ROOT "C:/Program Files/gstreamer/1.0/mingw_x86_64"
        CACHE PATH "Root of the matching Windows GStreamer development SDK")
    target_include_directories(centerDisplay PRIVATE
        "${GSTREAMER_ROOT}/include/gstreamer-1.0"
        "${GSTREAMER_ROOT}/include/glib-2.0"
        "${GSTREAMER_ROOT}/lib/glib-2.0/include"
    )
    # Link exact import libraries: adding this entire SDK directory to -L can
    # accidentally select its static libstdc++ instead of the compiler runtime.
    foreach(component gstreamer-1.0 gstbase-1.0 gstvideo-1.0 gstapp-1.0 gobject-2.0 glib-2.0)
        if(MINGW)
            set(import_library "${GSTREAMER_ROOT}/lib/lib${component}.dll.a")
        else()
            set(import_library "${GSTREAMER_ROOT}/lib/${component}.lib")
        endif()
        if(NOT EXISTS "${import_library}")
            message(FATAL_ERROR "Missing GStreamer import library: ${import_library}")
        endif()
        target_link_libraries(centerDisplay PRIVATE "${import_library}")
    endforeach()
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
install(FILES Main.qml MapPanel.qml RescuePanel.qml DESTINATION lib/qt6/qml/centerDisplay)

# Integration tests use an in-process RTSP server; no physical camera is needed.
option(CENTERDISPLAY_BUILD_TESTS "Build receiver integration tests (Linux)" OFF)
if(CENTERDISPLAY_BUILD_TESTS AND NOT WIN32)
    enable_testing()
    find_package(Qt6 REQUIRED COMPONENTS Test Network)
    pkg_check_modules(RTSP_SERVER IMPORTED_TARGET REQUIRED gstreamer-rtsp-server-1.0)
    qt_add_executable(receiver_tests tests/receiver_tests.cpp
        GstVideoReceiver.cpp GstVideoReceiver.h VideoItem.cpp VideoItem.h)
    target_include_directories(receiver_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(receiver_tests PRIVATE Qt6::Quick Qt6::Test Qt6::Network
        PkgConfig::GSTREAMER PkgConfig::RTSP_SERVER)
    add_test(NAME receiver_integration COMMAND receiver_tests)
    set_tests_properties(receiver_integration PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_NO_GLIB=1" TIMEOUT 100)

endif()

option(CENTERDISPLAY_BUILD_MAP_TESTS "Build map and telemetry tests without RTSP server" OFF)
if(CENTERDISPLAY_BUILD_MAP_TESTS)
    enable_testing()
    find_package(Qt6 REQUIRED COMPONENTS Test)
    qt_add_executable(map_tests tests/map_tests.cpp TelemetryModel.cpp TelemetryModel.h AppSettings.cpp AppSettings.h)
    target_compile_definitions(map_tests PRIVATE SIMULATOR_PATH="${CMAKE_CURRENT_SOURCE_DIR}/tools/simulate_uavs.py")
    target_include_directories(map_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(map_tests PRIVATE Qt6::Quick Qt6::Qml Qt6::Network Qt6::Test Qt6::Location Qt6::Positioning)
    qt_add_resources(map_tests "map_test_ui" PREFIX "/centerDisplay" FILES MapPanel.qml RescuePanel.qml)
    add_test(NAME map_telemetry COMMAND map_tests)
    set_tests_properties(map_telemetry PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software" TIMEOUT 30)
endif()

if(CENTERDISPLAY_BUILD_TESTS OR CENTERDISPLAY_BUILD_MAP_TESTS)
    qt_add_executable(settings_ui_tests tests/settings_ui_tests.cpp
        TelemetryModel.cpp TelemetryModel.h
        AppSettings.cpp AppSettings.h VideoItem.cpp VideoItem.h)
    target_include_directories(settings_ui_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(settings_ui_tests PRIVATE Qt6::Quick Qt6::Qml Qt6::Test Qt6::Network Qt6::Location Qt6::Positioning)
    qt_add_resources(settings_ui_tests "settings_test_ui" PREFIX "/centerDisplay" FILES Main.qml MapPanel.qml RescuePanel.qml)
    add_test(NAME settings_ui COMMAND settings_ui_tests)
    set_tests_properties(settings_ui PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software" TIMEOUT 30)
endif()

option(CENTERDISPLAY_BUILD_RESCUE_TESTS "Build PostgreSQL rescue integration tests (requires local demo database)" OFF)
if(CENTERDISPLAY_BUILD_RESCUE_TESTS)
    enable_testing()
    find_package(Qt6 REQUIRED COMPONENTS Test)
    qt_add_executable(rescue_tests integration/z2-flight/CenterPlanInbox.cc integration/z2-flight/CenterPlanInbox.h tests/rescue_tests.cpp RescueStore.cpp RescueStore.h AppSettings.cpp AppSettings.h TelemetryModel.cpp TelemetryModel.h)
    target_include_directories(rescue_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/integration/z2-flight)
    target_compile_definitions(rescue_tests PRIVATE RESCUE_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(rescue_tests PRIVATE Qt6::Quick Qt6::Qml Qt6::Network Qt6::Sql Qt6::Test Qt6::Location Qt6::Positioning)
    qt_add_resources(rescue_tests "rescue_test_ui" PREFIX "/centerDisplay" FILES MapPanel.qml RescuePanel.qml)
    add_test(NAME rescue_postgresql COMMAND rescue_tests)
    set_tests_properties(rescue_postgresql PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software" TIMEOUT 45)
endif()
