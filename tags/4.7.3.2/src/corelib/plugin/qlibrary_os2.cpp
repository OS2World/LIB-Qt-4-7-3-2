/***************************************************************************
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

#include "qplatformdefs.h"
#include "qlibrary_p.h"
#include "qfile.h"
#include "qdir.h"
#include "qfileinfo.h"
#include "qdir.h"

#include <float.h>

#ifndef QT_NO_LIBRARY

QT_BEGIN_NAMESPACE

// provide human readable strings for common OS/2 DOS errors
// @todo make exported if needed anywhere else
static QString qt_os2_error_string(APIRET code)
{
    // see qt_error_string()

    const char *s = 0;
    QString ret;

    switch (code) {
        case NO_ERROR:
            return ret;
        case ERROR_FILE_NOT_FOUND:
            s = QT_TRANSLATE_NOOP("QIODevice", "File not found");
            break;
        case ERROR_PATH_NOT_FOUND:
            s = QT_TRANSLATE_NOOP("QIODevice", "Path not found");
            break;
        case ERROR_TOO_MANY_OPEN_FILES:
            return qt_error_string(EMFILE);
        case ERROR_ACCESS_DENIED:
            s = QT_TRANSLATE_NOOP("QIODevice", "Access denied");
            break;
        case ERROR_SHARING_VIOLATION:
            s = QT_TRANSLATE_NOOP("QIODevice", "Sharing violation");
            break;
        default:
            break;
    }

    if (s)
        // ######## this breaks moc build currently
//         ret = QCoreApplication::translate("QIODevice", s);
        ret = QString::fromLatin1(s);
    else
        // ######## this breaks moc build currently
//         ret = QString(QCoreApplication::translate("QIODevice","System error %1")).arg(code);
        ret = QString::fromLatin1("System error %1").arg(code);
    return ret;
}

bool QLibraryPrivate::load_sys()
{
    pHnd = 0;

    QByteArray attempt = QFile::encodeName(QDir::toNativeSeparators(fileName));

    // It is known that the OS/2 DLL loader (or the init routine of some DLLs)
    // sometimes resets the FPU CW to a value that causes operations like
    // division by zero to throw SIGFPE instead of being handled by the FPU.
    // This is unexpected in many apps since they rely on the default C runtime
    // setting which is to handle them. The solution is to force the required
    // CW value and then restore it after loading the DLL.

    _clear87();
    unsigned int oldbits = _control87(0, 0);
    _control87(MCW_EM, MCW_EM);

    APIRET rc;
    char errModule[CCHMAXPATH] = { '\0' };
    rc = DosLoadModule(errModule, sizeof(errModule), attempt, &pHnd);
    if (rc == ERROR_FILE_NOT_FOUND && qstricmp(attempt, errModule) == 0) {
        // only retry with .DLL appended if the failed module is the tried one
        attempt += ".DLL";
        rc = DosLoadModule(errModule, sizeof(errModule), attempt, &pHnd);
    }

    _clear87();
    _control87(oldbits, 0xFFFFF);

    if (rc != NO_ERROR) {
        errorString = QLibrary::tr("Cannot load library %1: %2")
            .arg(fileName).arg(qt_os2_error_string(rc));
        if (*errModule != '\0')
            errorString += QLibrary::tr(" (failed module: %1)")
                .arg(QFile::decodeName(errModule));
        return false;
    }

    errorString.clear();
    rc = DosQueryModuleName(pHnd, sizeof(errModule), errModule);
    Q_ASSERT(rc == NO_ERROR);

    qualifiedFileName = QDir::fromNativeSeparators(QFile::decodeName(QByteArray(errModule)));

    return true;
}

bool QLibraryPrivate::unload_sys()
{
    APIRET rc = DosFreeModule(pHnd);
    if (rc != NO_ERROR) {
        errorString = QLibrary::tr("Cannot unload library %1: %2").arg(fileName).arg(qt_os2_error_string(rc));
        return false;
    }
    errorString.clear();
    return true;
}

void* QLibraryPrivate::resolve_sys(const char* symbol)
{
    void *address = 0;
    APIRET rc = DosQueryProcAddr(pHnd, 0, symbol, (PFN*) &address);
    if (rc != NO_ERROR) {
        errorString = QLibrary::tr("Cannot resolve symbol \"%1\" in %2: %3").arg(
            QString::fromAscii(symbol)).arg(fileName).arg(qt_os2_error_string(rc));
    } else {
        errorString.clear();
    }
    return address;
}
QT_END_NAMESPACE

#endif // QT_NO_LIBRARY
