// Copyright (C) 2024 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only

#include <QtQuick/private/qquicksafearea_p.h>

#include <QtQuick/private/qquickanchors_p_p.h>
#include <QtQuick/private/qquickitem_p.h>
#include <QtQuick/qquickwindow.h>
#include <QtQuick/qquickitem.h>

QT_BEGIN_NAMESPACE

Q_STATIC_LOGGING_CATEGORY(lcSafeArea, "qt.quick.safearea", QtWarningMsg)

/*!
    \qmltype SafeArea
    \nativetype QQuickSafeArea
    \inqmlmodule QtQuick
    \ingroup qtquick-visual
    \since 6.9
    \brief Provides access to the safe area properties of the item or window.

    The SafeArea attached type provides information about the areas of
    an Item or Window where content may risk being overlapped by other
    UI elements, such as system title bars or status bars.

    This information can be used to lay out children of an item within
    the safe area of the item, while still allowing a background color
    or effect to span the entire item.

    \table
    \row
      \li \snippet qml/safearea/basic.qml 0
      \li \inlineimage safearea-ios.webp
    \endtable

    The SafeArea margins are relative to the item they attach to. If an
    ancestor item has laid out its children within the safe area margins,
    any descendant item with its own SafeArea attached will report zero
    margins, unless \l{Additional margins}{additional margins} have been
    added.

    \note An item should not be positioned based on \e{its own} safe area,
    as that would result in a binding loop.

    \section2 Additional margins

    Sometimes an item's layout involves child items that overlap each other,
    for example in a window with a semi transparent header, where the rest
    of the window content flows underneath the header.

    In this scenario, the item may reflect the header's position and size
    to the child items via the additionalMargins property.

    The additional margins will be combined with any margins that the
    item already picks up from its parent hierarchy (including system
    margins, such as title bars or status bars), and child items will
    reflect the combined margins accordingly.

    \table
    \row
      \li \snippet qml/safearea/additional.qml 0
      \li \br \inlineimage safearea-ios-header.webp
    \endtable

    In the example above, the header item also takes safe area margins
    into account, to ensure there's enough room for its content, even
    when the header is partially covered by a title bar or status bar.

    This will increase the height of the header, which is then
    reflected via the additional safe area margins to children
    of the content item.

    \note In this example the header item does not overlap the child item,
    as the goal is to show how the items are positioned and resized in
    response to safe area margin changes.

    \section2 Controls

    Applying safe area margins to a Control is straightforward,
    as Control already offers properties to add padding to the
    control's content item.

    \snippet qml/safearea/controls.qml 0
 */

QQuickSafeArea *QQuickSafeArea::qmlAttachedProperties(QObject *attachee)
{
    auto *item = qobject_cast<QQuickItem*>(attachee);
    if (!item) {
        if (auto *window = qobject_cast<QQuickWindow*>(attachee))
            item = window->contentItem();
    }
    if (!item) {
        if (auto *safeAreaAttachable = qobject_cast<QQuickSafeAreaAttachable*>(attachee))
            item = safeAreaAttachable->safeAreaAttachmentItem();
    }
    if (!item) {
        qmlWarning(attachee) << "SafeArea can not be attached to this type";
        return nullptr;
    }

    return new QQuickSafeArea(item);
}

QQuickSafeArea::QQuickSafeArea(QQuickItem *item)
    : QObject(item)
{
    qCInfo(lcSafeArea) << "Creating" << this;

    connect(item, &QQuickItem::windowChanged,
            this, &QQuickSafeArea::windowChanged);

    item->setFlag(QQuickItem::ItemObservesViewport);
    QQuickItemPrivate::get(item)->addItemChangeListener(
        this, QQuickItemPrivate::Matrix);

    updateSafeArea();
}

QQuickSafeArea::~QQuickSafeArea()
{
    qCInfo(lcSafeArea) << "Destroying" << this;
}

/*!
    \qmlpropertygroup QtQuick::SafeArea::margins
    \qmlproperty real QtQuick::SafeArea::margins.top
    \qmlproperty real QtQuick::SafeArea::margins.left
    \qmlproperty real QtQuick::SafeArea::margins.right
    \qmlproperty real QtQuick::SafeArea::margins.bottom
    \readonly

    This property holds the safe area margins, relative
    to the attached item.

    \sa additionalMargins
 */
QMarginsF QQuickSafeArea::margins() const
{
    return m_safeAreaMargins;
}

/*!
    \qmlpropertygroup QtQuick::SafeArea::additionalMargins
    \qmlproperty real QtQuick::SafeArea::additionalMargins.top
    \qmlproperty real QtQuick::SafeArea::additionalMargins.left
    \qmlproperty real QtQuick::SafeArea::additionalMargins.right
    \qmlproperty real QtQuick::SafeArea::additionalMargins.bottom

    This property holds the additional safe area margins for the item.

    The resulting safe area margins of the item are the maximum of any
    inherited safe area margins (for example from title bars or status bar)
    and the additional margins applied to the item.

    \sa margins
 */

void QQuickSafeArea::setAdditionalMargins(const QMarginsF &additionalMargins)
{
    if (additionalMargins == m_additionalMargins)
        return;

    m_additionalMargins = additionalMargins;

    emit additionalMarginsChanged();

    auto *attachedItem = qobject_cast<QQuickItem*>(parent());
    QQuickItemPrivate::get(attachedItem)->transformChanged(attachedItem);
}

QMarginsF QQuickSafeArea::additionalMargins() const
{
    return m_additionalMargins;
}

static QMarginsF mapFromItemToItem(QQuickItem *fromItem, const QMarginsF &margins, QQuickItem *toItem)
{
    if (margins.isNull())
        return margins;

    const auto topLeft = toItem->mapFromItem(fromItem,
        QPointF(margins.left(), margins.top()));

    const auto bottomRight = toItem->mapFromItem(fromItem,
        QPointF(fromItem->width() - margins.right(),
                fromItem->height() - margins.bottom()));

    return QMarginsF(
        topLeft.x(), topLeft.y(),
        toItem->width() - bottomRight.x(),
        toItem->height() - bottomRight.y()
    );
}

void QQuickSafeArea::updateSafeArea()
{
    qCDebug(lcSafeArea) << "Updating" << this;

    auto *attachedItem = qobject_cast<QQuickItem*>(parent());

    QMarginsF windowMargins;
    if (auto *window = attachedItem->window()) {
        windowMargins = mapFromItemToItem(window->contentItem(),
            window->safeAreaMargins(), attachedItem);
    }

    QMarginsF additionalMargins;
    for (auto *item = attachedItem; item; item = item->parentItem()) {
        // We attach the safe area to the relevant item for an attachee
        // such as QQuickWindow or QQuickPopup, so we can't go via
        // qmlAttachedPropertiesObject to find the safe area for an
        // item, as the attached object cache is based on the original
        // attachee.
        if (auto *safeArea = item->findChild<QQuickSafeArea*>(Qt::FindDirectChildrenOnly)) {
            additionalMargins = additionalMargins | mapFromItemToItem(item,
                safeArea->additionalMargins(), attachedItem);
        }
    }

    // Combine margins, but make sure they are never negative
    const QMarginsF newMargins = QMarginsF() | windowMargins | additionalMargins;

    if (newMargins != m_safeAreaMargins) {
        qCDebug(lcSafeArea) << "Margins changed from" << m_safeAreaMargins
            << "to" << newMargins
            << "based on window margins" << windowMargins
            << "and additional margins" << additionalMargins;

        m_safeAreaMargins = newMargins;
        emit marginsChanged();
    }
}

void QQuickSafeArea::windowChanged()
{
    updateSafeArea();
}

void QQuickSafeArea::itemTransformChanged(QQuickItem *item, QQuickItem *transformedItem)
{
    Q_UNUSED(item);
    Q_UNUSED(transformedItem);
    updateSafeArea();
}

#ifndef QT_NO_DEBUG_STREAM
QDebug operator<<(QDebug debug, const QQuickSafeArea *safeArea)
{
    QDebugStateSaver saver(debug);
    debug.nospace();

    if (!safeArea) {
        debug << "QQuickSafeArea(nullptr)";
        return debug;
    }

    debug << safeArea->metaObject()->className() << '(' << static_cast<const void *>(safeArea);

    debug << ", attachedItem=" << safeArea->parent();
    debug << ", safeAreaMargins=" << safeArea->m_safeAreaMargins;
    debug << ", additionalMargins=" << safeArea->additionalMargins();

    debug << ')';
    return debug;
}
#endif // QT_NO_DEBUG_STREAM

QQuickSafeAreaAttachable::~QQuickSafeAreaAttachable() = default;

QT_END_NAMESPACE

#include "moc_qquicksafearea_p.cpp"
