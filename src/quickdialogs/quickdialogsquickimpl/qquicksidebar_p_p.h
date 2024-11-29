// Copyright (C) 2024 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only

#ifndef QQUICKSIDEBAR_P_P_H
#define QQUICKSIDEBAR_P_P_H

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

#include <QtQuickTemplates2/private/qquickcontainer_p_p.h>
#include <QtCore/qstandardpaths.h>

QT_BEGIN_NAMESPACE

class Q_QUICKDIALOGS2QUICKIMPL_EXPORT QQuickSideBarPrivate : public QQuickContainerPrivate
{
public:
    Q_DECLARE_PUBLIC(QQuickSideBar)

    static QQuickSideBarPrivate *get(QQuickSideBar *sidebar)
    {
        return sidebar->d_func();
    }

    void repopulate();
    void buttonClicked();
    QQuickItem *createDelegateItem(QQmlComponent *component, const QVariantMap &initialProperties);

    QUrl dialogFolder() const;
    void setDialogFolder(const QUrl &folder);
    QString displayNameFromFolderPath(const QString &filePath);
    QQuickIcon getFolderIcon(QStandardPaths::StandardLocation stdLocation) const;
    void folderChanged();

private:
    QQuickDialog *dialog = nullptr;
    QQmlComponent *buttonDelegate = nullptr;
    QList<QStandardPaths::StandardLocation> folderPaths;
    QUrl currentButtonClickedUrl;
    bool repopulating = false;
};

QT_END_NAMESPACE

#endif // QQUICKSIDEBAR_P_P_H
