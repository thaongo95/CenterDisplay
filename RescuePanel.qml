import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window

Rectangle {
    id: root
    property var store: null
    property int selectedSite: -1
    property int selectedPlan: -1
    property var routeSiteIds: []
    property int planningUav: 1
    function openPlanner(id) {
        planningUav=id; routeSiteIds=[];
        for(let i=0;i<areaChoice.count;++i) if(areaChoice.model[i].id===id) areaChoice.currentIndex=i;
        plannerDialog.open();
    }
    property var currentSite: {
        if (store) for (let site of store.sites) if (site.id === selectedSite) return site
        return null
    }
    onSelectedSiteChanged: if(selectedSite>=0) tabs.currentIndex=1
    signal focusSite(real latitude, real longitude)
    signal fitExercise()
    color: "#f4f7fb"
    border.color: "#ccd5df"
    function stateText(code) {
        const labels = {open:"Cần cứu trợ", delivered:"Đã giao · chờ trinh sát", resolved:"Xanh · đã xác nhận", draft:"Dự thảo", sending:"Đang gửi", demo_received:"Trạm mô phỏng đã nhận", received_for_review:"Trạm đã nhận, chờ kiểm tra", failed:"Gửi thất bại", cancelled:"Đã hủy"}
        return labels[code] || "Không rõ trạng thái"
    }
    function eventText(code) {
        const labels = {plan_cancelled:"Đã hủy dự thảo", recon_confirmed:"Trinh sát xác nhận xanh", site_discovered:"Phát hiện điểm cần cứu trợ", transport_departed:"UAV vận tải khởi hành", supply_delivered:"Đã giao hàng mô phỏng", mission_completed:"Đã về căn cứ", demo_seed:"Khởi tạo mô phỏng", recon_report:"Báo cáo trinh sát", plan_drafted:"Đã tạo dự thảo", demo_received:"Trạm mô phỏng đã nhận", received_for_review:"Đã nhận để kiểm tra", failed:"Giao nhận thất bại"}
        return labels[code] || "Sự kiện hệ thống"
    }
    function priorityColor(level) { return ["#e2b72d", "#f18b2b", "#d94343"][level-1] }
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 10
        spacing: 6
        Label { text: "ĐIỀU HÀNH CỨU TRỢ · MÔ PHỎNG"; font.bold: true; color: "#193249" }
        Label { text: store ? store.status : "Dịch vụ cơ sở dữ liệu chưa sẵn sàng"; wrapMode: Text.WordWrap; Layout.fillWidth: true; color: "#465669"; font.pixelSize: 12 }
        RowLayout {
            Button { text: root.store && root.store.online ? "Làm mới" : "Kết nối CSDL"; enabled: !!root.store && !root.store.busy; onClicked: root.store.online ? root.store.refresh() : root.store.open() }
            Button { text: "Xem tất cả điểm"; onClicked: root.fitExercise() }
        }
        Label {
            text: "Kết nối CSDL để hiện vùng ngập → Kế hoạch → Tạo R1/R2/R3 → Lưu và xem tuyến → Gửi đến trạm mô phỏng để UAV bắt đầu bay."
            wrapMode: Text.WordWrap; Layout.fillWidth: true; font.pixelSize: 12
        }
        RowLayout {
            visible: !!root.store && root.store.simulation
            Label { text: "Tốc độ mô phỏng" }
            ComboBox {
                objectName: "simulationSpeedSelector"
                model: ["Tạm dừng", "1×", "2×", "5×", "10×", "20×"]
                currentIndex: root.store ? [0,1,2,5,10,20].indexOf(root.store.simulationSpeed) : 1
                onActivated: root.store.setSimulationSpeed([0,1,2,5,10,20][currentIndex])
            }
            Label { text: root.store && root.store.simulationLive ? Math.floor((root.store.simulationStatus.elapsed || 0)/60)+" phút" : "Chờ mô phỏng" }
        }
        TabBar {
            id: tabs
            Layout.fillWidth: true
            TabButton { text: "Danh sách (" + (root.store ? root.store.sites.length : 0) + ")" }
            TabButton { text: "Hồ sơ" }
            TabButton { text: "Kế hoạch" }
        }
        StackLayout {
            currentIndex: tabs.currentIndex
            Layout.fillWidth: true
            Layout.fillHeight: true
            ListView {
                id: siteList
                clip: true
                spacing: 5
                model: root.store ? root.store.sites : []
                Label {
                    anchors.centerIn: parent; width: parent.width - 16
                    visible: siteList.count === 0
                    text: "Chưa phát hiện điểm cứu trợ. Kết nối CSDL, tạo và gửi kế hoạch trinh sát để bắt đầu."
                    wrapMode: Text.WordWrap; horizontalAlignment: Text.AlignHCenter
                }
                ScrollBar.vertical: ScrollBar {}
                delegate: Rectangle {
                    required property var modelData
                    width: siteList.width
                    height: 88
                    color: root.selectedSite === modelData.id ? "#dceaf9" : "white"
                    radius: 5
                    border.color: modelData.state === "resolved" ? "#26a65b" : modelData.state === "delivered" ? "#278bcc" : root.priorityColor(modelData.priority)
                    border.width: 2
                    Column {
                        anchors.fill: parent; anchors.margins: 8; spacing: 3
                        Text { text: "P" + modelData.id + " · " + modelData.name; font.bold: true; width: parent.width; elide: Text.ElideRight }
                        Text { text: modelData.state === "open" ? ["VÀNG · Theo dõi", "CAM · Khẩn cấp", "ĐỎ · Nguy cấp"][modelData.priority-1] : root.stateText(modelData.state); color: "#394554" }
                        Text { text: modelData.need; width: parent.width; elide: Text.ElideRight }
                        Text { text: modelData.people + " người · R" + modelData.recon_uav + " khảo sát"; color: "#647184" }
                    }
                    TapHandler { onTapped: { root.selectedSite=modelData.id; root.focusSite(modelData.latitude,modelData.longitude); tabs.currentIndex=1 } }
                }
            }
            ScrollView {
                id: recordScroll
                contentWidth: availableWidth
                clip: true
                ColumnLayout {
                    width: recordScroll.availableWidth
                    Label { text: root.currentSite ? "P" + root.currentSite.id + " · " + root.currentSite.name : "Chọn một điểm trên bản đồ hoặc danh sách"; font.bold: true; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                    Label { text: root.currentSite ? root.currentSite.need + "\n" + root.currentSite.people + " người · Giao R" + root.currentSite.recon_uav + "\nPhiên bản " + root.currentSite.revision : ""; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                    ComboBox { id: priority; model: ["Vàng · Theo dõi", "Cam · Khẩn cấp", "Đỏ · Nguy cấp"]; currentIndex: root.currentSite ? root.currentSite.priority-1 : 0; Layout.fillWidth: true }
                    TextArea { id: note; placeholderText: "Kết quả trinh sát / lý do thay đổi mức ưu tiên"; wrapMode: TextEdit.Wrap; Layout.fillWidth: true; Layout.preferredHeight: 80 }
                    CheckBox { id: resolved; enabled: !root.store || !root.store.simulation; text: "Đã xử lý địa điểm"; checked: root.currentSite ? root.currentSite.state === "resolved" : false }
                    Button { text: "Lưu báo cáo trinh sát"; enabled: !!root.currentSite && !!root.store && !root.store.busy; onClicked: { const s=root.currentSite; if(root.store.report(s.id,s.revision,s.recon_uav,priority.currentIndex+1,note.text,resolved.checked)) note.text="" } }
                    Button {
                        text: "Hủy chỉnh sửa báo cáo"
                        enabled: !!root.currentSite
                        onClicked: {
                            note.text="";
                            priority.currentIndex=Qt.binding(function(){return root.currentSite ? root.currentSite.priority-1 : 0});
                            resolved.checked=Qt.binding(function(){return root.currentSite ? root.currentSite.state==="resolved" : false});
                        }
                    }
                    Label { text: "NHẬT KÝ HOẠT ĐỘNG"; font.bold: true }
                    Repeater {
                        model: root.store ? root.store.records : []
                        delegate: Label {
                            required property var modelData
                            visible: !root.currentSite || modelData.site_id === root.selectedSite || modelData.plan_id !== null
                            text: modelData.created_at + "\n" + root.eventText(modelData.event) + " · " + modelData.detail
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                            font.pixelSize: 12
                        }
                    }
                }
            }
            ColumnLayout {
                GridLayout {
                    columns: 3; Layout.fillWidth: true
                    Repeater {
                        model: 6
                        Button {
                            required property int index
                            text: "Tạo " + (index<3 ? "R"+(index+1) : "T"+(index-2))
                            Layout.fillWidth: true
                            enabled: !!root.store && root.store.online && !root.store.busy
                            onClicked: {uav.currentIndex=index;root.openPlanner(index+1)}
                        }
                    }
                }
                ComboBox { id: uav; currentIndex: 3; Layout.fillWidth: true; model: ["R1 · Trinh sát", "R2 · Trinh sát", "R3 · Trinh sát", "T1 · Vận tải", "T2 · Vận tải", "T3 · Vận tải"] }
                Button { text: "Cài đặt nhận kế hoạch tại trạm"; enabled: !!root.store; onClicked: { const config=root.store.groundStation(uav.currentIndex+1); gcsHost.text=config.host || ""; gcsToken.text=config.token || ""; gcsDialog.open() } }
                Button { text: "Tự động phân tuyến"; enabled: !!root.store && root.store.online && !root.store.busy; onClicked: { if(root.store.draftPlan(uav.currentIndex+1)) root.selectedPlan=root.store.plans[0].id } }
                Label { text: "Trinh sát: quét vùng từ PostgreSQL. Vận tải: chia các điểm đã phát hiện cho các UAV còn sẵn sàng, ưu tiên đỏ → cam → vàng.\nCần kiểm tra dự thảo đường bay trước khi thực hiện."; wrapMode: Text.WordWrap; Layout.fillWidth: true; font.pixelSize: 12 }
                ListView {
                    id: planList
                    Layout.fillWidth: true; Layout.fillHeight: true
                    clip: true; spacing: 6
                    model: root.store ? root.store.plans : []
                    ScrollBar.vertical: ScrollBar {}
                    delegate: Rectangle {
                        required property var modelData
                        width: planList.width
                        height: planColumn.implicitHeight + 16
                        color: root.selectedPlan === modelData.id ? "#dceaf9" : "white"
                        border.color: "#a3b5c7"; radius: 5
                        ColumnLayout {
                            id: planColumn
                            anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 8
                            Button { text: "Kế hoạch #" + modelData.id + " · " + (modelData.uav_id<=3 ? "R"+modelData.uav_id : "T"+(modelData.uav_id-3)); onClicked: { root.selectedPlan=modelData.id; root.fitExercise() } }
                            Label { text: (modelData.purpose === "recon" ? "Trinh sát" : "Vận tải") + " · " + root.stateText(modelData.state) + "\n" + modelData.waypoints.length + " điểm hành trình"; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                            Label { text: modelData.note; font.pixelSize: 11; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                            Button {
                                objectName: "cancelPlan" + modelData.id
                                text: "Hủy dự thảo"
                                visible: modelData.state === "draft"
                                enabled: !!root.store && !root.store.busy
                                onClicked: root.store.cancelPlan(modelData.id)
                            }
                            Label {
                                visible: modelData.state === "sending" || modelData.state === "failed"
                                text: "Chưa rõ trạm đã nhận hay chưa; cần đối chiếu tại trạm trước khi hủy nhiệm vụ."
                                wrapMode: Text.WordWrap; Layout.fillWidth: true; font.pixelSize: 11
                            }
                            Button { text: "Gửi dự thảo đến Z2-Flight"; enabled: !!root.store && !root.store.busy && (modelData.state==="draft" || modelData.state==="failed" || modelData.state==="sending"); onClicked: root.store.sendToGroundStation(modelData.id) }
                            Button { text: "Gửi đến trạm mô phỏng"; enabled: !!root.store && !root.store.busy && (modelData.state==="draft" || modelData.state==="failed" || modelData.state==="sending"); onClicked: root.store.sendDemoPlan(modelData.id) }
                        }
                    }
                }
            }
        }
        Label { text: "Xanh dương: chờ xác nhận · Xanh lá: trinh sát đã xác nhận"; color: "#795b18"; font.pixelSize: 11 }
    }
    Dialog {
        id: plannerDialog
        parent: Overlay.overlay
        objectName: "createUavPlanDialog"
        title: "Tạo kế hoạch " + (root.planningUav<=3 ? "R"+root.planningUav : "T"+(root.planningUav-3))
        anchors.centerIn: parent
        width: Math.min(600, root.Window.window ? root.Window.window.width-24 : 360)
        height: Math.min(620, root.Window.window ? root.Window.window.height-40 : 550)
        modal: true
        contentItem: ColumnLayout {
            Label { text: root.planningUav<=3 ? "Khảo sát vùng theo dải bay" : "Bay theo thứ tự điểm cứu trợ"; font.bold: true }
            RowLayout {
                Label { text: "Cao độ (m)" }
                SpinBox { id: altitude; from: 10; to: 120; value: 80; editable: true }
            }
            ComboBox {
                id: areaChoice; visible: root.planningUav<=3; Layout.fillWidth: true
                model: root.store ? root.store.areas : []; textRole: "name"
            }
            RowLayout {
                visible: root.planningUav<=3
                Label { text: "Dải (m)" }
                SpinBox { id: spacing; from: 20; to: 1000; value: 150; editable: true }
                Label { text: "Góc (°)" }
                SpinBox { id: angle; from: 0; to: 359; value: 0; editable: true }
            }
            Label {
                visible: root.planningUav>3
                text: "Chọn tối đa 4 điểm đang cần cứu trợ; thứ tự chọn là thứ tự bay. Mỗi điểm 5 kg. Điểm đã giao cho kế hoạch khác sẽ bị từ chối khi lưu."
                wrapMode: Text.WordWrap; Layout.fillWidth: true
            }
            Label {
                visible: root.planningUav>3
                text: "Căn cứ → " + root.routeSiteIds.map(id=>"P"+id).join(" → ") + " → Căn cứ"
                wrapMode: Text.WordWrap; Layout.fillWidth: true; font.bold: true
            }
            ListView {
                visible: root.planningUav>3
                Layout.fillHeight: true; Layout.fillWidth: true; clip: true
                model: root.store ? root.store.sites.filter(site=>site.state==="open") : []
                ScrollBar.vertical: ScrollBar {}
                delegate: CheckBox {
                    required property var modelData
                    width: ListView.view.width
                    text: "P"+modelData.id+" · "+modelData.name+" · "+["Vàng","Cam","Đỏ"][modelData.priority-1]+" · "+modelData.people+" người"
                    checked: root.routeSiteIds.indexOf(modelData.id)>=0
                    onClicked: {
                        let ids=root.routeSiteIds.slice();const i=ids.indexOf(modelData.id);
                        if(i>=0) ids.splice(i,1); else if(ids.length<4) ids.push(modelData.id);
                        root.routeSiteIds=ids;
                    }
                }
            }
            Item { visible: root.planningUav<=3; Layout.fillHeight: true }
            Label {
                text: "Lưu để xem tuyến trên bản đồ. Tuyến gồm căn cứ, các điểm hành trình và trở về căn cứ. Đoạn nối có thể nằm ngoài vùng khảo sát lõm."
                wrapMode: Text.WordWrap; Layout.fillWidth: true
            }
            Label { text: root.store ? root.store.status : ""; wrapMode: Text.WordWrap; Layout.fillWidth: true; color: "#795b18" }
            RowLayout {
                Button { text: "Chọn lại"; visible: root.planningUav>3; onClicked: root.routeSiteIds=[] }
                Button { text: "Hủy"; onClicked: plannerDialog.close() }
                Button {
                    text: "Lưu và xem tuyến"; enabled: !!root.store && !root.store.busy
                    onClicked: {
                        const area=areaChoice.currentIndex>=0 ? areaChoice.model[areaChoice.currentIndex].id : -1;
                        if(root.store.createPlan(root.planningUav,area,altitude.value,spacing.value,angle.value,root.routeSiteIds)) {
                            root.selectedPlan=root.store.plans[0].id;plannerDialog.close();root.fitExercise();
                        }
                    }
                }
            }
        }
    }
    Dialog {
        id: gcsDialog
        title: "UAV " + (uav.currentIndex+1) + " · Hộp nhận Z2-Flight"
        anchors.centerIn: parent
        modal: true
        width: Math.min(350,root.width)
        ColumnLayout {
            width: parent.width
            Label { text: "Dùng IPv4 Tailscale của trạm điều khiển. Bật hộp nhận kế hoạch trên cổng 8767 với cùng số UAV và mã xác thực."; Layout.fillWidth: true; wrapMode: Text.WordWrap }
            TextField { id: gcsHost; placeholderText: "100.x.x.x"; Layout.fillWidth: true }
            TextField { id: gcsToken; placeholderText: "Mã xác thực dùng chung (ít nhất 24 ký tự)"; echoMode: TextInput.Password; Layout.fillWidth: true }
            Button { text: "Hủy"; onClicked: { gcsToken.text=""; gcsDialog.close() } }
            Button { text: "Lưu"; onClicked: if(root.store.configureGroundStation(uav.currentIndex+1,gcsHost.text,gcsToken.text)) gcsDialog.close() }
        }
    }
}
