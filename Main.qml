import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Video 1.0

ApplicationWindow {
    id: window
    visible: true
    width: 1920
    height: 1080
    minimumWidth: 640
    minimumHeight: 480

    readonly property int layoutMode: appSettings.layoutMode
    readonly property var rtspUrls: appSettings.cameraUrls
    readonly property var cameraNames: ["R1 · Trinh sát", "R2 · Trinh sát", "R3 · Trinh sát",
        "T1 · Vận tải", "T2 · Vận tải", "T3 · Vận tải"]

    readonly property bool simulationMode: Qt.application.arguments.indexOf("--simulation") !== -1
    title: simulationMode ? "CenterDisplay — MÔ PHỎNG (vị trí UAV giả lập)" : "CenterDisplay"
    property int viewMode: simulationMode ? 1 : 0
    property bool mapWasOpened: simulationMode
    onViewModeChanged: if (viewMode === 1) mapWasOpened = true

    // Receivers belong to display slots; camera rows can control multiple slots.
    property var selectedCameras: [0, 1, 2, 3, 4, 5]
    function cameraSlots(camera) {
        const slots = []
        for (let i = 0; i < selectedCameras.length; ++i)
            if (selectedCameras[i] === camera) slots.push(i)
        return slots
    }
    function cameraState(camera) {
        const slots = cameraSlots(camera)
        let result = 0
        for (const slot of slots) {
            const state = receivers[slot].state
            if (state === 3) result = 3
            else if (state === 1 && result !== 3) result = 1
            else if (state === 2 && result === 0) result = 2
        }
        return result
    }
    function selectCamera(slot, camera, url) {
        if (receivers[slot].connectCamera(url)) {
            const selected = selectedCameras.slice()
            selected[slot] = camera
            selectedCameras = selected
            receivers[slot].start()
        }
    }
    function toggleCamera(camera, url) {
        let slots = cameraSlots(camera)
        if (cameraState(camera) !== 0) {
            for (const slot of slots) receivers[slot].stop()
        } else {
            // If reassigned away, Start restores this camera to its numbered slot.
            if (slots.length === 0) slots = [camera]
            for (const slot of slots) selectCamera(slot, camera, url)
        }
    }

    Popup {
        id: settingsPopup
        objectName: "settingsPopup"
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(1080, window.width - 40)
        height: Math.min(680, window.height - 40)
        padding: 20
        modal: true
        property var draftUrls: []
        property int draftMode: 1

        onOpened: {
            draftUrls = Array.from(window.rtspUrls)
            draftMode = window.layoutMode
            layoutBox.currentIndex = [1, 4, 6].indexOf(draftMode)
            saveError.text = ""
        }
        function applySettings() {
            if (appSettings.save(draftUrls, draftMode)) {
                close()
            } else {
                saveError.text = "Không thể lưu cài đặt. Kiểm tra quyền ghi dữ liệu."
            }
        }
        background: Rectangle { color: "#303030"; radius: 10 }
        contentItem: ColumnLayout {
            spacing: 15
            Label {
                text: "Cài đặt đường dẫn RTSP"
                color: "white"
                font.pixelSize: 22
                Layout.alignment: Qt.AlignHCenter
            }
            RowLayout {
                Label { text: "Số lượng màn hình:"; color: "white" }
                ComboBox {
                    id: layoutBox
                    objectName: "layoutBox"
                    model: ["1", "4", "6"]
                    Layout.preferredWidth: 150
                    onActivated: settingsPopup.draftMode = [1, 4, 6][currentIndex]
                }
            }
            ScrollView {
                id: urlScroll
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                contentWidth: availableWidth
                ColumnLayout {
                    width: urlScroll.availableWidth
                    spacing: 12
                    Repeater {
                        model: 6
                        delegate: RowLayout {
                            required property int index
                            readonly property int connectionState: window.cameraState(index)
                            Layout.fillWidth: true
                            spacing: 8
                            Label {
                                text: window.cameraNames[index] + ":"
                                color: "white"
                                Layout.preferredWidth: 120
                            }
                            TextField {
                                objectName: "url" + (index + 1)
                                Layout.fillWidth: true
                                placeholderText: "rtsp://..."
                                text: settingsPopup.draftUrls[index] || ""
                                selectByMouse: true
                                onTextEdited: {
                                    const copy = settingsPopup.draftUrls.slice()
                                    copy[index] = text
                                    settingsPopup.draftUrls = copy
                                }
                            }
                            Rectangle {
                                objectName: "cameraStatus" + (index + 1)
                                Layout.preferredWidth: 110
                                Layout.preferredHeight: 32
                                radius: 5
                                color: ["#52606d", "#956400", "#187347", "#ac382d"][parent.connectionState]
                                Label {
                                    anchors.centerIn: parent
                                    text: ["Đã dừng", "Đang kết nối", "Trực tiếp", "Đang kết nối lại"][parent.parent.connectionState]
                                    color: "white"
                                    font.pixelSize: 12
                                }
                            }
                            Button {
                                objectName: "streamToggle" + (index + 1)
                                Layout.preferredWidth: 72
                                text: parent.connectionState === 0 ? "Bắt đầu" : "Dừng"
                                enabled: parent.connectionState !== 0 || (settingsPopup.draftUrls[index] || "").trim().length > 0
                                onClicked: window.toggleCamera(index, settingsPopup.draftUrls[index] || "")
                            }
                        }
                    }
                }
            }
            Label {
                text: "Bắt đầu sử dụng địa chỉ ở trên. Áp dụng để lưu. Dừng sẽ ngắt mọi màn hình đang hiển thị camera đó.\nNếu camera chưa được gán, Bắt đầu sẽ dùng màn hình cùng số."
                color: "#cccccc"
                font.pixelSize: 12
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
            }
            Label {
                id: saveError
                color: "#ffaaaa"
                visible: text.length > 0
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
            }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                spacing: 20
                Button { text: "Hủy"; onClicked: settingsPopup.close() }
                Button {
                    objectName: "applySettings"
                    text: "Áp dụng"
                    onClicked: settingsPopup.applySettings()
                }
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 50
            color: "#2f4f4f"
            RowLayout {
                anchors.fill: parent
                anchors.margins: 8
                spacing: 10
                ScrollView {
                    id: selectorScroll
                    objectName: "videoToolbar"
                    visible: window.viewMode === 0
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    contentHeight: availableHeight
                    Row {
                        spacing: 10
                        Repeater {
                            model: window.layoutMode
                            delegate: ComboBox {
                                required property int index
                                objectName: "selector" + (index + 1)
                                width: 150
                                height: 28
                                model: window.cameraNames
                                displayText: "Màn " + (index + 1) + ": Cam " + (window.selectedCameras[index] + 1)
                                currentIndex: window.selectedCameras[index]
                                property int displayIndex: index
                                onActivated: function(cameraIndex) {
                                    window.selectCamera(displayIndex, cameraIndex, window.rtspUrls[cameraIndex])
                                }
                            }
                        }
                    }
                }
                Item {
                    id: mapToolbarHost
                    objectName: "mapToolbarHost"
                    visible: window.viewMode === 1
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                }
                ComboBox {
                    objectName: "viewSelector"
                    model: ["Video", "Bản đồ"]
                    currentIndex: window.viewMode
                    onActivated: window.viewMode = currentIndex
                }
                Button {
                    objectName: "openSettings"
                    visible: window.viewMode === 0
                    text: "⚙"
                    Layout.preferredWidth: 34
                    Layout.preferredHeight: 34
                    onClicked: settingsPopup.open()
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            GridLayout {
                visible: window.viewMode === 0
                Layout.preferredWidth: 1
                objectName: "videoGrid"
                Layout.fillWidth: true
                Layout.fillHeight: true
                columns: window.layoutMode === 1 ? 1 : (window.layoutMode === 4 ? 2 : 3)
                rows: window.layoutMode === 6 ? 2 : columns
                columnSpacing: 5
                rowSpacing: 5
                Repeater {
                    model: 6
                    delegate: VideoItem {
                        required property int index
                        objectName: "video" + (index + 1)
                        visible: index < window.layoutMode
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.preferredWidth: 1
                        Layout.preferredHeight: 1
                        Component.onCompleted: receivers[index].setVideoItem(this)
                        Label {
                            anchors.top: parent.top
                            anchors.left: parent.left
                            anchors.margins: 10
                            text: parent.index + 1
                            color: "blue"
                        }
                        Connections {
                            target: receivers[index]
                            function onErrorOccurred(message) {
                                console.log("Màn hình " + (index + 1) + ":", message)
                            }
                        }
                    }
                }
            }
            Loader {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredWidth: 1
                id: mapPanelLoader
                objectName: "mapPanelLoader"
                visible: window.viewMode === 1
                active: window.mapWasOpened
                onLoaded: {
                    item.toolbar.parent = mapToolbarHost
                    item.toolbar.anchors.fill = mapToolbarHost
                }
                source: "MapPanel.qml"
            }
        }
    }
}
