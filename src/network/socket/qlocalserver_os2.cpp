/****************************************************************************
**
** Copyright (C) 2010 Nokia Corporation and/or its subsidiary(-ies).
** All rights reserved.
** Contact: Nokia Corporation (qt-info@nokia.com)
**
** Copyright (C) 2010 netlabs.org. OS/2 parts.
**
** This file is part of the QtNetwork module of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:LGPL$
** Commercial Usage
** Licensees holding valid Qt Commercial licenses may use this file in
** accordance with the Qt Commercial License Agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and Nokia.
**
** GNU Lesser General Public License Usage
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 2.1 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPL included in the
** packaging of this file.  Please review the following information to
** ensure the GNU Lesser General Public License version 2.1 requirements
** will be met: http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
**
** In addition, as a special exception, Nokia gives you certain additional
** rights.  These rights are described in the Nokia Qt LGPL Exception
** version 1.1, included in the file LGPL_EXCEPTION.txt in this package.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 3.0 as published by the Free Software
** Foundation and appearing in the file LICENSE.GPL included in the
** packaging of this file.  Please review the following information to
** ensure the GNU General Public License version 3.0 requirements will be
** met: http://www.gnu.org/copyleft/gpl.html.
**
** If you have questions regarding the use of this file, please contact
** Nokia at qt-info@nokia.com.
** $QT_END_LICENSE$
**
****************************************************************************/

#include "qlocalserver.h"
#include "qlocalserver_p.h"
#include "qlocalsocket.h"
#include "qlocalsocket_p.h"

#ifndef QT_NO_LOCALSERVER

#include <sys/socket.h>
#include <sys/un.h>

#include <qdebug.h>
#include <qdir.h>
#include <qdatetime.h>

QT_BEGIN_NAMESPACE

void QLocalServerPrivate::init()
{
}

bool QLocalServerPrivate::removeServer(const QString &name)
{
    Q_UNUSED(name);
    return true;
}

bool QLocalServerPrivate::listen(const QString &requestedServerName)
{
    Q_Q(QLocalServer);

    // Local OS/2 sockets must always start with "\socket\"
    const QLatin1String socketPath("\\socket\\");

    // determine the full server path
    fullServerName = QDir::toNativeSeparators(requestedServerName);
    if (!fullServerName.startsWith(socketPath)) {
        fullServerName = socketPath + requestedServerName;
    }
    serverName = requestedServerName;

    // create the unix socket
    listenSocket = ::socket(PF_UNIX, SOCK_STREAM, 0);
    if (-1 == listenSocket) {
        setError(QLatin1String("QLocalServer::listen"));
        closeServer();
        return false;
    }

    // Construct the unix address
    struct ::sockaddr_un addr;
    addr.sun_family = PF_UNIX;
    addr.sun_len = sizeof(sockaddr_un);
    QByteArray fsn = fullServerName.toLocal8Bit();
    if (sizeof(addr.sun_path) < (uint)fsn.size() + 1) {
        errno = E2BIG; // indicate buffer overflow
        setError(QLatin1String("QLocalServer::listen"));
        closeServer();
        return false;
    }
    ::strcpy(addr.sun_path, fsn);

    // bind
    if(-1 == ::bind(listenSocket, (sockaddr *)&addr, sizeof(sockaddr_un))) {
        setError(QLatin1String("QLocalServer::listen"));
        // if address is in use already, just close the socket, but do not delete the file
        if(errno == EADDRINUSE)
            QT_CLOSE(listenSocket);
        // otherwise, close the socket and delete the file
        else
            closeServer();
        listenSocket = -1;
        return false;
    }

    // listen for connections
    if (-1 == ::listen(listenSocket, 50)) {
        setError(QLatin1String("QLocalServer::listen"));
        closeServer();
        listenSocket = -1;
        return false;
    }
    Q_ASSERT(!socketNotifier);
    socketNotifier = new QSocketNotifier(listenSocket,
                                         QSocketNotifier::Read, q);
    q->connect(socketNotifier, SIGNAL(activated(int)),
               q, SLOT(_q_onNewConnection()));
    socketNotifier->setEnabled(maxPendingConnections > 0);
    return true;
}

void QLocalServerPrivate::closeServer()
{
    if (-1 != listenSocket) {
        // stop select()ing on this socket, otherwise close() will hang
        socketNotifier->setEnabled(false);
        QT_CLOSE(listenSocket);
        listenSocket = -1;
    }

    if (socketNotifier) {
        socketNotifier->setEnabled(false); // Otherwise, closed socket is checked before deleter runs
        socketNotifier->deleteLater();
        socketNotifier = 0;
    }
}

void QLocalServerPrivate::_q_onNewConnection()
{
    Q_Q(QLocalServer);
    if (-1 == listenSocket)
        return;

    ::sockaddr_un addr;
    int length = sizeof(sockaddr_un);
    int connectedSocket = ::accept(listenSocket, (sockaddr *)&addr, &length);
    if(-1 == connectedSocket) {
        setError(QLatin1String("QLocalSocket::activated"));
        closeServer();
    } else {
        socketNotifier->setEnabled(pendingConnections.size()
                                   <= maxPendingConnections);
        q->incomingConnection(connectedSocket);
    }
}

void QLocalServerPrivate::waitForNewConnection(int msec, bool *timedOut)
{
    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(listenSocket, &readfds);

    timeval timeout;
    timeout.tv_sec = msec / 1000;
    timeout.tv_usec = (msec % 1000) * 1000;

    int result = -1;
    result = ::select(listenSocket + 1, &readfds, 0, 0, (msec == -1) ? 0 : &timeout);
    if (-1 == result) {
        setError(QLatin1String("QLocalServer::waitForNewConnection"));
        closeServer();
    }
    if (result > 0)
        _q_onNewConnection();
    if (timedOut)
        *timedOut = (result == 0);
}

void QLocalServerPrivate::setError(const QString &function)
{
    if (EAGAIN == errno)
        return;

    switch (errno) {
    case EACCES:
        errorString = QLocalServer::tr("%1: Permission denied").arg(function);
        error = QAbstractSocket::SocketAccessError;
        break;
    case ELOOP:
    case ENOENT:
    case ENAMETOOLONG:
    case EROFS:
    case ENOTDIR:
        errorString = QLocalServer::tr("%1: Name error").arg(function);
        error = QAbstractSocket::HostNotFoundError;
        break;
    case EADDRINUSE:
        errorString = QLocalServer::tr("%1: Address in use").arg(function);
        error = QAbstractSocket::AddressInUseError;
        break;

    default:
        errorString = QLocalServer::tr("%1: Unknown error %2")
                      .arg(function).arg(errno);
        const char *err = strerror(errno);
        if (err)
            errorString += QString(QLatin1String(" (%1)")).arg(QLatin1String(err));
        error = QAbstractSocket::UnknownSocketError;
#if defined QLOCALSERVER_DEBUG
        qWarning() << errorString << "fullServerName:" << fullServerName;
#endif
    }
}

QT_END_NAMESPACE

#endif // QT_NO_LOCALSERVER
