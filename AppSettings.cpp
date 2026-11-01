#include "AppSettings.h"
#include <QStandardPaths>
#include <QCryptographicHash>

AppSettings::AppSettings(QObject *parent)
    : QObject(parent), m_settings(QSettings::IniFormat, QSettings::UserScope,
                                  "CenterDisplay", "CenterDisplay")
{
    const QStringList defaults {
        "rtsp://100.91.59.32:8554/main.264",
        "rtsp://100.87.186.117:8554/main.264",
        "rtsp://100.91.60.10:8554/main.264",
        "rtsp://100.91.60.11:8554/main.264",
        "rtsp://100.91.60.10:8554/main.264",
        "rtsp://100.91.60.11:8554/main.264"
    };
    m_urls = m_settings.value("cameras/urls", defaults).toStringList();
    while (m_urls.size() < 6) m_urls.append(QString());
    m_urls = m_urls.mid(0, 6);
    m_mode = m_settings.value("layout/mode", 1).toInt();
    if (m_mode == 9) m_mode = 6; // Migrate the former nine-camera layout.
    if (m_mode != 1 && m_mode != 4 && m_mode != 6) m_mode = 1;
}

bool AppSettings::save(const QStringList &urls, int mode)
{
    if (urls.size() != 6 || (mode != 1 && mode != 4 && mode != 6)) return false;
    m_settings.setValue("cameras/urls", urls);
    m_settings.setValue("layout/mode", mode);
    m_settings.sync();
    if (m_settings.status() != QSettings::NoError) return false;
    m_urls = urls;
    m_mode = mode;
    emit settingsChanged();
    return true;
}

QString AppSettings::mapUrl() const {
    switch(mapType()) {
    case 1: return "https://server.arcgisonline.com/ArcGIS/rest/services/World_Imagery/MapServer/tile/%z/%y/%x";
    case 2: return "https://server.arcgisonline.com/ArcGIS/rest/services/World_Topo_Map/MapServer/tile/%z/%y/%x";
    case 3: { auto url=customMapUrl(); return url.replace("{z}","%z").replace("{x}","%x").replace("{y}","%y"); }
    default: return "https://tile.openstreetmap.org/%z/%x/%y.png";
    }
}
bool AppSettings::saveMap(int type, const QString &url) {
    if(type<0 || type>3) return false;
    if(type==3 && (!url.startsWith("https://") || !url.contains("{z}") || !url.contains("{x}") || !url.contains("{y}"))) return false;
    m_settings.setValue("map/type",type); m_settings.setValue("map/customUrl",url);
    m_settings.sync();
    if(m_settings.status()!=QSettings::NoError) return false;
    emit settingsChanged(); return true;
}

QString AppSettings::mapCache() const {
    return QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/maps/" +
        QString::fromLatin1(QCryptographicHash::hash(mapUrl().toUtf8(),QCryptographicHash::Sha256).toHex());
}
