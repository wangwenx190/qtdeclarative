import QtQuick

Item {
    Item {
        property int magic: parent.x // ok: accessing parent from non-root component
    }
}
