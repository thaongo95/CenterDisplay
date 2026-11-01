#pragma once
#include <QGeoCoordinate>
#include <QJsonArray>
#include <QJsonObject>
#include <QLineF>
#include <QtMath>
#include <algorithm>
#include <cmath>

// Adapted from SITLControl/MissionPlanner.cpp: rotated scan lines in a local
// metric plane. Pair intersections to support the demo's concave boundaries.
inline QJsonArray surveyRoute(const QJsonArray &boundary, double spacing, double angle,
                             double altitude, QString &error) {
    QJsonArray result;
    if(boundary.size()<3 || boundary.size()>500 || !std::isfinite(spacing) || spacing<20 || spacing>1000 || !std::isfinite(angle)) {
        error="Cần vùng có 3–500 đỉnh, khoảng cách dải 20–1000 m và góc hợp lệ.";return result;
    }
    const auto first=boundary.first().toObject();
    const QGeoCoordinate origin(first["latitude"].toDouble(),first["longitude"].toDouble());
    const double radians=qDegreesToRadians(std::fmod(angle,360.)), c=std::cos(radians), s=std::sin(radians);
    QList<QPointF> polygon;
    for(const auto &value:boundary) {
        const auto p=value.toObject();QGeoCoordinate coordinate(p["latitude"].toDouble(999),p["longitude"].toDouble(999));
        if(!coordinate.isValid() || std::abs(coordinate.latitude())>85 || origin.distanceTo(coordinate)>10000) {
            error="Đỉnh vùng không hợp lệ hoặc cách đỉnh đầu quá 10 km.";return {};
        }
        const double d=origin.distanceTo(coordinate), a=qDegreesToRadians(origin.azimuthTo(coordinate));
        const double x=d*std::sin(a),y=d*std::cos(a);
        polygon.append({c*x-s*y,s*x+c*y});
    }
    double area=0;
    for(int i=0;i<polygon.size();++i) {
        QLineF edge(polygon[i],polygon[(i+1)%polygon.size()]);
        if(edge.length()<.1) {error="Vùng có đỉnh trùng nhau.";return {};}
        area+=edge.p1().x()*edge.p2().y()-edge.p2().x()*edge.p1().y();
        for(int j=i+1;j<polygon.size();++j) {
            if(j==i+1 || (i==0 && j==polygon.size()-1)) continue;
            if(edge.intersects(QLineF(polygon[j],polygon[(j+1)%polygon.size()]),nullptr)==QLineF::BoundedIntersection) {
                error="Đường biên vùng tự cắt nhau.";return {};
            }
        }
    }
    if(std::abs(area)<1) {error="Vùng không có diện tích khảo sát.";return {};}
    double low=polygon.first().y(),high=low;
    for(auto p:polygon){low=std::min(low,p.y());high=std::max(high,p.y());}
    int strips=std::max(1,int(std::ceil((high-low)/spacing)));
    if(strips>49){error="Quá nhiều điểm; tăng khoảng cách dải.";return {};}
    for(int row=0;row<strips;++row) {
        double y=low+(row+.5)*(high-low)/strips;QList<double> cuts;
        for(int i=0;i<polygon.size();++i) {
            auto a=polygon[i],b=polygon[(i+1)%polygon.size()];
            if((a.y()<=y&&b.y()>y)||(b.y()<=y&&a.y()>y)) cuts.append(a.x()+(y-a.y())*(b.x()-a.x())/(b.y()-a.y()));
        }
        std::sort(cuts.begin(),cuts.end());if(row%2)std::reverse(cuts.begin(),cuts.end());
        for(double x:cuts) {
            double east=c*x+s*y,north=-s*x+c*y;
            auto point=origin.atDistanceAndAzimuth(std::hypot(east,north),qRadiansToDegrees(std::atan2(east,north)));
            result.append(QJsonObject{{"latitude",point.latitude()},{"longitude",point.longitude()},{"relativeAltitudeM",altitude},{"action","survey"},{"siteId",0}});
        }
        if(result.size()>98){error="Quá 100 điểm hành trình; tăng khoảng cách dải.";return {};}
    }
    if(result.isEmpty())error="Không tạo được tuyến khảo sát.";
    return result;
}
