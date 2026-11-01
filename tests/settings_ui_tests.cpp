#include <QtTest>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickWindow>
#include <array>
#include "AppSettings.h"
#include "TelemetryModel.h"
#include "VideoItem.h"

class FakeReceiver : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int state READ state NOTIFY stateChanged)
public:
    int state() const { return currentState; }
    int currentState = 0;
    int stops = 0;
    Q_INVOKABLE void stop() { ++stops; currentState = 0; emit stateChanged(); }
    QString lastUrl;
    int starts = 0;
    VideoItem *video = nullptr;
    Q_INVOKABLE bool connectCamera(const QString &url) { lastUrl = url; return true; }
    Q_INVOKABLE void start() { ++starts; currentState = 2; emit stateChanged(); }
    Q_INVOKABLE void setVideoItem(VideoItem *item) { video = item; }
signals:
    void stateChanged();
    void errorOccurred(const QString &message);
};

static QQuickItem *findVisual(QQuickItem *item, const QString &name)
{
    if (item->objectName() == name) return item;
    for (auto *child : item->childItems())
        if (auto *found = findVisual(child, name)) return found;
    return nullptr;
}

class SettingsUiTests : public QObject
{
    Q_OBJECT
private:
    QTemporaryDir directory;
    QStringList urls() const {
        QStringList values;
        for (int i = 1; i <= 6; ++i)
            values << QString("rtsp://camera%1.local:8554/path?name=Cam %1&token=a=b").arg(i);
        return values;
    }
private slots:
    void initTestCase()
    {
        QVERIFY(directory.isValid());
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, directory.path());
        qmlRegisterType<VideoItem>("Video", 1, 0, "VideoItem");
    }
    void savesAcrossProcesses()
    {
        AppSettings settings;
        QCOMPARE(settings.cameraUrls().size(), 6);
        QVERIFY(!settings.save(urls(), 3));
        QVERIFY(settings.save(urls(), 6));
        QProcess child;
        child.start(QCoreApplication::applicationFilePath(), {"--check-saved", directory.path()});
        QVERIFY(child.waitForFinished(6000));
        QCOMPARE(child.exitCode(), 0);
    }
    void layoutsAndEditor()
    {
        AppSettings settings;
        QVERIFY(settings.save(urls(), 1));
        std::array<FakeReceiver, 6> receivers;
        QVariantList list;
        for (auto &receiver : receivers)
            list.append(QVariant::fromValue(static_cast<QObject *>(&receiver)));
        TelemetryModel telemetry;
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("telemetry", &telemetry);
        engine.rootContext()->setContextProperty("receivers", list);
        engine.rootContext()->setContextProperty("appSettings", &settings);
        engine.load(QUrl("qrc:/centerDisplay/Main.qml"));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QVERIFY(window);
        auto *popup = window->findChild<QObject *>("settingsPopup");
        QVERIFY(popup);
        auto *grid = window->findChild<QQuickItem *>("videoGrid");
        QVERIFY(grid);
        for (int mode : {1, 4, 6}) {
            QVERIFY(settings.save(urls(), mode));
            QTest::qWait(100);
            QCOMPARE(grid->property("columns").toInt(), mode == 1 ? 1 : mode == 4 ? 2 : 3);
            for (int i = 0; i < 6; ++i) {
                QVERIFY(receivers[i].video);
                QCOMPARE(receivers[i].video->isVisible(), i < mode);
                if (i < mode) QVERIFY(receivers[i].video->width() > 0);
            }
        }
        const QString screenshots = qEnvironmentVariable("CENTERDISPLAY_TEST_SCREENSHOTS");
        if (!screenshots.isEmpty())
            QVERIFY(window->grabWindow().save(screenshots + "/six-grid.png"));
        auto *selector = findVisual(window->contentItem(), "selector1");
        QVERIFY(selector);
        QCOMPARE(selector->property("count").toInt(), 6);
        // Selecting camera 6 in display 1 must still operate receiver 1.
        selector->setProperty("currentIndex", 5);
        QVERIFY(QMetaObject::invokeMethod(selector, "activated", Q_ARG(int, 5)));
        QCOMPARE(receivers[0].lastUrl, urls()[5]);
        QCOMPARE(receivers[0].starts, 1);
        QCOMPARE(receivers[5].starts, 0);
        QVERIFY(QMetaObject::invokeMethod(popup, "open"));
        QTest::qWait(250);
        // Camera 6 is assigned to both screen 1 and screen 6.
        auto *toggle = findVisual(window->contentItem(), "streamToggle6");
        auto *status = findVisual(window->contentItem(), "cameraStatus6");
        QVERIFY(toggle);
        QVERIFY(status);
        QCOMPARE(toggle->property("text").toString(), QString("Dừng"));
        QCOMPARE(status->property("color").value<QColor>(), QColor("#187347"));
        receivers[0].currentState = 3;
        emit receivers[0].stateChanged();
        QCOMPARE(status->property("color").value<QColor>(), QColor("#ac382d"));
        receivers[0].currentState = 1;
        emit receivers[0].stateChanged();
        QCOMPARE(status->property("color").value<QColor>(), QColor("#956400"));
        QVERIFY(QMetaObject::invokeMethod(toggle, "clicked"));
        QCOMPARE(receivers[0].stops, 1);
        QCOMPARE(receivers[5].stops, 1);
        QCOMPARE(status->property("color").value<QColor>(), QColor("#52606d"));
        QCOMPARE(toggle->property("text").toString(), QString("Bắt đầu"));
        QVERIFY(QMetaObject::invokeMethod(toggle, "clicked"));
        QCOMPARE(receivers[0].starts, 2);
        QCOMPARE(receivers[5].starts, 1);
        QCOMPARE(receivers[5].lastUrl, urls()[5]);

        auto *field = findVisual(window->contentItem(), "url6");
        QVERIFY(field);
        QCOMPARE(field->property("text").toString(), urls()[5]);
        if (!screenshots.isEmpty())
            QVERIFY(window->grabWindow().save(screenshots + "/six-settings.png"));
        field->setProperty("text", "rtsp://saved-camera-six/live");
        QVERIFY(QMetaObject::invokeMethod(field, "textEdited"));
        // Start uses the draft URL, while persistence still requires Apply.
        QVERIFY(QMetaObject::invokeMethod(toggle, "clicked"));
        QVERIFY(QMetaObject::invokeMethod(toggle, "clicked"));
        QCOMPARE(receivers[0].lastUrl, QString("rtsp://saved-camera-six/live"));
        QCOMPARE(receivers[5].lastUrl, receivers[0].lastUrl);
        QCOMPARE(settings.cameraUrls()[5], urls()[5]);
        QVERIFY(QMetaObject::invokeMethod(popup, "applySettings"));
        QTest::qWait(250);
        QCOMPARE(settings.cameraUrls()[5], QString("rtsp://saved-camera-six/live"));
        QVERIFY(QMetaObject::invokeMethod(popup, "open"));
        QTest::qWait(250);
        QCOMPARE(field->property("text").toString(), settings.cameraUrls()[5]);
        field->setProperty("text", "rtsp://discard-this/live");
        QVERIFY(QMetaObject::invokeMethod(field, "textEdited"));
        popup->setProperty("draftMode", 1);
        QVERIFY(QMetaObject::invokeMethod(popup, "close"));
        QTest::qWait(250);
        QCOMPARE(settings.cameraUrls()[5], QString("rtsp://saved-camera-six/live"));
        QCOMPARE(settings.layoutMode(), 6);
        QVERIFY(QMetaObject::invokeMethod(popup, "open"));
        QTest::qWait(250);
        QCOMPARE(field->property("text").toString(), settings.cameraUrls()[5]);
        window->resize(640, 480);
        QTest::qWait(150);
        QVERIFY(field->width() > 80);
        QVERIFY(toggle->mapToScene(QPointF(toggle->width(), 0)).x() <= window->width());
        if (!screenshots.isEmpty())
            QVERIFY(window->grabWindow().save(screenshots + "/settings-small.png"));
        for (int mode : {1, 0, 1}) {
            window->setProperty("viewMode", mode);
            QTest::qWait(150);
            QCOMPARE(grid->isVisible(), mode == 0);
            auto *viewSelector=findVisual(window->contentItem(), "viewSelector");
            QVERIFY(viewSelector);
            QCOMPARE(viewSelector->property("count").toInt(),2);
            QCOMPARE(viewSelector->property("currentIndex").toInt(),mode);
            QCOMPARE(findVisual(window->contentItem(), "videoToolbar")->isVisible(),mode==0);
            QCOMPARE(findVisual(window->contentItem(), "openSettings")->isVisible(),mode==0);
            QCOMPARE(findVisual(window->contentItem(), "mapToolbarHost")->isVisible(),mode==1);
            if(mode==1) {
                auto *toggle=findVisual(window->contentItem(), "toggleLocationInfo");
                auto *info=findVisual(window->contentItem(), "locationInfo");
                QVERIFY(toggle); QVERIFY(info);
                QVERIFY(info->isVisible());
                QVERIFY(QMetaObject::invokeMethod(toggle,"clicked"));
                QVERIFY(!info->isVisible());
                QVERIFY(QMetaObject::invokeMethod(toggle,"clicked"));
                QVERIFY(info->isVisible());
                QVERIFY(toggle->mapToScene(QPointF(toggle->width(),0)).x()<=window->width());
            }
        }
        window->close();
    }
};

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    if (app.arguments().contains("--check-saved")) {
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, app.arguments().last());
        AppSettings settings;
        return settings.layoutMode() == 6 && settings.cameraUrls().size() == 6 &&
            settings.cameraUrls()[5] == "rtsp://camera6.local:8554/path?name=Cam 6&token=a=b" ? 0 : 1;
    }
    SettingsUiTests tests;
    return QTest::qExec(&tests, argc, argv);
}
#include "settings_ui_tests.moc"
