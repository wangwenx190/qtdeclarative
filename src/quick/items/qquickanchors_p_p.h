// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only

#ifndef QQUICKANCHORS_P_P_H
#define QQUICKANCHORS_P_P_H

//
//  W A R N I N G
//  -------------
//
// This file is not part of the Qt API.  It exists purely as an
// implementation detail.  This header file may change from version to
// version without notice, or even be removed.
//
// We mean it.
//

#include "qquickanchors_p.h"
#include "qquickitemchangelistener_p.h"
#include <private/qobject_p.h>

QT_BEGIN_NAMESPACE

class Q_QUICK_EXPORT QQuickAnchorLine
{
    Q_GADGET
    QML_ANONYMOUS
    QML_ADDED_IN_VERSION(2, 0)
public:
    QQuickAnchorLine() {}
    QQuickAnchorLine(QQuickItem *i, QQuickAnchors::Anchor l) : item(i), anchorLine(l) {}
    QQuickAnchorLine(QQuickItem *i, uint l)
        : item(i)
        , anchorLine(static_cast<QQuickAnchors::Anchor>(l))
    { Q_ASSERT(l < ((QQuickAnchors::BaselineAnchor << 1) - 1)); }

    QQuickItem *item = nullptr;
    QQuickAnchors::Anchor anchorLine = QQuickAnchors::InvalidAnchor;
};

inline bool operator==(const QQuickAnchorLine& a, const QQuickAnchorLine& b)
{
    return a.item == b.item && a.anchorLine == b.anchorLine;
}

class QQuickAnchorsPrivate : public QObjectPrivate,
                             public QSafeQuickItemChangeListener<QQuickAnchorsPrivate>
{
    Q_DECLARE_PUBLIC(QQuickAnchors)
public:
    QQuickAnchorsPrivate(QQuickItem *i)
        : leftMargin(0)
        , rightMargin(0)
        , topMargin(0)
        , bottomMargin(0)
        , margins(0)
        , vCenterOffset(0)
        , hCenterOffset(0)
        , baselineOffset(0)
        , item(i)
        , fill(nullptr)
        , centerIn(nullptr)
        , leftAnchorItem(nullptr)
        , rightAnchorItem(nullptr)
        , topAnchorItem(nullptr)
        , bottomAnchorItem(nullptr)
        , vCenterAnchorItem(nullptr)
        , hCenterAnchorItem(nullptr)
        , baselineAnchorItem(nullptr)
        , leftAnchorLine(QQuickAnchors::InvalidAnchor)
        , leftMarginExplicit(false)
        , rightAnchorLine(QQuickAnchors::InvalidAnchor)
        , rightMarginExplicit(false)
        , topAnchorLine(QQuickAnchors::InvalidAnchor)
        , topMarginExplicit(false)
        , bottomAnchorLine(QQuickAnchors::InvalidAnchor)
        , bottomMarginExplicit(false)
        , vCenterAnchorLine(QQuickAnchors::InvalidAnchor)
        , updatingMe(false)
        , hCenterAnchorLine(QQuickAnchors::InvalidAnchor)
        , inDestructor(false)
        , baselineAnchorLine(QQuickAnchors::InvalidAnchor)
        , centerAligned(true)
        , usedAnchors(QQuickAnchors::InvalidAnchor)
        , componentComplete(true)
        , updatingFill(0)
        , updatingCenterIn(0)
        , updatingHorizontalAnchor(0)
        , updatingVerticalAnchor(0)
    {
    }

    void clearItem(QQuickItem *);

    QQuickGeometryChange calculateDependency(QQuickItem *) const;
    void addDepend(QQuickItem *);
    void remDepend(QQuickItem *);
    bool isItemComplete() const;

    void setItemHeight(qreal);
    void setItemWidth(qreal);
    void setItemX(qreal);
    void setItemY(qreal);
    void setItemPos(const QPointF &);
    void setItemSize(const QSizeF &);

    void update();
    void updateOnComplete();
    void updateMe();

    // QQuickItemGeometryListener interface
    void itemGeometryChanged(QQuickItem *, QQuickGeometryChange, const QRectF &) override;
    QQuickAnchorsPrivate *anchorPrivate() override { return this; }

    bool checkHValid() const;
    bool checkVValid() const;
    bool checkHAnchorValid(QQuickAnchorLine anchor) const;
    bool checkVAnchorValid(QQuickAnchorLine anchor) const;
    bool calcStretch(QQuickItem *edge1Item, QQuickAnchors::Anchor edge1Line,
                     QQuickItem *edge2Item, QQuickAnchors::Anchor edge2Line,
                     qreal offset1, qreal offset2, QQuickAnchors::Anchor line, qreal &stretch) const;

    bool isMirrored() const;
    void updateHorizontalAnchors();
    void updateVerticalAnchors();
    void fillChanged();
    void centerInChanged();

    qreal leftMargin;
    qreal rightMargin;
    qreal topMargin;
    qreal bottomMargin;
    qreal margins;
    qreal vCenterOffset;
    qreal hCenterOffset;
    qreal baselineOffset;

    QQuickItem *item;

    QQuickItem *fill;
    QQuickItem *centerIn;

    QQuickItem *leftAnchorItem;
    QQuickItem *rightAnchorItem;
    QQuickItem *topAnchorItem;
    QQuickItem *bottomAnchorItem;
    QQuickItem *vCenterAnchorItem;
    QQuickItem *hCenterAnchorItem;
    QQuickItem *baselineAnchorItem;

    // The bit fields below are carefully laid out in chunks of 1 byte, so the compiler doesn't
    // need to generate 2 loads (and combining shifts/ors) to create a single field.

    QQuickAnchors::Anchor leftAnchorLine     : 7;
    uint leftMarginExplicit                  : 1;
    QQuickAnchors::Anchor rightAnchorLine    : 7;
    uint rightMarginExplicit                 : 1;
    QQuickAnchors::Anchor topAnchorLine      : 7;
    uint topMarginExplicit                   : 1;
    QQuickAnchors::Anchor bottomAnchorLine   : 7;
    uint bottomMarginExplicit                : 1;

    QQuickAnchors::Anchor vCenterAnchorLine  : 7;
    uint updatingMe                          : 1;
    QQuickAnchors::Anchor hCenterAnchorLine  : 7;
    uint inDestructor                        : 1;
    QQuickAnchors::Anchor baselineAnchorLine : 7;
    uint centerAligned                       : 1;
    uint usedAnchors                         : 7; // QQuickAnchors::Anchors
    uint componentComplete                   : 1;

    // Instead of using a mostly empty bit field, we can stretch the following fields up to be full
    // bytes. The advantage is that incrementing/decrementing does not need any combining ands/ors.
    qint8 updatingFill;
    qint8 updatingCenterIn;
    qint8 updatingHorizontalAnchor;
    qint8 updatingVerticalAnchor;


    static inline QQuickAnchorsPrivate *get(QQuickAnchors *o) {
        return static_cast<QQuickAnchorsPrivate *>(QObjectPrivate::get(o));
    }
};

QT_END_NAMESPACE

Q_DECLARE_METATYPE(QQuickAnchorLine)

#endif
