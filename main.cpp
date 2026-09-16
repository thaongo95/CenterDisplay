#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QUrl>
#include <gst/gst.h>
#include "VideoItem.h"
#include "GstVideoReceiver.h"
#include <QImage>
#include <QMetaType>
#include <cstdlib>
#include <QDir>

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
#ifdef Q_OS_WIN
    // The portable Windows package keeps GStreamer beside the executable.
    const QDir appDir(QCoreApplication::applicationDirPath());
    qputenv("GST_PLUGIN_SYSTEM_PATH_1_0",
            QDir::toNativeSeparators(appDir.filePath("gstreamer-1.0")).toUtf8());
    qputenv("GST_PLUGIN_SCANNER_1_0",
            QDir::toNativeSeparators(appDir.filePath("gst-plugin-scanner.exe")).toUtf8());
#endif
    gst_init(&argc, &argv);
    qRegisterMetaType<QImage>("QImage");


    qmlRegisterType<VideoItem>(
        "Video",
        1, 0,
        "VideoItem");
    GstVideoReceiver receiver1, receiver2, receiver3, receiver4, receiver5, receiver6;
    QQmlApplicationEngine engine;

    engine.rootContext()->setContextProperty(
        "receiver1",
        &receiver1);
    engine.rootContext()->setContextProperty(
        "receiver2",
        &receiver2);
    engine.rootContext()->setContextProperty(
        "receiver3",
        &receiver3);
    engine.rootContext()->setContextProperty(
        "receiver4",
        &receiver4);
    engine.rootContext()->setContextProperty(
        "receiver5",
        &receiver5);
    engine.rootContext()->setContextProperty(
        "receiver6",
        &receiver6);
    engine.load(QUrl(QStringLiteral("qrc:/centerDisplay/Main.qml")));

    if (engine.rootObjects().isEmpty())
        return EXIT_FAILURE;

    return app.exec();
}
