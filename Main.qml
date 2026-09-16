import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Video 1.0

ApplicationWindow {
    visible: true
    width: 1920
    height: 1080

    property int layoutMode: 1

    property var rtspUrls: [
        "rtsp://100.91.59.32:8554/main.264",
        "rtsp://100.87.186.117:8554/main.264",
        "rtsp://100.91.60.10:8554/main.264",
        "rtsp://100.91.60.11:8554/main.264",
        "rtsp://100.91.60.10:8554/main.264",
        "rtsp://100.91.60.11:8554/main.264"
    ]
    Popup {
        id: settingsPopup
        width: parent.width/2; height: parent.height/2
        modal: true
        anchors.centerIn: parent

        Rectangle {

            anchors.fill: parent
            color: "#303030"; radius: 10

            ColumnLayout {

                anchors.fill: parent
                anchors.margins: 20
                spacing: 15

                Label {
                    text: "Cài đặt đường dẫn RTSP"
                    color: "white"
                    font.pixelSize: 22
                    Layout.alignment: Qt.AlignHCenter
                }

                GridLayout {

                    columns: 2
                    rowSpacing: 15
                    columnSpacing: 10
                    Label {
                        text: "So luong man hinh:"
                        color: "white"
                    }

                    ComboBox {
                        id: layoutBox
                        Layout.preferredWidth: 200
                        Layout.preferredHeight: 30
                        model: ["1","4","6"]
                        background: Rectangle {
                                border.color: "#888"
                                radius: 5         // ← rounds all four corners
                            }
                        onActivated: {
                            switch(currentIndex) {
                            case 0:
                                layoutMode = 1
                                break
                            case 1:
                                layoutMode = 4
                                break
                            case 2:
                                layoutMode = 6
                                break
                            }
                        }
                    }
                    Label {text: "Camera 1:"; color: "white"; visible: layoutMode >= 1}
                    TextField {id: url1; Layout.fillWidth: true; text: rtspUrls[0]; visible: layoutMode >= 1}

                    Label {text: "Camera 2:"; color: "white"; visible: layoutMode >= 4}
                    TextField {id: url2; Layout.fillWidth: true; text: rtspUrls[1]; visible: layoutMode >= 4}

                    Label {text: "Camera 3:"; color: "white"; visible: layoutMode >= 4}
                    TextField {id: url3; Layout.fillWidth: true; text: rtspUrls[2]; visible: layoutMode >= 4}

                    Label {text: "Camera 4:"; color: "white"; visible: layoutMode >= 4}
                    TextField {id: url4; Layout.fillWidth: true; text: rtspUrls[3]; visible: layoutMode >= 4}

                    Label {text: "Camera 5:"; color: "white"; visible: layoutMode >= 6}
                    TextField {id: url5; Layout.fillWidth: true; text: rtspUrls[4]; visible: layoutMode >= 6}

                    Label {text: "Camera 6:"; color: "white"; visible: layoutMode >= 6}
                    TextField {id: url6; Layout.fillWidth: true; text: rtspUrls[5]; visible: layoutMode >= 6}

                }

                RowLayout {

                    Layout.alignment: Qt.AlignRight
                    spacing: 20
                    Button {
                        text: "Hủy"
                        background: Rectangle {
                                border.color: "#888"
                                radius: 5         // ← rounds all four corners
                            }
                        onClicked: {
                            settingsPopup.close()
                        }
                    }

                    Button {
                        text: "Áp dụng"
                        background: Rectangle {
                                border.color: "#888"
                                radius: 5         // ← rounds all four corners
                            }
                        onClicked: {
                            rtspUrls = [url1.text,url2.text,url3.text,url4.text, url5.text, url6.text]
                            settingsPopup.close()
                        }
                    }
                }
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // ==========================
        // MENU BAR
        // ==========================
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 50

            color: "#2f4f4f"

            Button {
                id: settingBt
                text: "⚙"
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                anchors.rightMargin: 10
                height: parent.height*2/3
                width: height
                background: Rectangle {
                        implicitWidth: parent.width
                        implicitHeight: parent.height
                        color: settingBt.down ? "#ccc" : "#fff"
                        border.color: "#888"
                        radius: 5         // ← rounds all four corners
                    }
                onClicked: {
                    settingsPopup.open()
                }
            }
            RowLayout {
                anchors.fill: parent
                anchors.margins: 10
                spacing: 20

                RowLayout {

                    Layout.fillWidth: true
                    spacing: 10


                    Repeater {
                        model: layoutMode
                        delegate: ComboBox {
                            Layout.preferredWidth: 150
                            Layout.preferredHeight: 25
                            background: Rectangle {
                                    border.color: "#888"
                                    radius: 5         // ← rounds all four corners
                                }
                            property int displayIndex: index
                            model: [
                                "Camera 1",
                                "Camera 2",
                                "Camera 3",
                                "Camera 4",
                                "Camera 5",
                                "Camera 6"
                            ]
                            displayText: "Màn hình " + (displayIndex + 1)
                            onActivated: {
                                switch(displayIndex) {
                                case 0:
                                    receiver1.connectCamera(
                                        rtspUrls[currentIndex])
                                    receiver1.start()
                                    break
                                case 1:
                                    receiver2.connectCamera(
                                        rtspUrls[currentIndex])
                                    receiver2.start()
                                    break
                                case 2:
                                    receiver3.connectCamera(
                                        rtspUrls[currentIndex])
                                    receiver3.start()
                                    break
                                case 3:
                                    receiver4.connectCamera(
                                        rtspUrls[currentIndex])
                                    receiver4.start()
                                    break
                                case 4:
                                    receiver5.connectCamera(
                                        rtspUrls[currentIndex])
                                    receiver5.start()
                                    break
                                case 5:
                                    receiver6.connectCamera(
                                        rtspUrls[currentIndex])
                                    receiver6.start()
                                    break
                                }
                            }
                        }
                    }
                }

                Item {
                    Layout.fillWidth: true
                }
            }
        }

        // ==========================
        // VIDEO AREA
        // ==========================
        GridLayout {

            id: videoLayout

            Layout.fillWidth: true
            Layout.fillHeight: true

            anchors.margins: 5

            columns: layoutMode === 1 ? 1 : (layoutMode===4 ? 2 : 3)
            rows: layoutMode === 1 ? 1 : (layoutMode===4 ? 2 : 3)

            columnSpacing: 5
            rowSpacing: 5


            VideoItem {
                id: video1
                visible: true
                Layout.fillWidth: visible
                Layout.fillHeight: visible
                Component.onCompleted: {
                    receiver1.setVideoItem(video1)
                }
                Label{
                    anchors.top: parent.top
                    anchors.left: parent.left
                    anchors.margins: 10
                    text: "1"
                    color: "blue"
                }
            }

            VideoItem {
                id: video2
                visible: layoutMode >= 4
                Layout.fillWidth: visible
                Layout.fillHeight: visible
                Component.onCompleted: {
                    receiver2.setVideoItem(video2)
                }
                Label{
                    anchors.top: parent.top
                    anchors.left: parent.left
                    anchors.margins: 10
                    text: "2"
                    color: "blue"
                }
            }

            VideoItem {
                id: video3
                visible: layoutMode >= 4
                Layout.fillWidth: visible
                Layout.fillHeight: visible
                Component.onCompleted: {
                    receiver3.setVideoItem(video3)
                }
                Label{
                    anchors.top: parent.top
                    anchors.left: parent.left
                    anchors.margins: 10
                    text: "3"
                    color: "blue"
                }
            }

            VideoItem {
                id: video4
                visible: layoutMode >= 4
                Layout.fillWidth: visible
                Layout.fillHeight: visible
                Component.onCompleted: {
                    receiver4.setVideoItem(video4)
                }
                Label{
                    anchors.top: parent.top
                    anchors.left: parent.left
                    anchors.margins: 10
                    text: "4"
                    color: "blue"
                }
            }
            VideoItem {
                id: video5
                visible: layoutMode >= 6
                Layout.fillWidth: visible
                Layout.fillHeight: visible
                Component.onCompleted: {
                    receiver5.setVideoItem(video5)
                }
                Label{
                    anchors.top: parent.top
                    anchors.left: parent.left
                    anchors.margins: 10
                    text: "5"
                    color: "blue"
                }
            }
            VideoItem {
                id: video6
                visible: layoutMode >= 6
                Layout.fillWidth: visible
                Layout.fillHeight: visible
                Component.onCompleted: {
                    receiver6.setVideoItem(video6)
                }
                Label{
                    anchors.top: parent.top
                    anchors.left: parent.left
                    anchors.margins: 10
                    text: "6"
                    color: "blue"
                }
            }
        }
    }



    Connections {
        target: receiver1
        function onErrorOccurred(message)
        {
            console.log("Camera 1:", message)
        }
    }


    Connections {
        target: receiver2
        function onErrorOccurred(message)
        {
            console.log("Camera 2:", message)
        }
    }


    Connections {
        target: receiver3
        function onErrorOccurred(message)
        {
            console.log("Camera 3:", message)
        }
    }


    Connections {
        target: receiver4
        function onErrorOccurred(message)
        {
            console.log("Camera 4:", message)
        }
    }

    Connections {
        target: receiver5
        function onErrorOccurred(message)
        {
            console.log("Camera 5:", message)
        }
    }
    Connections {
        target: receiver6
        function onErrorOccurred(message)
        {
            console.log("Camera 6:", message)
        }
    }
}
