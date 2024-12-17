import QtQuick
import QtQuick.Controls

ApplicationWindow {
    id: window
    width: 600
    height: 400

    signal eventReceived(QtObject receiver)

    contentItem.ContextMenu.menu: Menu {
        objectName: "menu"
        onOpened: window.eventReceived(this)

        MenuItem {
            text: qsTr("Eat tomato")
        }
        MenuItem {
            text: qsTr("Throw tomato")
        }
        MenuItem {
            text: qsTr("Squash tomato")
        }
    }

    TapHandler {
        objectName: "tapHandler"
        acceptedButtons: Qt.RightButton
        // Ensure that it grabs mouse on press, as the default gesture policy results in a passive grab,
        // which would allow the ContextMenu to receive the context menu event.
        gesturePolicy: TapHandler.WithinBounds
        onTapped: window.eventReceived(this)
    }
}
