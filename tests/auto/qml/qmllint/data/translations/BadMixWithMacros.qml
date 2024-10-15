import QtQuick

Item {
    property string qsTrNoop: QT_TR_NOOP("hello")
    property string qsTrIdString: qsTrId(qsTrNoop)
}
