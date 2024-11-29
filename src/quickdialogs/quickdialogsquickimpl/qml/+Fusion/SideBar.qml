// Copyright (C) 2024 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only

import QtQuick
import QtQuick.Controls.Fusion
import QtQuick.Controls.impl
import QtQuick.Dialogs.quickimpl as DialogsQuickImpl

DialogsQuickImpl.SideBar {
    id: control

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding)

    contentItem: ListView {
        id: listView
        currentIndex: control.currentIndex
        model: control.contentModel
        clip: true

        Rectangle {
            anchors.fill: parent
            color: control.palette.light
            border.color: control.palette.mid
            radius: 2
            z: -1
        }
    }

    buttonDelegate: Button {
        id: buttonDelegateRoot
        flat: true
        highlighted: control.currentIndex === index
        width: listView.width

        contentItem: IconLabel {
            spacing: 5
            leftPadding: 10
            topPadding: 3
            bottomPadding: 3
            icon: buttonDelegateRoot.icon
            text: buttonDelegateRoot.folderName
            font: buttonDelegateRoot.font
            alignment: Qt.AlignLeft
        }

        required property int index
        required property string folderName
        required icon
    }
}
