import QtQuick

Window {
    visible: true
    flags: Qt.FramelessWindowHint
    width: 500; height: 500

    Item {
        objectName: "bindingLoopItem"
        x: SafeArea.margins.left
    }

    signal itemWidthChanged

    onWidthChanged: itemWidthChanged()
}
