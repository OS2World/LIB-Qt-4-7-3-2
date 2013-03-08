/****************************************************************************
**
** Copyright (C) 2010 Nokia Corporation and/or its subsidiary(-ies).
** All rights reserved.
** Contact: Nokia Corporation (qt-info@nokia.com)
**
** Copyright (C) 2010 netlabs.org. OS/2 parts.
**
** This file is part of the QtCore module of the Qt Toolkit.
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

#include "qcoreapplication.h"
#include "qcoreapplication_p.h"
#include "qstringlist.h"
#include "qt_os2.h"
#include "qvector.h"
#include "qmutex.h"
#include "qcorecmdlineargs_p.h"
#include <private/qthread_p.h>
#include <ctype.h>

QT_BEGIN_NAMESPACE

static char appFileName[CCHMAXPATH];     // application file name
static char theAppName[CCHMAXPATH];      // application name

static void set_app_name()
{
    static bool already_set = false;
    if (!already_set) {
        already_set = true;
        PPIB ppib;
        DosGetInfoBlocks(NULL, &ppib);
        DosQueryModuleName(ppib->pib_hmte, sizeof(appFileName), appFileName);

        const char *p = strrchr(appFileName, '\\');        // skip path
        if (p)
            memcpy(theAppName, p+1, qstrlen(p));
        int l = qstrlen(theAppName);
        if ((l > 4) && !qstricmp(theAppName + l - 4, ".exe"))
            theAppName[l-4] = '\0';                // drop .exe extension
    }
}

Q_CORE_EXPORT QString qAppFileName()                // get application file name
{
    set_app_name();
    return QString::fromLocal8Bit(appFileName);
}

QString QCoreApplicationPrivate::appName() const
{
    set_app_name();
    return QString::fromLocal8Bit(theAppName);
}

/*!
    The message procedure calls this function for every message
    received. Reimplement this function if you want to process window
    messages \a msg that are not processed by Qt. If you don't want
    the event to be processed by Qt, then return true and set \a result to the
    value that the window procedure should return. Otherwise return false.

    It is only directly addressed messages that are filtered. To
    handle system wide messages, such as messages from a registered
    hot key, you need to install an event filter on the event
    dispatcher, which is returned from
    QAbstractEventDispatcher::instance().
*/
bool QCoreApplication::pmEventFilter(QMSG * /*msg*/, MRESULT * /*result*/)
{
    return false;
}

QT_END_NAMESPACE
