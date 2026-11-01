#include <QtTest>
#include <QQmlEngine>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickWindow>
#include <QtEndian>
#include <cstring>
#include <limits>
#include "TelemetryModel.h"
#include "AppSettings.h"

static QQuickItem *findVisual(QQuickItem *item, const QString &name) {
    if(item->objectName()==name) return item;
    for(auto *child:item->childItems())
        if(auto *found=findVisual(child,name)) return found;
    return nullptr;
}

class MapTests:public QObject {
    Q_OBJECT
    QTemporaryDir dir;
    QByteArray frame(int version=2,int system=1,int latitude=108000000, int heading=9000) {
        QByteArray payload(28,0);
        qToLittleEndian<qint32>(latitude,payload.data()+4);
        qToLittleEndian<qint32>(1067000000,payload.data()+8);
        qToLittleEndian<qint32>(120000,payload.data()+16);
        qToLittleEndian<quint16>(heading,payload.data()+26);
        if(version==2) while(payload.size()>1 && payload.back()==0) payload.chop(1);
        QByteArray b;
        if(version==2) b=QByteArray::fromHex("fd000000000101210000");
        else b=QByteArray::fromHex("fe0000010121");
        b[1]=char(payload.size()); b[version==2?5:3]=char(system); b+=payload;
        quint16 crc=0xffff;
        auto accumulate=[&](quint8 byte){ quint8 t=byte^quint8(crc);t^=t<<4;crc=(crc>>8)^(quint16(t)<<8)^(quint16(t)<<3)^(quint16(t)>>4); };
        for(int i=1;i<b.size();++i) accumulate(quint8(b[i]));
        accumulate(104); b+=char(crc&255);b+=char(crc>>8);return b;
    }
    QByteArray attitudeFrame(float roll=0.5f, float pitch=-0.25f, float yaw=1.0f, int version=2, int system=1) {
        QByteArray payload(28,0);
        auto put=[&](int offset,float value) { quint32 bits;std::memcpy(&bits,&value,4);qToLittleEndian(bits,payload.data()+offset); };
        put(4,roll);put(8,pitch);put(12,yaw);
        if(version==2) while(payload.size()>1 && payload.back()==0)payload.chop(1);
        QByteArray b=version==2?QByteArray::fromHex("fd0000000001011e0000"):QByteArray::fromHex("fe000001011e");
        b[1]=char(payload.size());b[version==2?5:3]=char(system);b+=payload;
        quint16 crc=0xffff;
        auto accumulate=[&](quint8 byte){quint8 t=byte^quint8(crc);t^=t<<4;crc=(crc>>8)^(quint16(t)<<8)^(quint16(t)<<3)^(quint16(t)>>4);};
        for(int i=1;i<b.size();++i)accumulate(quint8(b[i]));
        accumulate(39);b+=char(crc&255);b+=char(crc>>8);return b;
    }
private slots:
    void attitude() {
        TelemetryModel model;QUdpSocket sender;const auto idx=model.index(0);
        QVERIFY(model.configure(0,24550,"127.0.0.1",1));
        for(int version : {1,2}) {
            sender.writeDatagram(attitudeFrame(.5f,-.25f,1.0f,version),QHostAddress::LocalHost,24550);
            QTRY_VERIFY(model.data(idx,TelemetryModel::AttitudeLive).toBool());
            QVERIFY(qAbs(model.data(idx,TelemetryModel::Roll).toDouble()-28.64788976)<0.001);
            QVERIFY(qAbs(model.data(idx,TelemetryModel::Pitch).toDouble()+14.32394488)<0.001);
            QVERIFY(qAbs(model.data(idx,TelemetryModel::Yaw).toDouble()-57.29577951)<0.001);
            QVERIFY(!model.data(idx,TelemetryModel::HasPosition).toBool());
            QVERIFY(model.configure(0,24550,"127.0.0.1",1));
        }
        auto bad=attitudeFrame();bad[14]^=1;
        sender.writeDatagram(bad,QHostAddress::LocalHost,24550);
        sender.writeDatagram(attitudeFrame(0,0,0,2,2),QHostAddress::LocalHost,24550);
        sender.writeDatagram(attitudeFrame(std::numeric_limits<float>::quiet_NaN()),QHostAddress::LocalHost,24550);
        sender.writeDatagram(attitudeFrame(9),QHostAddress::LocalHost,24550);
        auto signedFrame=attitudeFrame();signedFrame[2]=1;signedFrame+=QByteArray(13,0);
        sender.writeDatagram(signedFrame,QHostAddress::LocalHost,24550);
        QTest::qWait(100);QVERIFY(!model.data(idx,TelemetryModel::HasAttitude).toBool());
        // Fully zero angles use a one-byte MAVLink 2 payload and must be accepted.
        sender.writeDatagram(attitudeFrame(0,0,0),QHostAddress::LocalHost,24550);
        QTRY_VERIFY(model.data(idx,TelemetryModel::AttitudeLive).toBool());
        QCOMPARE(model.data(idx,TelemetryModel::Roll).toDouble(),0.0);
        QTest::qWait(5100);
        sender.writeDatagram(frame(),QHostAddress::LocalHost,24550);
        QTRY_VERIFY(model.data(idx,TelemetryModel::Live).toBool());
        QVERIFY(!model.data(idx,TelemetryModel::AttitudeLive).toBool());
        model.disconnectLocation(0);
        QVERIFY(!model.data(idx,TelemetryModel::HasAttitude).toBool());
    }
    void initTestCase(){QVERIFY(dir.isValid());QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,dir.path());}
    void telemetry() {
        TelemetryModel model; QUdpSocket sender;
        for(int i=0;i<6;++i) {
            QVERIFY(model.configure(i,24550+i,"127.0.0.1",1));
            QCOMPARE(sender.writeDatagram(frame(i%2+1),QHostAddress::LocalHost,24550+i),qint64(frame(i%2+1).size()));
        }
        for(int i=0;i<6;++i) {
            QTRY_VERIFY(model.data(model.index(i),TelemetryModel::Live).toBool());
            QCOMPARE(model.data(model.index(i),TelemetryModel::Latitude).toDouble(),10.8);
            QCOMPARE(model.data(model.index(i),TelemetryModel::Altitude).toDouble(),120.0);
        }
        QVERIFY(!model.configure(0,24551,"",1));
        QVERIFY(!model.configure(0,24550,"bad-ip",1));
        QVERIFY(model.configure(0,24550,"127.0.0.1",1));
        auto bad=frame();bad[12]^=1;
        sender.writeDatagram(bad,QHostAddress::LocalHost,24550);
        sender.writeDatagram(frame(2,2),QHostAddress::LocalHost,24550);
        sender.writeDatagram(frame(2,1,910000000),QHostAddress::LocalHost,24550);
        QTest::qWait(100);QVERIFY(!model.data(model.index(0),TelemetryModel::HasPosition).toBool());
        sender.writeDatagram(frame(2,1,110000000,0),QHostAddress::LocalHost,24550);
        QTRY_COMPARE(model.data(model.index(0),TelemetryModel::Latitude).toDouble(),11.0);
        QCOMPARE(model.data(model.index(0),TelemetryModel::Heading).toDouble(),0.0);
        QTest::qWait(5100);QVERIFY(!model.data(model.index(0),TelemetryModel::Live).toBool());
        QVERIFY(model.data(model.index(0),TelemetryModel::HasPosition).toBool());
    }
    void locationControls() {
        TelemetryModel model; QUdpSocket sender;
        QVERIFY(model.configure(0,24550,"127.0.0.1",1));
        QVERIFY(model.configure(1,24551,"127.0.0.1",1));
        sender.writeDatagram(frame(),QHostAddress::LocalHost,24550);
        QTRY_VERIFY(model.data(model.index(0),TelemetryModel::Live).toBool());
        model.disconnectLocation(0);
        QCOMPARE(model.data(model.index(0),TelemetryModel::Status).toString(),QString("Đã ngắt kết nối"));
        QVERIFY(!model.data(model.index(0),TelemetryModel::HasPosition).toBool());
        QVERIFY(!model.data(model.index(0),TelemetryModel::Listening).toBool());
        // Saving link settings must not reconnect a deliberately disconnected slot.
        QVERIFY(model.configure(0,24550,"127.0.0.1",1));
        QUdpSocket occupied;
        QVERIFY(occupied.bind(QHostAddress::AnyIPv4,24550,QUdpSocket::DontShareAddress));
        QVERIFY(!model.connectLocation(0));
        QVERIFY(!model.data(model.index(0),TelemetryModel::Listening).toBool());
        occupied.close();
        model.disconnectLocation(0);
        sender.writeDatagram(frame(),QHostAddress::LocalHost,24550);
        sender.writeDatagram(frame(),QHostAddress::LocalHost,24551);
        QTRY_VERIFY(model.data(model.index(1),TelemetryModel::Live).toBool());
        QVERIFY(!model.data(model.index(0),TelemetryModel::HasPosition).toBool());
        QVERIFY(model.connectLocation(0));
        QCOMPARE(model.data(model.index(0),TelemetryModel::Status).toString(),QString("Đang chờ vị trí"));
        sender.writeDatagram(frame(),QHostAddress::LocalHost,24550);
        QTRY_VERIFY(model.data(model.index(0),TelemetryModel::Live).toBool());
    }
    void pythonSimulator() {
        TelemetryModel model;
        for(int i=0;i<6;++i) QVERIFY(model.configure(i,24550+i,"127.0.0.1",1));
        QProcess simulator;
        simulator.start("python3", {QString(SIMULATOR_PATH), "--base-port", "24550", "--duration", "1"});
        QVERIFY(simulator.waitForStarted());
        for(int i=0;i<6;++i) QTRY_VERIFY(model.data(model.index(i),TelemetryModel::Live).toBool());
        const double first=model.data(model.index(0),TelemetryModel::Latitude).toDouble();
        QTRY_VERIFY(model.data(model.index(0),TelemetryModel::Latitude).toDouble()!=first);
        QTRY_COMPARE(simulator.state(), QProcess::NotRunning);
        QCOMPARE(simulator.exitCode(),0);
    }
    void mapUi() {
        AppSettings settings;TelemetryModel telemetry;
        QQmlEngine engine;
        engine.rootContext()->setContextProperty("appSettings",&settings);
        engine.rootContext()->setContextProperty("telemetry",&telemetry);
        QQmlComponent component(&engine,QUrl("qrc:/centerDisplay/MapPanel.qml"));
        QScopedPointer<QObject> object(component.create());
        QVERIFY2(object, qPrintable(component.errorString()));
        QQuickWindow window;window.resize(900,700);
        auto *item=qobject_cast<QQuickItem*>(object.data());QVERIFY(item);
        item->setParentItem(window.contentItem());item->setSize(QSizeF(900,700));window.show();
        QTest::qWait(300);
        auto *selector=object->findChild<QObject*>("mapTypeSelector");QVERIFY(selector);
        for(int i=0;i<3;++i) {
            selector->setProperty("currentIndex",i);
            QVERIFY(QMetaObject::invokeMethod(selector,"activated",Q_ARG(int,i)));
            QTest::qWait(100);QCOMPARE(settings.mapType(),i);
        }
        QUdpSocket sender;
        auto *map=object->findChild<QObject*>("uavMap"); QVERIFY(map);
        sender.writeDatagram(frame(),QHostAddress::LocalHost,24550);
        QTRY_COMPARE(map->property("zoomLevel").toDouble(),16.0);
        map->setProperty("zoomLevel",12.0);
        sender.writeDatagram(frame(2,1,108010000),QHostAddress::LocalHost,24550);
        QTest::qWait(400);
        QCOMPARE(map->property("zoomLevel").toDouble(),12.0);
        auto *toggle=findVisual(item,"locationToggle1"); QVERIFY(toggle);
        QCOMPARE(toggle->property("text").toString(),QString("Ngắt kết nối"));
        QVERIFY(QMetaObject::invokeMethod(toggle,"clicked"));
        QTRY_COMPARE(toggle->property("text").toString(),QString("Kết nối"));
        QTest::qWait(400);
        QVERIFY(QMetaObject::invokeMethod(toggle,"clicked"));
        sender.writeDatagram(frame(),QHostAddress::LocalHost,24550);
        QTRY_COMPARE(map->property("zoomLevel").toDouble(),16.0);
        for(int i=0;i<6;++i) sender.writeDatagram(frame(2,1,108000000+i*200000),QHostAddress::LocalHost,24550+i);
        QTest::qWait(200);
        auto *fit=object->findChild<QObject*>("fitUavs"); QVERIFY(fit);
        QVERIFY(QMetaObject::invokeMethod(fit,"clicked")); QTest::qWait(600);
        const auto output=qEnvironmentVariable("CENTERDISPLAY_MAP_SCREENSHOT");
        if(!output.isEmpty()) QVERIFY(window.grabWindow().save(output));
        QVERIFY(!settings.saveMap(3,"https://bad"));
        QVERIFY(settings.saveMap(3,"https://example.com/{z}/{x}/{y}.png"));
        QCOMPARE(settings.mapUrl(),QString("https://example.com/%z/%x/%y.png"));
    }
};
QTEST_MAIN(MapTests)
#include "map_tests.moc"
