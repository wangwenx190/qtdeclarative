/****************************************************************************
**
** Copyright (C) 2011 Nokia Corporation and/or its subsidiary(-ies).
** All rights reserved.
** Contact: Nokia Corporation (qt-info@nokia.com)
**
** This file is part of the test suite of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:LGPL$
** GNU Lesser General Public License Usage
** This file may be used under the terms of the GNU Lesser General Public
** License version 2.1 as published by the Free Software Foundation and
** appearing in the file LICENSE.LGPL included in the packaging of this
** file. Please review the following information to ensure the GNU Lesser
** General Public License version 2.1 requirements will be met:
** http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
**
** In addition, as a special exception, Nokia gives you certain additional
** rights. These rights are described in the Nokia Qt LGPL Exception
** version 1.1, included in the file LGPL_EXCEPTION.txt in this package.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU General
** Public License version 3.0 as published by the Free Software Foundation
** and appearing in the file LICENSE.GPL included in the packaging of this
** file. Please review the following information to ensure the GNU General
** Public License version 3.0 requirements will be met:
** http://www.gnu.org/copyleft/gpl.html.
**
** Other Usage
** Alternatively, this file may be used in accordance with the terms and
** conditions contained in a signed written agreement between you and Nokia.
**
**
**
**
**
** $QT_END_LICENSE$
**
****************************************************************************/
#include <QSignalSpy>
#include <QEventLoop>
#include <QTimer>

#include <private/qdeclarativedebugclient_p.h>
#include <private/qdeclarativedebugservice_p.h>

#include "debugutil_p.h"


bool QDeclarativeDebugTest::waitForSignal(QObject *receiver, const char *member, int timeout) {
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&timer, SIGNAL(timeout()), &loop, SLOT(quit()));
    QObject::connect(receiver, member, &loop, SLOT(quit()));
    timer.start(timeout);
    loop.exec();
    return timer.isActive();
}

QDeclarativeDebugTestService::QDeclarativeDebugTestService(const QString &s, QObject *parent)
    : QDeclarativeDebugService(s, parent)
{
}

void QDeclarativeDebugTestService::messageReceived(const QByteArray &ba)
{
    sendMessage(ba);
}

void QDeclarativeDebugTestService::statusChanged(Status)
{
    emit statusHasChanged();
}


QDeclarativeDebugTestClient::QDeclarativeDebugTestClient(const QString &s, QDeclarativeDebugConnection *c)
    : QDeclarativeDebugClient(s, c)
{
}

QByteArray QDeclarativeDebugTestClient::waitForResponse()
{
    lastMsg.clear();
    QDeclarativeDebugTest::waitForSignal(this, SIGNAL(serverMessage(QByteArray)));
    if (lastMsg.isEmpty()) {
        qWarning() << "tst_QDeclarativeDebugClient: no response from server!";
        return QByteArray();
    }
    return lastMsg;
}

void QDeclarativeDebugTestClient::statusChanged(Status stat)
{
    QCOMPARE(stat, status());
    emit statusHasChanged();
}

void QDeclarativeDebugTestClient::messageReceived(const QByteArray &ba)
{
    lastMsg = ba;
    emit serverMessage(ba);
}

QDeclarativeDebugProcess::QDeclarativeDebugProcess(const QString &executable)
    : m_executable(executable)
    , m_started(false)
{
    m_process.setProcessChannelMode(QProcess::MergedChannels);
    m_timer.setSingleShot(true);
    m_timer.setInterval(5000);
    connect(&m_process, SIGNAL(readyReadStandardOutput()), this, SLOT(processAppOutput()));
    connect(&m_timer, SIGNAL(timeout()), &m_eventLoop, SLOT(quit()));
}

QDeclarativeDebugProcess::~QDeclarativeDebugProcess()
{
    stop();
}

void QDeclarativeDebugProcess::start(const QStringList &arguments)
{
    m_mutex.lock();
    m_process.start(m_executable, arguments);
    m_process.waitForStarted();
    m_timer.start();
    m_mutex.unlock();
}

void QDeclarativeDebugProcess::stop()
{
    if (m_process.state() != QProcess::NotRunning) {
        m_process.terminate();
        m_process.waitForFinished(5000);
    }
}

bool QDeclarativeDebugProcess::waitForSessionStart()
{
    if (m_process.state() != QProcess::Running) {
        qWarning() << "Could not start up " << m_executable;
        return false;
    }
    m_eventLoop.exec(QEventLoop::ExcludeUserInputEvents);

    return m_started;
}

QString QDeclarativeDebugProcess::output() const
{
    return m_outputBuffer;
}

void QDeclarativeDebugProcess::processAppOutput()
{
    m_mutex.lock();
    const QString appOutput = m_process.readAll();
    static QRegExp newline("[\n\r]{1,2}");
    QStringList lines = appOutput.split(newline);
    foreach (const QString &line, lines) {
        if (line.isEmpty())
            continue;
        if (line.startsWith("Qml debugging is enabled")) // ignore
            continue;
        if (line.startsWith("QDeclarativeDebugServer:")) {
            if (line.contains("Waiting for connection ")) {
                m_started = true;
                m_eventLoop.quit();
                continue;
            }
            if (line.contains("Connection established")) {
                continue;
            }
        }
        m_outputBuffer.append(appOutput);
    }
    m_mutex.unlock();
}
