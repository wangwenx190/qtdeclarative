// Copyright (C) 2024 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only

#ifndef QQUICKSIDEBAR_P_H
#define QQUICKSIDEBAR_P_H

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

#include <QtQml/qqmlcomponent.h>
#include <QtQuickTemplates2/private/qquickcontainer_p.h>
#include <QtCore/qstandardpaths.h>

#include "qquickfiledialogimpl_p.h"

QT_BEGIN_NAMESPACE

class QQuickSideBarPrivate;

class Q_QUICKDIALOGS2QUICKIMPL_EXPORT QQuickSideBar : public QQuickContainer
{
    Q_OBJECT
    Q_PROPERTY(QQuickDialog *dialog READ dialog WRITE setDialog NOTIFY dialogChanged)
    Q_PROPERTY(QList<QStandardPaths::StandardLocation> folderPaths READ folderPaths WRITE setFolderPaths NOTIFY folderPathsChanged)
    Q_PROPERTY(QQmlComponent *buttonDelegate READ buttonDelegate WRITE setButtonDelegate NOTIFY buttonDelegateChanged)
    QML_NAMED_ELEMENT(SideBar)
    QML_ADDED_IN_VERSION(6, 9)

public:
    explicit QQuickSideBar(QQuickItem *parent = nullptr);
    ~QQuickSideBar();

    QQuickDialog *dialog() const;
    void setDialog(QQuickDialog *dialog);

    QList<QStandardPaths::StandardLocation> folderPaths() const;
    void setFolderPaths(const QList<QStandardPaths::StandardLocation>& folderPaths);

    QQmlComponent *buttonDelegate();
    void setButtonDelegate(QQmlComponent *delegate);

Q_SIGNALS:
    void dialogChanged();
    void folderPathsChanged();
    void buttonDelegateChanged();

protected:
    void componentComplete() override;

private:
    Q_DISABLE_COPY(QQuickSideBar)
    Q_DECLARE_PRIVATE(QQuickSideBar)
};

QT_END_NAMESPACE

#endif // QQUICKSIDEBAR_P_H
