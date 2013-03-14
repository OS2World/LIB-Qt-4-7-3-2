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

#include "qdesktopwidget.h"
#include "qt_os2.h"
#include "qapplication_p.h"
#include "qwidget_p.h"

#include "qdebug.h"

QT_BEGIN_NAMESPACE

// stolen from xWorkplace sources
static
APIRET qt_DosQueryProcAddr(PCSZ pcszModuleName, ULONG ulOrdinal, PFN *ppfn)
{
    HMODULE hmod = NULL;
    APIRET rc = 0;
    if (!(rc = DosQueryModuleHandle( (PSZ)pcszModuleName, &hmod))) {
        if ((rc = DosQueryProcAddr( hmod, ulOrdinal, NULL, ppfn))) {
            // the CP programming guide and reference says use
            // DosLoadModule if DosQueryProcAddr fails with this error
            if (rc == ERROR_INVALID_HANDLE) {
                if (!(rc = DosLoadModule(NULL, 0, (PSZ) pcszModuleName,
                                         &hmod))) {
                    rc = DosQueryProcAddr(hmod, ulOrdinal, NULL, ppfn);
                }
            }
        }
    }
    return rc;
}

class QDesktopWidgetPrivate : public QWidgetPrivate
{
public:
    QRect workArea;
};

/*
    \omit
    Function is commented out in header
    \fn void *QDesktopWidget::handle(int screen) const

    Returns the window system handle of the display device with the
    index \a screen, for low-level access.  Using this function is not
    portable.

    The return type varies with platform; see qwindowdefs.h for details.

    \sa x11Display(), QPaintDevice::handle()
    \endomit
*/

QDesktopWidget::QDesktopWidget()
    : QWidget(*new QDesktopWidgetPrivate, 0, Qt::Desktop)
{
    setObjectName(QLatin1String("desktop"));
}

QDesktopWidget::~QDesktopWidget()
{
}

bool QDesktopWidget::isVirtualDesktop() const
{
    return true;
}

int QDesktopWidget::primaryScreen() const
{
    return 0;
}

int QDesktopWidget::numScreens() const
{
    return 1;
}

QWidget *QDesktopWidget::screen(int /* screen */)
{
    // It seems that a Qt::WType_Desktop cannot be moved?
    return this;
}

const QRect QDesktopWidget::availableGeometry(int screen) const
{
    Q_D(const QDesktopWidget);

    if (!d->workArea.isValid())
        const_cast<QDesktopWidget *>(this)->resizeEvent(0);
    if (d->workArea.isValid())
        return d->workArea;

    return geometry();
}

const QRect QDesktopWidget::screenGeometry(int screen) const
{
    return geometry();
}

int QDesktopWidget::screenNumber(const QWidget * /* widget */) const
{
    return 0;
}

int QDesktopWidget::screenNumber(const QPoint & /* point */) const
{
    return 0;
}

void QDesktopWidget::resizeEvent(QResizeEvent *e)
{
    Q_D(QDesktopWidget);

    if (e == 0) {
        // this is a Work Area Changed notification, see WM_SYSVALUECHANGED
        // in qapplication_pm.cpp
        typedef
        BOOL (APIENTRY *WinQueryDesktopWorkArea_T) (HWND hwndDesktop,
                                                    PRECTL pwrcWorkArea);
        static WinQueryDesktopWorkArea_T WinQueryDesktopWorkArea =
            (WinQueryDesktopWorkArea_T) ~0;

        if ((ULONG) WinQueryDesktopWorkArea == (ULONG) ~0) {
            if (qt_DosQueryProcAddr("PMMERGE", 5469,
                                    (PFN *) &WinQueryDesktopWorkArea))
                WinQueryDesktopWorkArea = NULL;
        }

        if (WinQueryDesktopWorkArea) {
            RECTL rcl;
            if (WinQueryDesktopWorkArea(HWND_DESKTOP, &rcl)) {
                // flip y coordinates
                d->workArea.setCoords(rcl.xLeft, height() - rcl.yTop,
                                      rcl.xRight - 1,
                                      height() - (rcl.yBottom + 1));
            }
        }

        emit workAreaResized(0);
        return;
    }

    // otherwise nothing to do, the desktop cannot be dynamically resized
    // on OS/2
}

QT_END_NAMESPACE
