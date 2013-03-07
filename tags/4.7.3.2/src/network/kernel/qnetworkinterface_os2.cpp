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

#include "qset.h"
#include "qnetworkinterface.h"
#include "qnetworkinterface_p.h"
#include "qalgorithms.h"

#ifndef QT_NO_NETWORKINTERFACE

#include <sys/types.h>
#include <sys/socket.h>

#include <sys/sockio.h>
#include <net/if.h>
#include <net/route.h>

#include <qplatformdefs.h>

QT_BEGIN_NAMESPACE

static QNetworkInterface::InterfaceFlags convertFlags(uint rawFlags)
{
    QNetworkInterface::InterfaceFlags flags = 0;
    flags |= (rawFlags & IFF_UP) ? QNetworkInterface::IsUp : QNetworkInterface::InterfaceFlag(0);
    flags |= (rawFlags & IFF_RUNNING) ? QNetworkInterface::IsRunning : QNetworkInterface::InterfaceFlag(0);
    flags |= (rawFlags & IFF_BROADCAST) ? QNetworkInterface::CanBroadcast : QNetworkInterface::InterfaceFlag(0);
    flags |= (rawFlags & IFF_LOOPBACK) ? QNetworkInterface::IsLoopBack : QNetworkInterface::InterfaceFlag(0);
    flags |= (rawFlags & IFF_POINTOPOINT) ? QNetworkInterface::IsPointToPoint : QNetworkInterface::InterfaceFlag(0);
    flags |= (rawFlags & IFF_MULTICAST) ? QNetworkInterface::CanMulticast : QNetworkInterface::InterfaceFlag(0);
    return flags;
}

static QList<QNetworkInterfacePrivate *> interfaceListing()
{
    // in this function, we use OS/2 specific socket IOCTLs to get more precise
    // interface information than provided by the standard socket IOCTLs. Note
    // that os2_ioctl() does not support high-mem so all buffers should be
    // either on stack or in low-mem

    QList<QNetworkInterfacePrivate *> interfaces;

    int socket;
    if ((socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_IP)) == -1)
        return interfaces;      // error

    // rumors say that the address buffer should be at least 65536 bytes long
    char addrBuf[65536];
    short naddrs;
    statatreq *addrs;

    ifmib ifmibget;
    // zero the interface table because garbage will be returned in interface
    // names otherwise
    memset(&ifmibget,0, sizeof(ifmib));

    int rc;

    // get available interfaces
    rc = ::os2_ioctl(socket, SIOSTATIF, (char*)&ifmibget, sizeof(ifmib));
    if (rc == -1) {
        ::close(socket);
        return interfaces;
    }

    // get IP addresses
    rc = ::os2_ioctl(socket, SIOSTATAT, addrBuf, sizeof(addrBuf));
    if (rc == -1) {
        ::close(socket);
        return interfaces;
    }
    naddrs = *(short *)addrBuf;
    addrs = (statatreq *)(addrBuf + sizeof(unsigned short));

    // loop over interfaces
    int idx = 0;
    for (int i = 0; i < IFMIB_ENTRIES && idx < ifmibget.ifNumber; ++i) {
        // skip empty interface entries
        if (ifmibget.iftable[i].iftType == 0)
            continue;

        QNetworkInterfacePrivate *iface = new QNetworkInterfacePrivate;
        iface->index = ifmibget.iftable[i].iftIndex;

        // Derive the interface name. Not perfect, but there seems to be no
        // other documented way (taken from Odin sources)
        if (iface->index >= 0 && iface->index < 9) {// lanX
            iface->name = QString(QLatin1String("lan%1")).arg(iface->index);
        } else if (strstr(ifmibget.iftable[i].iftDescr, "back")) { // loopback
            iface->name = QLatin1String("lo");
        }
        else if (strstr(ifmibget.iftable[i].iftDescr, "ace ppp")) {// pppX
            iface->name = QLatin1String(strstr(ifmibget.iftable[i].iftDescr, "ppp"));
        } else if (strstr(ifmibget.iftable[i].iftDescr,"ace sl")) { // slX
            iface->name = QLatin1String(strstr(ifmibget.iftable[i].iftDescr, "sl"));
        } else if (strstr(ifmibget.iftable[i].iftDescr,"ace dod")) { // dodX
            iface->name = QLatin1String(strstr(ifmibget.iftable[i].iftDescr, "dod"));
        } else { // something else...
            iface->name = QString(QLatin1String("unk%1")).arg(iface->index);
        }

        iface->friendlyName = QString::fromLocal8Bit(ifmibget.iftable[i].iftDescr);
        iface->hardwareAddress =
            iface->makeHwAddress(sizeof(ifmibget.iftable[i].iftPhysAddr),
                                 (uchar *)&ifmibget.iftable[i].iftPhysAddr[0]);

        // get interface flags
        ifreq req;
        memset(&req, 0, sizeof(ifreq));
        strncpy(req.ifr_name, iface->name.toLatin1(), sizeof(req.ifr_name));
        if (::ioctl(socket, SIOCGIFFLAGS, &req) != -1) {
            iface->flags = convertFlags(req.ifr_flags);
        }

        // get interface addresses
        for (int j = 0; j < naddrs; ++j) {
            if (addrs[j].interface == iface->index) {
                QNetworkAddressEntry entry;
                entry.setIp(QHostAddress(htonl(addrs[j].addr)));
                // mask is the only one in network byte order for some reason
                entry.setNetmask(QHostAddress(addrs[j].mask));
                entry.setBroadcast(QHostAddress(htonl(addrs[j].broadcast)));
                iface->addressEntries << entry;
            }
        }

        // store the interface
        interfaces << iface;
    }

    ::close(socket);
    return interfaces;
}

QList<QNetworkInterfacePrivate *> QNetworkInterfaceManager::scan()
{
    return interfaceListing();
}

QT_END_NAMESPACE

#endif // QT_NO_NETWORKINTERFACE
