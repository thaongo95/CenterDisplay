#include "CenterPlanInbox.h"
#include <QTcpSocket>
#include <QTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QStandardPaths>
#include <QSaveFile>
#include <QFile>
#include <QDir>
#include <QUuid>
#include <cmath>

CenterPlanInbox::CenterPlanInbox(QObject *parent):QObject(parent) {
    _server.setMaxPendingConnections(4);
    connect(&_server,&QTcpServer::newConnection,this,&CenterPlanInbox::accept);
}
bool CenterPlanInbox::start(const QString &bindAddress,int port,int fleetSlot,const QString &token) {
    QHostAddress address(bindAddress);
    const bool trusted=address.isLoopback() || address.isInSubnet(QHostAddress("100.64.0.0"),10);
    if(!trusted || address.protocol()!=QAbstractSocket::IPv4Protocol || port<1024 || port>65535 || fleetSlot<1 || fleetSlot>6 || token.size()<24 || token.contains('\r') || token.contains('\n')) {
        _status="Use a Tailscale IPv4 or localhost, port 1024–65535, slot 1–6 and a token of at least 24 characters.";emit changed();return false;
    }
    stop();_latestPlan.clear();_slot=fleetSlot;_token=token;
    const bool ok=_server.listen(address,quint16(port));
    _status=ok?QString("Listening for center route drafts on %1:%2 · fleet slot %3").arg(bindAddress).arg(port).arg(fleetSlot):_server.errorString();emit changed();return ok;
}
void CenterPlanInbox::stop() {
    _server.close();
    for(auto *socket:findChildren<QTcpSocket*>()) socket->abort();
    _token.clear();_status="Center plan inbox stopped";emit changed();
}
void CenterPlanInbox::accept() {
    while(_server.hasPendingConnections()) {
        auto *socket=_server.nextPendingConnection();socket->setParent(this);
        if(findChildren<QTcpSocket*>().size()>8) {socket->abort();socket->deleteLater();continue;}
        socket->setReadBufferSize(270336);
        auto *timer=new QTimer(socket);timer->setSingleShot(true);timer->start(7000);
        connect(timer,&QTimer::timeout,socket,&QTcpSocket::abort);
        connect(socket,&QTcpSocket::disconnected,socket,&QObject::deleteLater);
        connect(socket,&QTcpSocket::readyRead,this,[this,socket]{
            if(socket->property("done").toBool()) return;
            QByteArray buffer=socket->property("buffer").toByteArray()+socket->readAll();
            auto respond=[socket](int code,const QJsonObject &object){
                socket->setProperty("done",true);
                const auto body=QJsonDocument(object).toJson(QJsonDocument::Compact);
                socket->write("HTTP/1.1 "+QByteArray::number(code)+" Result\r\nContent-Type: application/json\r\nConnection: close\r\nContent-Length: "+QByteArray::number(body.size())+"\r\n\r\n"+body);
                socket->disconnectFromHost();
            };
            if(buffer.size()>262144) {respond(413,{{"error","Request too large"}});return;}
            const int boundary=buffer.indexOf("\r\n\r\n");
            if(boundary<0) {if(buffer.size()>8192) respond(400,{{"error","Header too large"}});else socket->setProperty("buffer",buffer);return;}
            const auto headers=buffer.left(boundary).split('\n');
            if(headers.isEmpty() || headers.first().trimmed()!="POST /plans HTTP/1.1") {respond(404,{{"error","Unknown endpoint"}});return;}
            int length=-1;QByteArray authorization;
            for(auto header:headers) {
                header=header.trimmed();const auto lower=header.toLower();
                if(lower.startsWith("content-length:")) {if(length!=-1) {respond(400,{{"error","Duplicate length"}});return;}bool ok=false;length=header.mid(15).trimmed().toInt(&ok);if(!ok) length=-1;}
                if(lower.startsWith("authorization:")) authorization=header.mid(14).trimmed();
                if(lower.startsWith("transfer-encoding:")) {respond(400,{{"error","Chunked requests not supported"}});return;}
            }
            if(authorization!="Bearer "+_token.toUtf8() || !_server.isListening()) {respond(401,{{"error","Unauthorized"}});return;}
            if(length<=0 || length>250000) {respond(400,{{"error","Invalid length"}});return;}
            if(buffer.size()<boundary+4+length) {socket->setProperty("buffer",buffer);return;}
            const auto message=QJsonDocument::fromJson(buffer.mid(boundary+4,length)).object();
            const auto missionId=message["missionId"].toString();
            const QUuid uuid(missionId);
            const auto points=message["waypoints"].toArray();
            if(message["schema"].toString()!="center-rescue-plan/v1" || message["uavId"].toInt()!=_slot || message["planId"].toInt()<=0 || uuid.isNull() || uuid.toString(QUuid::WithoutBraces)!=missionId || points.size()<2 || points.size()>100) {respond(400,{{"error","Wrong fleet slot or invalid plan"}});return;}
            QJsonArray items;int sequence=1;
            for(const auto &value:points) {
                const auto point=value.toObject();
                const double lat=point["latitude"].toDouble(NAN),lon=point["longitude"].toDouble(NAN),alt=point["relativeAltitudeM"].toDouble(NAN);
                if(!std::isfinite(lat)||!std::isfinite(lon)||!std::isfinite(alt)||std::abs(lat)>85||std::abs(lon)>180||alt<10||alt>120) {respond(400,{{"error","Invalid waypoint or demo altitude outside 10–120 m"}});return;}
                items.append(QJsonObject{{"type","SimpleItem"},{"command",16},{"frame",3},{"autoContinue",true},{"doJumpId",sequence++},{"params",QJsonArray{0,0,0,QJsonValue(),lat,lon,alt}},{"Altitude",alt},{"AltitudeMode",1},{"AMSLAltAboveTerrain",QJsonValue()}});
            }
            const auto first=points.first().toObject();
            QJsonObject plan{{"fileType","Plan"},{"version",1},{"groundStation","QGroundControl"},
                {"geoFence",QJsonObject{{"version",2},{"circles",QJsonArray()},{"polygons",QJsonArray()}}},
                {"rallyPoints",QJsonObject{{"version",2},{"points",QJsonArray()}}},
                {"mission",QJsonObject{{"version",2},{"firmwareType",3},{"vehicleType",2},{"cruiseSpeed",15},{"hoverSpeed",5},{"globalPlanAltitudeMode",1},{"plannedHomePosition",QJsonArray{first["latitude"],first["longitude"],0}},{"items",items}}}};
            const auto directory=QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)+"/CenterPlanInbox";
            if(!QDir().mkpath(directory)) {respond(500,{{"error","Cannot create inbox"}});return;}
            const auto filename=directory+"/"+missionId+".plan";
            const auto data=QJsonDocument(plan).toJson();
            if(QFile::exists(filename)) {
                QFile file(filename);if(!file.open(QIODevice::ReadOnly)||file.readAll()!=data) {respond(409,{{"error","Mission ID conflicts with existing route"}});return;}
            } else {
                QSaveFile file(filename);if(!file.open(QIODevice::WriteOnly)||file.write(data)!=data.size()||!file.commit()) {respond(500,{{"error","Cannot save plan"}});return;}
            }
            _latestPlan=filename;_status=QString("Center plan #%1 received for review · fleet slot %2. Not uploaded or started.").arg(message["planId"].toInt()).arg(_slot);emit changed();
            respond(200,{{"status","received_for_review"},{"planId",message["planId"]},{"uavId",_slot},{"missionId",missionId}});
        });
    }
}
