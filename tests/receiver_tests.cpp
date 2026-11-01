#include <gst/rtsp-server/rtsp-server.h>
#include <QtTest>
#include <QGuiApplication>
#include <QTcpServer>
#include <thread>
#include <memory>
#include <atomic>

static std::atomic<unsigned> damagedPackets{0};
static std::atomic<bool> stallVideo{false};
#include "GstVideoReceiver.h"
#include "VideoFrame.h"

// The server has its own GLib context. The application under test uses only Qt.
class RtspServer
{
public:
    explicit RtspServer(quint16 port = 0, bool corrupt = false)
    {
        context = g_main_context_new();
        loop = g_main_loop_new(context, FALSE);
        server = gst_rtsp_server_new();
        gst_rtsp_server_set_address(server, "127.0.0.1");
        gst_rtsp_server_set_service(server, QByteArray::number(port).constData());
        GstRTSPMountPoints *mounts = gst_rtsp_server_get_mount_points(server);
        GstRTSPMediaFactory *factory = gst_rtsp_media_factory_new();
        // Audio is deliberately advertised first to catch accidental audio selection.
        gst_rtsp_media_factory_set_launch(factory,
            "( audiotestsrc is-live=true ! alawenc ! rtppcmapay name=pay0 pt=8 "
            "videotestsrc is-live=true pattern=ball ! video/x-raw,width=160,height=120,framerate=10/1 "
            "! x264enc tune=zerolatency speed-preset=ultrafast key-int-max=10 ! rtph264pay name=pay1 pt=96 )");
        if (corrupt) {
            g_signal_connect(factory, "media-configure", G_CALLBACK(+[](GstRTSPMediaFactory *, GstRTSPMedia *media, gpointer) {
                GstElement *pipeline = gst_rtsp_media_get_element(media);
                GstElement *pay = gst_bin_get_by_name_recurse_up(GST_BIN(pipeline), "pay1");
                GstPad *pad = gst_element_get_static_pad(pay, "src");
                gst_pad_add_probe(pad, GstPadProbeType(GST_PAD_PROBE_TYPE_BUFFER | GST_PAD_PROBE_TYPE_BUFFER_LIST),
                    +[](GstPad *, GstPadProbeInfo *info, gpointer data) -> GstPadProbeReturn {
                        if (stallVideo.load()) return GST_PAD_PROBE_DROP;
                        auto damage = [data](GstBuffer *buffer) {
                            auto *counter = static_cast<unsigned *>(data);
                            if (++*counter % 17 != 0) return;
                            const auto size = gst_buffer_get_size(buffer);
                            // Preserve RTP and NAL headers, damage encoded video payload.
                            if (size > 16) {
                                gst_buffer_memset(buffer, 16, 0xff, size - 16);
                                ++damagedPackets;
                            }
                        };
                        if (GST_PAD_PROBE_INFO_TYPE(info) & GST_PAD_PROBE_TYPE_BUFFER_LIST) {
                            auto *list = gst_buffer_list_make_writable(GST_PAD_PROBE_INFO_BUFFER_LIST(info));
                            GST_PAD_PROBE_INFO_DATA(info) = list;
                            for (guint i = 0; i < gst_buffer_list_length(list); ++i)
                                damage(gst_buffer_list_get_writable(list, i));
                        } else {
                            GstBuffer *buffer = gst_buffer_make_writable(GST_PAD_PROBE_INFO_BUFFER(info));
                            GST_PAD_PROBE_INFO_DATA(info) = buffer;
                            damage(buffer);
                        }
                        return GST_PAD_PROBE_OK;
                    }, new unsigned(0), +[](gpointer p) { delete static_cast<unsigned *>(p); });
                gst_object_unref(pad);
                gst_object_unref(pay);
                gst_object_unref(pipeline);
            }), nullptr);
        }
        gst_rtsp_mount_points_add_factory(mounts, "/camera", factory);
        g_object_unref(mounts);
        source = gst_rtsp_server_attach(server, context);
        boundPort = gst_rtsp_server_get_bound_port(server);
        thread = std::thread([this] { g_main_loop_run(loop); });
    }
    ~RtspServer()
    {
        g_main_context_invoke(context, [](gpointer data) -> gboolean {
            auto *self = static_cast<RtspServer *>(data);
            gst_rtsp_server_client_filter(self->server,
                [](GstRTSPServer *, GstRTSPClient *client, gpointer) {
                    gst_rtsp_client_close(client);
                    return GST_RTSP_FILTER_REMOVE;
                }, nullptr);
            if (GSource *s = g_main_context_find_source_by_id(self->context, self->source))
                g_source_destroy(s);
            g_main_loop_quit(self->loop);
            return G_SOURCE_REMOVE;
        }, this);
        thread.join();
        g_object_unref(server);
        g_main_loop_unref(loop);
        g_main_context_unref(context);
    }
    QString url() const { return QString("rtsp://127.0.0.1:%1/camera").arg(boundPort); }
    int boundPort = 0;
private:
    GMainContext *context;
    GMainLoop *loop;
    GstRTSPServer *server;
    guint source;
    std::thread thread;
};

class ReceiverTests : public QObject
{
    Q_OBJECT
private slots:
    void corruptRtpStillRecoversAndStops()
    {
        damagedPackets = 0;
        RtspServer server(0, true);
        GstVideoReceiver receiver;
        QSignalSpy frames(&receiver, &GstVideoReceiver::frameReceived);
        QVERIFY(receiver.connectCamera(server.url()));
        receiver.start();
        QTRY_VERIFY_WITH_TIMEOUT(frames.count() >= 25, 15000);
        QVERIFY(damagedPackets.load() > 0);
        struct ResetStall { ~ResetStall() { stallVideo = false; } } reset;
        stallVideo = true;
        QTRY_COMPARE_WITH_TIMEOUT(receiver.state(), GstVideoReceiver::Reconnecting, 14000);
        stallVideo = false;
        const int stalledFrames = frames.count();
        QTRY_VERIFY_WITH_TIMEOUT(receiver.connected() && frames.count() > stalledFrames + 5, 10000);
        receiver.stop();
        int before = frames.count();
        QTest::qWait(500);
        QCOMPARE(frames.count(), before);
        receiver.start();
        QTRY_VERIFY_WITH_TIMEOUT(frames.count() >= before + 10, 10000);
    }

    void damagedFramesAreRejected()
    {
        GstCaps *caps = gst_caps_from_string("video/x-raw,format=BGRA,width=16,height=16,framerate=10/1");
        GstBuffer *buffer = gst_buffer_new_allocate(nullptr, 16 * 16 * 4, nullptr);
        gst_buffer_memset(buffer, 0, 0x7f, 16 * 16 * 4);
        GstSample *sample = gst_sample_new(buffer, caps, nullptr, nullptr);
        QImage image = copyVideoFrame(sample);
        QCOMPARE(image.size(), QSize(16, 16));
        GST_BUFFER_FLAG_SET(buffer, GST_BUFFER_FLAG_CORRUPTED);
        QVERIFY(copyVideoFrame(sample).isNull());
        GST_BUFFER_FLAG_UNSET(buffer, GST_BUFFER_FLAG_CORRUPTED);
        GST_BUFFER_FLAG_SET(buffer, GST_BUFFER_FLAG_DECODE_ONLY);
        QVERIFY(copyVideoFrame(sample).isNull());
        gst_sample_unref(sample);
        gst_buffer_unref(buffer);
        buffer = gst_buffer_new_allocate(nullptr, 8, nullptr);
        sample = gst_sample_new(buffer, caps, nullptr, nullptr);
        QVERIFY(copyVideoFrame(sample).isNull());
        gst_sample_unref(sample);
        gst_buffer_unref(buffer);
        gst_caps_unref(caps);
        // The good frame remains owned after its original buffer was freed.
        QCOMPARE(image.constBits()[0], uchar(0x7f));
    }

    void failingCameraDoesNotInterruptHealthyCamera()
    {
        RtspServer server;
        GstVideoReceiver healthy, failing;
        QSignalSpy frames(&healthy, &GstVideoReceiver::frameReceived);
        QSignalSpy errors(&failing, &GstVideoReceiver::errorOccurred);
        QVERIFY(healthy.connectCamera(server.url()));
        healthy.start();
        QTRY_VERIFY_WITH_TIMEOUT(healthy.connected(), 8000);
        QVERIFY(failing.connectCamera("rtsp://127.0.0.1:1/camera"));
        failing.start();
        QTRY_VERIFY_WITH_TIMEOUT(errors.count() >= 3, 8000);
        int before = frames.count();
        failing.stop();
        QTRY_VERIFY_WITH_TIMEOUT(frames.count() > before + 5, 3000);
        QVERIFY(healthy.connected());
        // A stalled GUI must receive fresh frames without draining a queued backlog.
        QThread::msleep(1000);
        healthy.stop();
        before = frames.count();
        QTest::qWait(300);
        QCOMPARE(frames.count(), before);
    }

    void missingPluginReported()
    {
        QTemporaryDir isolated;
        QVERIFY(isolated.isValid());
        auto env = QProcessEnvironment::systemEnvironment();
        env.insert("GST_PLUGIN_SYSTEM_PATH_1_0", isolated.path());
        env.insert("GST_PLUGIN_PATH_1_0", isolated.path());
        env.insert("GST_REGISTRY_1_0", isolated.filePath("registry.bin"));
        QProcess child;
        child.setProcessEnvironment(env);
        child.start(QCoreApplication::applicationFilePath(), {"--missing-plugins"});
        QVERIFY(child.waitForFinished(6000));
        QCOMPARE(child.exitCode(), 0);
    }

    void invalidUrl()
    {
        GstVideoReceiver receiver;
        QSignalSpy errors(&receiver, &GstVideoReceiver::errorOccurred);
        QVERIFY(!receiver.connectCamera("not a camera"));
        QCOMPARE(errors.count(), 1);
        QCOMPARE(receiver.state(), GstVideoReceiver::Stopped);
        QVERIFY(!receiver.connected());
    }

    void initialFailureRetriesAndStopCancels()
    {
        QTcpServer reserve;
        QVERIFY(reserve.listen(QHostAddress::LocalHost));
        const auto url = QString("rtsp://127.0.0.1:%1/camera").arg(reserve.serverPort());
        reserve.close();
        GstVideoReceiver receiver;
        QSignalSpy errors(&receiver, &GstVideoReceiver::errorOccurred);
        QVERIFY(receiver.connectCamera(url));
        receiver.start();
        QTRY_VERIFY_WITH_TIMEOUT(errors.count() >= 1, 4000);
        QCOMPARE(receiver.state(), GstVideoReceiver::Reconnecting);
        QTRY_VERIFY_WITH_TIMEOUT(errors.count() >= 2, 8000);
        QElapsedTimer elapsed;
        elapsed.start();
        receiver.stop();
        QVERIFY(elapsed.elapsed() < 100);
        const int stoppedErrors = errors.count();
        QTest::qWait(6000);
        QCOMPARE(errors.count(), stoppedErrors);
        QCOMPARE(receiver.state(), GstVideoReceiver::Stopped);
    }

    void audioFirstVideoPlaysAndStopDropsFrames()
    {
        RtspServer server;
        QVERIFY(server.boundPort > 0);
        GstVideoReceiver receiver;
        QSignalSpy frames(&receiver, &GstVideoReceiver::frameReceived);
        QVERIFY(receiver.connectCamera(server.url()));
        receiver.start();
        QTRY_VERIFY_WITH_TIMEOUT(frames.count() >= 5, 8000);
        QVERIFY(receiver.connected());
        receiver.stop();
        const int stoppedFrames = frames.count();
        QTest::qWait(1000);
        QCOMPARE(frames.count(), stoppedFrames);
        QCOMPARE(receiver.state(), GstVideoReceiver::Stopped);
    }

    void serverLossAndReturnRecovers()
    {
        auto server = std::make_unique<RtspServer>();
        const auto port = server->boundPort;
        const auto url = server->url();
        GstVideoReceiver receiver;
        QSignalSpy frames(&receiver, &GstVideoReceiver::frameReceived);
        QVERIFY(receiver.connectCamera(url));
        receiver.start();
        QTRY_VERIFY_WITH_TIMEOUT(receiver.connected(), 8000);
        server.reset();
        QTRY_COMPARE_WITH_TIMEOUT(receiver.state(), GstVideoReceiver::Reconnecting, 12000);
        const int before = frames.count();
        server = std::make_unique<RtspServer>(port);
        QCOMPARE(server->boundPort, port);
        QTRY_VERIFY_WITH_TIMEOUT(frames.count() > before + 3 && receiver.connected(), 12000);
        receiver.stop();
    }

    void switchDuringRetryAndRapidStopStart()
    {
        RtspServer server;
        GstVideoReceiver receiver;
        QSignalSpy errors(&receiver, &GstVideoReceiver::errorOccurred);
        QVERIFY(receiver.connectCamera("rtsp://127.0.0.1:1/camera"));
        receiver.start();
        QTRY_COMPARE_WITH_TIMEOUT(receiver.state(), GstVideoReceiver::Reconnecting, 4000);
        for (int i = 0; i < 5; ++i) {
            QVERIFY(receiver.connectCamera(server.url()));
            receiver.start();
            receiver.stop();
        }
        QVERIFY(receiver.connectCamera(server.url()));
        receiver.start();
        QTRY_VERIFY_WITH_TIMEOUT(receiver.connected(), 8000);
        const int errorsAfterSwitch = errors.count();
        QTest::qWait(6000); // Past the old connection's retry deadline.
        QVERIFY(receiver.connected());
        QCOMPARE(errors.count(), errorsAfterSwitch);
        receiver.stop();
    }
};

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    gst_init(&argc, &argv);
    if (app.arguments().contains("--missing-plugins")) {
        GstVideoReceiver receiver;
        int result = 2;
        QObject::connect(&receiver, &GstVideoReceiver::errorOccurred, &app,
            [&](const QString &message) {
                result = message.contains("Missing GStreamer elements") ? 0 : 1;
                receiver.stop();
                app.quit();
            });
        receiver.connectCamera("rtsp://127.0.0.1:1/camera");
        receiver.start();
        QTimer::singleShot(4000, &app, &QCoreApplication::quit);
        app.exec();
        return result;
    }
    ReceiverTests tests;
    return QTest::qExec(&tests, argc, argv);
}
#include "receiver_tests.moc"
