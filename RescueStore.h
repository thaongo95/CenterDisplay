#pragma once
#include <QObject>
#include <QSqlDatabase>
#include <QVariantList>
#include <QNetworkAccessManager>

// Center-PC PostgreSQL store for the fictional rescue exercise.
class RescueStore : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool simulation READ simulation CONSTANT)
    Q_PROPERTY(QVariantMap simulationStatus READ simulationStatus NOTIFY simulationChanged)
    Q_PROPERTY(int simulationSpeed READ simulationSpeed NOTIFY simulationChanged)
    Q_PROPERTY(bool simulationLive READ simulationLive NOTIFY simulationChanged)
    Q_PROPERTY(bool online READ online NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QVariantList sites READ sites NOTIFY changed)
    Q_PROPERTY(QVariantList areas READ areas NOTIFY changed)
    Q_PROPERTY(QVariantList plans READ plans NOTIFY changed)
    Q_PROPERTY(QVariantList records READ records NOTIFY changed)
public:
    explicit RescueStore(QObject *parent=nullptr);
    ~RescueStore() override;
    bool online() const { return m_db.isOpen(); }
    bool busy() const { return m_busy; }
    QString status() const { return m_status; }
    QVariantList sites() const { return m_sites; }
    QVariantList areas() const { return m_areas; }
    QVariantList plans() const { return m_plans; }
    QVariantList records() const { return m_records; }
    bool simulation() const { return !m_demoDirectory.isEmpty(); }
    QVariantMap simulationStatus() const { return m_simulationStatus; }
    int simulationSpeed() const { return m_simulationSpeed; }
    bool simulationLive() const { return m_simulationLive; }
    Q_INVOKABLE bool setSimulationSpeed(int speed);
    Q_INVOKABLE bool open();
    Q_INVOKABLE bool refresh();
    Q_INVOKABLE bool report(int siteId, int revision, int reconId, int priority, const QString &note, bool resolved);
    Q_INVOKABLE bool cancelPlan(int planId);
    Q_INVOKABLE bool draftPlan(int uavId);
    Q_INVOKABLE bool createPlan(int uavId, int areaId, double altitude, double spacing, double angle, const QVariantList &siteIds);
private:
    bool buildPlan(int uavId, int areaId, double altitude, double spacing, double angle, const QVariantList &siteIds, bool manual);
public:
    Q_INVOKABLE bool sendDemoPlan(int planId) { return sendPlan(planId,true); }
    Q_INVOKABLE bool sendToGroundStation(int planId) { return sendPlan(planId,false); }
    Q_INVOKABLE bool configureGroundStation(int uavId, const QString &host, const QString &token);
    Q_INVOKABLE QVariantMap groundStation(int uavId) const;
signals:
    void changed();
    void simulationChanged();
private:
    QString m_demoDirectory;
    QVariantMap m_simulationStatus;
    int m_simulationSpeed=1;
    bool m_simulationLive=false;
    QSqlDatabase m_db;
    QString m_connection, m_status="Chưa kết nối cơ sở dữ liệu cứu trợ";
    QVariantList m_sites,m_areas,m_plans,m_records;
    QNetworkAccessManager m_network;
    bool m_busy=false;
    bool sendPlan(int planId, bool demo);
    bool fail(const QString &message);
};
