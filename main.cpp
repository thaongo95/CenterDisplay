#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <qqml.h>
#include <gst/gst.h>
#include "VideoItem.h"
#include "GstVideoReceiver.h"
#include <QImage>
#include <QMetaType>

int main(int argc, char *argv[])
{
    gst_init(&argc, &argv);

    QGuiApplication app(argc, argv);
    qRegisterMetaType<QImage>("QImage");


    qmlRegisterType<VideoItem>(
        "Video",
        1, 0,
        "VideoItem");
    GstVideoReceiver receiver1, receiver2, receiver3, receiver4;
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
    engine.loadFromModule(
        "centerDisplay",
        "Main");

    return app.exec();
}