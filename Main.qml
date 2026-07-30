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
        "rtsp://100.91.60.11:8554/main.264"
    ]
    Popup {
        id: settingsPopup
        width: 600; height: 350
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
                        text: "Camera 1:"
                        color: "white"
                    }

                    TextField {
                        id: url1
                        Layout.fillWidth: true
                        text: rtspUrls[0]
                    }

                    Label {
                        text: "Camera 2:"
                        color: "white"
                    }

                    TextField {
                        id: url2
                        Layout.fillWidth: true
                        text: rtspUrls[1]
                    }

                    Label {
                        text: "Camera 3:"
                        color: "white"
                    }

                    TextField {
                        id: url3
                        Layout.fillWidth: true
                        text: rtspUrls[2]
                    }

                    Label {
                        text: "Camera 4:"
                        color: "white"
                    }

                    TextField {
                        id: url4
                        Layout.fillWidth: true
                        text: rtspUrls[3]
                    }

                }

                RowLayout {

                    Layout.alignment: Qt.AlignRight
                    spacing: 20
                    Button {
                        text: "Hủy"
                        onClicked: {
                            settingsPopup.close()
                        }
                    }

                    Button {
                        text: "Áp dụng"
                        onClicked: {
                            rtspUrls = [url1.text,url2.text,url3.text,url4.text]
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
            Layout.preferredHeight: 60

            color: "#404040"

            Button {
                text: "⚙"
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                anchors.rightMargin: 10
                height: parent.height/2
                width: height

                onClicked: {
                    settingsPopup.open()
                }
            }
            RowLayout {
                anchors.fill: parent
                anchors.margins: 10
                spacing: 20

                ComboBox {
                    id: layoutBox
                    Layout.preferredWidth: 150
                    model: ["1 Màn hình","2 Màn hình","4 Màn hình"]
                    onActivated: {
                        switch(currentIndex) {
                        case 0:
                            layoutMode = 1
                            break
                        case 1:
                            layoutMode = 2
                            break
                        case 2:
                            layoutMode = 4
                            break
                        }
                    }
                }

                RowLayout {

                    Layout.fillWidth: true
                    spacing: 10


                    Repeater {
                        model: layoutMode
                        delegate: ComboBox {
                            Layout.preferredWidth: 150
                            property int displayIndex: index
                            model: [
                                "Camera 1",
                                "Camera 2",
                                "Camera 3",
                                "Camera 4"
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

            columns: layoutMode === 1 ? 1 : 2
            rows: layoutMode === 4 ? 2 : 1

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
            }

            VideoItem {
                id: video2
                visible: layoutMode >= 2
                Layout.fillWidth: visible
                Layout.fillHeight: visible
                Component.onCompleted: {
                    receiver2.setVideoItem(video2)
                }
            }

            VideoItem {
                id: video3
                visible: layoutMode === 4
                Layout.fillWidth: visible
                Layout.fillHeight: visible
                Component.onCompleted: {
                    receiver3.setVideoItem(video3)
                }
            }

            VideoItem {
                id: video4
                visible: layoutMode === 4
                Layout.fillWidth: visible
                Layout.fillHeight: visible
                Component.onCompleted: {
                    receiver4.setVideoItem(video4)
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
}