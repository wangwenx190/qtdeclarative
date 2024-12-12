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

    // We may already have created a safe area for Window, and are now
    // requesting one for Window.contentItem (or the other way around).
    // As both map to the same safe area item, we need to check first
    // if we already have created one for this item.
    if (auto *safeArea = item->findChild<QQuickSafeArea*>(Qt::FindDirectChildrenOnly))
        return safeArea;

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
    updateSafeAreasRecursively(attachedItem);
}

QMarginsF QQuickSafeArea::additionalMargins() const
{
    return m_additionalMargins;
}

/*
    Maps the safe area \a margins from \a fromItem to \a toItem
*/
static QMarginsF toLocalMargins(const QMarginsF &margins, QQuickItem *fromItem, QQuickItem *toItem)
{
    if (margins.isNull())
        return margins;

    const auto localMarginRect = fromItem->mapRectToItem(toItem,
        QRectF(margins.left(), margins.top(),
               fromItem->width() - margins.left() - margins.right(),
               fromItem->height() - margins.top() - margins.bottom()));

    // Only return a mapped margin if there was an original margin
    return QMarginsF(
        margins.left() > 0 ? localMarginRect.left() : 0,
        margins.top() > 0 ? localMarginRect.top() : 0,
        margins.right() > 0 ? toItem->width() - localMarginRect.right() : 0,
        margins.bottom() > 0 ? toItem->height() - localMarginRect.bottom() : 0
    );
}

void QQuickSafeArea::updateSafeArea()
{
    qCDebug(lcSafeArea) << "Updating" << this;

    auto *attachedItem = qobject_cast<QQuickItem*>(parent());
    if (!QQuickItemPrivate::get(attachedItem)->componentComplete) {
        qCDebug(lcSafeArea) << attachedItem << "is not complete. Deferring";
        return;
    }

    QMarginsF windowMargins;
    if (auto *window = attachedItem->window()) {
        windowMargins = toLocalMargins(window->safeAreaMargins(),
            window->contentItem(), attachedItem);
    }

    QMarginsF additionalMargins;
    for (auto *item = attachedItem; item; item = item->parentItem()) {
        // We attach the safe area to the relevant item for an attachee
        // such as QQuickWindow or QQuickPopup, so we can't go via
        // qmlAttachedPropertiesObject to find the safe area for an
        // item, as the attached object cache is based on the original
        // attachee.
        if (auto *safeArea = item->findChild<QQuickSafeArea*>(Qt::FindDirectChildrenOnly)) {
            additionalMargins = additionalMargins | toLocalMargins(
                safeArea->additionalMargins(), item, attachedItem);
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

        if (emittingMarginsUpdate) {
            // We are already in the process of emitting an update for this
            // safe area, which resulted in the safe area margins changing.
            // This can be a binding loop if the margins do not stabilize,
            // which we'll detect when we return from the root emit below.
            qCDebug(lcSafeArea) << "Already emitting update for" << this;
            return;
        }

        QBoolBlocker blocker(emittingMarginsUpdate, true);
        emit marginsChanged();

        if (m_safeAreaMargins != newMargins) {
            qCDebug(lcSafeArea) << "⚠️ Possible binding loop for" << this
                << newMargins << "changed to" << m_safeAreaMargins;

            for (int i = 0; i < 5; ++i) {
                auto marginsBeforeEmit = m_safeAreaMargins;
                emit marginsChanged();
                if (m_safeAreaMargins == marginsBeforeEmit) {
                    qCDebug(lcSafeArea) << "✅ Margins stabilized for" << this;
                    return;
                }

                qCDebug(lcSafeArea) << qPrintable(QStringLiteral("‼️").repeated(i + 1))
                    << marginsBeforeEmit << "changed to" << m_safeAreaMargins;
            }

            qmlWarning(attachedItem) << "Safe area binding loop detected";
        }
    }
}

void QQuickSafeArea::windowChanged()
{
    updateSafeArea();
}

void QQuickSafeArea::itemTransformChanged(QQuickItem *item, QQuickItem *transformedItem)
{
    Q_ASSERT(item == parent());

    auto *transformedItemPrivate = QQuickItemPrivate::get(transformedItem);
    qCDebug(lcSafeArea) << "Transform changed for" << transformedItem
                        << "with dirty state" << transformedItemPrivate->dirtyToString();

    // The dirtying of position and size will be followed by a geometry change,
    // which via anchors or event listeners may result in an ancestor invalidating
    // its transform, which might invalidate the margins we're about to compute.
    // Instead of processing the margin change now, possibly resulting in a flip-
    // flop of the margins, we wait for the geometry notification, where the item
    // hierarchy has already reacted to the geometry change of the transformed item.
    // This accounts for anchors, and items that listen to geometry changes, but not
    // property bindings, as those are emitted after notifying listeners (us) about
    // the geometry change. We intentionally limit this optimization to pure size
    // and/or position changes, and only if the transformed item is an ancestor
    // to the one the safe area is attached to.
    if (transformedItem != item) {
        auto dirtyAttributes = transformedItemPrivate->dirtyAttributes;
        if (dirtyAttributes == (QQuickItemPrivate::Position | QQuickItemPrivate::Size)
            || dirtyAttributes == QQuickItemPrivate::Position
            || dirtyAttributes == QQuickItemPrivate::Size) {
            qCDebug(lcSafeArea) << "Deferring update of" << this << "until geometry change";
            transformedItemPrivate->addItemChangeListener(
                this, QQuickItemPrivate::Geometry);
            return;
        }
    }
    updateSafeArea();
}

void QQuickSafeArea::itemGeometryChanged(QQuickItem *item, QQuickGeometryChange change, const QRectF &oldGeometry)
{
    Q_UNUSED(change);
    Q_UNUSED(oldGeometry);

    auto *itemPrivate = QQuickItemPrivate::get(item);
    itemPrivate->removeItemChangeListener(this, QQuickItemPrivate::Geometry);

    qCDebug(lcSafeArea) << "Geometry changed for" << item << "from" << oldGeometry
                        << "to" << QRectF(item->position(), item->size());
    updateSafeArea();
}

void QQuickSafeArea::updateSafeAreasRecursively(QQuickItem *item)
{
    Q_ASSERT(item);

    if (auto *safeArea = item->findChild<QQuickSafeArea*>(Qt::FindDirectChildrenOnly))
        safeArea->updateSafeArea();

    auto *itemPrivate = QQuickItemPrivate::get(item);
    const auto paintOrderChildItems = itemPrivate->paintOrderChildItems();
    for (auto *child : paintOrderChildItems)
        updateSafeAreasRecursively(child);
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
