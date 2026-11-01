#include "GstVideoReceiver.h"

#include <QElapsedTimer>
#include <QImage>
#include <QTimer>
#include <QUrl>
#include <gst/app/gstappsink.h>
#include <gst/rtsp/gstrtsptransport.h>
#include <gst/video/video.h>
#include <memory>
#include <QMutex>
#include <QMutexLocker>
#include "VideoFrame.h"

struct LatestFrame {
    QMutex mutex;
    QImage image;
    quint64 request = 0;
    quint64 serial = 0;
};

class ReceiverWorker final : public QObject
{
    Q_OBJECT
public:
    explicit ReceiverWorker(std::shared_ptr<LatestFrame> frames) : m_frames(std::move(frames))
    {
        m_busTimer = new QTimer(this);
        m_busTimer->setInterval(50);
        connect(m_busTimer, &QTimer::timeout, this, &ReceiverWorker::pollBus);
        m_retryTimer = new QTimer(this);
        m_retryTimer->setInterval(5000);
        m_retryTimer->setSingleShot(true);
        connect(m_retryTimer, &QTimer::timeout, this, [this] {
            if (m_active) open();
        });
        m_watchdog = new QTimer(this);
        m_watchdog->setInterval(1000);
        connect(m_watchdog, &QTimer::timeout, this, [this] {
            if (m_active && m_lastFrame.elapsed() >= 10000)
                fail(QStringLiteral("Không nhận được khung hình trong 10 giây"));
        });
    }

    void begin(quint64 request, const QString &url)
    {
        halt();
        m_request = request;
        m_url = url;
        m_active = true;
        m_retryDelay = 1000;
        open();
    }

    void halt()
    {
        m_active = false;
        m_retryTimer->stop();
        dispose();
    }

signals:
    void state(quint64 request, GstVideoReceiver::State state);
    void error(quint64 request, const QString &message);

private:
    struct SampleContext { ReceiverWorker *worker; quint64 attempt; };
    std::shared_ptr<LatestFrame> m_frames;
    quint64 m_seenSerial = 0;
    int m_retryDelay = 1000;
    QElapsedTimer m_playingSince;
    QTimer *m_busTimer;
    QTimer *m_retryTimer;
    QTimer *m_watchdog;
    QElapsedTimer m_lastFrame;
    GstElement *m_pipeline = nullptr;
    GstElement *m_sink = nullptr;
    GstBus *m_bus = nullptr;
    std::unique_ptr<SampleContext> m_sampleContext;
    QString m_url;
    quint64 m_request = 0;
    quint64 m_attempt = 0;
    bool m_active = false;
    bool m_hasFrame = false;

    void dispose()
    {
        ++m_attempt; // Reject already-queued callbacks from the old pipeline.
        m_busTimer->stop();
        m_watchdog->stop();
        m_hasFrame = false;
        if (m_pipeline) {
            if (m_sink)
                g_signal_handlers_disconnect_by_data(m_sink, m_sampleContext.get());
            // NULL completes streaming shutdown before callback data is released.
            // This potentially blocking operation runs only on the worker thread.
            gst_element_set_state(m_pipeline, GST_STATE_NULL);
            if (m_bus) gst_object_unref(m_bus);
            gst_object_unref(m_pipeline);
        }
        m_bus = nullptr;
        m_pipeline = nullptr;
        m_sink = nullptr;
        m_sampleContext.reset();
        QMutexLocker lock(&m_frames->mutex);
        m_frames->image = {};
        m_seenSerial = m_frames->serial;
    }

    void fail(const QString &message)
    {
        if (!m_active || m_retryTimer->isActive()) return;
        emit error(m_request, message);
        emit state(m_request, GstVideoReceiver::Reconnecting);
        dispose();
        if (m_active) {
            m_retryTimer->start(m_retryDelay);
            m_retryDelay = qMin(m_retryDelay * 2, 5000);
        }
    }

    void open()
    {
        dispose();
        emit state(m_request, GstVideoReceiver::Connecting);
        m_pipeline = gst_pipeline_new(nullptr);
        GstElement *src = gst_element_factory_make("rtspsrc", nullptr);
        GstElement *decode = gst_element_factory_make("decodebin", nullptr);
        GstElement *convert = gst_element_factory_make("videoconvert", nullptr);
        GstElement *capsFilter = gst_element_factory_make("capsfilter", nullptr);
        m_sink = gst_element_factory_make("appsink", nullptr);
        if (!m_pipeline || !src || !decode || !convert || !capsFilter || !m_sink) {
            QStringList missing;
            if (!src) missing << "rtspsrc";
            if (!decode) missing << "decodebin";
            if (!convert) missing << "videoconvert";
            if (!capsFilter) missing << "capsfilter";
            if (!m_sink) missing << "appsink";
            for (auto *element : {src, decode, convert, capsFilter, m_sink})
                if (element) gst_object_unref(element);
            m_sink = nullptr;
            fail(missing.isEmpty() ? QStringLiteral("Không tạo được luồng xử lý video")
                 : QStringLiteral("Thiếu thành phần GStreamer: %1").arg(missing.join(", ")));
            return;
        }
        gst_bin_add_many(GST_BIN(m_pipeline), src, decode, convert, capsFilter, m_sink, nullptr);
        m_bus = gst_element_get_bus(m_pipeline);
        g_object_set(src, "location", m_url.toUtf8().constData(),
                     "latency", 100u, "drop-on-latency", TRUE,
                     "tcp-timeout", static_cast<guint64>(5000000),
                     "protocols", GST_RTSP_LOWER_TRANS_TCP, nullptr);
        g_object_set(m_sink, "emit-signals", TRUE, "sync", FALSE,
                     "drop", TRUE, "max-buffers", 1u,
                     "wait-on-eos", FALSE, "enable-last-sample", FALSE, nullptr);
        GstCaps *caps = gst_caps_from_string("video/x-raw,format=BGRA");
        g_object_set(capsFilter, "caps", caps, nullptr);
        gst_caps_unref(caps);
        if (!gst_element_link_many(convert, capsFilter, m_sink, nullptr)) {
            fail(QStringLiteral("Không liên kết được luồng chuyển đổi video"));
            return;
        }
        g_signal_connect(src, "pad-added", G_CALLBACK(rtspPad), decode);
        g_signal_connect(decode, "pad-added", G_CALLBACK(decodedPad), convert);
        m_sampleContext = std::make_unique<SampleContext>(SampleContext{this, m_attempt});
        g_signal_connect(m_sink, "new-sample", G_CALLBACK(sample), m_sampleContext.get());
        // Polling uses Qt on every platform; no GLib main loop is required.
        m_lastFrame.start(); // Also covers a connection that never produces a frame.
        m_busTimer->start();
        m_watchdog->start();
        if (gst_element_set_state(m_pipeline, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE)
            fail(QStringLiteral("Không khởi động được luồng video"));
    }

    void pollBus()
    {
        bool newFrame = false;
        {
            QMutexLocker lock(&m_frames->mutex);
            newFrame = m_frames->serial != m_seenSerial;
            m_seenSerial = m_frames->serial;
        }
        if (newFrame) {
            m_lastFrame.restart();
            if (!m_hasFrame) {
                m_hasFrame = true;
                m_playingSince.start();
                emit state(m_request, GstVideoReceiver::Playing);
            }
            // A single lucky frame must not reset backoff on a broken stream.
            if (m_playingSince.elapsed() >= 10000) m_retryDelay = 1000;
        }
        // Drain all message types, including ones we do not handle, without waiting.
        for (int i = 0; m_bus && i < 64; ++i) {
            GstMessage *message = gst_bus_pop(m_bus);
            if (!message) break;
            QString failure;
            if (GST_MESSAGE_TYPE(message) == GST_MESSAGE_ERROR) {
                GError *err = nullptr;
                gchar *debug = nullptr;
                gst_message_parse_error(message, &err, &debug);
                failure = err ? QString::fromUtf8(err->message) : QStringLiteral("Lỗi xử lý video");
                if (err) g_error_free(err);
                g_free(debug);
            } else if (GST_MESSAGE_TYPE(message) == GST_MESSAGE_EOS) {
                failure = QStringLiteral("Luồng video đã kết thúc");
            }
            gst_message_unref(message);
            if (!failure.isEmpty()) {
                fail(failure);
                return; // fail() releases this bus.
            }
        }
    }

    static void linkPad(GstPad *pad, GstElement *target)
    {
        GstPad *sink = gst_element_get_static_pad(target, "sink");
        if (!sink) return;
        if (!gst_pad_is_linked(sink)) gst_pad_link(pad, sink);
        gst_object_unref(sink);
    }

    static void rtspPad(GstElement *, GstPad *pad, gpointer target)
    {
        GstCaps *caps = gst_pad_get_current_caps(pad);
        if (!caps) caps = gst_pad_query_caps(pad, nullptr);
        if (caps && !gst_caps_is_empty(caps) && !gst_caps_is_any(caps)) {
            const GstStructure *s = gst_caps_get_structure(caps, 0);
            // Both microphone audio and video use application/x-rtp.
            if (gst_structure_has_name(s, "application/x-rtp") &&
                g_strcmp0(gst_structure_get_string(s, "media"), "video") == 0)
                linkPad(pad, GST_ELEMENT(target));
        }
        if (caps) gst_caps_unref(caps);
    }

    static void decodedPad(GstElement *, GstPad *pad, gpointer target)
    {
        GstCaps *caps = gst_pad_get_current_caps(pad);
        if (caps && !gst_caps_is_empty(caps) && !gst_caps_is_any(caps) &&
            gst_structure_has_name(gst_caps_get_structure(caps, 0), "video/x-raw"))
            linkPad(pad, GST_ELEMENT(target));
        if (caps) gst_caps_unref(caps);
    }

    static GstFlowReturn sample(GstAppSink *sink, gpointer data)
    {
        const auto context = *static_cast<SampleContext *>(data);
        GstSample *sample = gst_app_sink_pull_sample(sink);
        if (!sample) return GST_FLOW_FLUSHING;
        const QImage image = copyVideoFrame(sample);
        gst_sample_unref(sample);
        // Isolated bad frames are discarded. The watchdog handles sustained loss.
        if (image.isNull()) return GST_FLOW_OK;
        auto &frames = *context.worker->m_frames;
        QMutexLocker lock(&frames.mutex);
        frames.image = image;
        frames.request = context.worker->m_request;
        ++frames.serial;
        return GST_FLOW_OK;
    }
};

GstVideoReceiver::GstVideoReceiver(QObject *parent) : QObject(parent)
{
    qRegisterMetaType<GstVideoReceiver::State>();
    auto frames = std::make_shared<LatestFrame>();
    m_worker = new ReceiverWorker(frames);
    m_worker->moveToThread(&m_thread);
    connect(&m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(m_worker, &ReceiverWorker::state, this, [this](quint64 request, State state) {
        if (m_requested && request == m_request) setState(state);
    });
    connect(m_worker, &ReceiverWorker::error, this, [this](quint64 request, const QString &error) {
        if (m_requested && request == m_request) emit errorOccurred(error);
    });
    auto *presentTimer = new QTimer(this);
    presentTimer->setInterval(33);
    connect(presentTimer, &QTimer::timeout, this, [this, frames] {
        QImage image;
        quint64 request;
        {
            QMutexLocker lock(&frames->mutex);
            image.swap(frames->image);
            request = frames->request;
        }
        if (image.isNull() || !m_requested || request != m_request) return;
        if (m_videoItem) m_videoItem->setFrame(image);
        emit frameReceived();
    });
    presentTimer->start();
    m_thread.start();
}

GstVideoReceiver::~GstVideoReceiver()
{
    // Join before destroying callback targets. No detached thread can outlive us.
    QMetaObject::invokeMethod(m_worker, [this] { m_worker->halt(); }, Qt::BlockingQueuedConnection);
    m_thread.quit();
    m_thread.wait();
}

void GstVideoReceiver::setState(State state)
{
    if (m_state == state) return;
    const bool wasConnected = connected();
    m_state = state;
    emit stateChanged();
    if (wasConnected != connected()) emit connectedChanged();
}

void GstVideoReceiver::setVideoItem(VideoItem *item)
{
    m_videoItem = item;
}

bool GstVideoReceiver::connectCamera(const QString &url)
{
    stop();
    const QUrl parsed(url.trimmed(), QUrl::StrictMode);
    if (!parsed.isValid() || parsed.host().isEmpty() ||
        (parsed.scheme() != "rtsp" && parsed.scheme() != "rtsps")) {
        m_url.clear();
        emit errorOccurred(QStringLiteral("Nhập địa chỉ camera rtsp:// hoặc rtsps:// hợp lệ"));
        return false;
    }
    m_url = parsed.toString(QUrl::FullyEncoded);
    return true;
}

void GstVideoReceiver::start()
{
    if (m_requested) return;
    if (m_url.isEmpty()) {
        emit errorOccurred(QStringLiteral("Chưa cấu hình địa chỉ camera hợp lệ"));
        return;
    }
    m_requested = true;
    const quint64 request = ++m_request;
    const QString url = m_url;
    setState(Connecting);
    QMetaObject::invokeMethod(m_worker, [worker = m_worker, request, url] {
        worker->begin(request, url);
    }, Qt::QueuedConnection);
}

void GstVideoReceiver::stop()
{
    m_requested = false;
    ++m_request;
    setState(Stopped);
    if (m_videoItem) m_videoItem->setFrame(QImage());
    QMetaObject::invokeMethod(m_worker, [worker = m_worker] { worker->halt(); }, Qt::QueuedConnection);
}

#include "GstVideoReceiver.moc"
