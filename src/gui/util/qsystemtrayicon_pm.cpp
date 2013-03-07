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

#include "qsystemtrayicon_p.h"

#ifndef QT_NO_SYSTEMTRAYICON

#include "qapplication.h"
#include "qdesktopwidget.h"
#include "qlibrary.h"
#include "qt_os2.h"

// declare function pointers for dynamic linking to xsystray DLL
#define XSTAPI_FPTRS_STATIC
#include "xsystray.h"

QT_BEGIN_NAMESPACE

#define WM_XST_MYNOTIFY     (WM_USER + 1000)

static ULONG WM_XST_CREATED = 0;
static ULONG MaxTextLen = 0;

class QSystemTrayIconSys : QWidget
{
public:
    QSystemTrayIconSys(QSystemTrayIcon *object);
    ~QSystemTrayIconSys();

    void createIcon();

    void addToTray();
    void removeFromTray();
    QRect geometry();
    void updateIcon();
    void setToolTip();
    void showMessage(const QString &title, const QString &message,
                     QSystemTrayIcon::MessageIcon type, int timeOut);

protected:
    bool pmEvent(QMSG *msg, MRESULT *result);

private:
    HPOINTER hIcon;
    QPoint globalPos;
    QSystemTrayIcon *q;
};

QSystemTrayIconSys::QSystemTrayIconSys(QSystemTrayIcon *object)
    : hIcon(NULLHANDLE), q(object)
{
}

QSystemTrayIconSys::~QSystemTrayIconSys()
{
    if (hIcon != NULLHANDLE)
        WinDestroyPointer(hIcon);
}

void QSystemTrayIconSys::createIcon()
{
    HPOINTER hIconToDestroy = hIcon;

    hIcon = QPixmap::toPmHPOINTER(q->icon());

    if (hIconToDestroy != NULLHANDLE)
        WinDestroyPointer(hIconToDestroy);
}

void QSystemTrayIconSys::addToTray()
{
    QByteArray toolTip = q->toolTip().toLocal8Bit();
    xstAddSysTrayIcon(winId(), 0, hIcon, toolTip.constData(), WM_XST_MYNOTIFY, 0);
}

void QSystemTrayIconSys::removeFromTray()
{
    xstRemoveSysTrayIcon(winId(), 0);
}

QRect QSystemTrayIconSys::geometry()
{
    QRect rect;
    RECTL rcl;
    if (xstQuerySysTrayIconRect(winId(), 0, &rcl)) {
        int sh = qt_display_height();
        // flip y coordinates
        rcl.yTop = sh - rcl.yTop;
        rcl.yBottom = sh - rcl.yBottom;
        rect.setCoords(rcl.xLeft, rcl.yTop, rcl.xRight, rcl.yBottom);
    }
    return rect;
}

void QSystemTrayIconSys::updateIcon()
{
    createIcon();
    xstReplaceSysTrayIcon(winId(), 0, hIcon);
}

void QSystemTrayIconSys::setToolTip()
{
    QByteArray toolTip = q->toolTip().toLocal8Bit();
    xstSetSysTrayIconToolTip(winId(), 0, toolTip.constData());
}

void QSystemTrayIconSys::showMessage(const QString &title, const QString &message,
                                     QSystemTrayIcon::MessageIcon type, int timeOut)
{
    uint uSecs = 0;
    if ( timeOut < 0)
        uSecs = 10000; //10 sec default
    else uSecs = (int)timeOut;

    // so far, we use fallbacks
    // @todo use xstShowSysTrayIconBalloon() when it's implemented
    QRect iconPos = geometry();
    if (iconPos.isValid()) {
        QBalloonTip::showBalloon(type, title, message, q, iconPos.center(), uSecs, true);
    }
}

static void closeNormalPopups()
{
    if (QApplication::activePopupWidget()) {
        // The system tray area is actually another application so we close all
        // normal popups for consistency (see qapplication_pm.cpp). In case some
        // popup refuses to close, we give up after 1024 attempts (to avoid an
        // infinite loop).
        int maxiter = 1024;
        QWidget *popup;
        while ((popup = QApplication::activePopupWidget()) && maxiter--)
            popup->close();
    }
}

bool QSystemTrayIconSys::pmEvent(QMSG *msg, MRESULT *result)
{
    switch(msg->msg) {

        case WM_XST_MYNOTIFY: {
            switch (SHORT2FROMMP(msg->mp1)) {
                case XST_IN_MOUSE: {
                    closeNormalPopups();
                    PXSTMOUSEMSG pMsg = (PXSTMOUSEMSG)msg->mp2;
                    switch (pMsg->ulMouseMsg) {
                        case WM_BUTTON1CLICK:
                            emit q->activated(QSystemTrayIcon::Trigger);
                            break;
                        case WM_BUTTON1DBLCLK:
                            emit q->activated(QSystemTrayIcon::DoubleClick);
                            break;
                        case WM_BUTTON3CLICK:
                            emit q->activated(QSystemTrayIcon::MiddleClick);
                            break;
                        default:
                            break;
                    }
                    break;
                }
                case XST_IN_CONTEXT: {
                    closeNormalPopups();
                    if (QApplication::activePopupWidget()) {
                        // The system tray area is actually another application
                        // so we close all normal popups for consistency (see
                        // qapplication_pm.cpp). In case some popup refuses to
                        // close, we give up after 1024 attempts (to avoid an
                        // infinite loop).
                        int maxiter = 1024;
                        QWidget *popup;
                        while ((popup=QApplication::activePopupWidget()) && maxiter--)
                            popup->close();
                    }
                    PXSTCONTEXTMSG pMsg = (PXSTCONTEXTMSG)msg->mp2;
                    if (q->contextMenu()) {
                        QPoint gpos(pMsg->ptsPointerPos.x,
                                    // flip y coordinate
                                    qt_display_height() - (pMsg->ptsPointerPos.y + 1));
                        q->contextMenu()->popup(gpos);
                        q->contextMenu()->activateWindow();
                        // Must be activated for proper keyboardfocus and
                        // menu closing on OS/2
                    }
                    emit q->activated(QSystemTrayIcon::Context);
                    break;
                }
                case XST_IN_WHEEL: {
                    closeNormalPopups();
                }
                default:
                    break;
            }
            break;
        }

        default: {
            if (msg->msg == WM_XST_CREATED) {
                addToTray();
                return true;
            }
            break;
        }
    }

    return QWidget::pmEvent(msg, result);
}

template <typename T>
inline T AssignFromVoidPtr(T &var, void *val) { var = (T)val; return var; }

static bool gotApis = false;

static bool resolveApis()
{
    static bool tried = false;

    if (!tried) {
        tried = true;

        do {
            // link to the xsystray API DLL at runtime
            QLibrary xsystray(QLatin1String("xsystray"));

            #define R(f) if (AssignFromVoidPtr(f, xsystray.resolve(#f)) == NULL) break

            R(xstQuerySysTrayVersion);
            R(xstAddSysTrayIcon);
            R(xstReplaceSysTrayIcon);
            R(xstRemoveSysTrayIcon);
            R(xstSetSysTrayIconToolTip);
            R(xstQuerySysTrayIconRect);
            R(xstShowSysTrayIconBalloon);
            R(xstHideSysTrayIconBalloon);
            R(xstGetSysTrayCreatedMsgId);
            R(xstGetSysTrayMaxTextLen);

            #undef R

            // initialize some constants
            WM_XST_CREATED = xstGetSysTrayCreatedMsgId();
            MaxTextLen = xstGetSysTrayMaxTextLen();

            gotApis = true;

        } while (0);
    }

    return gotApis;
}

void QSystemTrayIconPrivate::install_sys()
{
    if (!gotApis && !resolveApis())
        return;

    Q_Q(QSystemTrayIcon);
    if (!sys) {
        // @todo make QSystemTrayIconSys a singleton and use different icon IDs
        // to differentiate between icons (this will save us from creating a new
        // HWND per each icon the application wants to show)
        sys = new QSystemTrayIconSys(q);
        sys->createIcon();
        sys->addToTray();
    }
}

void QSystemTrayIconPrivate::
showMessage_sys(const QString &title, const QString &message,
                QSystemTrayIcon::MessageIcon type, int timeOut)
{
    if (!sys)
        return;

    sys->showMessage(title, message, type, timeOut);
}

QRect QSystemTrayIconPrivate::geometry_sys() const
{
    if (!sys)
        return QRect();

    return sys->geometry();
}

void QSystemTrayIconPrivate::remove_sys()
{
    if (!sys)
        return;

    sys->removeFromTray();
    delete sys;
    sys = 0;
}

void QSystemTrayIconPrivate::updateIcon_sys()
{
    if (!sys)
        return;

    sys->updateIcon();
}

void QSystemTrayIconPrivate::updateMenu_sys()
{
    // nothing to do
}

void QSystemTrayIconPrivate::updateToolTip_sys()
{
    if (!sys)
        return;

    sys->setToolTip();
}

bool QSystemTrayIconPrivate::isSystemTrayAvailable_sys()
{
    if (!gotApis && !resolveApis())
        return false;
    return xstQuerySysTrayVersion(0, 0, 0);
}

bool QSystemTrayIconPrivate::supportsMessages_sys()
{
    return true;
}

QT_END_NAMESPACE

#endif // ifndef QT_NO_SYSTEMTRAYICON

