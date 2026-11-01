#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QUrl>
#include <gst/gst.h>
#include "VideoItem.h"
#include "GstVideoReceiver.h"
#include "AppSettings.h"
#include "TelemetryModel.h"
#include "RescueStore.h"
#include <array>
#include <memory>
#include <QImage>
#include <QMetaType>
#include <cstdlib>
#include <QDir>
#include <QTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QDateTime>
#include <QQuickWindow>

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
    AppSettings settings;
    TelemetryModel telemetry;
    RescueStore rescue;
    std::array<std::unique_ptr<GstVideoReceiver>, 6> ownedReceivers;
    QVariantList receivers;
    for (auto &receiver : ownedReceivers) {
        receiver = std::make_unique<GstVideoReceiver>();
        receivers.append(QVariant::fromValue(static_cast<QObject *>(receiver.get())));
    }
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("telemetry", &telemetry);
    engine.rootContext()->setContextProperty("rescueStore", &rescue);
    engine.rootContext()->setContextProperty("receivers", receivers);
    engine.rootContext()->setContextProperty("appSettings", &settings);
    engine.load(QUrl(QStringLiteral("qrc:/centerDisplay/Main.qml")));

    if (engine.rootObjects().isEmpty())
        return EXIT_FAILURE;

    // Optional local diagnostics for end-to-end telemetry checks.
    const auto optionValue = [&app](const QString &name) {
        const auto args = app.arguments();
        const int index = args.indexOf(name);
        return index >= 0 && index + 1 < args.size() ? args.at(index + 1) : QString();
    };
    const QString reportPath = optionValue("--telemetry-report");
    const QString screenshotPath = optionValue("--telemetry-screenshot");
    if (!reportPath.isEmpty() || !screenshotPath.isEmpty()) {
        auto *reportTimer = new QTimer(&app);
        reportTimer->setInterval(1000);
        QObject::connect(reportTimer, &QTimer::timeout, &app,
            [&telemetry, &engine, reportPath, screenshotPath, captured = false]() mutable {
                QJsonArray vehicles;
                bool allLive = true;
                for (int i = 0; i < telemetry.rowCount(); ++i) {
                    QJsonObject row;
                    const auto roles = telemetry.roleNames();
                    for (auto it = roles.cbegin(); it != roles.cend(); ++it)
                        row[QString::fromUtf8(it.value())] = QJsonValue::fromVariant(
                            telemetry.data(telemetry.index(i), it.key()));
                    allLive = allLive && row["live"].toBool() && row["attitudeLive"].toBool();
                    vehicles.append(row);
                }
                if (!reportPath.isEmpty()) {
                    QSaveFile file(reportPath);
                    if (file.open(QIODevice::WriteOnly)) {
                        file.write(QJsonDocument(QJsonObject{
                            {"timestamp", QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},
                            {"allLive", allLive}, {"vehicles", vehicles}}).toJson());
                        file.commit();
                    }
                }
                if (allLive && !captured && !screenshotPath.isEmpty()) {
                    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
                    if (window) captured = window->grabWindow().save(screenshotPath);
                }
            });
        reportTimer->start();
    }

    if(app.arguments().contains("--rescue-demo")) {
        // Wait for the operator to connect the database.
        auto *poll = new QTimer(&rescue);
        poll->setInterval(1000);
        QObject::connect(poll,&QTimer::timeout,&rescue,[&rescue]{
            if(rescue.online() && !rescue.busy()) rescue.refresh();
        });
        poll->start();
    }
    return app.exec();
}
