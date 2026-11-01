#include "TelemetryModel.h"
#include <QNetworkDatagram>
#include <QSettings>
#include <QtEndian>
#include <cmath>
#include <cstring>

TelemetryModel::TelemetryModel(QObject *parent):QAbstractListModel(parent) {
    m_clock.start();
    QSettings settings(QSettings::IniFormat,QSettings::UserScope,"CenterDisplay","CenterDisplay");
    for(int i=0;i<6;++i) {
        const QString key=QString("telemetry/%1/").arg(i);
        auto &v=m_vehicles[i];
        v.port=settings.value(key+"port",14550+i).toInt();
        v.source=settings.value(key+"source").toString();
        v.systemId=settings.value(key+"system",1).toInt();
        if(!configure(i,v.port,v.source,v.systemId) && v.error.isEmpty())
            v.error="Cài đặt kết nối đã lưu không hợp lệ";
    }
    connect(&m_timer,&QTimer::timeout,this,[this]{emit dataChanged(index(0),index(5),{Live,Age,Status,AttitudeLive,AttitudeAge});});
    m_timer.start(500);
}
QHash<int,QByteArray> TelemetryModel::roleNames() const {
    return {{Number,"number"},{Latitude,"latitude"},{Longitude,"longitude"},{Altitude,"altitude"},
        {Heading,"heading"},{HasPosition,"hasPosition"},{Live,"live"},{Age,"age"},{Port,"port"},
        {Source,"source"},{SystemId,"systemId"},{Status,"status"},{Listening,"listening"},{Roll,"roll"},{Pitch,"pitch"},{Yaw,"yaw"},
        {HasAttitude,"hasAttitude"},{AttitudeLive,"attitudeLive"},{AttitudeAge,"attitudeAge"},
        {AltitudeAMSL,"altitudeAMSL"},{GroundSpeed,"groundSpeed"},{ClimbRate,"climbRate"}};
}
QVariant TelemetryModel::data(const QModelIndex &idx,int role) const {
    if(!idx.isValid() || idx.row()<0 || idx.row()>=6) return {};
    const auto &v=m_vehicles[idx.row()];
    const qint64 age=v.received<0?-1:m_clock.elapsed()-v.received;
    const qint64 attitudeAge=v.attitudeReceived<0?-1:m_clock.elapsed()-v.attitudeReceived;
    switch(role) {
    case Roll:return v.roll; case Pitch:return v.pitch; case Yaw:return v.yaw;
    case HasAttitude:return v.attitudeReceived>=0;
    case AttitudeLive:return attitudeAge>=0 && attitudeAge<5000;
    case AttitudeAge:return attitudeAge<0?-1:int(attitudeAge/1000);
    case AltitudeAMSL:return v.altitudeAMSL; case GroundSpeed:return v.groundSpeed; case ClimbRate:return v.climbRate;
    case Number:return idx.row()+1; case Latitude:return v.latitude; case Longitude:return v.longitude;
    case Altitude:return v.altitude; case Heading:return v.heading; case HasPosition:return v.received>=0;
    case Live:return age>=0 && age<5000; case Age:return age<0?-1:int(age/1000);
    case Port:return v.port; case Source:return v.source; case SystemId:return v.systemId;
    case Listening:return v.socket && v.socket->state()==QAbstractSocket::BoundState;
    case Status:if(!v.enabled) return QString("Đã ngắt kết nối");
        return !v.error.isEmpty()?v.error:age<0?QString("Đang chờ vị trí"):age<5000?QString("Trực tiếp"):QString("Chưa cập nhật (%1 giây)").arg(age/1000);
    default:return {};
    }
}
QVariantMap TelemetryModel::configuration(int slot) const {
    if(slot<0 || slot>=6) return {};
    const auto &v=m_vehicles[slot];
    return {{"port",v.port},{"source",v.source},{"systemId",v.systemId}};
}
bool TelemetryModel::configure(int slot,int port,const QString &source,int systemId) {
    QHostAddress address;
    if(slot<0 || slot>=6 || port<1024 || port>65535 || systemId<1 || systemId>255 ||
       (!source.trimmed().isEmpty() && !address.setAddress(source.trimmed()))) return false;
    for(int i=0;i<6;++i) if(i!=slot && m_vehicles[i].port==port) return false;
    auto &v=m_vehicles[slot];
    QUdpSocket *socket=v.socket;
    if(v.enabled && (!socket || port!=v.port || socket->state()!=QAbstractSocket::BoundState)) {
        socket=new QUdpSocket(this);
        if(!socket->bind(QHostAddress::AnyIPv4,quint16(port),QUdpSocket::DontShareAddress)) {
            v.error="Không mở được cổng UDP. Kiểm tra cổng đang được sử dụng hoặc quyền truy cập."; delete socket;
            emit dataChanged(index(slot),index(slot)); return false;
        }
    }
    QSettings settings(QSettings::IniFormat,QSettings::UserScope,"CenterDisplay","CenterDisplay");
    const QString key=QString("telemetry/%1/").arg(slot);
    settings.setValue(key+"port",port); settings.setValue(key+"source",source.trimmed());
    settings.setValue(key+"system",systemId); settings.sync();
    if(settings.status()!=QSettings::NoError) { if(socket!=v.socket) delete socket; return false; }
    if(socket!=v.socket) {
        delete v.socket; v.socket=socket;
        connect(socket,&QUdpSocket::readyRead,this,[this,slot]{receive(slot);});
    }
    v.port=port; v.source=source.trimmed(); v.systemId=systemId; v.received=-1; v.attitudeReceived=-1; v.error.clear();
    emit dataChanged(index(slot),index(slot)); return true;
}
bool TelemetryModel::connectLocation(int slot) {
    if(slot<0 || slot>=6) return false;
    auto &v=m_vehicles[slot];
    if(v.socket && v.socket->state()==QAbstractSocket::BoundState) return true;
    v.enabled=true;
    return configure(slot,v.port,v.source,v.systemId);
}
void TelemetryModel::disconnectLocation(int slot) {
    if(slot<0 || slot>=6) return;
    auto &v=m_vehicles[slot];
    v.enabled=false;
    delete v.socket;
    v.socket=nullptr;
    v.received=-1; v.attitudeReceived=-1;
    v.error.clear();
    emit dataChanged(index(slot),index(slot));
}
void TelemetryModel::receive(int slot) {
    auto &v=m_vehicles[slot];
    while(v.socket && v.socket->hasPendingDatagrams()) {
        const auto packet=v.socket->receiveDatagram();
        if(v.source.isEmpty() || packet.senderAddress()==QHostAddress(v.source)) parse(slot,packet.data());
    }
}
static quint16 crcByte(quint16 crc,quint8 b) {
    quint8 t=b^quint8(crc); t^=t<<4;
    return (crc>>8)^(quint16(t)<<8)^(quint16(t)<<3)^(quint16(t)>>4);
}
void TelemetryModel::parse(int slot,const QByteArray &bytes) {
    auto &v=m_vehicles[slot];
    // UDP datagrams contain complete MAVLink frames; never combine different senders.
    for(int p=0;p<bytes.size();) {
        const auto *b=reinterpret_cast<const uchar*>(bytes.constData()+p);
        const bool v2=b[0]==0xfd;
        if(!v2 && b[0]!=0xfe) {++p;continue;}
        const int header=v2?10:6;
        if(bytes.size()-p<header) break;
        const int len=b[1], total=header+len+2+(v2 && (b[2]&1)?13:0);
        if(bytes.size()-p<total) {++p;continue;}
        const int msg=v2?(int(b[7])|(int(b[8])<<8)|(int(b[9])<<16)):b[5];
        // Signed frames are deliberately rejected: no signing-key verification here.
        if((v2 && b[2]!=0) || (msg!=33 && msg!=30) || b[v2?5:3]!=v.systemId || b[v2?6:4]!=1 ||
           len>28 || (!v2 && len!=28) || len==0) {p+=total;continue;}
        quint16 crc=0xffff;
        for(int i=1;i<header+len;++i) crc=crcByte(crc,b[i]);
        crc=crcByte(crc,msg==33?104:39); // GLOBAL_POSITION_INT / ATTITUDE CRC_EXTRA
        if(crc!=qFromLittleEndian<quint16>(b+header+len)) {++p;continue;}
        QByteArray payload(reinterpret_cast<const char*>(b+header),len); payload.resize(28,'\0');
        const auto *d=reinterpret_cast<const uchar*>(payload.constData());
        if(msg==30) {
            auto number=[d](int offset) {
                const quint32 bits=qFromLittleEndian<quint32>(d+offset);
                float value; std::memcpy(&value,&bits,sizeof(value)); return double(value);
            };
            const double roll=number(4), pitch=number(8), yaw=number(12);
            constexpr double pi=3.14159265358979323846;
            if(std::isfinite(roll) && std::isfinite(pitch) && std::isfinite(yaw)
                && std::abs(roll)<=pi+0.001 && std::abs(pitch)<=pi+0.001 && std::abs(yaw)<=pi+0.001) {
                v.roll=roll*180/pi; v.pitch=pitch*180/pi; v.yaw=std::fmod(yaw*180/pi+360,360);
                v.attitudeReceived=m_clock.elapsed();
                // Attitude alone never refreshes the position/map freshness timestamp.
                emit dataChanged(index(slot),index(slot),{Roll,Pitch,Yaw,HasAttitude,AttitudeLive,AttitudeAge});
            }
            p+=total; continue;
        }
        const double lat=qFromLittleEndian<qint32>(d+4)/1e7, lon=qFromLittleEndian<qint32>(d+8)/1e7;
        const auto heading=qFromLittleEndian<quint16>(d+26);
        if(std::abs(lat)<=90 && std::abs(lon)<=180 && (lat!=0 || lon!=0) && (heading==65535 || heading<36000)) {
            v.latitude=lat;v.longitude=lon;v.altitude=qFromLittleEndian<qint32>(d+16)/1000.0;
            v.altitudeAMSL=qFromLittleEndian<qint32>(d+12)/1000.0;
            v.groundSpeed=std::hypot(double(qFromLittleEndian<qint16>(d+20)),double(qFromLittleEndian<qint16>(d+22)))/100.0;
            v.climbRate=-qFromLittleEndian<qint16>(d+24)/100.0;
            v.heading=heading==65535?-1:heading/100.0;v.received=m_clock.elapsed();v.error.clear();
            emit dataChanged(index(slot),index(slot));
        }
        p+=total;
    }
}
