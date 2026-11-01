#pragma once
#include <QObject>
#include <QSettings>
#include <QStringList>

class AppSettings final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QStringList cameraUrls READ cameraUrls NOTIFY settingsChanged)
    Q_PROPERTY(int layoutMode READ layoutMode NOTIFY settingsChanged)
    Q_PROPERTY(QString mapCache READ mapCache NOTIFY settingsChanged)
    Q_PROPERTY(int mapType READ mapType NOTIFY settingsChanged)
    Q_PROPERTY(QString mapUrl READ mapUrl NOTIFY settingsChanged)
    Q_PROPERTY(QString customMapUrl READ customMapUrl NOTIFY settingsChanged)
public:
    int mapType() const { return m_settings.value("map/type", 0).toInt(); }
    QString customMapUrl() const { return m_settings.value("map/customUrl").toString(); }
    QString mapUrl() const;
    QString mapCache() const;
    Q_INVOKABLE bool saveMap(int type, const QString &url);
    explicit AppSettings(QObject *parent = nullptr);
    QStringList cameraUrls() const { return m_urls; }
    int layoutMode() const { return m_mode; }
    Q_INVOKABLE bool save(const QStringList &urls, int mode);
signals:
    void settingsChanged();
private:
    QSettings m_settings;
    QStringList m_urls;
    int m_mode = 1;
};
