// Copyright (C) 2024 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only

#include "qquicksidebar_p.h"
#include "qquicksidebar_p_p.h"
#include "qquickfiledialogimpl_p_p.h"

/*!
    \internal

 Private class for sidebar in a file/folder dialog.

 Given a FileDialog, SideBar creates a ListView containing delegate buttons that navigate to
 standard paths. It is planned to add a favorites section that includes drag and drop
 functionality in the future.

*/

using namespace Qt::Literals::StringLiterals;

QList<QStandardPaths::StandardLocation> effectiveFolderPaths()
{
    constexpr QStandardPaths::StandardLocation defaultPaths[] = {
        QStandardPaths::HomeLocation,     QStandardPaths::DesktopLocation,
        QStandardPaths::DownloadLocation, QStandardPaths::DocumentsLocation,
        QStandardPaths::MusicLocation,    QStandardPaths::PicturesLocation,
        QStandardPaths::MoviesLocation,
    };

    QList<QStandardPaths::StandardLocation> effectivePaths = { QStandardPaths::HomeLocation };

    // The home location is never returned as empty
    QString homeLocation = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
    for (int i = 1; i < 7; ++i)
        // if a standard path is not found, it will be resolved to home location
        if (QStandardPaths::writableLocation(defaultPaths[i]) != homeLocation)
            effectivePaths.append(defaultPaths[i]);
    return effectivePaths;
}

QQuickItem *QQuickSideBarPrivate::createDelegateItem(QQmlComponent *component,
                                                     const QVariantMap &initialProperties)
{
    Q_Q(QQuickSideBar);
    // If we don't use the correct context, it won't be possible to refer to
    // the control's id from within the delegates.
    QQmlContext *context = component->creationContext();
    // The component might not have been created in QML, in which case
    // the creation context will be null and we have to create it ourselves.
    if (!context)
        context = qmlContext(q);

    // If we have initial properties we assume that all necessary information is passed via
    // initial properties.
    if (!component->isBound() && initialProperties.isEmpty()) {
        context = new QQmlContext(context, q);
        context->setContextObject(q);
    }

    QQuickItem *item = qobject_cast<QQuickItem *>(
            component->createWithInitialProperties(initialProperties, context));
    if (item)
        QQml_setParent_noEvent(item, q);
    return item;
}

void QQuickSideBarPrivate::repopulate()
{
    Q_Q(QQuickSideBar);

    if (repopulating || !buttonDelegate || !q->contentItem())
        return;

    QBoolBlocker repopulateGuard(repopulating);

    auto createButtonDelegate = [this, q](int i, const QString &folderPath, const QQuickIcon& icon) {
        const QString displayName = displayNameFromFolderPath(folderPath);
        QVariantMap initialProperties = {
            { "index"_L1, QVariant::fromValue(i) },
            { "folderName"_L1, QVariant::fromValue(displayName) },
            { "icon"_L1, QVariant::fromValue(icon) },
        };

        if (QQuickItem *buttonItem = createDelegateItem(buttonDelegate, initialProperties)) {
            if (QQuickAbstractButton *button = qobject_cast<QQuickAbstractButton *>(buttonItem))
                QObjectPrivate::connect(button, &QQuickAbstractButton::clicked, this,
                                        &QQuickSideBarPrivate::buttonClicked);
            insertItem(q->count(), buttonItem);
        }
    };

    // clean up
    while (q->count() > 0)
        q->removeItem(q->itemAt(0));

    // repopulate
    for (int i = 0; i < folderPaths.size(); ++i)
        createButtonDelegate(i, QStandardPaths::displayName(folderPaths.at(i)), getFolderIcon(folderPaths.at(i)));

    q->setCurrentIndex(-1);
}

void QQuickSideBarPrivate::buttonClicked()
{
    Q_Q(QQuickSideBar);
    if (QQuickAbstractButton *button = qobject_cast<QQuickAbstractButton *>(q->sender())) {
        const int buttonIndex = contentModel->indexOf(button, nullptr);
        q->setCurrentIndex(buttonIndex);

        currentButtonClickedUrl = QUrl::fromLocalFile(
                    QStandardPaths::writableLocation(folderPaths.at(buttonIndex)));

        currentButtonClickedUrl.setScheme("file"_L1);
        setDialogFolder(currentButtonClickedUrl);
    }
}

QQuickSideBar::QQuickSideBar(QQuickItem *parent)
    : QQuickContainer(*(new QQuickSideBarPrivate), parent)
{
    Q_D(QQuickSideBar);
    d->folderPaths = effectiveFolderPaths();

    QObject::connect(this, &QQuickContainer::currentIndexChanged, [d](){
        d->currentButtonClickedUrl.clear();
    });
}

QQuickSideBar::~QQuickSideBar()
{
    this->disconnect();
}

QQuickDialog *QQuickSideBar::dialog() const
{
    Q_D(const QQuickSideBar);
    return d->dialog;
}

void QQuickSideBar::setDialog(QQuickDialog *dialog)
{
    Q_D(QQuickSideBar);
    if (dialog == d->dialog)
        return;

    if (auto fileDialog = qobject_cast<QQuickFileDialogImpl *>(d->dialog)) {
        QObjectPrivate::disconnect(fileDialog, &QQuickFileDialogImpl::currentFolderChanged, d,
                                   &QQuickSideBarPrivate::folderChanged);
    }

    d->dialog = dialog;

    if (auto fileDialog = qobject_cast<QQuickFileDialogImpl *>(d->dialog)) {
        QObjectPrivate::connect(fileDialog, &QQuickFileDialogImpl::currentFolderChanged, d,
                                &QQuickSideBarPrivate::folderChanged);
    }

    emit dialogChanged();
}

QList<QStandardPaths::StandardLocation> QQuickSideBar::folderPaths() const
{
    Q_D(const QQuickSideBar);
    return d->folderPaths;
}

void QQuickSideBar::setFolderPaths(const QList<QStandardPaths::StandardLocation> &folderPaths)
{
    Q_D(QQuickSideBar);
    if (folderPaths == d->folderPaths)
        return;

    d->folderPaths = folderPaths;
    emit folderPathsChanged();

    d->repopulate();
}

QQmlComponent *QQuickSideBar::buttonDelegate()
{
    Q_D(QQuickSideBar);
    return d->buttonDelegate;
}

void QQuickSideBar::setButtonDelegate(QQmlComponent *delegate)
{
    Q_D(QQuickSideBar);
    if (d->componentComplete) {
        // Simplify the code by disallowing this.
        return;
    }

    if (delegate == d->buttonDelegate)
        return;

    d->buttonDelegate = delegate;
    emit buttonDelegateChanged();
}

void QQuickSideBarPrivate::folderChanged()
{
    Q_Q(QQuickSideBar);

    if (dialog->property("currentFolder").toUrl() != currentButtonClickedUrl)
        q->setCurrentIndex(-1);
}

QString QQuickSideBarPrivate::displayNameFromFolderPath(const QString &folderPath)
{
    return folderPath.section(QLatin1Char('/'), -1);
}

QUrl QQuickSideBarPrivate::dialogFolder() const
{
    return dialog->property("currentFolder").toUrl();
}

void QQuickSideBarPrivate::setDialogFolder(const QUrl &folder)
{
    Q_Q(QQuickSideBar);
    if (!dialog->setProperty("currentFolder", folder))
        qmlWarning(q) << "Failed to set currentFolder property of dialog" << dialog->objectName()
                      << "to" << folder;
}

void QQuickSideBar::componentComplete()
{
    Q_D(QQuickSideBar);
    QQuickContainer::componentComplete();
    d->repopulate();
}

QQuickIcon QQuickSideBarPrivate::getFolderIcon(QStandardPaths::StandardLocation stdLocation) const
{
    QQuickIcon icon;
    switch (stdLocation) {
    case QStandardPaths::DesktopLocation:
        icon.setSource(QUrl("../images/sidebar-desktop.png"_L1));
        break;
    case QStandardPaths::DocumentsLocation:
        icon.setSource(QUrl("../images/sidebar-documents.png"_L1));
        break;
    case QStandardPaths::MusicLocation:
        icon.setSource(QUrl("../images/sidebar-music.png"_L1));
        break;
    case QStandardPaths::MoviesLocation:
        icon.setSource(QUrl("../images/sidebar-video.png"_L1));
        break;
    case QStandardPaths::PicturesLocation:
        icon.setSource(QUrl("../images/sidebar-photo.png"_L1));
        break;
    case QStandardPaths::HomeLocation:
        icon.setSource(QUrl("../images/sidebar-home.png"_L1));
        break;
    case QStandardPaths::DownloadLocation:
        icon.setSource(QUrl("../images/sidebar-downloads.png"_L1));
        break;
    default:
        icon.setSource(QUrl("../images/sidebar-folder.png"_L1));
        break;
    }
    icon.setWidth(16);
    icon.setHeight(16);
    return icon;
}
