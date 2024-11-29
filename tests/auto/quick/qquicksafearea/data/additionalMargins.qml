import QtQuick

Window {
    visible: true
    flags: Qt.FramelessWindowHint
    width: 500; height: 500

    property var margins: SafeArea.margins

    Item {
        objectName: "additionalItem"
        anchors.fill: parent
        SafeArea.additionalMargins {
            left: 20; top: 10
            right: 40; bottom: 30
        }
        property var margins: SafeArea.margins

        Item {
            objectName: "additionalChild"
            anchors {
                fill: parent; margins: 3
            }
            property var margins: SafeArea.margins
        }
    }

    Item {
        objectName: "additionalSibling"
        anchors.fill: parent
        property var margins: SafeArea.margins
    }
}
