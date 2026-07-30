#pragma once

#include <QObject>

#include <gst/gst.h>
#include <gst/app/gstappsink.h>
#include <QImage>
#include <QMetaObject>

#include <QTimer>
#include <QElapsedTimer>
#include <QPointer>

#include "VideoItem.h"

class VideoItem;

class GstVideoReceiver : public QObject
{
    Q_OBJECT
    Q_DISABLE_COPY(GstVideoReceiver)
public:
    explicit GstVideoReceiver(QObject *parent = nullptr);
    ~GstVideoReceiver();

    Q_INVOKABLE bool connectCamera(const QString &url);
    Q_INVOKABLE void start();
    Q_INVOKABLE void stop();

    Q_INVOKABLE void setVideoItem(VideoItem *item);

private:

    static GstFlowReturn onNewSample(
        GstAppSink *sink,
        gpointer user_data);

    GstFlowReturn processSample(
        GstAppSink *sink);
private:

    static void onRtspPadAdded(
        GstElement *src,
        GstPad *newPad,
        gpointer userData);


    static void onDecodePadAdded(
        GstElement *decode,
        GstPad *newPad,
        gpointer userData);
    GstElement *m_pipeline = nullptr;
    GstElement *m_appsink = nullptr;

    QPointer<VideoItem> m_videoItem;
private:

    GstBus *m_bus = nullptr;
    guint watchId {0};

    static gboolean onBusMessage(
        GstBus *bus,
        GstMessage *message,
        gpointer userData);
signals:

    void connectedChanged();

    void errorOccurred(
        const QString &message);
private:

    QString m_rtspUrl;

    QTimer m_reconnectTimer;

    QTimer m_watchdogTimer;


    bool m_connected = false;
    bool m_reconnecting = false;


private slots:

    void reconnect();

    void streamTimeout();
};