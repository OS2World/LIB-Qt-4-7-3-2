/****************************************************************************
**
** Copyright (C) 2010 Nokia Corporation and/or its subsidiary(-ies).
** All rights reserved.
** Contact: Nokia Corporation (qt-info@nokia.com)
**
** Copyright (C) 2010 netlabs.org. OS/2 parts.
**
** This file is part of the QtGui module of the Qt Toolkit.
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

#ifndef QWINDOWDEFS_PM_H
#define QWINDOWDEFS_PM_H

#include <QtCore/qglobal.h>
#include <QtCore/qstring.h>

QT_BEGIN_HEADER

QT_BEGIN_NAMESPACE

QT_MODULE(Gui)

QT_END_NAMESPACE

// common typedefs from os2.h to avoid inclusion of qt_os2.h in public headers
typedef long LONG;
typedef unsigned long ULONG;
typedef unsigned short USHORT;
typedef unsigned long LHANDLE;
typedef LHANDLE HWND;
typedef LHANDLE HPS;
typedef LHANDLE HBITMAP;
typedef LHANDLE HPOINTER;
typedef LHANDLE HRGN;
typedef struct _QMSG QMSG;
typedef void *MRESULT;

typedef struct _RECTL RECTL;
typedef RECTL *PRECTL;

typedef HWND WId;

#define NULLHANDLE ((LHANDLE)0)

#define APIENTRY _System

// constants to address extra window data (nothing so far)
#define QT_EXTRAWINDATASIZE     (sizeof(LONG) * 0)

QT_BEGIN_NAMESPACE

class QWidget;

Q_GUI_EXPORT HPS qt_display_ps();

Q_GUI_EXPORT int qt_display_width();
Q_GUI_EXPORT int qt_display_height();

Q_GUI_EXPORT QWidget *qt_widget_from_hwnd(HWND hwnd);

#ifdef QT_PM_NATIVEWIDGETMASK

enum PWOFlags {
    PWO_Children = 0x01,
    PWO_Siblings = 0x02,
    PWO_Ancestors = 0x04,
    PWO_Screen = 0x08,
    PWO_TopLevel = 0x80000000,
    PWO_Default = 0 /*PWO_Children | PWO_Siblings | PWO_Ancestors | PWO_Screen*/,
};

#ifdef Q_QDOC
LONG qt_WinProcessWindowObstacles(HWND hwnd, RECTL *prcl, HRGN hrgn,
                                  LONG op, LONG flags);
#else
Q_GUI_EXPORT LONG APIENTRY qt_WinProcessWindowObstacles(HWND hwnd, RECTL *prcl,
                                                        HRGN hrgn, LONG op,
                                                        LONG flags);
#endif

#endif // QT_PM_NATIVEWIDGETMASK

// QDebug helpers for debugging various API types

// don't drag qdebug.h but require it to be included first
#if defined(QDEBUG_H) && !defined(QT_NO_DEBUG_STREAM)

struct QDebugHWND { HWND hwnd; };
inline QDebugHWND qDebugHWND(HWND hwnd) { QDebugHWND d = { hwnd }; return d; }
Q_GUI_EXPORT QDebug operator<<(QDebug debug, const QDebugHWND &d);

struct QDebugHRGN { HRGN hrgn; };
inline QDebugHRGN qDebugHRGN(HRGN hrgn) { QDebugHRGN d = { hrgn }; return d; }
Q_GUI_EXPORT QDebug operator<<(QDebug debug, const QDebugHRGN &d);

// the following declarations require OS/2 types not defined here,
// don't drag them in as well but require qt_os2.h to be included first
#if defined(QT_OS2_H)

Q_GUI_EXPORT QDebug operator<<(QDebug debug, const RECTL &rcl);
Q_GUI_EXPORT QDebug operator<<(QDebug debug, const SWP &swp);
Q_GUI_EXPORT QDebug operator<<(QDebug debug, const QMSG &qmsg);

#endif // defined(QT_OS2_H)

#endif // defined(QDEBUG_H) && !defined(QT_NO_DEBUG_STREAM)

QT_END_NAMESPACE

QT_END_HEADER

#endif // QWINDOWDEFS_PM_H
