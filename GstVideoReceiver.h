#pragma once

#include <QObject>
#include <QPointer>
#include <QThread>
#include "VideoItem.h"

class ReceiverWorker;

// GUI-thread facade. All pipeline operations and timers live on the worker.
class GstVideoReceiver : public QObject
{
    Q_OBJECT
    Q_DISABLE_COPY(GstVideoReceiver)
    Q_PROPERTY(bool connected READ connected NOTIFY connectedChanged)
    Q_PROPERTY(State state READ state NOTIFY stateChanged)
public:
    enum State { Stopped, Connecting, Playing, Reconnecting };
    Q_ENUM(State)
    explicit GstVideoReceiver(QObject *parent = nullptr);
    ~GstVideoReceiver() override;

    // Validates/configures a URL. Pipeline failures are reported asynchronously.
    Q_INVOKABLE bool connectCamera(const QString &url);
    Q_INVOKABLE void start();
    Q_INVOKABLE void stop();
    Q_INVOKABLE void setVideoItem(VideoItem *item);
    bool connected() const { return m_state == Playing; }
    State state() const { return m_state; }

signals:
    void connectedChanged();
    void stateChanged();
    void errorOccurred(const QString &message);
    void frameReceived();

private:
    void setState(State state);
    QThread m_thread;
    ReceiverWorker *m_worker = nullptr;
    QPointer<VideoItem> m_videoItem;
    QString m_url;
    quint64 m_request = 0;
    bool m_requested = false;
    State m_state = Stopped;
};
