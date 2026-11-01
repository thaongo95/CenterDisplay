#include "RescueStore.h"
#include "SurveyRoute.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QSqlRecord>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUuid>
#include <QTimer>
#include <QRegularExpression>
#include <QSettings>
#include <QHostAddress>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QDateTime>

static QString env(const char *key,const QString &fallback) { return qEnvironmentVariable(key,fallback); }
static QString vietnameseDisplay(const QString &text) {
    static const QHash<QString,QString> translations {
        {"Riverside school","Trường học ven sông"},
        {"North clinic","Trạm y tế phía Bắc"},
        {"Canal homes","Khu dân cư ven kênh"},
        {"East shelter","Điểm trú ẩn phía Đông"},
        {"Bridge checkpoint","Chốt kiểm soát cầu"},
        {"Island settlement","Khu dân cư trên cù lao"},
        {"South warehouse","Kho phía Nam"},
        {"West apartments","Chung cư phía Tây"},
        {"Community hall","Nhà sinh hoạt cộng đồng"},
        {"Evacuation pickup","Điểm tập kết sơ tán"},
        {"Drinking water and first-aid kits","Nước uống và bộ sơ cứu"},
        {"Medical supplies","Vật tư y tế"},
        {"Food packages","Các phần thực phẩm"},
        {"Blankets and water","Chăn và nước uống"},
        {"Reconnaissance and situation check","Trinh sát và kiểm tra tình hình"},
        {"Water and emergency supplies","Nước uống và vật tư khẩn cấp"},
        {"Assess access route","Đánh giá tuyến tiếp cận"},
        {"Food and hygiene kits","Thực phẩm và bộ vệ sinh"},
        {"Monitor water level","Theo dõi mực nước"},
        {"First-aid supplies for waiting evacuees","Vật tư sơ cứu cho người chờ sơ tán"},
        {"Canal flood zone","Vùng ngập ven kênh"},
        {"South river overflow","Vùng ngập do tràn sông phía Nam"},
        {"Severe","Nghiêm trọng"},
        {"Moderate","Trung bình"},
        {"Fictional rescue exercise: 10 locations, 2 flood polygons, 3 recon and 3 transport UAVs.","Tình huống cứu trợ giả lập: 10 địa điểm, 2 vùng ngập, 3 UAV trinh sát và 3 UAV vận tải."},
        {"DEMO draft: straight segments, 80 m relative to home; terrain, obstacles, payload, airspace and landing sites are not validated.","Dự thảo MÔ PHỎNG: các đoạn thẳng, cao 80 m so với điểm nhà; chưa kiểm tra địa hình, vật cản, tải trọng, vùng trời và vị trí hạ cánh."},
        {"Mock GCS acknowledged receipt. No aircraft upload, arming or flight occurred.","Trạm mô phỏng đã xác nhận nhận kế hoạch. Chưa tải lên UAV, kích hoạt động cơ hoặc thực hiện chuyến bay."},
        {"Z2-Flight saved the plan for review. The GCS operator must review, upload and start using the existing flight controls.","Z2-Flight đã lưu kế hoạch để kiểm tra. Người điều khiển cần kiểm tra, tải lên UAV và bắt đầu bằng các chức năng điều khiển hiện có."},
        {"No valid acknowledgement. Delivery is unconfirmed; check the GCS and retry this same plan ID.","Chưa có xác nhận hợp lệ. Chưa xác nhận giao nhận; kiểm tra trạm và gửi lại cùng mã kế hoạch."}
    };
    return translations.value(text,text);
}
static QVariantList rows(QSqlQuery &q) {
    QVariantList result;
    while(q.next()) {
        QVariantMap row;
        for(int i=0;i<q.record().count();++i) {
            const auto key=q.record().fieldName(i);
            row[key]=QStringList{"name","need","severity","note","detail"}.contains(key) ? QVariant(vietnameseDisplay(q.value(i).toString())) : q.value(i);
        }
        result.append(row);
    }
    return result;
}
RescueStore::RescueStore(QObject *parent):QObject(parent) {
    m_demoDirectory=qEnvironmentVariable("CENTER_DEMO_MISSIONS_DIR");
    if(simulation()) {
        auto *timer=new QTimer(this);
        connect(timer,&QTimer::timeout,this,[this]{
            QFile file(m_demoDirectory+"/status.json");
            const bool live=QFileInfo(file).lastModified().msecsTo(QDateTime::currentDateTime())<3000 && file.exists();
            if(file.open(QIODevice::ReadOnly)) {
                const auto document=QJsonDocument::fromJson(file.readAll());
                if(document.isObject()) m_simulationStatus=document.object().toVariantMap();
            }
            m_simulationLive=live;
            emit simulationChanged();
        });
        timer->start(250);
    }
    m_connection="rescue-"+QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_db=QSqlDatabase::addDatabase("QPSQL",m_connection);
}
RescueStore::~RescueStore() { m_db.close(); m_db=QSqlDatabase(); QSqlDatabase::removeDatabase(m_connection); }
bool RescueStore::setSimulationSpeed(int speed) {
    if(!simulation() || !QList<int>{0,1,2,5,10,20}.contains(speed)) return false;
    QSaveFile file(m_demoDirectory+"/control.json");
    if(!file.open(QIODevice::WriteOnly)) return fail("Không thể đổi tốc độ mô phỏng.");
    const auto data=QJsonDocument(QJsonObject{{"speed",speed}}).toJson();
    if(file.write(data)!=data.size() || !file.commit()) return fail("Không thể lưu tốc độ mô phỏng.");
    m_simulationSpeed=speed;emit simulationChanged();return true;
}
bool RescueStore::fail(const QString &message) { m_status=message; emit changed(); return false; }
bool RescueStore::open() {
    if(m_busy) return false;
    m_db.setHostName(env("CENTER_DB_HOST","127.0.0.1"));
    m_db.setPort(env("CENTER_DB_PORT","55432").toInt());
    m_db.setDatabaseName(env("CENTER_DB_NAME","center_rescue"));
    m_db.setUserName(env("CENTER_DB_USER","center_demo"));
    m_db.setPassword(env("CENTER_DB_PASSWORD","center_demo_local"));
    const QString schema=env("CENTER_DB_SCHEMA","public");
    if(!QRegularExpression("^[a-z_][a-z0-9_]*$").match(schema).hasMatch()) return fail("Tên lược đồ cơ sở dữ liệu không hợp lệ.");
    m_db.setConnectOptions("connect_timeout=2;options='-c statement_timeout=2000 -c lock_timeout=1500 -c search_path="+schema+"'");
    if(!m_db.open()) return fail("Không kết nối được PostgreSQL. Khởi động CSDL cứu trợ và kiểm tra cấu hình CENTER_DB_*.");
    return refresh();
}
bool RescueStore::refresh() {
    if(!online()) return fail("PostgreSQL chưa kết nối. Khởi động CSDL cứu trợ rồi nhấn Kết nối CSDL.");
    QSqlQuery q(m_db);
    if(!q.exec("SELECT * FROM rescue_site ORDER BY priority DESC,id")) return fail("Không đọc được danh sách điểm cứu trợ.");
    auto sites=rows(q);
    if(!q.exec("SELECT id,name,severity,path::text FROM rescue_area ORDER BY id")) return fail("Không thể truy vấn cơ sở dữ liệu cứu trợ.");
    auto areas=rows(q);
    for(auto &v:areas) { auto r=v.toMap();r["path"]=QJsonDocument::fromJson(r["path"].toByteArray()).array().toVariantList();v=r; }
    if(!q.exec("SELECT id,uav_id,purpose,state,waypoints::text,note,created_at::text FROM rescue_plan ORDER BY id DESC LIMIT 60")) return fail("Không thể truy vấn cơ sở dữ liệu cứu trợ.");
    auto plans=rows(q);
    for(auto &v:plans) { auto r=v.toMap();r["waypoints"]=QJsonDocument::fromJson(r["waypoints"].toByteArray()).array().toVariantList();v=r; }
    if(!q.exec("SELECT id,site_id,plan_id,uav_id,event,detail,created_at::text FROM rescue_record ORDER BY id DESC LIMIT 100")) return fail("Không thể truy vấn cơ sở dữ liệu cứu trợ.");
    const auto records=rows(q);
    if(m_records==records && m_sites==sites && m_areas==areas && m_plans==plans) return true;
    m_records=records;m_sites=sites;m_areas=areas;m_plans=plans;
    m_status="Đã kết nối PostgreSQL · tình huống cứu trợ giả lập";emit changed();return true;
}
bool RescueStore::report(int siteId,int revision,int reconId,int priority,const QString &note,bool resolved) {
    if(!online() || m_busy) return false;
    if(reconId<1 || reconId>3 || priority<1 || priority>3 || note.trimmed().isEmpty() || note.size()>2000) return fail("Chọn UAV trinh sát, mức ưu tiên và nhập ghi chú (tối đa 2000 ký tự).");
    if(simulation() && resolved) return fail("Trong mô phỏng, UAV trinh sát phải xác nhận sau khi giao hàng để điểm chuyển xanh.");
    if(!m_db.transaction()) return fail("Không thể bắt đầu giao dịch cơ sở dữ liệu.");
    QSqlQuery q(m_db);
    q.prepare("UPDATE rescue_site SET priority=:priority,state=CASE WHEN state='delivered' AND :state='open' THEN state ELSE :state END,revision=revision+1,updated_at=now() WHERE id=:id AND revision=:revision AND recon_uav=:uav");
    q.bindValue(":priority",priority);q.bindValue(":state",resolved?"resolved":"open");q.bindValue(":id",siteId);q.bindValue(":revision",revision);q.bindValue(":uav",reconId);
    if(!q.exec() || q.numRowsAffected()!=1) {m_db.rollback();return fail("Chưa lưu báo cáo: dữ liệu đã thay đổi hoặc UAV chưa được giao điểm này. Hãy làm mới và thử lại.");}
    q.prepare("INSERT INTO rescue_record(site_id,uav_id,event,detail) VALUES(:site,:uav,'recon_report',:detail)");
    q.bindValue(":site",siteId);q.bindValue(":uav",reconId);q.bindValue(":detail",QString("Mức ưu tiên %1; %2. %3").arg(priority).arg(resolved?"đã xử lý":"chưa xử lý",note.trimmed()));
    if(!q.exec() || !m_db.commit()) {m_db.rollback();return fail("Không thể lưu báo cáo vào cơ sở dữ liệu.");}
    return refresh();
}
bool RescueStore::cancelPlan(int planId) {
    if(!online() || m_busy) return false;
    if(!m_db.transaction()) return fail("Không thể bắt đầu hủy kế hoạch.");
    QSqlQuery q(m_db);
    if(!q.exec("SELECT pg_advisory_xact_lock(619426)")) {m_db.rollback();return fail("Không thể khóa kế hoạch.");}
    q.prepare("UPDATE rescue_plan SET state='cancelled' WHERE id=:id AND state='draft' RETURNING uav_id");
    q.bindValue(":id",planId);
    if(!q.exec() || !q.next()) {
        m_db.rollback();return fail("Chỉ hủy được dự thảo chưa gửi. Kế hoạch đã gửi hoặc chưa rõ giao nhận cần đối chiếu tại trạm.");
    }
    const int uav=q.value(0).toInt();
    q.prepare("INSERT INTO rescue_record(plan_id,uav_id,event,detail) VALUES(:id,:uav,'plan_cancelled','Người điều hành hủy dự thảo chưa gửi; giải phóng các điểm đã phân công.')");
    q.bindValue(":id",planId);q.bindValue(":uav",uav);
    if(!q.exec() || !m_db.commit()) {m_db.rollback();return fail("Không thể lưu thao tác hủy kế hoạch.");}
    refresh();m_status="Đã hủy dự thảo và giải phóng các điểm cứu trợ.";emit changed();return true;
}
bool RescueStore::draftPlan(int uavId) { return buildPlan(uavId,uavId,80,150,0,{},false); }
bool RescueStore::createPlan(int uavId,int areaId,double altitude,double spacing,double angle,const QVariantList &siteIds) {
    return buildPlan(uavId,areaId,altitude,spacing,angle,siteIds,true);
}
bool RescueStore::buildPlan(int uavId,int areaId,double altitude,double spacing,double angle,const QVariantList &siteIds,bool manual) {
    if(!std::isfinite(altitude) || altitude<10 || altitude>120) return fail("Cao độ phải từ 10 đến 120 m so với căn cứ.");
    if(manual && uavId>3 && (siteIds.isEmpty() || siteIds.size()>4)) return fail("Chọn 1–4 điểm cứu trợ theo thứ tự bay.");
    if(!online() || m_busy || uavId<1 || uavId>6) return false;
    if(simulation() && uavId>=4) {
        const auto vehicles=m_simulationStatus.value("uavs").toList();
        if(!m_simulationLive || vehicles.size()!=6) return fail("Chờ trạng thái UAV mô phỏng trước khi lập kế hoạch.");
        const auto vehicle=vehicles[uavId-1].toMap();
        if(vehicle.value("mission_active").toBool()) return fail("UAV đang thực hiện tuyến; chờ UAV về căn cứ.");
        if(vehicle.value("payload").toDouble()<5) return fail("UAV đã hết hàng. Chọn UAV còn hàng hoặc mở phiên mô phỏng mới.");
    }
    if(!m_db.transaction()) return fail("Không thể bắt đầu giao dịch cơ sở dữ liệu.");
    QSqlQuery q(m_db);
    const bool recon=uavId<=3;
    if(!q.exec("SELECT pg_advisory_xact_lock(619426)")) {m_db.rollback();return fail("Không thể truy vấn cơ sở dữ liệu cứu trợ.");}
    q.prepare("UPDATE rescue_plan SET state='cancelled' WHERE uav_id=:uav AND state IN ('draft','failed')");q.bindValue(":uav",uavId);
    if(!q.exec()) {m_db.rollback();return fail("Không thể truy vấn cơ sở dữ liệu cứu trợ.");}
    int transportLimit=3;
    if(!recon) {
        // Divide currently discovered, unassigned locations among available transports.
        if(!q.exec("SELECT (SELECT count(*) FROM rescue_site s WHERE state='open' AND NOT EXISTS(SELECT 1 FROM rescue_plan p WHERE p.purpose='transport' AND p.state IN ('draft','sending','demo_received','received_for_review') AND p.site_ids @> jsonb_build_array(s.id))), (SELECT count(*) FROM rescue_uav u WHERE role='transport' AND NOT EXISTS(SELECT 1 FROM rescue_plan p WHERE p.uav_id=u.id AND p.state IN ('draft','sending','demo_received','received_for_review')))") || !q.next()) {
            m_db.rollback();return fail("Không thể phân chia các điểm cứu trợ.");
        }
        const int remaining=q.value(0).toInt(), available=qMax(1,q.value(1).toInt());
        transportLimit=qBound(1,(remaining+available-1)/available,4);
        if(simulation()) transportLimit=qMin(transportLimit,int(m_simulationStatus.value("uavs").toList()[uavId-1].toMap().value("payload").toDouble()/5));
    }
    // Lock the selected snapshot until both plan and history have been recorded.
    q.prepare(recon?"SELECT * FROM rescue_site WHERE state='open' AND recon_uav=:uav ORDER BY priority DESC,id FOR SHARE":"SELECT * FROM rescue_site s WHERE state='open' AND NOT EXISTS(SELECT 1 FROM rescue_plan p WHERE p.purpose='transport' AND p.state IN ('draft','sending','demo_received','received_for_review') AND p.site_ids @> jsonb_build_array(s.id)) ORDER BY priority DESC,id LIMIT :limit FOR SHARE");
    if(recon) q.bindValue(":uav",uavId); else q.bindValue(":limit",transportLimit);
    if(!q.exec()) {m_db.rollback();return fail("Không thể truy vấn cơ sở dữ liệu cứu trợ.");}
    auto selected=rows(q);
    if(manual && !recon) {
        // Fetch every eligible site, then preserve the operator's explicit order.
        if(!q.exec("SELECT * FROM rescue_site s WHERE state='open' AND NOT EXISTS(SELECT 1 FROM rescue_plan p WHERE p.purpose='transport' AND p.state IN ('draft','sending','demo_received','received_for_review') AND p.site_ids @> jsonb_build_array(s.id)) FOR SHARE")) {m_db.rollback();return fail("Không đọc được điểm cứu trợ.");}
        const auto available=rows(q);selected.clear();QSet<int> seen;
        for(const auto &idValue:siteIds) {
            int id=idValue.toInt();bool found=false;
            for(const auto &row:available) if(row.toMap()["id"].toInt()==id && !seen.contains(id)) {selected.append(row);found=true;seen.insert(id);break;}
            if(!found){m_db.rollback();return fail("Điểm đã được giao, đã xử lý hoặc bị chọn trùng. Làm mới danh sách.");}
        }
        if(simulation() && selected.size()*5>m_simulationStatus.value("uavs").toList()[uavId-1].toMap()["payload"].toDouble()) {m_db.rollback();return fail("Tuyến vượt tải hàng còn lại.");}
    }
    if(!recon && selected.isEmpty()) {m_db.rollback();return fail("Không còn địa điểm chưa xử lý phù hợp với kế hoạch này.");}
    QJsonArray points, ids;QJsonObject revisions;
    auto waypoint=[altitude](double lat,double lon,const QString &action,int site){return QJsonObject{{"latitude",lat},{"longitude",lon},{"relativeAltitudeM",altitude},{"action",action},{"siteId",site}};};
    points.append(waypoint(10.790,106.695,"base",0));
    QString surveyNote;
    if(recon) {
        q.prepare("SELECT name,path::text FROM rescue_area WHERE id=:area FOR SHARE");q.bindValue(":area",areaId);
        if(!q.exec() || !q.next()){m_db.rollback();return fail("Chọn vùng khảo sát từ PostgreSQL.");}
        const auto boundary=QJsonDocument::fromJson(q.value(1).toByteArray()).array();QString error;
        const auto route=surveyRoute(boundary,spacing,angle,altitude,error);
        if(route.isEmpty()){m_db.rollback();return fail(error);}
        surveyNote=QString("Khảo sát %1 · dải %2 m · góc %3° · ").arg(q.value(0).toString()).arg(spacing).arg(angle);
        for(const auto &point:route) points.append(point);
        selected.clear(); // Survey geometry does not depend on discovered site revisions.
    }
    for(const auto &value:selected) {
        const auto row=value.toMap();const int id=row["id"].toInt();ids.append(id);revisions[QString::number(id)]=row["revision"].toInt();
        points.append(waypoint(row["latitude"].toDouble(),row["longitude"].toDouble(),recon?"survey":"supply_review",id));
    }
    points.append(waypoint(10.790,106.695,"return_to_base",0));
    double routeMeters=0;
    for(int i=1;i<points.size();++i) {
        const auto a=points[i-1].toObject(),b=points[i].toObject();
        routeMeters+=QGeoCoordinate(a["latitude"].toDouble(),a["longitude"].toDouble()).distanceTo(QGeoCoordinate(b["latitude"].toDouble(),b["longitude"].toDouble()));
    }
    const QString routeSummary=QString("%1 km · khoảng %2 phút ở 25 m/s. ").arg(routeMeters/1000.,0,'f',1).arg(routeMeters/1500.,0,'f',1);
    const QString note=surveyNote+routeSummary+QString("Cao %1 m so với căn cứ. Tuyến dự thảo; đoạn nối có thể ra ngoài vùng lõm. Cần kiểm tra địa hình, vật cản và vùng trời trước khi bay.").arg(altitude);
    q.prepare("INSERT INTO rescue_plan(uav_id,purpose,waypoints,site_ids,site_revisions,note) VALUES(:uav,:purpose,CAST(:points AS jsonb),CAST(:sites AS jsonb),CAST(:revisions AS jsonb),:note) RETURNING id");
    q.bindValue(":uav",uavId);q.bindValue(":purpose",recon?"recon":"transport");q.bindValue(":points",QString::fromUtf8(QJsonDocument(points).toJson(QJsonDocument::Compact)));q.bindValue(":sites",QString::fromUtf8(QJsonDocument(ids).toJson(QJsonDocument::Compact)));q.bindValue(":revisions",QString::fromUtf8(QJsonDocument(revisions).toJson(QJsonDocument::Compact)));q.bindValue(":note",note);
    if(!q.exec() || !q.next()) {m_db.rollback();return fail("Không thể truy vấn cơ sở dữ liệu cứu trợ.");}
    const auto planId=q.value(0);
    q.prepare("INSERT INTO rescue_record(plan_id,uav_id,event,detail) VALUES(:plan,:uav,'plan_drafted',:detail)");q.bindValue(":plan",planId);q.bindValue(":uav",uavId);q.bindValue(":detail",note);
    if(!q.exec() || !m_db.commit()) {m_db.rollback();return fail("Không thể lưu kế hoạch vào cơ sở dữ liệu.");}
    return refresh();
}
QVariantMap RescueStore::groundStation(int uavId) const {
    QSettings settings(QSettings::IniFormat,QSettings::UserScope,"CenterDisplay","CenterDisplay");
    const QString key=QString("groundStation/%1/").arg(uavId);
    return {{"host",settings.value(key+"host")},{"token",settings.value(key+"token")}};
}
bool RescueStore::configureGroundStation(int uavId,const QString &host,const QString &token) {
    QHostAddress address(host.trimmed());
    if(uavId<1||uavId>6||address.protocol()!=QAbstractSocket::IPv4Protocol||(!address.isLoopback()&&!address.isInSubnet(QHostAddress("100.64.0.0"),10))||token.size()<24||token.contains('\r')||token.contains('\n')) return fail("Dùng IPv4 Tailscale (hoặc localhost để thử nghiệm) và mã xác thực ít nhất 24 ký tự.");
    QSettings settings(QSettings::IniFormat,QSettings::UserScope,"CenterDisplay","CenterDisplay");
    const QString key=QString("groundStation/%1/").arg(uavId);
    settings.setValue(key+"host",host.trimmed());settings.setValue(key+"token",token);settings.sync();
    if(settings.status()!=QSettings::NoError) return fail("Không thể lưu cấu hình trạm điều khiển.");
    m_status="Đã lưu cấu hình nhận kế hoạch tại trạm cho UAV "+QString::number(uavId);emit changed();return true;
}
bool RescueStore::sendPlan(int planId,bool demo) {
    if(!online() || m_busy) return false;
    if(!m_db.transaction()) return fail("Không thể bắt đầu giao dịch cơ sở dữ liệu.");
    QSqlQuery q(m_db);
    q.prepare("SELECT uav_id,purpose,waypoints::text,site_revisions::text,state,dispatch_id::text FROM rescue_plan WHERE id=:id FOR UPDATE");q.bindValue(":id",planId);
    if(!q.exec() || !q.next()) {m_db.rollback();return fail("Không tìm thấy kế hoạch.");}
    const int uav=q.value(0).toInt();const QString purpose=q.value(1).toString();
    const auto points=QJsonDocument::fromJson(q.value(2).toByteArray()).array();
    const auto revisions=QJsonDocument::fromJson(q.value(3).toByteArray()).object();
    const auto state=q.value(4).toString();
    if(demo && simulation() && uav>=4) {
        const auto vehicles=m_simulationStatus.value("uavs").toList();
        if(!m_simulationLive || vehicles.size()!=6) {m_db.rollback();return fail("Chưa có trạng thái UAV mô phỏng.");}
        const auto vehicle=vehicles[uav-1].toMap();
        int deliveries=0;for(const auto &point:points) if(point.toObject().value("siteId").toInt()>0) ++deliveries;
        if(vehicle.value("mission_active").toBool() || vehicle.value("payload").toDouble()<deliveries*5 || vehicle.value("battery").toDouble()<20) {
            m_db.rollback();return fail("UAV đang bay, thiếu hàng hoặc pin dưới 20%. Chọn UAV khác.");
        }
    }
    const auto missionId=q.value(5).toString();
    const auto station=groundStation(uav);
    if(!demo && (station["host"].toString().isEmpty() || station["token"].toString().isEmpty())) {m_db.rollback();return fail("Hãy cấu hình hộp nhận Z2-Flight của UAV đã chọn trước.");}
    if(state!="draft" && state!="failed" && state!="sending") {m_db.rollback();return fail("Chỉ gửi được kế hoạch dự thảo hoặc chưa xác nhận.");}
    for(auto it=revisions.begin();it!=revisions.end();++it) {
        QSqlQuery check(m_db);check.prepare("SELECT revision,state FROM rescue_site WHERE id=:id FOR SHARE");check.bindValue(":id",it.key().toInt());
        if(!check.exec() || !check.next() || check.value(0).toInt()!=it.value().toInt() || check.value(1).toString()!="open") {m_db.rollback();return fail("Hồ sơ địa điểm đã thay đổi. Hãy tạo kế hoạch mới trước khi gửi.");}
    }
    q.prepare("UPDATE rescue_plan SET state='sending' WHERE id=:id");q.bindValue(":id",planId);
    if(!q.exec() || !m_db.commit()) {m_db.rollback();return fail("Không thể cập nhật trạng thái đang gửi kế hoạch.");}
    m_busy=true;refresh();
    QJsonObject message{{"schema",demo?"center-rescue-demo/v1":"center-rescue-plan/v1"},{"missionId",missionId},{"planId",planId},{"uavId",uav},{"purpose",purpose},{"demo",demo},{"waypoints",points}};
    // Real inbox traffic is restricted to the authenticated Tailscale/loopback adapter.
    QNetworkRequest request(QUrl(demo?"http://127.0.0.1:8766/plans":"http://"+station["host"].toString()+":8767/plans"));
    if(!demo) request.setRawHeader("Authorization","Bearer "+station["token"].toString().toUtf8());request.setHeader(QNetworkRequest::ContentTypeHeader,"application/json");request.setTransferTimeout(5000);
    auto *reply=m_network.post(request,QJsonDocument(message).toJson(QJsonDocument::Compact));
    connect(reply,&QNetworkReply::finished,this,[this,reply,planId,uav,demo,missionId]{
        const auto result=QJsonDocument::fromJson(reply->readAll()).object();
        const bool accepted=reply->error()==QNetworkReply::NoError && reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt()==200 && result["planId"].toInt()==planId && result["uavId"].toInt()==uav && result["status"].toString()==(demo?"demo_received":"received_for_review") && (demo || result["missionId"].toString()==missionId);
        const QString state=accepted?(demo?"demo_received":"received_for_review"):"failed";
        const QString detail=accepted?(demo?"Trạm mô phỏng đã xác nhận nhận kế hoạch. Chưa tải lên UAV, kích hoạt động cơ hoặc thực hiện chuyến bay.":"Z2-Flight đã lưu kế hoạch để kiểm tra. Người điều khiển cần kiểm tra, tải lên UAV và bắt đầu bằng các chức năng điều khiển hiện có."):"Chưa có xác nhận hợp lệ. Chưa xác nhận giao nhận; kiểm tra trạm và gửi lại cùng mã kế hoạch.";
        bool saved=m_db.transaction();
        QSqlQuery q(m_db);q.prepare("UPDATE rescue_plan SET state=:state WHERE id=:id");q.bindValue(":state",state);q.bindValue(":id",planId);saved=saved && q.exec();
        q.prepare("INSERT INTO rescue_record(plan_id,uav_id,event,detail) VALUES(:id,:uav,:event,:detail)");q.bindValue(":id",planId);q.bindValue(":uav",uav);q.bindValue(":event",state);q.bindValue(":detail",detail);saved=saved && q.exec();
        if(saved) saved=m_db.commit();
        if(!saved) m_db.rollback();
        m_busy=false;reply->deleteLater();refresh();
        m_status=saved?detail:"Đã nhận xác nhận nhưng cập nhật CSDL thất bại; cần đối chiếu trạng thái kế hoạch.";emit changed();
    });
    return true;
}
