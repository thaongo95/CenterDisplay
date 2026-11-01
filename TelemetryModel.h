#pragma once
#include <QAbstractListModel>
#include <QUdpSocket>
#include <QElapsedTimer>
#include <QTimer>
#include <array>

// Receive-only, one dedicated UDP port per camera/UAV slot.
class TelemetryModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Role { Number = Qt::UserRole+1, Latitude, Longitude, Altitude, Heading,
                HasPosition, Live, Age, Port, Source, SystemId, Status, Listening,
                Roll, Pitch, Yaw, HasAttitude, AttitudeLive, AttitudeAge, AltitudeAMSL, GroundSpeed, ClimbRate };
    explicit TelemetryModel(QObject *parent=nullptr);
    int rowCount(const QModelIndex &parent={}) const override { return parent.isValid()?0:6; }
    QVariant data(const QModelIndex &, int) const override;
    QHash<int,QByteArray> roleNames() const override;
    Q_INVOKABLE bool configure(int slot, int port, const QString &source, int systemId);
    Q_INVOKABLE bool connectLocation(int slot);
    Q_INVOKABLE void disconnectLocation(int slot);
    Q_INVOKABLE QVariantMap configuration(int slot) const;
private:
    struct Vehicle {
        QUdpSocket *socket=nullptr;
        bool enabled=true;
        int port=0, systemId=1;
        QString source, error;
        double latitude=0, longitude=0, altitude=0, heading=-1;
        double roll=0, pitch=0, yaw=0, altitudeAMSL=0, groundSpeed=0, climbRate=0;
        qint64 received=-1, attitudeReceived=-1;
    };
    std::array<Vehicle,6> m_vehicles;
    QElapsedTimer m_clock;
    QTimer m_timer;
    void receive(int slot);
    void parse(int slot, const QByteArray &bytes);
};
