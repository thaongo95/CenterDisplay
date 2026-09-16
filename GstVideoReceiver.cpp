#include "GstVideoReceiver.h"
#include <gst/rtsp/gstrtsptransport.h>
#include <QImage>
#include <QMetaObject>
#include <QDebug>

#include "VideoItem.h"
GstVideoReceiver::GstVideoReceiver(QObject *parent)
    : QObject(parent)
{
    // retry connection every 5 seconds
    m_reconnectTimer.setInterval(5000);

    connect(&m_reconnectTimer,
            &QTimer::timeout,
            this,
            &GstVideoReceiver::reconnect);



    // detect frozen stream
    m_watchdogTimer.setInterval(10000);

    connect(&m_watchdogTimer,
            &QTimer::timeout,
            this,
            &GstVideoReceiver::streamTimeout);
}

GstVideoReceiver::~GstVideoReceiver()
{
    stop();
}



void GstVideoReceiver::setVideoItem(VideoItem *item)
{
    m_videoItem = item;

    if (!m_videoItem)
        emit errorOccurred("Invalid QML video item.");
}

bool GstVideoReceiver::connectCamera(const QString &rtspUrl)
{
    m_rtspUrl = rtspUrl;
    stop();
    m_pipeline = gst_pipeline_new("pipeline");

    GstElement *src = gst_element_factory_make("rtspsrc", "src");
    GstElement *decode = gst_element_factory_make("decodebin", "decode");
    GstElement *convert = gst_element_factory_make("videoconvert", "convert");
    GstElement *caps = gst_element_factory_make("capsfilter", "caps");
    m_appsink = gst_element_factory_make("appsink", "sink");
    if (!m_pipeline || !src || !decode || !convert || !caps || !m_appsink)
    {
        if (src)
            gst_object_unref(src);

        if (decode)
            gst_object_unref(decode);

        if (convert)
            gst_object_unref(convert);

        if (caps)
            gst_object_unref(caps);

        if (m_appsink)
            gst_object_unref(m_appsink);

        if (m_pipeline)
            gst_object_unref(m_pipeline);

        m_pipeline = nullptr;
        m_appsink = nullptr;

        return false;
    }
    gst_bin_add_many(GST_BIN(m_pipeline),
                     src,
                     decode,
                     convert,
                     caps,
                     m_appsink,
                     nullptr);
    m_bus = gst_element_get_bus(m_pipeline);

    watchId  = gst_bus_add_watch(
        m_bus,
        onBusMessage,
        this);
    g_object_set(src,
                 "location", rtspUrl.toUtf8().constData(),
                 "latency", 100,
                 "drop-on-latency",TRUE,
                 "tcp-timeout", 5000000,
                 "protocols", GST_RTSP_LOWER_TRANS_TCP,
                 nullptr);

    g_object_set(m_appsink,
                 "emit-signals", TRUE,
                 "sync", FALSE,
                 "drop", TRUE,
                 "max-buffers", 1,
                 nullptr);
    g_signal_connect(
        m_appsink,
        "new-sample",
        G_CALLBACK(onNewSample),
        this);

    GstCaps *gstCaps =
        gst_caps_from_string("video/x-raw,format=BGRA");

    g_object_set(caps,
                 "caps",
                 gstCaps,
                 nullptr);

    gst_caps_unref(gstCaps);

    if (!gst_element_link(convert, caps) ||
        !gst_element_link(caps, m_appsink)) {
        stop();   // or explicit cleanup
        return false;
    }
    g_signal_connect(src,
                     "pad-added",
                     G_CALLBACK(onRtspPadAdded),
                     decode);

    g_signal_connect(decode,
                     "pad-added",
                     G_CALLBACK(onDecodePadAdded),
                     convert);


    emit connectedChanged();

    return true;
}

void GstVideoReceiver::start()
{
    if (!m_pipeline)
        return;

    GstStateChangeReturn ret =
        gst_element_set_state(
            m_pipeline,
            GST_STATE_PLAYING);

    if (ret == GST_STATE_CHANGE_FAILURE)
    {
        emit errorOccurred("Failed to start pipeline");

        if (!m_reconnectTimer.isActive())
            m_reconnectTimer.start();
    }
}
void GstVideoReceiver::stop()
{
    if (!m_pipeline)
        return;

    // Disconnect appsink callback first
    if (m_appsink)
    {
        g_signal_handlers_disconnect_by_data(m_appsink, this);
    }


    gst_element_set_state(m_pipeline, GST_STATE_NULL);

    gst_element_get_state(
        m_pipeline,
        nullptr,
        nullptr,
        GST_SECOND * 5);

    if (watchId != 0) {
        g_source_remove(watchId);
        watchId = 0;
    }
    if (m_bus) {
        gst_object_unref(m_bus);
        m_bus = nullptr;
    }
    gst_object_unref(m_pipeline);

    m_pipeline = nullptr;
    m_appsink = nullptr;

    m_connected = false;

    emit connectedChanged();
}
GstFlowReturn GstVideoReceiver::onNewSample(
    GstAppSink *sink,
    gpointer userData)
{
    GstVideoReceiver *receiver =
        static_cast<GstVideoReceiver*>(userData);

    return receiver->processSample(sink);
}
GstFlowReturn GstVideoReceiver::processSample(
    GstAppSink *sink)
{
    // Reset watchdog every received frame
    QMetaObject::invokeMethod(
        this,
        [this]()
        {
            m_watchdogTimer.start();
        },
        Qt::QueuedConnection);
    GstSample *sample =
        gst_app_sink_pull_sample(sink);


    if (!sample)
        return GST_FLOW_ERROR;


    GstCaps *caps =
        gst_sample_get_caps(sample);


    if (!caps)
    {
        gst_sample_unref(sample);
        return GST_FLOW_ERROR;
    }


    GstStructure *structure =
        gst_caps_get_structure(
            caps,
            0);


    int width = 0;
    int height = 0;


    gst_structure_get_int(
        structure,
        "width",
        &width);


    gst_structure_get_int(
        structure,
        "height",
        &height);


    GstBuffer *buffer =
        gst_sample_get_buffer(sample);


    GstMapInfo map;


    if (gst_buffer_map(
            buffer,
            &map,
            GST_MAP_READ))
    {

        QImage image(
            map.data,
            width,
            height,
            QImage::Format_ARGB32);


        if (m_videoItem)
        {
            QImage copy =
                image.copy();


            QMetaObject::invokeMethod(
                m_videoItem,
                "setFrame",
                Qt::QueuedConnection,
                Q_ARG(QImage, copy));
        }


        gst_buffer_unmap(
            buffer,
            &map);
    }


    gst_sample_unref(sample);


    return GST_FLOW_OK;
}

void GstVideoReceiver::onRtspPadAdded(
    GstElement *src,
    GstPad *newPad,
    gpointer userData)
{
    GstElement *decode =
        static_cast<GstElement *>(userData);


    GstPad *sinkPad =
        gst_element_get_static_pad(
            decode,
            "sink");


    if (!sinkPad)
        return;


    if (gst_pad_is_linked(sinkPad))
    {
        gst_object_unref(sinkPad);
        return;
    }


    GstCaps *caps =
        gst_pad_get_current_caps(newPad);


    if (!caps)
    {
        caps = gst_pad_query_caps(newPad, nullptr);
    }


    const gchar *name =
        gst_structure_get_name(
            gst_caps_get_structure(caps, 0));


    /*
       rtspsrc produces:

       application/x-rtp

       We only want video streams.
    */

    if (g_str_has_prefix(name, "application/x-rtp"))
    {
        GstPadLinkReturn ret =
            gst_pad_link(
                newPad,
                sinkPad);


        if (ret != GST_PAD_LINK_OK)
        {
            qWarning()
            << "Failed to link RTSP pad";
        }
    }


    if (caps)
        gst_caps_unref(caps);


    gst_object_unref(sinkPad);
}

void GstVideoReceiver::onDecodePadAdded(
    GstElement *decode,
    GstPad *newPad,
    gpointer userData)
{
    GstElement *convert =
        static_cast<GstElement *>(userData);


    GstPad *sinkPad =
        gst_element_get_static_pad(
            convert,
            "sink");


    if (!sinkPad)
        return;


    if (gst_pad_is_linked(sinkPad))
    {
        gst_object_unref(sinkPad);
        return;
    }


    GstCaps *caps =
        gst_pad_get_current_caps(newPad);


    if (!caps)
    {
        gst_object_unref(sinkPad);
        return;
    }


    GstStructure *structure =
        gst_caps_get_structure(
            caps,
            0);


    const gchar *name =
        gst_structure_get_name(
            structure);


    /*
       Only accept video.
       Example:
       video/x-raw
    */

    if (g_str_has_prefix(name, "video/"))
    {
        GstPadLinkReturn ret =
            gst_pad_link(
                newPad,
                sinkPad);


        if (ret != GST_PAD_LINK_OK)
        {
            qWarning()
            << "Failed to link decoder";
        }
    }


    gst_caps_unref(caps);
    gst_object_unref(sinkPad);
}
gboolean GstVideoReceiver::onBusMessage(
    GstBus *,
    GstMessage *message,
    gpointer userData)
{
    auto receiver =
        static_cast<GstVideoReceiver *>(userData);

    switch (GST_MESSAGE_TYPE(message))
    {

    case GST_MESSAGE_ERROR:
    {
        GError *err = nullptr;
        gchar *debug = nullptr;

        gst_message_parse_error(
            message,
            &err,
            &debug);

        QString text =
            err ? QString::fromUtf8(err->message)
                : QStringLiteral("Unknown error");

        QMetaObject::invokeMethod(
            receiver,
            [receiver, text]()
            {
                emit receiver->errorOccurred(text);

                if (!receiver->m_reconnectTimer.isActive())
                    receiver->m_reconnectTimer.start();
            },
            Qt::QueuedConnection);

        if (debug)
            g_free(debug);

        if (err)
            g_error_free(err);

        break;
    }

    case GST_MESSAGE_EOS:
    {
        QMetaObject::invokeMethod(
            receiver,
            [receiver]()
            {
                emit receiver->errorOccurred("EOS");

                if (!receiver->m_reconnectTimer.isActive())
                    receiver->m_reconnectTimer.start();
            },
            Qt::QueuedConnection);

        break;
    }

    case GST_MESSAGE_STATE_CHANGED:
    {
        if (GST_MESSAGE_SRC(message) ==
            GST_OBJECT(receiver->m_pipeline))
        {
            GstState oldState;
            GstState newState;

            gst_message_parse_state_changed(
                message,
                &oldState,
                &newState,
                nullptr);

            if (newState == GST_STATE_PLAYING)
            {
                QMetaObject::invokeMethod(
                    receiver,
                    [receiver]()
                    {
                        receiver->m_connected = true;
                        receiver->m_reconnectTimer.stop();
                        receiver->m_watchdogTimer.start();

                        emit receiver->connectedChanged();
                    },
                    Qt::QueuedConnection);
            }
        }

        break;
    }

    default:
        break;
    }

    return TRUE;
}

void GstVideoReceiver::reconnect()
{
    if (m_reconnecting)
        return;

    m_reconnecting = true;

    qDebug() << "Reconnect:" << m_rtspUrl;

    m_reconnectTimer.stop();
    m_watchdogTimer.stop();

    stop();

    QTimer::singleShot(
        1000,
        this,
        [this]()
        {
            if (connectCamera(m_rtspUrl))
            {
                start();
            }

            m_reconnecting = false;
        });
}
void GstVideoReceiver::streamTimeout()
{
    if (m_reconnecting)
        return;

    emit errorOccurred("No frame received");

    reconnect();
}