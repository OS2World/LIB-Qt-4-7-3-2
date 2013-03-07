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

//#define QHOSTINFO_DEBUG

static const int RESOLVER_TIMEOUT = 2000;

#include "qt_os2.h"
#include "qplatformdefs.h"

#include "qhostinfo_p.h"
#include "qiodevice.h"
#include <qbytearray.h>
#include <qlibrary.h>
#include <qurl.h>
#include <qfile.h>

extern "C" {
#include <sys/types.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <resolv.h>
}

#include <qmutex.h>
QT_BEGIN_NAMESPACE
Q_GLOBAL_STATIC(QMutex, getHostByNameMutex)
QT_END_NAMESPACE

QT_BEGIN_NAMESPACE

QHostInfo QHostInfoAgent::fromName(const QString &hostName)
{
    QHostInfo results;
    results.setHostName(hostName);

#if defined(QHOSTINFO_DEBUG)
    qDebug("QHostInfoAgent::fromName(%s) looking up...",
           hostName.toLatin1().constData());
#endif

    QHostAddress address;
    if (address.setAddress(hostName)) {
        // Reverse lookup
        in_addr_t inetaddr = inet_addr(hostName.toLatin1().constData());
        struct hostent *ent = gethostbyaddr((const char *)&inetaddr, sizeof(inetaddr), AF_INET);
        if (!ent) {
            results.setError(QHostInfo::HostNotFound);
            results.setErrorString(tr("Host not found"));
            return results;
        }
        results.setHostName(QString::fromLatin1(ent->h_name));
    }

    // Fall back to gethostbyname for platforms that don't define
    // getaddrinfo. gethostbyname does not support IPv6, and it's not
    // reentrant on all platforms. For now this is okay since we only
    // use one QHostInfoAgent, but if more agents are introduced, locking
    // must be provided.
    QMutexLocker locker(::getHostByNameMutex());
    hostent *result = gethostbyname(hostName.toLatin1().constData());
    if (result) {
        if (result->h_addrtype == AF_INET) {
            QList<QHostAddress> addresses;
            for (char **p = result->h_addr_list; *p != 0; p++) {
                QHostAddress addr;
                addr.setAddress(ntohl(*((quint32 *)*p)));
                if (!addresses.contains(addr))
                    addresses.prepend(addr);
            }
            results.setAddresses(addresses);
        } else {
            results.setError(QHostInfo::UnknownError);
            results.setErrorString(tr("Unknown address type"));
        }
    } else if (h_errno == HOST_NOT_FOUND || h_errno == NO_DATA
               || h_errno == NO_ADDRESS) {
        results.setError(QHostInfo::HostNotFound);
        results.setErrorString(tr("Host not found"));
    } else {
        results.setError(QHostInfo::UnknownError);
        results.setErrorString(tr("Unknown error"));
    }

#if defined(QHOSTINFO_DEBUG)
    if (results.error() != QHostInfo::NoError) {
        qDebug("QHostInfoAgent::fromName(): error #%d %s",
               h_errno, results.errorString().toLatin1().constData());
    } else {
        QString tmp;
        QList<QHostAddress> addresses = results.addresses();
        for (int i = 0; i < addresses.count(); ++i) {
            if (i != 0) tmp += ", ";
            tmp += addresses.at(i).toString();
        }
        qDebug("QHostInfoAgent::fromName(): found %i entries for \"%s\": {%s}",
               addresses.count(), hostName.toLatin1().constData(),
               tmp.toLatin1().constData());
    }
#endif
    return results;
}

QString QHostInfo::localHostName()
{
    char hostName[512];
    if (gethostname(hostName, sizeof(hostName)) == -1)
        return QString();
    hostName[sizeof(hostName) - 1] = '\0';
    return QString::fromLocal8Bit(hostName);
}

QString QHostInfo::localDomainName()
{
    // We have to call res_init to be sure that _res was initialized
    // So, for systems without getaddrinfo (which is thread-safe), we lock the mutex too
    {
        QMutexLocker locker(::getHostByNameMutex());
        if ((_res.options & RES_INIT) || res_init() == 0) {
            QString domainName = QUrl::fromAce(_res.defdname);
            if (domainName.isEmpty())
                domainName = QUrl::fromAce(_res.dnsrch[0]);
            return domainName;
        }
    }

    // nothing worked, try doing it by ourselves:
    QFile resolvconf;
    QByteArray etc = qgetenv("ETC");
    if (etc.isEmpty()) {
        etc = "x:\\MPTN\\ETC";
        ULONG boot = 0;
        DosQuerySysInfo(QSV_BOOT_DRIVE, QSV_BOOT_DRIVE, (PVOID)boot, sizeof(boot));
        etc[0] = (char)(boot + 'A' - 1);
    }
    // currently, resolv takes precedence over resolv2 assuming that resolv
    // is created for PPP connections and therefore should override resolv2
    // which is created for permanent LAN connections.
    resolvconf.setFileName(QFile::decodeName(etc) + QLatin1String("\\resolv"));
    if (!resolvconf.exists())
        resolvconf.setFileName(QFile::decodeName(etc)+ QLatin1String("\\resolv2"));

    if (!resolvconf.open(QIODevice::ReadOnly))
        return QString();       // failure

    QString domainName;
    while (!resolvconf.atEnd()) {
        QByteArray line = resolvconf.readLine().trimmed();
        if (line.startsWith("domain "))
            return QUrl::fromAce(line.mid(sizeof "domain " - 1).trimmed());

        // in case there's no "domain" line, fall back to the first "search" entry
        if (domainName.isEmpty() && line.startsWith("search ")) {
            QByteArray searchDomain = line.mid(sizeof "search " - 1).trimmed();
            int pos = searchDomain.indexOf(' ');
            if (pos != -1)
                searchDomain.truncate(pos);
            domainName = QUrl::fromAce(searchDomain);
        }
    }

    // return the fallen-back-to searched domain
    return domainName;
}

QT_END_NAMESPACE
