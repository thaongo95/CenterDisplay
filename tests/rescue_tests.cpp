#include <QtTest>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QQmlEngine>
#include <QQmlContext>
#include <QQmlComponent>
#include <QQuickItem>
#include <QQuickWindow>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QStandardPaths>
#include "RescueStore.h"
#include "AppSettings.h"
#include "TelemetryModel.h"
#include "CenterPlanInbox.h"

class RescueTests:public QObject {
    Q_OBJECT
    QSqlDatabase db;
    QString schema;
    QTemporaryDir config;
private slots:
    void initTestCase() {
        QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,config.path());
        schema="rescue_test_"+QString::number(QCoreApplication::applicationPid());
        qputenv("CENTER_DB_SCHEMA",schema.toUtf8());
        db=QSqlDatabase::addDatabase("QPSQL","rescue-test-admin");
        db.setHostName("127.0.0.1");db.setPort(55432);db.setDatabaseName("center_rescue");db.setUserName("center_demo");db.setPassword("center_demo_local");
        QVERIFY2(db.open(),qPrintable(db.lastError().text()));
        QSqlQuery q(db);QVERIFY(q.exec("CREATE SCHEMA "+schema));QVERIFY(q.exec("SET search_path TO "+schema));
        for(const QString &path:{QString(RESCUE_SOURCE_DIR)+"/database/schema.sql",QString(RESCUE_SOURCE_DIR)+"/database/seed.sql"}) {
            QFile f(path);QVERIFY(f.open(QIODevice::ReadOnly));
            QVERIFY2(q.exec(QString::fromUtf8(f.readAll())),qPrintable(q.lastError().text()));
        }
    }
    void operatorConnectsDatabase() {
        RescueStore store;
        QVERIFY(!store.online());QVERIFY(store.areas().isEmpty());QVERIFY(store.plans().isEmpty());
        QVERIFY(store.open());QCOMPARE(store.areas().size(),3);QVERIFY(store.plans().isEmpty());
    }
    void customPlanning() {
        RescueStore store;QVERIFY(store.open());
        for(int uav=1;uav<=3;++uav) {
            QVERIFY2(store.createPlan(uav,uav,120,150,30,{}),qPrintable(store.status()));
            auto plan=store.plans().first().toMap();auto points=plan["waypoints"].toList();
            QVERIFY(points.size()>10);QVERIFY(points.size()<=100);
            QCOMPARE(points[1].toMap()["relativeAltitudeM"].toDouble(),120.0);
            QCOMPARE(points[1].toMap()["siteId"].toInt(),0);
        }
        QVERIFY(store.createPlan(4,0,90,150,0,{3,1}));
        auto points=store.plans().first().toMap()["waypoints"].toList();
        QCOMPARE(points.size(),4);QCOMPARE(points[1].toMap()["siteId"].toInt(),3);QCOMPARE(points[2].toMap()["siteId"].toInt(),1);
        QVERIFY(!store.createPlan(5,0,90,150,0,{1})); // already assigned
        QVERIFY(!store.createPlan(4,0,90,150,0,{3,3})); // duplicate; previous plan survives rollback
        QVERIFY(!store.createPlan(1,999,80,150,0,{}));
        QVERIFY(!store.createPlan(1,1,0,150,0,{}));
        QSqlQuery q(db);QVERIFY(q.exec("DELETE FROM rescue_record WHERE plan_id IS NOT NULL"));QVERIFY(q.exec("DELETE FROM rescue_plan"));
        QVERIFY(q.exec("UPDATE rescue_site SET state='resolved'"));
        QVERIFY(store.createPlan(1,1,80,150,0,{})); // area survey does not require open sites
        QVERIFY(q.exec("UPDATE rescue_site SET state='open'"));
        QVERIFY(q.exec("DELETE FROM rescue_record WHERE plan_id IS NOT NULL"));QVERIFY(q.exec("DELETE FROM rescue_plan"));
    }
    void cancelDraft() {
        RescueStore store;QVERIFY(store.open());
        QVERIFY(store.createPlan(4,0,80,150,0,{1,2}));
        const int id=store.plans().first().toMap()["id"].toInt();
        QVERIFY(store.cancelPlan(id));
        QCOMPARE(store.plans().first().toMap()["state"].toString(),QString("cancelled"));
        QVERIFY(!store.cancelPlan(id));
        QVERIFY(!store.sendDemoPlan(id));
        QVERIFY(store.createPlan(5,0,80,150,0,{1,2})); // cancellation releases assignments
        const int other=store.plans().first().toMap()["id"].toInt();
        QSqlQuery q(db);
        for(const auto state:{"sending","failed","demo_received","received_for_review"}) {
            QVERIFY(q.exec(QString("UPDATE rescue_plan SET state='%1' WHERE id=%2").arg(state).arg(other)));
            QVERIFY(!store.cancelPlan(other));
        }
        QVERIFY(q.exec(QString("SELECT count(*) FROM rescue_record WHERE plan_id=%1 AND event='plan_cancelled'").arg(id)));
        QVERIFY(q.next());QCOMPARE(q.value(0).toInt(),1);
        QVERIFY(q.exec("DELETE FROM rescue_record WHERE plan_id IS NOT NULL"));
        QVERIFY(q.exec("DELETE FROM rescue_plan"));
    }
    void balancedTransportAssignments() {
        RescueStore store;QVERIFY(store.open());
        QSet<int> assigned;
        for(int uav=4;uav<=6;++uav) {
            QVERIFY2(store.draftPlan(uav),qPrintable(store.status()));
            QSqlQuery q(db);
            QVERIFY(q.exec(QString("SELECT site_ids::text FROM rescue_plan WHERE uav_id=%1").arg(uav)));
            QVERIFY(q.next());
            const auto ids=QJsonDocument::fromJson(q.value(0).toByteArray()).array();
            QCOMPARE(ids.size(),uav==4 ? 4 : 3);
            for(const auto id:ids) { QVERIFY(!assigned.contains(id.toInt())); assigned.insert(id.toInt()); }
        }
        QCOMPARE(assigned.size(),10);
        QSqlQuery q(db);
        QVERIFY(q.exec("DELETE FROM rescue_record WHERE plan_id IS NOT NULL"));
        QVERIFY(q.exec("DELETE FROM rescue_plan"));
    }
    void persistenceAndPlanning() {
        RescueStore store;QVERIFY2(store.open(),qPrintable(store.status()));
        QCOMPARE(store.sites().size(),10);QCOMPARE(store.areas().size(),3);
        QVERIFY(store.report(1,1,1,2,"Verified water stock; urgent replenishment",false));
        QVERIFY(!store.report(1,1,1,3,"Outdated report",false));
        QVERIFY(!store.report(2,1,4,3,"Transport cannot submit recon report",false));
        RescueStore second;QVERIFY(second.open());
        bool found=false;for(const auto &value:second.sites()) if(value.toMap()["id"].toInt()==1) {found=true;QCOMPARE(value.toMap()["revision"].toInt(),2);}
        QVERIFY(found);
        for(int id=1;id<=3;++id) QVERIFY2(store.draftPlan(id),qPrintable(store.status()));
        QVERIFY(store.draftPlan(4));
        const int first=store.plans().first().toMap()["id"].toInt();
        QSqlQuery q(db);QVERIFY(q.exec(QString("SELECT site_ids::text FROM rescue_plan WHERE id=%1").arg(first)));QVERIFY(q.next());
        const auto ids=QJsonDocument::fromJson(q.value(0).toByteArray()).array();
        QCOMPARE(ids,QJsonArray({2,6,10,1}));
        QVERIFY(store.draftPlan(5));
        QVERIFY(q.exec("SELECT count(*) FROM rescue_plan p1 JOIN rescue_plan p2 ON p1.id<p2.id AND p1.purpose='transport' AND p2.purpose='transport' AND p1.site_ids @> jsonb_build_array((p2.site_ids->>0)::int)"));QVERIFY(q.next());QCOMPARE(q.value(0).toInt(),0);
        QVERIFY(store.report(2,1,1,1,"Supplies confirmed; monitor only",false));
        QVERIFY(!store.sendDemoPlan(first)); // stale snapshot cannot dispatch
        QVERIFY(store.draftPlan(4));
        const int next=store.plans().first().toMap()["id"].toInt();
        QProcess mock;mock.start("python3",{QString(RESCUE_SOURCE_DIR)+"/tools/mock_gcs.py"});QVERIFY(mock.waitForStarted());QVERIFY(mock.waitForReadyRead(3000));
        QVERIFY(store.sendDemoPlan(next));QTRY_VERIFY_WITH_TIMEOUT(!store.busy(),7000);
        QCOMPARE(store.plans().first().toMap()["state"].toString(),QString("demo_received"));
        QVERIFY(!store.sendDemoPlan(next));
        mock.terminate();QVERIFY(mock.waitForFinished(3000));
    }
    void groundStationInbox() {
        QStandardPaths::setTestModeEnabled(true);
        CenterPlanInbox inbox;
        QVERIFY(!inbox.start("0.0.0.0",8767,4,"test-token-at-least-24-characters"));
        QVERIFY(inbox.start("127.0.0.1",8767,6,"test-token-at-least-24-characters"));
        RescueStore store;QVERIFY(store.open());QVERIFY(store.draftPlan(6));
        const int plan=store.plans().first().toMap()["id"].toInt();
        QVERIFY(store.configureGroundStation(6,"127.0.0.1","test-token-at-least-24-characters"));
        QVERIFY(store.sendToGroundStation(plan));QTRY_VERIFY_WITH_TIMEOUT(!store.busy(),7000);
        QCOMPARE(store.plans().first().toMap()["state"].toString(),QString("received_for_review"));
        QVERIFY(!inbox.latestPlan().isEmpty());
        QFile file(inbox.latestPlan());QVERIFY(file.open(QIODevice::ReadOnly));
        const auto document=QJsonDocument::fromJson(file.readAll()).object();
        QCOMPARE(document["fileType"].toString(),QString("Plan"));
        const auto items=document["mission"].toObject()["items"].toArray();QVERIFY(items.size()>=2);
        for(auto item:items) QCOMPARE(item.toObject()["command"].toInt(),16);
        file.close();QVERIFY(file.remove());
        inbox.stop();
        QVERIFY(store.configureGroundStation(6,"127.0.0.1","wrong-token-at-least-24-characters"));
        QVERIFY(inbox.start("127.0.0.1",8767,6,"test-token-at-least-24-characters"));
        QVERIFY(store.draftPlan(1));
        const int bad=store.plans().first().toMap()["id"].toInt();
        QVERIFY(store.configureGroundStation(1,"127.0.0.1","wrong-token-at-least-24-characters"));
        QVERIFY(store.sendToGroundStation(bad));QTRY_VERIFY_WITH_TIMEOUT(!store.busy(),7000);
        QCOMPARE(store.plans().first().toMap()["state"].toString(),QString("failed"));
        QVERIFY(store.configureGroundStation(1,"127.0.0.1","test-token-at-least-24-characters"));
        QVERIFY(store.sendToGroundStation(bad));QTRY_VERIFY_WITH_TIMEOUT(!store.busy(),7000);
        QCOMPARE(store.plans().first().toMap()["state"].toString(),QString("failed")); // correct token, wrong fleet slot
        inbox.stop();
    }
    void simulationControls() {
        QTemporaryDir directory;
        qputenv("CENTER_DEMO_MISSIONS_DIR",directory.path().toUtf8());
        RescueStore store;
        qunsetenv("CENTER_DEMO_MISSIONS_DIR");
        QVERIFY(store.simulation());
        QVERIFY(!store.setSimulationSpeed(7));
        for(const int speed:{0,1,2,5,10,20}) {
            QVERIFY(store.setSimulationSpeed(speed));
            QFile control(directory.path()+"/control.json");QVERIFY(control.open(QIODevice::ReadOnly));
            QCOMPARE(QJsonDocument::fromJson(control.readAll()).object()["speed"].toInt(),speed);
        }
        QFile status(directory.path()+"/status.json");QVERIFY(status.open(QIODevice::WriteOnly));
        status.write("{\"elapsed\":100,\"speed\":20,\"uavs\":[]}");status.close();
        QTRY_VERIFY(store.simulationLive());
        QCOMPARE(store.simulationStatus()["elapsed"].toDouble(),100);
        QVERIFY(status.remove());QTRY_VERIFY(!store.simulationLive());
    }
    void ui() {
        QTemporaryDir simulationDirectory;
        qputenv("CENTER_DEMO_MISSIONS_DIR",simulationDirectory.path().toUtf8());
        RescueStore store;
        qunsetenv("CENTER_DEMO_MISSIONS_DIR");
        QJsonArray vehicles;
        for(int i=0;i<6;++i) vehicles.append(QJsonObject{{"battery",82-i*6},{"payload",i>=3?15:0},{"status",i<3?"Đang khảo sát / xác nhận điểm":"Đang vận chuyển"}});
        QFile status(simulationDirectory.path()+"/status.json");QVERIFY(status.open(QIODevice::WriteOnly));
        status.write(QJsonDocument(QJsonObject{{"elapsed",300},{"speed",1},{"uavs",vehicles}}).toJson());status.close();
        QVERIFY(store.open());AppSettings settings;TelemetryModel telemetry;
        QQmlEngine engine;engine.rootContext()->setContextProperty("rescueStore",&store);engine.rootContext()->setContextProperty("appSettings",&settings);engine.rootContext()->setContextProperty("telemetry",&telemetry);
        QQmlComponent component(&engine,QUrl("qrc:/centerDisplay/MapPanel.qml"));
        QScopedPointer<QObject> object(component.create());QVERIFY2(object,qPrintable(component.errorString()));
        auto *item=qobject_cast<QQuickItem*>(object.data());QVERIFY(item);
        QQuickWindow window;window.resize(1280,900);item->setParentItem(window.contentItem());item->setSize(QSizeF(1280,900));window.show();
        QTest::qWait(500);QVERIFY(QMetaObject::invokeMethod(object.data(),"fitExercise"));
        object->setProperty("selectedPlanId",store.plans().first().toMap()["id"]);
        QTest::qWait(800);
        const auto path=qEnvironmentVariable("RESCUE_SCREENSHOT");if(!path.isEmpty()) QVERIFY(window.grabWindow().save(path));
        auto *operations=object->findChild<QObject*>("rescuePanel");QVERIFY(operations);
        QVERIFY(QMetaObject::invokeMethod(operations,"openPlanner",Q_ARG(QVariant,QVariant(1))));
        QTest::qWait(250);
        if(!path.isEmpty()) QVERIFY(window.grabWindow().save(path+"-recon.png"));
        QVERIFY(QMetaObject::invokeMethod(operations,"openPlanner",Q_ARG(QVariant,QVariant(4))));
        QTest::qWait(250);
        if(!path.isEmpty()) QVERIFY(window.grabWindow().save(path+"-transport.png"));
    }
    void cleanupTestCase() {
        QSqlQuery q(db);QVERIFY(q.exec("DROP SCHEMA "+schema+" CASCADE"));db.close();db=QSqlDatabase();QSqlDatabase::removeDatabase("rescue-test-admin");qunsetenv("CENTER_DB_SCHEMA");
    }
};
QTEST_MAIN(RescueTests)
#include "rescue_tests.moc"
