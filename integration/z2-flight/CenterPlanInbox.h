#pragma once
#include <QObject>
#include <QTcpServer>

// Receives route drafts on a trusted Tailscale/loopback interface.
// It saves plans for review and never uploads, arms or starts a vehicle.
class CenterPlanInbox : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QString latestPlan READ latestPlan NOTIFY changed)
    Q_PROPERTY(bool listening READ listening NOTIFY changed)
public:
    explicit CenterPlanInbox(QObject *parent=nullptr);
    QString status() const { return _status; }
    QString latestPlan() const { return _latestPlan; }
    bool listening() const { return _server.isListening(); }
    Q_INVOKABLE bool start(const QString &bindAddress, int port, int fleetSlot, const QString &token);
    Q_INVOKABLE void stop();
signals:
    void changed();
private:
    QTcpServer _server;
    QString _token, _status="Center plan inbox stopped", _latestPlan;
    int _slot=0;
    void accept();
};
