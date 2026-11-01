import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtLocation
import QtPositioning

Item {
    id: panel
    property var operations: typeof rescueStore !== "undefined" ? rescueStore : null
    property bool operationsVisible: true
    property int selectedSiteId: -1
    property int selectedPlanId: -1
    onSelectedSiteIdChanged: if(operationsPanel) operationsPanel.selectedSite=selectedSiteId
    onSelectedPlanIdChanged: if(operationsPanel) operationsPanel.selectedPlan=selectedPlanId
    property bool exerciseFramed: false
    Connections {
        target: panel.operations
        function onChanged() {
            if(panel.operations.plans.length && panel.selectedPlanId<0) panel.selectedPlanId=panel.operations.plans[0].id
        }
    }
    function pathCoordinates(path) {
        const result=[]
        for(let point of path) result.push(QtPositioning.coordinate(point.latitude,point.longitude))
        return result
    }
    function fitExercise() {
        if(!mapLoader.item) return
        const items=[]
        for(let item of mapLoader.item.mapItems) if(item.objectName === "rescueSiteMarker" || item.objectName === "rescueArea") items.push(item)
        if(items.length) mapLoader.item.fitViewportToMapItems(items)
    }
    property alias toolbar: mapToolbar
    property bool locationInfoVisible: true
    property int selectedUav: 0
    property bool follow: false
    property var savedCenter: QtPositioning.coordinate(16.05, 108.2)
    property real savedZoom: 6
    property string tileUrl: appSettings.mapUrl
    ColumnLayout {
        anchors.fill: parent
        spacing: 4
        RowLayout {
            id: mapToolbar
            objectName: "mapToolbar"
            spacing: 4
            Layout.fillWidth: true
            ComboBox {
                id: mapType
                objectName: "mapTypeSelector"
                Layout.fillWidth: true
                Layout.minimumWidth: 80
                model: ["Đường phố (OpenStreetMap)", "Vệ tinh (Esri)", "Địa hình (Esri)", "Máy chủ bản đồ tùy chỉnh"]
                currentIndex: appSettings.mapType
                onActivated: {
                    if (!appSettings.saveMap(currentIndex, customUrl.text)) { mapError.text = "Nhập địa chỉ HTTPS chứa {z}, {x}, {y}, rồi nhấn Áp dụng."; return }
                    mapError.text = ""
                    panel.tileUrl = appSettings.mapUrl
                    mapLoader.active = false
                    mapLoader.active = true
                }
            }
            Button { objectName: "fitUavs"; text: "Vừa"; Layout.preferredWidth: 42; onClicked: if (mapLoader.item) { panel.follow = false; mapLoader.item.fitViewportToMapItems() } }
            Button { text: "−"; Layout.preferredWidth: 32; onClicked: if (mapLoader.item) mapLoader.item.zoomLevel -= 1 }
            Button { text: "+"; Layout.preferredWidth: 32; onClicked: if (mapLoader.item) mapLoader.item.zoomLevel += 1 }
            CheckBox { text: "Bám theo"; checked: panel.follow; onToggled: panel.follow = checked }
            Button {
                objectName: "toggleLocationInfo"
                text: panel.locationInfoVisible ? "Ẩn vị trí" : "Hiện vị trí"
                onClicked: panel.locationInfoVisible = !panel.locationInfoVisible
            }
        }
        RowLayout {
            visible: mapType.currentIndex === 3
            Layout.fillWidth: true
            Button { text: "Hủy sửa"; enabled: customUrl.text !== appSettings.customMapUrl; onClicked: customUrl.text=appSettings.customMapUrl }
            TextField { id: customUrl; Layout.fillWidth: true; text: appSettings.customMapUrl; placeholderText: "https://server/{z}/{x}/{y}.png" }
            Button {
                text: "Áp dụng"
                onClicked: {
                    if (appSettings.saveMap(3, customUrl.text)) {
                        mapError.text = ""
                        panel.tileUrl = appSettings.mapUrl
                        mapLoader.active = false; mapLoader.active = true
                    } else mapError.text = "Địa chỉ không hợp lệ hoặc không thể lưu cài đặt bản đồ."
                }
            }
        }
        Label { id: mapError; visible: text.length > 0; color: "#b00020"; Layout.fillWidth: true; wrapMode: Text.WordWrap }
        RowLayout {
            visible: !!panel.operations
            Layout.fillWidth: true
            Label { text: "R1–R3 trinh sát · T1–T3 vận tải · Xanh dương: dự báo ngập · Tím: khảo sát · Xanh ngọc: tiếp tế"; Layout.fillWidth: true; elide: Text.ElideRight }
            Button { text: panel.operationsVisible ? "Ẩn điều hành" : "Bản đồ–Danh sách–Hồ sơ"; onClicked: panel.operationsVisible=!panel.operationsVisible }
        }
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
        Loader {
            id: mapLoader
            Layout.fillWidth: true
            Layout.fillHeight: true
            sourceComponent: Component {
                Map {
                    id: map
                    objectName: "uavMap"
                    property bool hadLivePositions: false
                    // Only frame on acquisition/reacquisition, not every position update.
                    Timer {
                        interval: 250
                        running: map.mapReady
                        repeat: true
                        onTriggered: {
                            if(panel.operations && panel.operations.areas.length && !panel.exerciseFramed) {
                                panel.fitExercise(); panel.exerciseFramed=true
                            }
                            const current = []
                            for (let marker of map.mapItems)
                                if (typeof marker.live !== "undefined" && marker.live && marker.hasPosition) current.push(marker)
                            if (current.length > 0 && !map.hadLivePositions) {
                                if(panel.operations && panel.operations.areas.length && panel.operationsVisible) panel.fitExercise()
                                else if (current.length === 1) {
                                    map.center = current[0].coordinate
                                    map.zoomLevel = 16
                                } else {
                                    map.fitViewportToMapItems(current)
                                    map.zoomLevel = Math.min(map.zoomLevel, 16)
                                }
                            }
                            map.hadLivePositions = current.length > 0
                        }
                    }
                    center: panel.savedCenter
                    zoomLevel: panel.savedZoom
                    Component.onDestruction: { panel.savedCenter = center; panel.savedZoom = zoomLevel }
                    plugin: Plugin {
                        name: "osm"
                        PluginParameter { name: "osm.mapping.providersrepository.disabled"; value: true }
                        PluginParameter { name: "osm.mapping.custom.host"; value: panel.tileUrl }
                        PluginParameter { name: "osm.mapping.custom.mapcopyright"; value: mapType.currentIndex === 0 ? "© Cộng đồng đóng góp OpenStreetMap" : mapType.currentIndex === 3 ? "Nhà cung cấp tùy chỉnh — kiểm tra thông tin bản quyền" : "Bản đồ © Esri — Esri, Maxar, Earthstar Geographics và cộng đồng GIS" }
                        PluginParameter { name: "osm.mapping.cache.directory"; value: appSettings.mapCache }
                        PluginParameter { name: "osm.useragent"; value: "CenterDisplay/1.0" }
                    }
                    Component.onCompleted: {
                        for (let type of supportedMapTypes)
                            if (type.style === MapType.CustomMap) activeMapType = type
                    }
                    DragHandler { target: null; onTranslationChanged: function(delta) { panel.follow=false; map.pan(-delta.x, -delta.y) } }
                    WheelHandler { onWheel: function(event) { map.zoomLevel += event.angleDelta.y / 120; } }
                    PinchHandler { target: null; onScaleChanged: function(delta) { map.zoomLevel += Math.log2(delta) } }
                    MapItemView {
                        model: panel.operations ? panel.operations.areas : []
                        delegate: MapPolygon {
                            objectName: "rescueArea"
                            required property var modelData
                            path: panel.pathCoordinates(modelData.path)
                            color: "#554391db"
                            border.color: "#2675bb"
                            border.width: 2
                            z: 0
                        }
                    }
                    MapItemView {
                        model: panel.operations ? panel.operations.areas : []
                        delegate: MapQuickItem {
                            required property var modelData
                            coordinate: QtPositioning.coordinate(modelData.path[0].latitude, modelData.path[0].longitude)
                            z: 1
                            sourceItem: Rectangle {
                                width: areaLabel.implicitWidth + 12; height: areaLabel.implicitHeight + 8
                                color: "#e6ffffff"; radius: 4
                                Text { id: areaLabel; anchors.centerIn: parent; text: modelData.name; color: "#174b79"; font.pixelSize: 11 }
                            }
                        }
                    }
                    MapItemView {
                        model: panel.operations ? panel.operations.plans : []
                        delegate: MapPolyline {
                            required property var modelData
                            visible: modelData.state !== "cancelled"
                            path: panel.pathCoordinates(modelData.waypoints)
                            line.width: modelData.id === panel.selectedPlanId ? 5 : 2
                            line.color: ["#764dc7", "#a15cc2", "#6554a4", "#008e97", "#198b4f", "#b06b16"][modelData.uav_id-1]
                            z: 1
                        }
                    }
                    MapItemView {
                        model: panel.operations ? panel.operations.sites : []
                        delegate: MapQuickItem {
                            required property var modelData
                            objectName: "rescueSiteMarker"
                            coordinate: QtPositioning.coordinate(modelData.latitude,modelData.longitude)
                            anchorPoint.x: 18; anchorPoint.y: 18
                            z: 2
                            sourceItem: Rectangle {
                                width: 36; height: 36; radius: 6
                                color: modelData.state === "resolved" ? "#26a65b" : modelData.state === "delivered" ? "#278bcc" : ["#e2b72d","#f18b2b","#d94343"][modelData.priority-1]
                                border.color: modelData.id === panel.selectedSiteId ? "#132e52" : "white"
                                border.width: modelData.id === panel.selectedSiteId ? 3 : 2
                                Text { anchors.centerIn: parent; text: "P"+modelData.id; color: "#172332"; font.bold: true }
                                TapHandler { onTapped: { panel.selectedSiteId=modelData.id; panel.operationsVisible=true } }
                            }
                        }
                    }
                    MapItemView {
                        model: telemetry
                        delegate: MapQuickItem {
                            required property int number
                            required property real latitude
                            required property real longitude
                            required property real heading
                            required property bool hasPosition
                            required property bool live
                            z: 3
                            coordinate: QtPositioning.coordinate(latitude, longitude)
                            visible: hasPosition
                            anchorPoint.x: 32
                            anchorPoint.y: 32
                            sourceItem: Item {
                                width: 64
                                height: 86
                                readonly property color markerColor: live ? (number === panel.selectedUav + 1 ? "#007db8" : "#16865a") : "#747d86"
                                Canvas {
                                    id: paperPlane
                                    width: 64
                                    height: 64
                                    // The tip points north at zero degrees; only the plane rotates.
                                    rotation: heading >= 0 ? heading : 0
                                    opacity: heading >= 0 ? 1 : 0.65
                                    property color planeColor: parent.markerColor
                                    onPlaneColorChanged: requestPaint()
                                    onPaint: {
                                        const ctx = getContext("2d")
                                        ctx.clearRect(0, 0, width, height)
                                        ctx.lineJoin = "round"
                                        ctx.lineWidth = 3
                                        ctx.strokeStyle = "white"
                                        ctx.fillStyle = planeColor
                                        ctx.beginPath()
                                        ctx.moveTo(32, 5)
                                        ctx.lineTo(54, 53)
                                        ctx.lineTo(32, 43)
                                        ctx.lineTo(10, 53)
                                        ctx.closePath()
                                        ctx.fill()
                                        ctx.stroke()
                                        // Fold line and shaded wing give the paper-plane shape depth.
                                        ctx.fillStyle = "rgba(255, 255, 255, 0.28)"
                                        ctx.beginPath()
                                        ctx.moveTo(32, 5)
                                        ctx.lineTo(32, 43)
                                        ctx.lineTo(10, 53)
                                        ctx.closePath()
                                        ctx.fill()
                                        ctx.lineWidth = 1.5
                                        ctx.beginPath()
                                        ctx.moveTo(32, 7)
                                        ctx.lineTo(32, 43)
                                        ctx.stroke()
                                    }
                                }
                                Rectangle {
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    y: 64
                                    width: 44
                                    height: 21
                                    radius: 5
                                    color: parent.markerColor
                                    border.color: "white"
                                    Text {
                                        anchors.centerIn: parent
                                        text: (number<=3 ? "R"+number : "T"+(number-3)) + (heading < 0 ? " ?" : "")
                                        color: "white"
                                        font.bold: true
                                        font.pixelSize: 12
                                    }
                                }
                                TapHandler { onTapped: panel.selectedUav = number - 1 }
                            }
                            onCoordinateChanged: if (panel.follow && number === panel.selectedUav + 1 && live) map.center = coordinate
                        }
                    }
                    Label {
                        anchors.top: parent.top; anchors.horizontalCenter: parent.horizontalCenter
                        visible: map.error !== Map.NoError
                        text: "Không tải được bản đồ. Kiểm tra kết nối mạng và máy chủ bản đồ."
                        color: "#b00020"; background: Rectangle { color: "white" }
                    }
                }
            }
        }
        RescuePanel {
            id: operationsPanel
            objectName: "rescuePanel"
            visible: !!panel.operations && panel.operationsVisible
            Layout.preferredWidth: Math.min(370, panel.width * 0.46)
            Layout.fillHeight: true
            store: panel.operations
            selectedSite: panel.selectedSiteId
            selectedPlan: panel.selectedPlanId
            onSelectedSiteChanged: panel.selectedSiteId=selectedSite
            onSelectedPlanChanged: panel.selectedPlanId=selectedPlan
            onFocusSite: function(latitude, longitude) {
                if(mapLoader.item) { panel.follow=false; mapLoader.item.center=QtPositioning.coordinate(latitude,longitude); mapLoader.item.zoomLevel=15 }
            }
            onFitExercise: panel.fitExercise()
        }
        }
        Label { visible: panel.locationInfoVisible; text: "Vị trí UAV được nhận riêng qua MAVLink. Màu xám: vị trí chưa cập nhật quá 5 giây."; wrapMode: Text.WordWrap; Layout.fillWidth: true }
        ScrollView {
            id: locationInfo
            objectName: "locationInfo"
            visible: panel.locationInfoVisible
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(locationRows.implicitHeight, panel.height * 0.6)
            clip: true
            contentWidth: availableWidth
            ColumnLayout {
                id: locationRows
                width: locationInfo.availableWidth
                Repeater {
                    model: telemetry
                    delegate: RowLayout {
                        required property int number
                        required property string status
                        required property bool listening
                        required property bool hasPosition
                        required property real latitude
                        required property real longitude
                        required property real altitude
                        required property real roll
                        required property real pitch
                        required property real yaw
                        required property bool attitudeLive
                        required property bool hasAttitude
                        required property real groundSpeed
                        Layout.fillWidth: true
                        Label {
                            Layout.preferredWidth: 220
                            visible: !!panel.operations && panel.operations.simulation
                            property var vehicleInfo: panel.operations && panel.operations.simulationStatus.uavs ? panel.operations.simulationStatus.uavs[number-1] : null
                            text: !vehicleInfo || !panel.operations.simulationLive ? "Chờ trạng thái mô phỏng" :
                                  "Pin " + vehicleInfo.battery.toFixed(0) + "% · ≈" + (vehicleInfo.battery*0.3).toFixed(0) + " phút" + (number>3 ? " · Hàng " + vehicleInfo.payload.toFixed(0) + "/20 kg" : "") + "\n" + vehicleInfo.status
                            color: vehicleInfo && vehicleInfo.battery<20 ? "#b42318" : "#26516a"
                            wrapMode: Text.WordWrap
                            font.pixelSize: 12
                        }
                        Button { text: number<=3 ? "R"+number+" Trinh sát" : "T"+(number-3)+" Vận tải"; highlighted: panel.selectedUav === number-1; onClicked: { panel.selectedUav=number-1; if(hasPosition && mapLoader.item) { mapLoader.item.center=QtPositioning.coordinate(latitude,longitude); mapLoader.item.zoomLevel=16 } } }
                        ColumnLayout {
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            spacing: 2
                            Label {
                                Layout.fillWidth: true
                                text: status + (hasPosition ? "  " + latitude.toFixed(6) + ", " + longitude.toFixed(6) + "  Cao độ " + altitude.toFixed(1) + " m (tương đối)  Tốc độ " + groundSpeed.toFixed(1) + " m/s" : "")
                                elide: Text.ElideRight
                            }
                            Label {
                                Layout.fillWidth: true
                                text: attitudeLive ? "Nghiêng ngang " + roll.toFixed(1) + "°   Chúi ngóc " + pitch.toFixed(1) + "°   Hướng " + yaw.toFixed(1) + "°"
                                                   : (hasAttitude ? "Góc tư thế chưa cập nhật" : "Đang chờ dữ liệu tư thế")
                                color: attitudeLive ? palette.text : "#c07818"
                                elide: Text.ElideRight
                            }
                        }
                        Button {
                            objectName: "locationToggle" + number
                            text: listening ? "Ngắt kết nối" : "Kết nối"
                            onClicked: {
                                if (listening) telemetry.disconnectLocation(number-1)
                                else telemetry.connectLocation(number-1)
                            }
                        }
                        Button { text: "Cấu hình"; onClicked: linkDialog.edit(number-1) }
                    }
                }
            }
        }
    }
    Dialog {
        id: linkDialog
        title: "UAV " + (slot+1) + " · Dữ liệu bay"
        anchors.centerIn: parent
        modal: true
        width: Math.min(420, panel.width)
        property int slot: 0
        function edit(index) {
            slot=index
            const config=telemetry.configuration(index)
            portField.text=config.port; sourceField.text=config.source; systemField.text=config.systemId
            errorLabel.text=""; open()
        }
        ColumnLayout {
            anchors.fill: parent
            Label { text: "Chuyển tiếp MAVLink UDP từ trạm điều khiển đến máy tính này qua cổng:"; wrapMode: Text.WordWrap; Layout.fillWidth: true }
            TextField { id: portField; Layout.fillWidth: true; placeholderText: "Cổng UDP (1024–65535)"; validator: IntValidator { bottom: 1024; top: 65535 } }
            TextField { id: sourceField; Layout.fillWidth: true; placeholderText: "IP trạm điều khiển (để trống: nhận mọi nguồn)" }
            TextField { id: systemField; Layout.fillWidth: true; placeholderText: "Mã hệ thống MAVLink (1–255)"; validator: IntValidator { bottom: 1; top: 255 } }
            Label { id: errorLabel; color: "#b00020"; wrapMode: Text.WordWrap; Layout.fillWidth: true }
            Button { text: "Hủy"; onClicked: linkDialog.close() }
            Button { text: "Lưu kết nối"; onClicked: { if(telemetry.configure(linkDialog.slot,Number(portField.text),sourceField.text,Number(systemField.text))) linkDialog.close(); else errorLabel.text="Không thể lưu hoặc mở cổng. Kiểm tra cổng, IP nguồn và mã hệ thống; mỗi UAV cần một cổng riêng." } }
        }
    }
}
