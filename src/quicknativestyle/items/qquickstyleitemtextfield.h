// Copyright (C) 2020 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only

#ifndef QQUICKSTYLEITEMTEXTFIELD_H
#define QQUICKSTYLEITEMTEXTFIELD_H

#if 0
#pragma qt_sync_skip_header_check
#endif

#include "qquickstyleitem.h"
#include <QtQuickTemplates2/private/qquicktextfield_p.h>

QT_BEGIN_NAMESPACE

class QQuickStyleItemTextField : public QQuickStyleItem
{
    Q_OBJECT
    QML_NAMED_ELEMENT(TextField)

public:
    QFont styleFont(QQuickItem *control) const override;

protected:
    void connectToControl() const override;
    void paintEvent(QPainter *painter) const override;
    StyleItemGeometry calculateGeometry() override;

private:
    void initStyleOption(QStyleOptionFrame &styleOption) const;
};

QT_END_NAMESPACE

#endif // QQUICKSTYLEITEMTEXTFIELD_H
