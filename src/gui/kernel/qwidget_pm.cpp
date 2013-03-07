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

#include "qt_os2.h"

#include "qdebug.h"

#include "qwidget.h"
#include "qwidget_p.h"

#include "qapplication.h"
#include "qdesktopwidget.h"
#include "qevent.h"

#include "private/qapplication_p.h"
#include "private/qbackingstore_p.h"
#include "private/qwindowsurface_raster_p.h"
#include "private/qwindowsurface_pm_p.h"

//#define QT_DEBUGWINCREATEDESTROY
//#define QT_DEBUGWIDGETMASK

QT_BEGIN_NAMESPACE

static QWidget *mouseGrb    = 0;
static QCursor *mouseGrbCur = 0;
static QWidget *keyboardGrb = 0;

// defined in qapplication_pm.cpp
extern bool qt_nograb();
extern MRESULT EXPENTRY QtWndProc(HWND, ULONG, MPARAM, MPARAM);
extern PFNWP QtOldFrameProc;
extern MRESULT EXPENTRY QtFrameProc(HWND, ULONG, MPARAM, MPARAM);
extern PFNWP QtOldFrameCtlProcs[FID_HORZSCROLL - FID_SYSMENU + 1];
extern MRESULT EXPENTRY QtFrameCtlProc(HWND, ULONG, MPARAM, MPARAM);

#ifndef QT_PM_NATIVEWIDGETMASK
#define qt_WinSetWindowPos WinSetWindowPos
#endif

#if !defined(QT_NO_SESSIONMANAGER)
bool qt_about_to_destroy_wnd = false;
#endif

typedef QSet<QString> WinClassNameHash;
Q_GLOBAL_STATIC(WinClassNameHash, winclassNames)

// register window class
static const QString qt_reg_winclass(QWidget *w)
{
    int flags = w->windowFlags();
    int type = flags & Qt::WindowType_Mask;

    QString cname;
    ULONG style = 0;

    if (type == Qt::Popup || type == Qt::ToolTip) {
        cname = QLatin1String("QPopup");
        style |= CS_SAVEBITS;
    } else if (w->isWindow()) {
        // this class is for top level widgets which are client HWNDs of a
        // WC_FRAME parent HWND internally maintaned for them
        cname = QLatin1String("QWindow");
    } else {
        cname = QLatin1String("QWidget");
    }

    if (winclassNames()->contains(cname)) // already registered in our list
        return cname;

    // QT_EXTRAWINDATASIZE is defined in qwindowdefs_pm.h
    WinRegisterClass(0, cname.toLatin1(), QtWndProc, style, QT_EXTRAWINDATASIZE);

    winclassNames()->insert(cname);
    return cname;

    // Note: there is no need to unregister private window classes registered by
    // this function -- it is done automatically upon process termination.
}

#ifdef QT_PM_NATIVEWIDGETMASK

/*!
    \internal

    Extended version of WinQueryClipRegion(). If the given window has a clip
    region, the given region will receive a copy of the clip region clipped to
    the current window rectangle. If there is no clip region, the given region
    will contain only the window rectangle on return.
 */
void qt_WinQueryClipRegionOrRect(HWND hwnd, HRGN hrgn)
{
    RECTL rcl;
    WinQueryWindowRect(hwnd, &rcl);

    HPS hps = qt_display_ps();
    GpiSetRegion(hps, hrgn, 1, &rcl);
    if (WinQueryClipRegion(hwnd, 0) != QCRGN_NO_CLIP_REGION) {
        HRGN hrgnTemp = GpiCreateRegion(hps, 0, NULL);
        WinQueryClipRegion(hwnd, hrgnTemp);
        GpiCombineRegion(hps, hrgn, hrgnTemp, hrgn, CRGN_AND);
        GpiDestroyRegion(hps, hrgnTemp);
    }
}

/*!
    \internal

    Extended version of WinInvalidateRegion(): invalidates the specified region
    of the given window and regions of children from \a hwndFrom to \a hwndTo
    if they intersect with the invalid region. If either of child window
    handles is NULLHANDLE, children are not invalidated at all. Also, HWND_TOP
    can be used as \a hwndFrom, HWND_BOTTOM \a can be used as \a hwndTo.
 */
static BOOL qt_WinInvalidateRegionEx(HWND hwnd, HRGN hrgn,
                                     HWND hwndFrom, HWND hwndTo)
{
#if defined(QT_DEBUGWIDGETMASK)
    qDebug() << "qt_WinInvalidateRegionEx: hwnd" << qDebugHWND(hwnd)
             << "hwndFrom" << qDebugFmtHex(hwndFrom)
             << "hwndTo" << qDebugFmtHex(hwndTo);
#endif

    if (hwndFrom == HWND_TOP)
        hwndFrom = WinQueryWindow(hwnd, QW_TOP);
    if (hwndTo == HWND_BOTTOM)
        hwndTo = WinQueryWindow(hwnd, QW_BOTTOM);

    if (hwndFrom == 0 || hwndTo == 0)
        return WinInvalidateRegion(hwnd, hrgn, FALSE);

    if (WinQueryWindow(hwndFrom, QW_PARENT) != hwnd ||
        WinQueryWindow(hwndTo, QW_PARENT) != hwnd)
        return FALSE;

    HPS hps = qt_display_ps();

    SWP swp;
    HWND child = hwndFrom;
    HRGN hrgnChild = GpiCreateRegion(hps, 0, NULL);
    HRGN hrgnInv = GpiCreateRegion(hps, 0, NULL);
    GpiCombineRegion(hps, hrgnInv, hrgn, 0, CRGN_COPY);

    LONG cmplx = RGN_RECT;

    while (child) {
        WinQueryWindowPos(child, &swp);
#if defined(QT_DEBUGWIDGETMASK)
        qDebug() << " child" << qDebugHWND(child) << "fl" << qDebugFmtHex(swp.fl);
#endif
        // proceed only if not hidden
        if (swp.fl & SWP_SHOW) {
            // get sibling's bounds (clip region or rect)
            qt_WinQueryClipRegionOrRect(child, hrgnChild);
            // translate the region to the parent's coordinate system
            POINTL ptl = { swp.x, swp.y };
            GpiOffsetRegion(hps, hrgnChild, &ptl);
            // intersect the child's region with the invalid one
            // and invalidate if not NULL
            cmplx = GpiCombineRegion(hps, hrgnChild, hrgnChild, hrgnInv,
                                      CRGN_AND);
            if (cmplx != RGN_NULL) {
                POINTL ptl2 = { -swp.x, -swp.y };
                GpiOffsetRegion(hps, hrgnChild, &ptl2);
                WinInvalidateRegion(child, hrgnChild, TRUE);
                GpiOffsetRegion(hps, hrgnChild, &ptl);
                // substract the invalidated area from the widget's region
                // (no need to invalidate it any more)
                cmplx = GpiCombineRegion(hps, hrgnInv, hrgnInv, hrgnChild,
                                          CRGN_DIFF);
#if defined(QT_DEBUGWIDGETMASK)
                qDebug("  processed");
#endif
                // finish if nothing left
                if (cmplx == RGN_NULL)
                    break;
            }
        }
        // iterate to the next window (below)
        if (child == hwndTo)
            break;
        child = WinQueryWindow(child, QW_NEXT);
    }

    BOOL ok = (cmplx == RGN_NULL) || (child == hwndTo);

    if (ok) {
        // invalidate what's left invalid after substracting children
        WinInvalidateRegion(hwnd, hrgnInv, FALSE);
    }

    GpiDestroyRegion(hps, hrgnInv);
    GpiDestroyRegion(hps, hrgnChild);

    return ok;
}

/*!
    \internal

    \enum PWOFlags
    \relates QWidget

    Flags for qt_WinProcessWindowObstacles() that define which relative windows
    to process.

    \warning This enum is only available on OS/2.

    \value PWO_Children Child windows.
    \value PWO_Siblings Sibling windows.
    \value PWO_Ancestors Siblings of the parent window and all ancestors.
    \value PWO_Screen Screen area.
    \value PWO_TopLevel All top level windows.
    \value PWO_Default Default value suitable for most paint operations
        (equivalent to specifying all flags except PWO_TopLevel).
*/

/*!
    \internal

    \fn LONG qt_WinProcessWindowObstacles(HWND hwnd, RECTL *prcl, HRGN hrgn,
                                          LONG op, LONG flags)
    \relates QWidget

    Collects all relative PM windows intersecting with the given \a hwnd and
    placed above it in Z-order. The area of interest is limited to the \a prcl
    rectangle (in window coordinates) which may be \c NULL to indicate the whole
    window. If \a hrgn is not \c NULL, all found obstacles are combined with
    the given region using the \a op operation (\c CRGN_*); otherwise they are
    directly validated on the window. The scope of relativeness is defined by
    the \a flags argument which is one or more PWOFlags OR-ed together.

    Returns the complexity of the combined region (only when \a hrgn is not
    \c NULL). Note that if no combining occurs (e.g. no relatives in the
    requested scope), the return value is \c RGN_NULL regardless of the original
    complexity of \a hrgn.

    This function is especially useful for 3rd-party applications that embed
    themselves into a Qt application by painting directly on a PM window of a Qt
    widget (that gets created when QWidget::winId() is called), bypassing Qt
    completely. An example of such an application would be a video player that
    renders and paints frames in an external non-Qt thread or process.

    Qt does not use the \c WS_CLIPSIBLINGS and \c WS_CLIPCHILDREN flags when
    creating PM windows for non-top-level widgets (because that would break
    support for non-rectangular widgets due to a bug in PM) and this function
    acts as a functional replacement for these flags. Any application that
    paints on the PM window of the Qt widget directy must call
    qt_WinProcessWindowObstacles() to correctly clip out portions of the widget
    covered by other widgets placed above it in Z-order and avoid unexpected
    painting over these widgets.

    \snippet doc/src/snippets/code/src_gui_painting_embedded_pm.cpp 0

    \warning This function is only available on OS/2.
 */
LONG APIENTRY qt_WinProcessWindowObstacles(HWND hwnd, RECTL *prcl, HRGN hrgn,
                                           LONG op, LONG flags)
{
    Q_ASSERT(hwnd);

    if (flags == 0)
        flags = PWO_Children | PWO_Siblings | PWO_Ancestors | PWO_Screen;

    HPS displayPS = qt_display_ps();

#if defined(QT_DEBUGWIDGETMASK)
    qDebug() << "qt_WinProcessWindowObstacles: hwnd" << qDebugHWND(hwnd)
             << "prcl" << prcl << "hrgn" << qDebugFmtHex(hrgn)
             << "op" << op << "flags" << qDebugFmtHex(flags);
#endif

    SWP swpSelf;
    WinQueryWindowPos(hwnd, &swpSelf);

    RECTL rclSelf = { 0, 0, swpSelf.cx, swpSelf.cy };
    if (prcl)
        rclSelf = *prcl;

    HRGN whrgn = GpiCreateRegion(displayPS, 0, NULL);

    LONG cmplx = RGN_NULL;
    HWND relative;
    SWP swp;

    bool cmplxChanged = false;

    // first, process areas placed outside the screen bounds
    if (flags & PWO_Screen) {
        RECTL rclScr = { 0, 0, qt_display_width(), qt_display_height() };
        WinMapWindowPoints(HWND_DESKTOP, hwnd, (PPOINTL) &rclScr, 2);
        // rough check of whether some window part is outside bounds
        if (rclSelf.xLeft < rclScr.xLeft ||
            rclSelf.yBottom < rclScr.yBottom ||
            rclSelf.xRight > rclScr.xRight ||
            rclSelf.yTop > rclScr.yTop) {
            GpiSetRegion(displayPS, whrgn, 1, &rclSelf);
            HRGN hrgnScr = GpiCreateRegion(displayPS, 1, &rclScr);
            // substract the screen region from this window's region
            // to get parts placed outside
            GpiCombineRegion(displayPS, whrgn, whrgn, hrgnScr, CRGN_DIFF);
            GpiDestroyRegion(displayPS, hrgnScr);
            // process the region
            if (hrgn != NULLHANDLE) {
                cmplx = GpiCombineRegion(displayPS, hrgn, hrgn, whrgn, op);
                cmplxChanged = true;
            } else {
                WinValidateRegion(hwnd, whrgn, FALSE);
            }
#if defined(QT_DEBUGWIDGETMASK)
            qDebug(" collected areas outside screen bounds");
#endif
         }
    }

    // next, go through all children (in z-order)
    if (flags & PWO_Children) {
        relative = WinQueryWindow(hwnd, QW_BOTTOM);
        if (relative != NULLHANDLE) {
            for (; relative != HWND_TOP; relative = swp.hwndInsertBehind) {
                WinQueryWindowPos(relative, &swp);
#if defined(QT_DEBUGWIDGETMASK)
                qDebug() << " child" << qDebugHWND(relative)
                         << "fl" << qDebugFmtHex(swp.fl);
#endif
                // skip if hidden
                if (!(swp.fl & SWP_SHOW))
                    continue;
                // rough check for intersection
                if (swp.x >= rclSelf.xRight || swp.y >= rclSelf.yTop ||
                    swp.x + swp.cx <= rclSelf.xLeft ||
                    swp.y + swp.cy <= rclSelf.yBottom)
                    continue;
                // get the bounds (clip region or rect)
                qt_WinQueryClipRegionOrRect(relative, whrgn);
                // translate the region to this window's coordinate system
                POINTL ptl = { swp.x, swp.y };
                GpiOffsetRegion(displayPS, whrgn, &ptl);
                // process the region
                if (hrgn != NULLHANDLE) {
                    cmplx = GpiCombineRegion(displayPS, hrgn, hrgn, whrgn, op);
                    cmplxChanged = true;
                } else {
                    WinValidateRegion(hwnd, whrgn, FALSE);
                }
#if defined(QT_DEBUGWIDGETMASK)
                qDebug("  collected");
#endif
            }
        }
    }

    HWND desktop = WinQueryDesktopWindow(0, 0);
    HWND parent = WinQueryWindow(hwnd, QW_PARENT);

    // next, go through all siblings placed above (in z-order),
    // but only if they are not top-level windows (that cannot be
    // non-rectangular and thus are always correctly clipped by the system)
    if ((flags & PWO_Siblings) && parent != desktop) {
        for (relative = swpSelf.hwndInsertBehind;
              relative != HWND_TOP; relative = swp.hwndInsertBehind) {
            WinQueryWindowPos(relative, &swp);
#if defined(QT_DEBUGWIDGETMASK)
            qDebug() << " sibling" << qDebugHWND(relative)
                     << "fl" << qDebugFmtHex(swp.fl);
#endif
            // skip if hidden
            if (!(swp.fl & SWP_SHOW))
                continue;
            // rough check for intersection
            if (swp.x >= swpSelf.x + rclSelf.xRight ||
                swp.y >= swpSelf.y + rclSelf.yTop ||
                swp.x + swp.cx <= swpSelf.x + rclSelf.xLeft ||
                swp.y + swp.cy <= swpSelf.y + rclSelf.yBottom)
                continue;
            // get the bounds (clip region or rect)
            qt_WinQueryClipRegionOrRect(relative, whrgn);
            // translate the region to this window's coordinate system
            POINTL ptl = { swp.x - swpSelf.x, swp.y - swpSelf.y };
            GpiOffsetRegion(displayPS, whrgn, &ptl);
            // process the region
            if (hrgn != NULLHANDLE) {
                cmplx = GpiCombineRegion(displayPS, hrgn, hrgn, whrgn, op);
                cmplxChanged = true;
            } else {
                WinValidateRegion(hwnd, whrgn, FALSE);
            }
#if defined(QT_DEBUGWIDGETMASK)
            qDebug("  collected");
#endif
        }
    }

    // last, go through all siblings of our parent and its ancestors
    // placed above (in z-order)
    if (flags & PWO_Ancestors) {
        POINTL delta = { swpSelf.x, swpSelf.y };
        while (parent != desktop) {
            HWND grandParent = WinQueryWindow(parent, QW_PARENT);
            if (!(flags & PWO_TopLevel)) {
                // When PWO_TopLevel is not specified, top-level windows
                // (children of the desktop) are not processed. It makes sense
                // when qt_WinProcessWindowObstacles() is used to clip out
                // overlying windows during regular paint operations (WM_PAINT
                // processing or drawing in a window directly through
                // WinGetPS()): in this case, top-level windows are always
                // correctly clipped out by the system (because they cannot be
                // non-rectangular).
                if (grandParent == desktop)
                    break;
            }

            WinQueryWindowPos(parent, &swp);
#if defined(QT_DEBUGWIDGETMASK)
            qDebug() << " parent" << qDebugHWND(parent)
                     << "fl" << qDebugFmtHex(swp.fl);
#endif
            delta.x += swp.x;
            delta.y += swp.y;
            for (relative = swp.hwndInsertBehind;
                 relative != HWND_TOP; relative = swp.hwndInsertBehind) {
                WinQueryWindowPos(relative, &swp);
#if defined(QT_DEBUGWIDGETMASK)
                qDebug() << " ancestor" << qDebugHWND(relative)
                         << "fl" << qDebugFmtHex(swp.fl);
#endif
                // skip if hidden
                if (!(swp.fl & SWP_SHOW))
                    continue;
                // rough check for intersection
                if (swp.x - delta.x >= rclSelf.xRight ||
                    swp.y - delta.y >= rclSelf.yTop ||
                    swp.x - delta.x + swp.cx <= rclSelf.xLeft ||
                    swp.y - delta.y + swp.cy <= rclSelf.yBottom)
                    continue;
                // get the bounds (clip region or rect)
                qt_WinQueryClipRegionOrRect(relative, whrgn);
                // translate the region to this window's coordinate system
                POINTL ptl = { swp.x - delta.x, swp.y - delta.y };
                GpiOffsetRegion(displayPS, whrgn, &ptl);
                // process the region
                if (hrgn != NULLHANDLE) {
                    cmplx = GpiCombineRegion(displayPS, hrgn, hrgn, whrgn, op);
                    cmplxChanged = true;
                } else {
                    WinValidateRegion(hwnd, whrgn, FALSE);
                }
#if defined(QT_DEBUGWIDGETMASK)
                qDebug("  collected");
#endif
            }
            parent = grandParent;
        }
    }

    GpiDestroyRegion(displayPS, whrgn);

    if (hrgn != NULLHANDLE && cmplxChanged == false) {
        // make sure to return the original complexity of the region
        RECTL rclDummy;
        cmplx = GpiQueryRegionBox(displayPS, hrgn, &rclDummy);
    }

    return cmplx;
}

/*!
    \internal

    Partial reimplementation of the WinSetWindowPos() API that obeys window clip
    regions. Currently supported flags are SWP_ZORDER, SWP_SHOW, SWP_HIDE,
    SWP_ACTIVATE, SWP_MOVE, SWP_SIZE and SWP_NOREDRAW; other flags should no be
    used.

    Note that if the above restrictions are not met or if the given window is a
    top-level window, this function acts exactly like the original
    WinSetWindowPos() function.
 */
static BOOL qt_WinSetWindowPos(HWND hwnd, HWND hwndInsertBehind,
                               LONG x, LONG y, LONG cx, LONG cy,
                               ULONG fl)
{
    // @todo We need to send WM_VRNENABLED/WM_VRNDISABLED to all affected
    // windows as it turned out that WinSetWindowPos() called with SWP_NOREDRAW
    // does not do that. The problem here is that it's unknown how to determine
    // which windows asked to send them visible region change notifications with
    // WinSetVisibleRegionNotify(). This is the main reason why we do not define
    // QT_PM_NATIVEWIDGETMASK by default. Not sending those notifications breaks
    // painting to widgets that depend on this information, e.g. all direct
    // painting modes using DIVE. Note that this only affects cases when native
    // windows are created for child widgets. Normally, this is not the case,
    // all masking is done by Qt and this code is not involved at all, so
    // disabling it doesn't affect applications.

#if defined(QT_DEBUGWIDGETMASK)
    qDebug() << "qt_WinSetWindowPos: hwnd" << qDebugHWND(hwnd)
             << "behind" << qDebugFmtHex(hwndInsertBehind)
             << x << y << cx << cy
             << "fl" << qDebugFmtHex(fl);
#endif

    HWND desktop = WinQueryDesktopWindow(0, 0);

    Q_ASSERT(((fl & ~(SWP_ZORDER | SWP_SHOW | SWP_HIDE | SWP_ACTIVATE |
                      SWP_MOVE | SWP_SIZE | SWP_NOREDRAW)) == 0) ||
             hwnd == desktop || WinQueryWindow(hwnd, QW_PARENT) == desktop);

    if ((fl & ~(SWP_ZORDER | SWP_SHOW | SWP_HIDE | SWP_ACTIVATE |
                SWP_MOVE | SWP_SIZE | SWP_NOREDRAW)) != 0 ||
         hwnd == desktop || WinQueryWindow(hwnd, QW_PARENT) == desktop) {
        return WinSetWindowPos(hwnd, hwndInsertBehind, x, y, cx, cy, fl);
    }

    SWP swpOld;
    WinQueryWindowPos(hwnd, &swpOld);

#if defined(QT_DEBUGWIDGETMASK)
    qDebug() << "  old pos" << swpOld;
    if (QWidget *w = qt_widget_from_hwnd(hwnd)) {
        qDebug() << "  old geo" << w->geometry();
        if (w->parentWidget()) {
            qDebug() << "  parent" << w->parentWidget();
            if (w->parentWidget()->internalWinId()) {
                SWP swp;
                WinQueryWindowPos(w->parentWidget()->internalWinId(), &swp);
                qDebug() << "    pos" << swp;
            }
            qDebug() << "    geo" << w->parentWidget()->geometry();
        }
    }
#endif

    // do some checks
    if ((fl & SWP_ZORDER) && swpOld.hwndInsertBehind == hwndInsertBehind)
        fl &= ~SWP_ZORDER;
    if ((fl & SWP_SHOW) && (swpOld.fl & SWP_SHOW))
        fl &= ~SWP_SHOW;
    if ((fl & SWP_HIDE) && (swpOld.fl & SWP_HIDE))
        fl &= ~SWP_HIDE;
    if ((fl & (SWP_SHOW | SWP_HIDE)) == (SWP_SHOW | SWP_HIDE))
        fl &= ~SWP_HIDE;

    // do the job but not invalidate or redraw
    BOOL rc = WinSetWindowPos(hwnd, hwndInsertBehind, x, y, cx, cy,
                              fl | SWP_NOREDRAW);
    if (rc == FALSE || (fl & SWP_NOREDRAW))
        return rc;

    SWP swpNew;
    WinQueryWindowPos(hwnd, &swpNew);

    if (swpOld.hwndInsertBehind == swpNew.hwndInsertBehind)
        fl &= ~SWP_ZORDER;

    if ((fl & (SWP_ZORDER | SWP_SHOW | SWP_HIDE | SWP_MOVE | SWP_SIZE)) == 0)
        return rc;

    HPS hps = qt_display_ps();
    HWND hwndParent = WinQueryWindow(hwnd, QW_PARENT);

    // get window bounds
    HRGN hrgnSelf = GpiCreateRegion(hps, 0, NULL);
    qt_WinQueryClipRegionOrRect(hwnd, hrgnSelf);

    if (fl & SWP_SHOW) {
        WinInvalidateRegion(hwnd, hrgnSelf, TRUE);
    } else if (fl & SWP_HIDE) {
        // translate the region to the parent coordinate system
        POINTL ptl = { swpNew.x, swpNew.y };
        GpiOffsetRegion(hps, hrgnSelf, &ptl);
        // invalidate the parent and children below this window
        qt_WinInvalidateRegionEx(hwndParent, hrgnSelf,
                                  WinQueryWindow(hwnd, QW_NEXT), HWND_BOTTOM);
    } else {
        // SWP_ZORDER or SWP_MOVE or SWP_SIZE

        if (fl & SWP_ZORDER) {
            // below we assume that WinSetWindowPos() returns FALSE if
            // an incorrect (unrelated) hwndInsertBehind is passed when SWP_ZORDER
            // is set

            // first, detect whether we are moving up or down
            BOOL up;
            HWND hwndFrom, hwndTo;
            if (swpOld.hwndInsertBehind == HWND_TOP) {
                up = FALSE;
                hwndFrom = WinQueryWindow(hwndParent, QW_TOP);
                hwndTo = swpNew.hwndInsertBehind;
            } else {
                up = TRUE;
                for (HWND hwndAbove = hwnd;
                      (hwndAbove = WinQueryWindow(hwndAbove, QW_PREV)) != 0;) {
                    if (hwndAbove == swpOld.hwndInsertBehind) {
                        up = FALSE;
                        break;
                    }
                }
                if (up) {
                    hwndFrom = swpOld.hwndInsertBehind;
                    hwndTo = WinQueryWindow(hwnd, QW_NEXT);
                } else {
                    hwndFrom = WinQueryWindow(swpOld.hwndInsertBehind, QW_NEXT);
                    hwndTo = swpNew.hwndInsertBehind;
                }
            }
#if defined(QT_DEBUGWIDGETMASK)
            qDebug() << " moving up?" << up;
            qDebug() << " hwndFrom" << qDebugHWND(hwndFrom);
            qDebug() << " hwndTo" << qDebugHWND(hwndTo);
#endif

            SWP swp;
            HWND sibling = hwndFrom;
            HRGN hrgn = GpiCreateRegion(hps, 0, NULL);
            HRGN hrgnUpd = GpiCreateRegion(hps, 0, NULL);

            if (up) {
                // go upwards in z-order
                while (1) {
                    WinQueryWindowPos(sibling, &swp);
#if defined(QT_DEBUGWIDGETMASK)
                    qDebug() << " sibling" << qDebugHWND(sibling)
                             << "fl" << qDebugFmtHex(swp.fl);
#endif
                    // proceed only if not hidden
                    if (swp.fl & SWP_SHOW) {
                        // get sibling's bounds (clip region or rect)
                        qt_WinQueryClipRegionOrRect(sibling, hrgn);
                        // translate the region to this window's coordinate system
                        POINTL ptl = { swp.x - swpNew.x, swp.y - swpNew.y };
                        GpiOffsetRegion(hps, hrgn, &ptl);
                        // add to the region of siblings we're moving on top of
                        GpiCombineRegion(hps, hrgnUpd, hrgnUpd, hrgn, CRGN_OR);
#if defined(QT_DEBUGWIDGETMASK)
                        qDebug("  processed");
#endif
                    }
                    // iterate to the prev window (above)
                    if (sibling == hwndTo)
                        break;
                    sibling = swp.hwndInsertBehind;
                }
                // intersect the resulting region with the widget region and
                // invalidate
                GpiCombineRegion(hps, hrgnUpd, hrgnSelf, hrgnUpd, CRGN_AND);
                WinInvalidateRegion(hwnd, hrgnUpd, TRUE);
            } else {
                // go downwards in reverse z-order
                POINTL ptl = { 0, 0 };
                while (1) {
                    WinQueryWindowPos(sibling, &swp);
#if defined(QT_DEBUGWIDGETMASK)
                    qDebug() << " sibling" << qDebugHWND(sibling)
                             << "fl" << qDebugFmtHex(swp.fl);
#endif
                    // proceed only if not hidden
                    if (swp.fl & SWP_SHOW) {
                        // get sibling's bounds (clip region or rect)
                        qt_WinQueryClipRegionOrRect(sibling, hrgn);
                        // undo the previous translation and translate this window's
                        // region to the siblings's coordinate system
                        ptl.x += swpNew.x - swp.x;
                        ptl.y += swpNew.y - swp.y;
                        GpiOffsetRegion(hps, hrgnSelf, &ptl);
                        // intersect the sibling's region with the translated one
                        // and invalidate the sibling
                        GpiCombineRegion(hps, hrgnUpd, hrgnSelf, hrgn, CRGN_AND);
                        WinInvalidateRegion(sibling, hrgnUpd, TRUE);
                        // substract the invalidated area from the widget's region
                        // (no need to invalidate it any more)
                        GpiCombineRegion(hps, hrgnSelf, hrgnSelf, hrgnUpd, CRGN_DIFF);
                        // prepare the translation from the sibling's
                        // coordinates back to this window's coordinates
                        ptl.x = swp.x - swpNew.x;
                        ptl.y = swp.y - swpNew.y;
#if defined(QT_DEBUGWIDGETMASK)
                        qDebug("  processed");
#endif
                    }
                    // iterate to the next window (below)
                    if (sibling == hwndTo)
                        break;
                    sibling = WinQueryWindow(sibling, QW_NEXT);
                }
            }

            GpiDestroyRegion(hps, hrgnUpd);
            GpiDestroyRegion(hps, hrgn);
        }

        if (fl & (SWP_MOVE | SWP_SIZE)) {
            // Since we don't use WS_CLIPCHILDREN and WS_CLIPSIBLINGS,
            // WinSetWindowPos() does not correctly update involved windows.
            // The fix is to do it ourselves, taking clip regions into account.
            // set new and old rectangles
            const RECTL rcls [2] = {
                // new (relative to parent)
                { swpNew.x, swpNew.y, swpNew.x + swpNew.cx, swpNew.y + swpNew.cy },
                // old (relative to parent)
                { swpOld.x, swpOld.y, swpOld.x + swpOld.cx, swpOld.y + swpOld.cy }
            };
            const RECTL &rclNew = rcls [0];
            const RECTL &rclOld = rcls [1];
            // delta to shift coordinate space from parent to this widget
            POINTL ptlToSelf = { -swpNew.x, -swpNew.y };
            // use parent PS for blitting
            HPS hps = WinGetPS(hwndParent);
            // get old and new clip regions (relative to parent)
            HRGN hrgnOld = GpiCreateRegion(hps, 1, &rclOld);
            HRGN hrgnNew = GpiCreateRegion(hps, 1, &rclNew);
            if (WinQueryClipRegion(hwnd, 0) != QCRGN_NO_CLIP_REGION) {
                HRGN hrgnTemp = GpiCreateRegion(hps, 0, NULL);
                // old (clipped to the old rect)
                WinQueryClipRegion(hwnd, hrgnTemp);
                GpiOffsetRegion(hps, hrgnTemp, (PPOINTL) &rclOld);
                GpiCombineRegion(hps, hrgnOld, hrgnTemp, hrgnOld, CRGN_AND);
                // new (clipped to the new rect)
                WinQueryClipRegion(hwnd, hrgnTemp);
                if (swpOld.cy != swpNew.cy) {
                    // keep the clip region top-left aligned by adding the
                    // height delta (new size - old size)
                    POINTL ptl = {0, swpNew.cy - swpOld.cy };
                    GpiOffsetRegion(hps, hrgnTemp, &ptl);
                    WinSetClipRegion(hwnd, hrgnTemp);
                }
                GpiOffsetRegion(hps, hrgnTemp, (PPOINTL) &rclNew);
                GpiCombineRegion(hps, hrgnNew, hrgnTemp, hrgnNew, CRGN_AND);
                GpiDestroyRegion(hps, hrgnTemp);
            }
            // the rest is useful only when the widget is visible
            if (swpNew.fl & SWP_SHOW) {
                // create affected region (old + new, relative to widget)
                HRGN hrgnAff = GpiCreateRegion(hps, 0, NULL);
                GpiCombineRegion(hps, hrgnAff, hrgnOld, hrgnNew, CRGN_OR);
                GpiOffsetRegion(hps, hrgnAff, &ptlToSelf);
                // get bounding rectangle of affected region
                RECTL rclAff;
                GpiQueryRegionBox(hps, hrgnAff, &rclAff);
                // get region of obstacles limited to affected rectangle
                HRGN hrgnObst = GpiCreateRegion(hps, 0, NULL);
                qt_WinProcessWindowObstacles(hwnd, &rclAff, hrgnObst, CRGN_OR,
                                             PWO_Siblings | PWO_Ancestors |
                                             PWO_Screen | PWO_TopLevel);
                // shift region of obstacles and affected region back to
                // parent coords
                GpiOffsetRegion(hps, hrgnObst, (PPOINTL) &rclNew);
                GpiOffsetRegion(hps, hrgnAff, (PPOINTL) &rclNew);
                // get parent bounds (clip region or rect)
                HRGN hrgnUpd = GpiCreateRegion(hps, 0, NULL);
                qt_WinQueryClipRegionOrRect(hwndParent, hrgnUpd);
                // add parts of old region beyond parent bounds to
                // region of obstacles
                GpiCombineRegion(hps, hrgnOld, hrgnOld, hrgnUpd, CRGN_DIFF);
                GpiCombineRegion(hps, hrgnObst, hrgnObst, hrgnOld, CRGN_OR);
                // substract region of obstacles from affected region
                GpiCombineRegion(hps, hrgnAff, hrgnAff, hrgnObst, CRGN_DIFF);
                // remember it as parent update region (need later)
                GpiCombineRegion(hps, hrgnUpd, hrgnAff, 0, CRGN_COPY);
                // copy region of obstacles to delta region and shift it by
                // delta (note: movement is considered to be top-left aligned)
                HRGN hrgnDelta = GpiCreateRegion(hps, 0, NULL);
                GpiCombineRegion(hps, hrgnDelta, hrgnObst, 0, CRGN_COPY);
                POINTL ptlDelta = { rclNew.xLeft - rclOld.xLeft,
                                    rclNew.yTop - rclOld.yTop };
                GpiOffsetRegion(hps, hrgnDelta, &ptlDelta);
                // substract region of obstacles from delta region to get
                // pure delta
                GpiCombineRegion(hps, hrgnDelta, hrgnDelta, hrgnObst, CRGN_DIFF);
                // calculate minimal rectangle to blit (top-left aligned)
                int minw = qMin(swpOld.cx, swpNew.cx);
                int minh = qMin(swpOld.cy, swpNew.cy);
                POINTL blitPtls [4] = {
                    // target (new)
                    { rclNew.xLeft, rclNew.yTop - minh },
                    { rclNew.xLeft + minw, rclNew.yTop },
                    // source (old)
                    { rclOld.xLeft, rclOld.yTop - minh },
                };
                // proceed with blitting only if target and source rects differ
                if (blitPtls[0].x !=  blitPtls[2].x ||
                    blitPtls[0].y !=  blitPtls[2].y)
                {
                    // Substract delta region from affected region (to minimize
                    // flicker)
                    GpiCombineRegion(hps, hrgnAff, hrgnAff, hrgnDelta, CRGN_DIFF);
                    // set affected region to parent PS
                    GpiSetClipRegion(hps, hrgnAff, NULL);
                    // blit minimal rectangle
                    GpiBitBlt(hps, hps, 3, blitPtls, ROP_SRCCOPY, BBO_IGNORE);
                    GpiSetClipRegion(hps, 0, NULL);
                }
                // substract new widget region from the parent update region
                // and invalidate it (with underlying children)
                GpiCombineRegion(hps, hrgnUpd, hrgnUpd, hrgnNew, CRGN_DIFF );
                qt_WinInvalidateRegionEx(hwndParent, hrgnUpd,
                                         WinQueryWindow(hwnd, QW_NEXT),
                                         HWND_BOTTOM);
                // intersect pure delta region with new region
                // (to detect areas clipped off to minimize flicker when blitting)
                GpiCombineRegion(hps, hrgnDelta, hrgnDelta, hrgnNew, CRGN_AND);
                // substract blitted rectangle from new region
                GpiSetRegion(hps, hrgnAff, 1, (PRECTL) &blitPtls);
                GpiCombineRegion(hps, hrgnNew, hrgnNew, hrgnAff, CRGN_DIFF);
                // combine the rest with intersected delta region
                GpiCombineRegion(hps, hrgnNew, hrgnNew, hrgnDelta, CRGN_OR);
                // shift the result back to widget coords and invalidate
                GpiOffsetRegion(hps, hrgnNew, &ptlToSelf);
                WinInvalidateRegion(hwnd, hrgnNew, TRUE);
                // free resources
                GpiDestroyRegion(hps, hrgnDelta);
                GpiDestroyRegion(hps, hrgnUpd);
                GpiDestroyRegion(hps, hrgnObst);
                GpiDestroyRegion(hps, hrgnAff);
            }
            // free resources
            GpiDestroyRegion(hps, hrgnOld);
            GpiDestroyRegion(hps, hrgnNew);
            WinReleasePS(hps);
        }
    }

    GpiDestroyRegion(hps, hrgnSelf);

    return TRUE;
}

#endif // QT_PM_NATIVEWIDGETMASK

/*!
    \internal

    Helper function to extract regions of all windows that overlap the given
    hwnd subject to their z-order (excluding children of hwnd) from the given
    hrgn. hps is the presentation space of hrgn.
 */
static void qt_WinExcludeOverlappingWindows(HWND hwnd, HPS hps, HRGN hrgn)
{
    HRGN vr = GpiCreateRegion(hps, 0, NULL);
    RECTL r;

    // enumerate all windows that are on top of this hwnd
    HWND id = hwnd, deskId = QApplication::desktop()->winId();
    do {
        HWND i = id;
        while((i = WinQueryWindow( i, QW_PREV ))) {
            if (WinIsWindowVisible(i)) {
                WinQueryWindowRect(i, &r);
                WinMapWindowPoints(i, hwnd, (PPOINTL) &r, 2);
                GpiSetRegion(hps, vr, 1, &r);
                GpiCombineRegion(hps, hrgn, hrgn, vr, CRGN_DIFF);
            }
        }
        id = WinQueryWindow(id, QW_PARENT);
    } while(id != deskId);

    GpiDestroyRegion(hps, vr);
}

/*!
    \internal

    Helper function to scroll window contents. WinScrollWindow() is a bit
    buggy and corrupts update regions sometimes (which leaves some outdated
    areas unpainted after scrolling), so we reimplement its functionality in
    this function. dy and clip->yBottom/yTop should be GPI coordinates, not Qt.
    all clip coordinates are inclusive.
 */
static void qt_WinScrollWindowWell(HWND hwnd, LONG dx, LONG dy,
                                   const PRECTL clip = NULL)
{
    WinLockVisRegions(HWND_DESKTOP, TRUE);

    HPS lhps = WinGetClipPS(hwnd, HWND_TOP,
                            PSF_LOCKWINDOWUPDATE | PSF_CLIPSIBLINGS);
    if (clip)
        GpiIntersectClipRectangle(lhps, clip);

    // remember the update region and validate it. it will be shifted and
    // invalidated again later
    HRGN update = GpiCreateRegion(lhps, 0, NULL);
    WinQueryUpdateRegion(hwnd, update);
    WinValidateRegion(hwnd, update, TRUE);

    char points[sizeof(POINTL) * 4];
    register PPOINTL ptls = reinterpret_cast<PPOINTL>(points);
    RECTL &sr = *reinterpret_cast<PRECTL>(&ptls[2]);
    RECTL &tr = *reinterpret_cast<PRECTL>(&ptls[0]);

    // get the source rect for scrolling
    GpiQueryClipBox(lhps, &sr);
    sr.xRight++; sr.yTop++; // inclusive -> exclusive

    // get the visible region ignoring areas covered by children
    HRGN visible = GpiCreateRegion(lhps, 1, &sr);
    qt_WinExcludeOverlappingWindows(hwnd, lhps, visible);

    // scroll visible region rectangles separately to avoid the flicker
    // that could be produced by scrolling parts of overlapping windows
    RGNRECT ctl;
    ctl.ircStart = 1;
    ctl.crc = 0;
    ctl.crcReturned = 0;
    if (dx >= 0) {
        if (dy >= 0) ctl.ulDirection = RECTDIR_RTLF_TOPBOT;
        else ctl.ulDirection = RECTDIR_RTLF_BOTTOP;
    } else {
        if (dy >= 0) ctl.ulDirection = RECTDIR_LFRT_TOPBOT;
        else ctl.ulDirection = RECTDIR_LFRT_BOTTOP;
    }
    GpiQueryRegionRects(lhps, visible, NULL, &ctl, NULL);
    ctl.crc = ctl.crcReturned;
    int rclcnt = ctl.crcReturned;
    PRECTL rcls = new RECTL[rclcnt];
    GpiQueryRegionRects(lhps, visible, NULL, &ctl, rcls);
    PRECTL r = rcls;
    for (int i = 0; i < rclcnt; i++, r++) {
        // source rect
        sr = *r;
        // target rect
        tr.xLeft = sr.xLeft + dx;
        tr.xRight = sr.xRight + dx;
        tr.yBottom = sr.yBottom + dy;
        tr.yTop = sr.yTop + dy;
        GpiBitBlt(lhps, lhps, 3, ptls, ROP_SRCCOPY, BBO_IGNORE);
    }
    delete [] rcls;

    // make the scrolled version of the visible region
    HRGN scrolled = GpiCreateRegion(lhps, 0, NULL);
    GpiCombineRegion(lhps, scrolled, visible, 0, CRGN_COPY);
    // invalidate the initial update region
    GpiCombineRegion(lhps, scrolled, scrolled, update, CRGN_DIFF);
    // shift the region
    POINTL dp = { dx, dy };
    GpiOffsetRegion(lhps, scrolled, &dp);
    // substract scrolled from visible to get invalid areas
    GpiCombineRegion(lhps, scrolled, visible, scrolled, CRGN_DIFF);

    WinInvalidateRegion(hwnd, scrolled, TRUE);

    GpiDestroyRegion(lhps, scrolled);
    GpiDestroyRegion(lhps, visible);
    GpiDestroyRegion(lhps, update);

    WinReleasePS(lhps);

    WinLockVisRegions(HWND_DESKTOP, FALSE);

    WinUpdateWindow(hwnd);
}

/*!
 * \internal
 * For some unknown reason, PM sends WM_SAVEAPPLICATION to every window
 * being destroyed, which makes it indistinguishable from WM_SAVEAPPLICATION
 * sent to top level windows during system shutdown. We use our own version of
 * WinDestroyWindow() and a special flag (qt_about_to_destroy_wnd) to
 * distinguish it in qapplication_pm.cpp.
 */
static BOOL qt_WinDestroyWindow(HWND hwnd)
{
#if !defined(QT_NO_SESSIONMANAGER)
    qt_about_to_destroy_wnd = true;
#endif
    BOOL rc = WinDestroyWindow(hwnd);
#if !defined(QT_NO_SESSIONMANAGER)
    qt_about_to_destroy_wnd = false;
#endif
    return rc;
}

static PFNWP QtOldSysMenuProc;
static MRESULT EXPENTRY QtSysMenuProc(HWND hwnd, ULONG msg, MPARAM mp1, MPARAM mp2)
{
    if (msg == WM_MENUEND) {
        // the pull-down menu is closed, always dismiss the system menu itself
        WinPostMsg(hwnd, MM_ENDMENUMODE, MPFROMSHORT(TRUE), 0);
    }
    return QtOldSysMenuProc(hwnd, msg, mp1, mp2);
}

static void removeSysMenuAccels(HWND frame)
{
    HWND sysMenu = WinWindowFromID(frame, FID_SYSMENU);
    if (!sysMenu)
        return;

    SHORT subId = SHORT1FROMMR(WinSendMsg(sysMenu, MM_ITEMIDFROMPOSITION, 0, 0));
    if (subId != MIT_ERROR) {
        MENUITEM item;
        WinSendMsg(sysMenu, MM_QUERYITEM, MPFROM2SHORT(subId, FALSE), MPFROMP(&item));
        HWND subMenu = item.hwndSubMenu;
        if (subMenu) {
            USHORT cnt = SHORT1FROMMR(WinSendMsg(subMenu, MM_QUERYITEMCOUNT, 0, 0));
            for (int i = 0; i < cnt; i++) {
                USHORT id = SHORT1FROMMR(
                    WinSendMsg(subMenu, MM_ITEMIDFROMPOSITION, MPFROMSHORT(i), 0));
                if (id == SC_TASKMANAGER || id == SC_CLOSE) {
                    // accels for these entries always work in Qt, skip them
                    continue;
                }
                USHORT len = SHORT1FROMMR(
                    WinSendMsg(subMenu, MM_QUERYITEMTEXTLENGTH, MPFROMSHORT(id), 0));
                if (len++) {
                    char *text = new char[len];
                    WinSendMsg(subMenu, MM_QUERYITEMTEXT,
                        MPFROM2SHORT(id, len), MPFROMP(text));
                    char *tab = strrchr(text, '\t');
                    if (tab) {
                        *tab = 0;
                        WinSendMsg(subMenu, MM_SETITEMTEXT,
                            MPFROMSHORT(id), MPFROMP(text));
                    }
                    delete[] text;
                }
            }
            // sublclass the system menu to leave the menu mode completely
            // when the user presses the ESC key. by default, pressing ESC
            // while the pull-down menu is showing brings us to the menu bar,
            // which is confusing in the case of the system menu, because
            // there is only one item on the menu bar, and we cannot see
            // that it is active when the frame window has an icon.
            PFNWP oldProc = WinSubclassWindow(sysMenu, QtSysMenuProc);
            // set QtOldSysMenuProc only once: it must be the same for
            // all FID_SYSMENU windows.
            if (!QtOldSysMenuProc)
                QtOldSysMenuProc = oldProc;
        }
    }
}

/*****************************************************************************
  QWidget member functions
 *****************************************************************************/

void QWidgetPrivate::create_sys(WId window, bool initializeWindow, bool destroyOldWindow)
{
    // @todo when window is not zero, it represents an existing (external)
    // window handle we should create a QWidget "wrapper" for to incorporate it
    // to the Qt widget hierarchy. This functionality isn't really necessary on
    // OS/2 at the moment, so it is not currently supported (and the window
    // argument is simply ignored). Note that destroyOldWindow only makes
    //  sense when window is != 0 so it is also ignored.

    Q_ASSERT(window == 0);
    Q_UNUSED(destroyOldWindow);

    Q_Q(QWidget);
    static int sw = -1, sh = -1;

    Qt::WindowType type = q->windowType();
    Qt::WindowFlags flags = data.window_flags;

    bool topLevel = q->isWindow();
    bool popup = (type == Qt::Popup || type == Qt::ToolTip);
    bool dialog = (type == Qt::Dialog
                   || type == Qt::Sheet
                   || (flags & Qt::MSWindowsFixedSizeDialogHint));
    bool desktop = (type == Qt::Desktop);
    bool tool = (type == Qt::Tool || type == Qt::Drawer);

    WId id;

    QByteArray className = qt_reg_winclass(q).toLatin1();

    // @todo WindowStaysOnTopHint is ignored for now (does nothing)
    if (popup)
        flags |= Qt::WindowStaysOnTopHint; // a popup stays on top

    if (sw < 0) { // get the screen size
        sw = WinQuerySysValue(HWND_DESKTOP, SV_CXSCREEN);
        sh = WinQuerySysValue(HWND_DESKTOP, SV_CYSCREEN);
    }

    if (desktop && !q->testAttribute(Qt::WA_DontShowOnScreen)) { // desktop widget
        popup = false; // force this flags off
        // @todo use WinGetMaxPosition to take such things as XCenter into account?
        data.crect.setRect(0, 0, sw, sh);
    }

    QByteArray title;
    ULONG style = 0;
    ULONG fId = 0, fStyle = 0, fcFlags = 0;

    if (!desktop) {
        // @todo testing for Qt::WA_PaintUnclipped is commented out because it
        // is said that it causes some problems on Win32 and we also saw the
        // problems with this line enabled in Qt3 on OS/2 in QT_PM_NO_WIDGETMASK
        // mode (terrible flicker in QFileDialog because QSplitter used there
        // sets WA_PaintUnclipped). Note that in QT_PM_NATIVEWIDGETMASK mode it
        // doesn't play any role since all clipping is manually done by us
        // anyway (see below).
#if 0
        if (!testAttribute(Qt::WA_PaintUnclipped))
#endif
        {
#ifdef QT_PM_NATIVEWIDGETMASK
            // We don't use WS_CLIPSIBLINGS and WS_CLIPCHILDREN because when these
            // styles are set and the child (sibling) window has a non-NULL clip region,
            // PM still continues to exclude the entire child's rectangle from the
            // parent window's update region, ignoring its clip region. As a result,
            // areas outside the clip region are left unpainted. Instead, we correct
            // the update region of the window ourselves, on every WM_PAINT event.
            // Note: for top-level widgets, we specify WS_CLIPSIBLINGS anyway to let
            // the system do correct clipping for us (qt_WinProcessWindowObstacles()
            // relies on this). It's ok because top-level widgets cannot be non-
            // rectangular and therefore don't require our magic clipping procedure.
            if (topLevel)
                style |= WS_CLIPSIBLINGS;
#else
                style |= WS_CLIPSIBLINGS | WS_CLIPCHILDREN;
#endif
        }

        // for all top-level windows except popups we create a WC_FRAME
        // as a parent and owner.
    	if (topLevel && !popup) {
            if ((type == Qt::Window || dialog || tool)) {
                if (!(flags & Qt::FramelessWindowHint)) {
                    if (flags & Qt::MSWindowsFixedSizeDialogHint) {
                        fcFlags |= FCF_DLGBORDER;
                    } else if (tool) {
                        // note: while it's common that top-level tool widgets
                        // have a thiner frame, FCF_BORDER makes it too thin and
                        // it even cannot be resized. So, use FCF_SIZEBORDER too.
                        fcFlags |= FCF_SIZEBORDER;
                    } else {
                        fcFlags |= FCF_SIZEBORDER;
                    }
                }
                if (flags & Qt::WindowTitleHint)
                    fcFlags |= FCF_TITLEBAR;
                if (flags & Qt::WindowSystemMenuHint)
                    fcFlags |= FCF_SYSMENU | FCF_CLOSEBUTTON;
                if (flags & Qt::WindowMinimizeButtonHint)
                    fcFlags |= FCF_MINBUTTON;
                if (flags & Qt::WindowMaximizeButtonHint)
                    fcFlags |= FCF_MAXBUTTON;
            } else {
                fcFlags |= FCF_BORDER;
            }

            fStyle |= FS_NOMOVEWITHOWNER | FS_NOBYTEALIGN;
            fStyle |= WS_CLIPSIBLINGS | WS_CLIPCHILDREN;
    	}
    }

    if (flags & Qt::WindowTitleHint) {
        QString t = topLevel ? qAppName() : q->objectName();
        t = t.left(1).toUpper() + t.mid(1).toLower();
        title = t.toLocal8Bit();
    }

    // The Qt::WA_WState_Created flag is checked by translateConfigEvent() in
    // qapplication_pm.cpp. We switch it off temporarily to avoid move
    // and resize events during creation
    q->setAttribute(Qt::WA_WState_Created, false);

    if (desktop) { // desktop widget
        id = WinQueryDesktopWindow(0, 0);
        // @todo commented out on Win32 too
//      QWidget *otherDesktop = QWidget::find(id); // is there another desktop?
//      if (otherDesktop && otherDesktop->testWFlags(Qt::WPaintDesktop)) {
//          otherDesktop->d_func()->setWinId(0); // remove id from widget mapper
//          setWinId(id); // make sure otherDesktop is found first
//          otherDesktop->d_func()->setWinId(id);
//      } else {
            setWinId(id);
//      }
    } else if (topLevel) {
        // create top-level widget
        if (!popup) {
            HWND ownerw = NULLHANDLE;
            QWidget *parent = q->parentWidget();

            if (parent && !(parent->windowType() == Qt::Desktop))
                ownerw = parent->window()->d_func()->frameWinId();
            // create WC_FRAME
            FRAMECDATA fcData;
            fcData.cb = sizeof(FRAMECDATA);
            fcData.flCreateFlags = fcFlags;
            fcData.hmodResources = NULL;
            fcData.idResources = 0;
            // check whether a default icon is present in .EXE and use it if so
            ULONG sz = 0;
            if (DosQueryResourceSize(NULL, RT_POINTER, 1, &sz) == 0) {
                fcData.flCreateFlags |= FCF_ICON;
                fcData.idResources = 1;
            }
#if defined(QT_DEBUGWINCREATEDESTROY)
            qDebug() <<   "|Creating top level window (frame)" << q
                     << "\n|  owner" << qDebugHWND(ownerw)
                     << "\n|  title" << title
                     << "\n|  style" << qDebugFmtHex(fStyle)
                     << "\n|  fcFlags" << qDebugFmtHex(fcFlags);
#endif
            fId = WinCreateWindow(HWND_DESKTOP, WC_FRAME, title, fStyle,
                                  0, 0, 0, 0, ownerw, HWND_TOP, 0,
                                  &fcData, NULL);
#if defined(QT_DEBUGWINCREATEDESTROY)
            qDebug() << "|  hwnd" << qDebugFmtHex(fId);
#endif
            if (fId == NULLHANDLE)
                qWarning("QWidget::create(): WinCreateWindow(WC_FRAME) "
                         "failed with 0x%08lX", WinGetLastError(0));

            if (fcData.flCreateFlags & FCF_ICON) {
                // mark that we already have the window icon taken from .EXE to
                // prevent setWindowIcon_sys() from resetting it to nothing
                createTLExtra();
                extra->topextra->iconPointer =
                    (HPOINTER)WinSendMsg(fId, WM_QUERYICON, 0, 0);
            }

            PFNWP oldProc = WinSubclassWindow(fId, QtFrameProc);
            // remember QtOldFrameProc only once: it's the same for
            // all WC_FRAME windows.
            if (!QtOldFrameProc)
                QtOldFrameProc = oldProc;

            // subclass all standard frame controls to get non-client mouse events
            // (note: the size of the ctls array must match QtOldFrameCtlProcs)
            HWND ctls[FID_HORZSCROLL - FID_SYSMENU + 1];
            if (WinMultWindowFromIDs(fId, ctls, FID_SYSMENU, FID_HORZSCROLL) > 0) {
                for (size_t i = 0; i < sizeof(ctls)/sizeof(ctls[0]); ++i) {
                    if (ctls[i] != NULLHANDLE) {
                        oldProc = WinSubclassWindow(ctls[i], QtFrameCtlProc);
                        // remember the old proc only once: it's the same for
                        // all standard frame control windows.
                        if (!QtOldFrameCtlProcs[i])
                            QtOldFrameCtlProcs[i] = oldProc;
                    }
                }
            }

            removeSysMenuAccels(fId);

            // create client window
#if defined(QT_DEBUGWINCREATEDESTROY)
            qDebug() <<   "|Creating top level window (client)" << q
                     << "\n|  owner & parent" << qDebugHWND(fId)
                     << "\n|  class" << className
                     << "\n|  title" << title
                     << "\n|  style" << qDebugFmtHex(style);
#endif
            // note that we place the client on top (HWND_TOP) to exclude other
            // frame controls from being analyzed in qt_WinProcessWindowObstacles
            id = WinCreateWindow(fId, className, title, style, 0, 0, 0, 0,
                                 fId, HWND_TOP, FID_CLIENT, NULL, NULL);
        } else {
#if defined(QT_DEBUGWINCREATEDESTROY)
            qDebug() <<   "|Creating top level window (popup)" << q
                     << "\n|  class" << className
                     << "\n|  title" << title
                     << "\n|  style" << qDebugFmtHex(style);
#endif
            id = WinCreateWindow(HWND_DESKTOP, className, title, style,
                                 0, 0, 0, 0, NULLHANDLE, HWND_TOP, 0, NULL, NULL);
        }
#if defined(QT_DEBUGWINCREATEDESTROY)
        qDebug() << "|  hwnd" << qDebugFmtHex(id);
#endif
        if (id == NULLHANDLE)
            qWarning("QWidget::create(): WinCreateWindow "
                     "failed with 0x%08lX", WinGetLastError(0));
        setWinId(id);

        // it isn't mentioned in PMREF that PM is obliged to initialize window
        // data with zeroes (although seems to), so do it ourselves
        for (LONG i = 0; i <= (LONG) (QT_EXTRAWINDATASIZE - 4); i += 4)
            WinSetWindowULong(id, i, 0);

        SWP swp;
        // Get the default window position and size. Note that when the
        // FS_SHELLPOSITION flag is specified during WC_FRAME window
        // creation its size and position remains zero until it is shown
        // for the first time. So, we don't use FS_SHELLPOSITION but emulate
        // its functionality here.
        WinQueryTaskSizePos(0, 0, &swp);

        // update position & initial size of POPUP window
        const bool wasMoved = q->testAttribute(Qt::WA_Moved);
        const bool wasResized = q->testAttribute(Qt::WA_Resized);
        const bool isVisibleOnScreen = !q->testAttribute(Qt::WA_DontShowOnScreen);
        if (popup && initializeWindow && isVisibleOnScreen) {
            if (!wasResized) {
                swp.cx = sw / 2;
                swp.cy = 4 * sh / 10;
            } else {
                swp.cx = data.crect.width();
                swp.cy = data.crect.height();
            }
            if (!wasMoved) {
                swp.x = sw / 2 - swp.cx / 2;
                swp.y = sh / 2 - swp.cy / 2;
            } else {
                swp.x = data.crect.x();
                swp.y = data.crect.y();
                // flip y coordinate
                swp.y = sh - (swp.y + swp.cy);
            }
        }

        ULONG fl = SWP_SIZE | SWP_MOVE;
        HWND insertBehind = NULLHANDLE;
        if ((flags & Qt::WindowStaysOnTopHint) || (type == Qt::ToolTip)) {
            insertBehind = HWND_TOP;
            fl |= SWP_ZORDER;
            if (flags & Qt::WindowStaysOnBottomHint)
                qWarning() << "QWidget: Incompatible window flags: the window "
                              "can't be on top and on bottom at the same time";
        } else if (flags & Qt::WindowStaysOnBottomHint) {
            insertBehind = HWND_BOTTOM;
            fl |= SWP_ZORDER;
        }
        WinSetWindowPos(popup ? id : fId, insertBehind,
                        swp.x, swp.y, swp.cx, swp.cy, fl);

        if (!popup) {
            QTLWExtra *top = topData();
            top->fId = fId;
            WinQueryWindowPos(fId, &swp);
            SWP cswp;
            WinQueryWindowPos(id, &cswp);
            // flip y coordinates
            swp.y = sh - (swp.y + swp.cy);
            cswp.y = swp.cy - (cswp.y + cswp.cy);
            // store frame dimensions
            QRect &fs = top->frameStrut;
            fs.setCoords(cswp.x, cswp.y, swp.cx - cswp.x - cswp.cx,
                                         swp.cy - cswp.y - cswp.cy);
            data.fstrut_dirty = false;
            if (wasMoved || wasResized) {
                // resize & move if necessary (we couldn't do it earlier
                // because we didn't know the frame dimensions yet)
                if (wasMoved) {
                    // QWidget::move() includes frame strut so no correction is
                    // necessary (crect was abused to store the frame position
                    // until window creation)
                    swp.x = data.crect.x();
                    swp.y = data.crect.y();
                }
                if (wasResized) {
                    swp.cx = data.crect.width() + fs.left() + fs.right();
                    swp.cy = data.crect.height() + fs.top() + fs.bottom();
                }
                // flip y coordinate
                swp.y = sh - (swp.y + swp.cy);
                WinSetWindowPos(fId, NULLHANDLE, swp.x, swp.y, swp.cx, swp.cy,
                                SWP_SIZE | SWP_MOVE);
            }
        }

        if (!popup || (initializeWindow && isVisibleOnScreen)) {
            // fetch the actual geometry
            WinQueryWindowPos(popup ? id : fId, &swp);
            // flip y coordinate
            swp.y = sh - (swp.y + swp.cy);
            if (popup) {
                data.crect.setRect(swp.x, swp.y, swp.cx, swp.cy);
            } else {
                const QRect &fs = topData()->frameStrut;
                data.crect.setRect(swp.x + fs.left(), swp.y + fs.top(),
                                   swp.cx - fs.left() - fs.right(),
                                   swp.cy - fs.top() - fs.bottom());
            }
        }
    } else if (q->testAttribute(Qt::WA_NativeWindow) || paintOnScreen()) {
        // create child widget
        Q_ASSERT(q->parentWidget());
        HWND parentw = q->parentWidget()->effectiveWinId();

#if defined(QT_DEBUGWINCREATEDESTROY)
        qDebug() <<   "|Creating child window" << q
                 << "\n|  owner & parent" << qDebugHWND(parentw)
                 << "\n|  class" << className
                 << "\n|  title" << title
                 << "\n|  style" << qDebugFmtHex(style);
#endif
        id = WinCreateWindow(parentw, className, title, style,
                             data.crect.left(),
                             // flip y coordinate
                             q->parentWidget()->height() - data.crect.bottom() - 1,
                             data.crect.width(), data.crect.height(),
                             parentw, HWND_TOP, 0, NULL, NULL);
#if defined(QT_DEBUGWINCREATEDESTROY)
        qDebug() << "|  hwnd" << qDebugFmtHex(id);
#endif
        if (id == NULLHANDLE)
            qWarning("QWidget::create(): WinCreateWindow "
                     "failed with 0x%08lX", WinGetLastError(0));
    	setWinId(id);
    }

    if (desktop) {
        q->setAttribute(Qt::WA_WState_Visible);
    }

    q->setAttribute(Qt::WA_WState_Created); // accept move/resize events

    hd = 0; // no presentation space

    if (extra && !extra->mask.isEmpty())
        setMask_sys(extra->mask);

    if (topLevel && (data.crect.width() == 0 || data.crect.height() == 0)) {
        q->setAttribute(Qt::WA_OutsideWSRange, true);
    }

    if (!topLevel && q->testAttribute(Qt::WA_NativeWindow) && q->testAttribute(Qt::WA_Mapped)) {
        Q_ASSERT(q->internalWinId() != NULLHANDLE);
        WinShowWindow(q->internalWinId(), TRUE);
    }
}

void QWidget::destroy(bool destroyWindow, bool destroySubWindows)
{
    Q_D(QWidget);
    if (!isWindow() && parentWidget())
        parentWidget()->d_func()->invalidateBuffer(d->effectiveRectFor(geometry()));
    d->deactivateWidgetCleanup();
    if (testAttribute(Qt::WA_WState_Created)) {
        setAttribute(Qt::WA_WState_Created, false);
        for(int i = 0; i < d->children.size(); ++i) { // destroy all widget children
            register QObject *obj = d->children.at(i);
            if (obj->isWidgetType())
                ((QWidget*)obj)->destroy(destroySubWindows,
                                         destroySubWindows);
        }
        if (mouseGrb == this)
            releaseMouse();
        if (keyboardGrb == this)
            releaseKeyboard();
        if (testAttribute(Qt::WA_ShowModal)) // just be sure we leave modal
            QApplicationPrivate::leaveModal(this);
        else if ((windowType() == Qt::Popup))
            qApp->d_func()->closePopup(this);
        if (destroyWindow && !(windowType() == Qt::Desktop) &&
            d->frameWinId() != NULLHANDLE) {
            // destroy HWND
            HWND id = d->frameWinId();
#if defined(QT_DEBUGWINCREATEDESTROY)
            qDebug() << "|Destroying window" << this
                     << "\n|  hwnd"  << qDebugFmtHex(id);
            if (id != internalWinId())
                qDebug() << "|  hwnd" << qDebugFmtHex(internalWinId()) << "(client)";
#endif
            qt_WinDestroyWindow(id);
        }
        QT_TRY {
            QTLWExtra *top = d->maybeTopData();
            if (top)
                top->fId = 0;
            d->setWinId(0);
        } QT_CATCH (const std::bad_alloc &) {
            // swallow - destructors must not throw
        }
    }
}

void QWidgetPrivate::reparentChildren()
{
    Q_Q(QWidget);
    QObjectList chlist = q->children();
    for (int i = 0; i < chlist.size(); ++i) { // reparent children
        QObject *obj = chlist.at(i);
        if (obj->isWidgetType()) {
            QWidget *w = (QWidget *)obj;
            if ((w->windowType() == Qt::Popup)) {
                ;
            } else if (w->isWindow()) {
                bool showIt = w->isVisible();
                QPoint old_pos = w->pos();
                w->setParent(q, w->windowFlags());
                w->move(old_pos);
                if (showIt)
                    w->show();
            } else {
                w->d_func()->invalidateBuffer(w->rect());
                WinSetParent(w->effectiveWinId(), q->effectiveWinId(), FALSE);
                WinSetOwner(w->effectiveWinId(), q->effectiveWinId());
                w->d_func()->reparentChildren();
#if 0 // @todo check if this is really necessary any longer
                // bring PM coords into accordance with Qt coords,
                // otherwise setGeometry() below will wrongly position
                // children if this widget manages their layout.
                SWP swp;
                int hd = q->height() - old_height;
                WinQueryWindowPos(w->effectiveWinId(), &swp);
                swp.y += hd;
                WinSetWindowPos(w->effectiveWinId(), 0, swp.x, swp.y, 0, 0, SWP_MOVE);
#endif
            }
        }
    }
}

void QWidgetPrivate::setParent_sys(QWidget *parent, Qt::WindowFlags f)
{
    Q_Q(QWidget);
    bool wasCreated = q->testAttribute(Qt::WA_WState_Created);
    if (q->isVisible() && q->parentWidget() && parent != q->parentWidget())
        q->parentWidget()->d_func()->invalidateBuffer(effectiveRectFor(q->geometry()));

    WId old_fid = frameWinId();

    // hide and reparent our own window away. Otherwise we might get
    // destroyed when emitting the child remove event below. See QWorkspace.
    if (q->isVisible() && old_fid != NULLHANDLE) {
        qt_WinSetWindowPos(old_fid, 0, 0, 0, 0, 0, SWP_HIDE);
        WinSetParent(old_fid, HWND_OBJECT, FALSE);
        WinSetOwner(old_fid, NULLHANDLE);
    }
    bool dropSiteWasRegistered = false;
    if (q->testAttribute(Qt::WA_DropSiteRegistered)) {
        dropSiteWasRegistered = true;
        q->setAttribute(Qt::WA_DropSiteRegistered, false); // ole dnd unregister (we will register again below)
    }

    if ((q->windowType() == Qt::Desktop))
        old_fid = 0;

    if (extra && extra->topextra)
        extra->topextra->fId = 0;
    setWinId(0);

    QObjectPrivate::setParent_helper(parent);
    bool explicitlyHidden = q->testAttribute(Qt::WA_WState_Hidden) && q->testAttribute(Qt::WA_WState_ExplicitShowHide);

    data.window_flags = f;
    data.fstrut_dirty = true;
    q->setAttribute(Qt::WA_WState_Created, false);
    q->setAttribute(Qt::WA_WState_Visible, false);
    q->setAttribute(Qt::WA_WState_Hidden, false);
    adjustFlags(data.window_flags, q);
    // keep compatibility with previous versions, we need to preserve the created state
    // (but we recreate the winId for the widget being reparented, again for compatibility)
    if (wasCreated || (!q->isWindow() && parent->testAttribute(Qt::WA_WState_Created)))
        createWinId();
    if (q->isWindow() || (!parent || parent->isVisible()) || explicitlyHidden)
        q->setAttribute(Qt::WA_WState_Hidden);
    q->setAttribute(Qt::WA_WState_ExplicitShowHide, explicitlyHidden);

    if (wasCreated) {
        reparentChildren();
    }

    if (extra && !extra->mask.isEmpty()) {
        QRegion r = extra->mask;
        extra->mask = QRegion();
        q->setMask(r);
    }
    if (extra && extra->topextra && !extra->topextra->caption.isEmpty()) {
        setWindowIcon_sys(true);
        setWindowTitle_helper(extra->topextra->caption);
    }

    if (old_fid != NULLHANDLE && q->windowType() != Qt::Desktop) {
#if defined(QT_DEBUGWINCREATEDESTROY)
            qDebug() << "|Destroying window (reparent)" << q
                     << "\n|  hwnd"  << qDebugFmtHex(old_fid);
#endif
        qt_WinDestroyWindow(old_fid);
    }

    if (q->testAttribute(Qt::WA_AcceptDrops) || dropSiteWasRegistered
        || (!q->isWindow() && q->parentWidget() && q->parentWidget()->testAttribute(Qt::WA_DropSiteRegistered)))
        q->setAttribute(Qt::WA_DropSiteRegistered, true);

    invalidateBuffer(q->rect());
}

QPoint QWidget::mapToGlobal(const QPoint &pos) const
{
    Q_D(const QWidget);
    QWidget *parentWindow = window();
    if (!isVisible() || parentWindow->isMinimized() || !testAttribute(Qt::WA_WState_Created) || !internalWinId()) {
        QPoint toGlobal = mapTo(parentWindow, pos) + parentWindow->pos();
        // Adjust for window decorations
        toGlobal += parentWindow->geometry().topLeft() - parentWindow->frameGeometry().topLeft();
        return toGlobal;
    }
    QPoint pos2 = d->mapToWS(pos);
    POINTL ptl;
    ptl.x = pos2.x();
    // flip y (local) coordinate
    ptl.y = height() - (pos2.y() + 1);
    WinMapWindowPoints(internalWinId(), HWND_DESKTOP, &ptl, 1);
    // flip y (global) coordinate
    ptl.y = qt_display_height() - (ptl.y + 1);
    return QPoint(ptl.x, ptl.y);
}

QPoint QWidget::mapFromGlobal(const QPoint &pos) const
{
    Q_D(const QWidget);
    QWidget *parentWindow = window();
    if (!isVisible() || parentWindow->isMinimized() || !testAttribute(Qt::WA_WState_Created) || !internalWinId()) {
        QPoint fromGlobal = mapFrom(parentWindow, pos - parentWindow->pos());
        // Adjust for window decorations
        fromGlobal -= parentWindow->geometry().topLeft() - parentWindow->frameGeometry().topLeft();
        return fromGlobal;
    }
    POINTL ptl;
    ptl.x = pos.x();
    // flip y (global) coordinate
    ptl.y = qt_display_height() - (pos.y() + 1);
    WinMapWindowPoints(HWND_DESKTOP, internalWinId(), &ptl, 1);
    // flip y (local) coordinate
    ptl.y = height() - (ptl.y + 1);
    return d->mapFromWS(QPoint(ptl.x, ptl.y));
}

void QWidgetPrivate::updateSystemBackground() {}

#ifndef QT_NO_CURSOR

void QWidgetPrivate::setCursor_sys(const QCursor &cursor)
{
    Q_UNUSED(cursor);
    Q_Q(QWidget);
    qt_pm_set_cursor(q, false);
}

void QWidgetPrivate::unsetCursor_sys()
{
    Q_Q(QWidget);
    qt_pm_set_cursor(q, false);
}
#endif

void QWidgetPrivate::setWindowTitle_sys(const QString &caption)
{
    Q_Q(QWidget);
    if (!q->isWindow())
        return;

    Q_ASSERT(q->testAttribute(Qt::WA_WState_Created));

    QByteArray cap = caption.toLocal8Bit();
    WinSetWindowText(frameWinId(), cap);

    HSWITCH swEntry = topData()->swEntry;
    if (swEntry) {
        SWCNTRL swc;
        WinQuerySwitchEntry(swEntry, &swc);
        strncpy(swc.szSwtitle, cap, sizeof(swc.szSwtitle)-1);
        swc.szSwtitle[sizeof(swc.szSwtitle)-1] = 0;
        WinChangeSwitchEntry(swEntry, &swc);
    }
}

void QWidgetPrivate::setWindowIcon_sys(bool forceReset)
{
    Q_Q(QWidget);
    if (!q->testAttribute(Qt::WA_WState_Created) || !q->isWindow())
        return;
    QTLWExtra *x = this->topData();
    if (x->iconPointer != NULLHANDLE && !forceReset)
        // already been set
        return;

    if (x->iconPointer != NULLHANDLE)
        WinDestroyPointer(x->iconPointer);

    x->iconPointer = QPixmap::toPmHPOINTER(q->windowIcon());
    WinSendMsg(frameWinId(), WM_SETICON, (MPARAM)x->iconPointer, NULL);
}

void QWidgetPrivate::setWindowIconText_sys(const QString &iconText)
{
    Q_UNUSED(iconText);
}

QCursor *qt_grab_cursor()
{
    return mouseGrbCur;
}

void QWidget::grabMouse()
{
    Q_D(QWidget);
    if (!qt_nograb()) {
        if (mouseGrb)
            mouseGrb->releaseMouse();
        Q_ASSERT(testAttribute(Qt::WA_WState_Created));
        WinSetCapture(HWND_DESKTOP, d->effectiveFrameWinId());
        mouseGrb = this;
#ifndef QT_NO_CURSOR
        mouseGrbCur = new QCursor(mouseGrb->cursor());
#endif
    }
}

#ifndef QT_NO_CURSOR
void QWidget::grabMouse(const QCursor &cursor)
{
    Q_D(QWidget);
    if (!qt_nograb()) {
        if (mouseGrb)
            mouseGrb->releaseMouse();
        Q_ASSERT(testAttribute(Qt::WA_WState_Created));
        WinSetCapture(HWND_DESKTOP, d->effectiveFrameWinId());
        mouseGrbCur = new QCursor(cursor);
        WinSetPointer(HWND_DESKTOP, mouseGrbCur->handle());
        mouseGrb = this;
    }
}
#endif

void QWidget::releaseMouse()
{
    if (!qt_nograb() && mouseGrb == this) {
        WinSetCapture(HWND_DESKTOP, 0);
        if (mouseGrbCur) {
            delete mouseGrbCur;
            mouseGrbCur = 0;
        }
        mouseGrb = 0;
    }
}

void QWidget::grabKeyboard()
{
    if (!qt_nograb()) {
        if (keyboardGrb)
            keyboardGrb->releaseKeyboard();
        keyboardGrb = this;
    }
}

void QWidget::releaseKeyboard()
{
    if (!qt_nograb() && keyboardGrb == this)
        keyboardGrb = 0;
}

QWidget *QWidget::mouseGrabber()
{
    return mouseGrb;
}

QWidget *QWidget::keyboardGrabber()
{
    return keyboardGrb;
}

void QWidget::activateWindow()
{
    window()->createWinId();
    WinSetActiveWindow(HWND_DESKTOP, window()->d_func()->frameWinId());
}

void QWidget::setWindowState(Qt::WindowStates newstate)
{
    Q_D(QWidget);
    Qt::WindowStates oldstate = windowState();
    if (oldstate == newstate)
        return;

    ULONG fl = (newstate & Qt::WindowActive) ? SWP_ACTIVATE : 0;

    if (isWindow()) {
        createWinId();
        Q_ASSERT(testAttribute(Qt::WA_WState_Created));

        HWND fId = d->frameWinId();
        Q_ASSERT(fId != NULLHANDLE);

        // Ensure the initial size is valid, since we store it as normalGeometry below.
        if (!testAttribute(Qt::WA_Resized) && !isVisible())
            adjustSize();

        if ((oldstate & Qt::WindowMaximized) != (newstate & Qt::WindowMaximized)) {
            if (newstate & Qt::WindowMaximized && !(oldstate & Qt::WindowFullScreen))
                d->topData()->normalGeometry = geometry();
            if (isVisible() && !(newstate & Qt::WindowMinimized)) {
                fl |= (newstate & Qt::WindowMaximized) ? SWP_MAXIMIZE : SWP_RESTORE;
                WinSetWindowPos(fId, 0, 0, 0, 0, 0, fl);
                if (!(newstate & Qt::WindowFullScreen)) {
                    QRect r = d->topData()->normalGeometry;
                    if (!(newstate & Qt::WindowMaximized) && r.width() >= 0) {
                        if (pos() != r.topLeft() || size() !=r.size()) {
                            d->topData()->normalGeometry = QRect(0,0,-1,-1);
                            setGeometry(r);
                        }
                    }
                } else {
                    // @todo most likely, we don't need this as in PM the frame
                    // strut seems to never change during window lifetime
//                  d->updateFrameStrut();
                }
            }
        }

        if ((oldstate & Qt::WindowFullScreen) != (newstate & Qt::WindowFullScreen)) {
            if (newstate & Qt::WindowFullScreen) {
                if (d->topData()->normalGeometry.width() < 0 && !(oldstate & Qt::WindowMaximized))
                    d->topData()->normalGeometry = geometry();
                QRect r = QApplication::desktop()->screenGeometry(this);
                QRect fs(d->frameStrut());
                r.adjust(-fs.left(), -fs.top(), fs.right(), fs.bottom());
                fl |= SWP_ZORDER | SWP_MOVE | SWP_SIZE;
                WinSetWindowPos(fId, HWND_TOP, r.left(),
                                // flip y coodrinate
                                qt_display_height() - (r.top() + r.height()),
                                r.width(), r.height(), fl);
            } else {
                // preserve maximized state
                if (isVisible()) {
                    fl |= (newstate & Qt::WindowMaximized) ? SWP_MAXIMIZE : SWP_RESTORE;
                    WinSetWindowPos(fId, 0, 0, 0, 0, 0, fl);
                }

                if (!(newstate & Qt::WindowMaximized)) {
                    QRect r = d->topData()->normalGeometry;
                    d->topData()->normalGeometry = QRect(0,0,-1,-1);
                    if (r.isValid())
                        setGeometry(r);
                }
            }
        }

        if ((oldstate & Qt::WindowMinimized) != (newstate & Qt::WindowMinimized)) {
            if (isVisible()) {
                fl |= (newstate & Qt::WindowMinimized) ? SWP_MINIMIZE :
                      (newstate & Qt::WindowMaximized) ? SWP_MAXIMIZE : SWP_RESTORE;
                WinSetWindowPos(fId, 0, 0, 0, 0, 0, fl);
            }
        }
    }

    data->window_state = newstate;
    // Note: QWindowStateChangeEvent is sent from QtWndProc(WM_MINMAXFRAME)
}

/*!
    Adds the PM event filter to this widget. This is functionally equivalent to
    overriding the QWidget::pmEvent() virtual method.

    Note that the event filters added with this method are called before
    QWidget::pmEvent() and may prevent Qt from further event processing
    including passing it to QWidget::pmEvent(). The order in which these filters
    are called corresponds to the order they are added to this widget.

    \warning This function is not portable and exists only on OS/2.
*/
void QWidget::addPmEventFilter(PmEventFilter *filter)
{
    Q_D(QWidget);
    d->createExtra();
    d->extra->pmEventFilters.prepend(filter);
}

/*!
    Removes the PM event filter added with addPmEventFilter() from this widget.

    \warning This function is not portable and exists only on OS/2.
*/
void QWidget::removePmEventFilter(PmEventFilter *filter)
{
    Q_D(QWidget);
    if (d->extra)
        d->extra->pmEventFilters.removeOne(filter);
}

/*
    \internal
    Platform-specific part of QWidget::hide().
*/
void QWidgetPrivate::hide_sys()
{
    Q_Q(QWidget);
    deactivateWidgetCleanup();
    Q_ASSERT(q->testAttribute(Qt::WA_WState_Created));
    if (q->windowFlags() != Qt::Desktop) {
        HWND wid = frameWinId();
        if (wid != NULLHANDLE) {
            ULONG fl = SWP_HIDE;
            if (q->isWindow()) {
                if ((q->windowFlags() & Qt::Popup) != Qt::Popup)
                    fl |= SWP_DEACTIVATE;
            }
            qt_WinSetWindowPos(wid, 0, 0, 0, 0, 0, fl);
            HSWITCH swEntry = maybeTopData() ? maybeTopData()->swEntry : NULLHANDLE;
            if (swEntry != NULLHANDLE) {
                SWCNTRL swc;
                WinQuerySwitchEntry(swEntry, &swc);
                swc.uchVisibility = SWL_INVISIBLE;
                WinChangeSwitchEntry(swEntry, &swc);
            }
        }
    }
    if (q->isWindow()) {
        if (QWidgetBackingStore *bs = maybeBackingStore())
            bs->releaseBuffer();
    } else {
        invalidateBuffer(q->rect());
    }
    q->setAttribute(Qt::WA_Mapped, false);
}

/*
    \internal
    Platform-specific part of QWidget::show().
*/
void QWidgetPrivate::show_sys()
{
    Q_Q(QWidget);
    if (q->testAttribute(Qt::WA_OutsideWSRange))
        return;
    q->setAttribute(Qt::WA_Mapped);
    Q_ASSERT(q->testAttribute(Qt::WA_WState_Created));

    if (q->testAttribute(Qt::WA_DontShowOnScreen)) {
        invalidateBuffer(q->rect());
        return;
    }

    HWND wid = frameWinId();
    if (wid != NULLHANDLE) {
        ULONG fl = SWP_SHOW;
        if (q->isWindow()) {
            if (q->isMinimized()) {
                fl = SWP_MINIMIZE;
            } else if (q->isMaximized()) {
                fl |= SWP_MAXIMIZE;
            } else {
                fl |= SWP_RESTORE;
            }

            if (!(q->testAttribute(Qt::WA_ShowWithoutActivating)
                  || (q->windowType() == Qt::Popup)
                  || (q->windowType() == Qt::ToolTip)
                  || (q->windowType() == Qt::Tool))) {
                fl |= SWP_ACTIVATE;
            }
        }

        qt_WinSetWindowPos(wid, 0, 0, 0, 0, 0, fl);

        if (q->isWindow()) {
            if (q->windowType() != Qt::Popup &&
                q->windowType() != Qt::ToolTip &&
                q->windowType() != Qt::Tool &&
                (q->windowType() != Qt::Dialog || !q->parentWidget()) &&
                (q->windowType() != Qt::SplashScreen ||
                 (data.window_flags & Qt::WindowTitleHint))
            ) {
                HSWITCH &swEntry = topData()->swEntry;
                if (!swEntry) {
                    // lazily create a new window list entry
                    PID pid;
                    WinQueryWindowProcess(wid, &pid, NULL);
                    SWCNTRL swc;
                    memset(&swc, 0, sizeof(SWCNTRL));
                    swc.hwnd = wid;
                    swc.idProcess = pid;
                    swc.uchVisibility = SWL_VISIBLE;
                    swc.fbJump = SWL_JUMPABLE;
                    WinQueryWindowText(wid, sizeof(swc.szSwtitle), swc.szSwtitle);
                    swEntry = WinAddSwitchEntry(&swc);
                } else {
                    SWCNTRL swc;
                    WinQuerySwitchEntry(swEntry, &swc);
                    swc.uchVisibility = SWL_VISIBLE;
                    WinChangeSwitchEntry(swEntry, &swc);
                }
            }

            ULONG wstyle = WinQueryWindowULong(wid, QWL_STYLE);
            if (wstyle & WS_MINIMIZED)
                data.window_state |= Qt::WindowMinimized;
            if (wstyle & WS_MAXIMIZED)
                data.window_state |= Qt::WindowMaximized;
        }
    }

    invalidateBuffer(q->rect());
}

void QWidgetPrivate::setFocus_sys()
{
    Q_Q(QWidget);
    if (q->testAttribute(Qt::WA_WState_Created) && q->window()->windowType() != Qt::Popup)
        WinSetFocus(HWND_DESKTOP, q->effectiveWinId());
}

void QWidgetPrivate::raise_sys()
{
    Q_Q(QWidget);
    Q_ASSERT(q->testAttribute(Qt::WA_WState_Created));
    if (frameWinId() != NULLHANDLE)
        qt_WinSetWindowPos(frameWinId(), HWND_TOP, 0, 0, 0, 0, SWP_ZORDER);
    Q_UNUSED(q);
}

void QWidgetPrivate::lower_sys()
{
    Q_Q(QWidget);
    Q_ASSERT(q->testAttribute(Qt::WA_WState_Created));
    if (frameWinId() != NULLHANDLE)
        qt_WinSetWindowPos(frameWinId(), HWND_BOTTOM, 0, 0, 0, 0, SWP_ZORDER);
    invalidateBuffer(q->rect());
}

void QWidgetPrivate::stackUnder_sys(QWidget* w)
{
    Q_Q(QWidget);
    Q_ASSERT(q->testAttribute(Qt::WA_WState_Created));
    if (frameWinId() != NULLHANDLE && w->d_func()->frameWinId() != NULLHANDLE)
        WinSetWindowPos(frameWinId(), w->d_func()->frameWinId(), 0, 0, 0, 0, SWP_ZORDER);
    invalidateBuffer(q->rect());
}

#define XCOORD_MAX 32767
#define WRECT_MAX 32767

/*
  Helper function for non-toplevel widgets. Helps to map Qt's 32bit
  coordinate system to PM 16bit coordinate system.

  This code is duplicated from the X11 code, so any changes there
  should also (most likely) be reflected here.

  (In all comments below: s/X/PM/g)
 */

void QWidgetPrivate::setWSGeometry(bool dontShow, const QRect &)
{
    Q_Q(QWidget);
    Q_ASSERT(q->testAttribute(Qt::WA_WState_Created));

    /*
      There are up to four different coordinate systems here:
      Qt coordinate system for this widget.
      X coordinate system for this widget (relative to wrect).
      Qt coordinate system for parent
      X coordinate system for parent (relative to parent's wrect).
     */
    QRect validRange(-XCOORD_MAX,-XCOORD_MAX, 2*XCOORD_MAX, 2*XCOORD_MAX);
    QRect wrectRange(-WRECT_MAX,-WRECT_MAX, 2*WRECT_MAX, 2*WRECT_MAX);
    QRect wrect;
    //xrect is the X geometry of my X widget. (starts out in  parent's Qt coord sys, and ends up in parent's X coord sys)
    QRect xrect = data.crect;

    const QWidget *const parent = q->parentWidget();
    QRect parentWRect = parent->data->wrect;

    if (parentWRect.isValid()) {
        // parent is clipped, and we have to clip to the same limit as parent
        if (!parentWRect.contains(xrect)) {
            xrect &= parentWRect;
            wrect = xrect;
            //translate from parent's to my Qt coord sys
            wrect.translate(-data.crect.topLeft());
        }
        //translate from parent's Qt coords to parent's X coords
        xrect.translate(-parentWRect.topLeft());

    } else {
        // parent is not clipped, we may or may not have to clip

        if (data.wrect.isValid() && QRect(QPoint(),data.crect.size()).contains(data.wrect)) {
            // This is where the main optimization is: we are already
            // clipped, and if our clip is still valid, we can just
            // move our window, and do not need to move or clip
            // children

            QRect vrect = xrect & parent->rect();
            vrect.translate(-data.crect.topLeft()); //the part of me that's visible through parent, in my Qt coords
            if (data.wrect.contains(vrect)) {
                xrect = data.wrect;
                xrect.translate(data.crect.topLeft());
                if (q->internalWinId() != NULLHANDLE) {
                    Q_ASSERT(parent->internalWinId() != NULLHANDLE);
                    // important: use real parent height (it may not match crect yet)
                    RECTL rcl;
                    WinQueryWindowRect(parent->internalWinId(), &rcl);
                    qt_WinSetWindowPos(q->internalWinId(), 0, xrect.x(),
                                       // flip y coordinate
                                       rcl.yTop - (xrect.y() + xrect.height()),
                                       xrect.width(), xrect.height(),
                                       SWP_MOVE | SWP_SIZE);
                }
                return;
            }
        }

        if (!validRange.contains(xrect)) {
            // we are too big, and must clip
            xrect &=wrectRange;
            wrect = xrect;
            wrect.translate(-data.crect.topLeft());
            //parent's X coord system is equal to parent's Qt coord
            //sys, so we don't need to map xrect.
        }

    }


    // unmap if we are outside the valid window system coord system
    bool outsideRange = !xrect.isValid();
    bool mapWindow = false;
    if (q->testAttribute(Qt::WA_OutsideWSRange) != outsideRange) {
        q->setAttribute(Qt::WA_OutsideWSRange, outsideRange);
        if (outsideRange) {
            if (q->internalWinId() != NULLHANDLE)
                qt_WinSetWindowPos(q->internalWinId(), 0, 0, 0, 0, 0, SWP_HIDE);
            q->setAttribute(Qt::WA_Mapped, false);
        } else if (!q->isHidden()) {
            mapWindow = true;
        }
    }

    if (outsideRange)
        return;

    bool jump = (data.wrect != wrect);
    data.wrect = wrect;

    // and now recursively for all children...
    for (int i = 0; i < children.size(); ++i) {
        QObject *object = children.at(i);
        if (object->isWidgetType()) {
            QWidget *w = static_cast<QWidget *>(object);
            if (!w->isWindow() && w->testAttribute(Qt::WA_WState_Created))
                w->d_func()->setWSGeometry();
        }
    }

    // move ourselves to the new position and map (if necessary) after
    // the movement. Rationale: moving unmapped windows is much faster
    // than moving mapped windows
    if (q->internalWinId() != NULLHANDLE) {
        Q_ASSERT(parent->effectiveWinId());
        // important: use real parent height (it may not match crect yet)
        RECTL rcl;
        WinQueryWindowRect(parent->effectiveWinId(), &rcl);
        qt_WinSetWindowPos(q->internalWinId(), 0, xrect.x(),
                           // flip y coordinate
                           rcl.yTop - (xrect.y() + xrect.height()),
                           xrect.width(), xrect.height(), SWP_MOVE | SWP_SIZE);
    }
    if (mapWindow && !dontShow) {
        q->setAttribute(Qt::WA_Mapped);
        if (q->internalWinId() != NULLHANDLE)
            qt_WinSetWindowPos(q->internalWinId(), 0, 0, 0, 0, 0, SWP_SHOW);
    }

    if (jump && q->internalWinId() != NULLHANDLE)
        WinInvalidateRect(q->internalWinId(), NULL, FALSE);
}

//
// The internal qPMRequestConfig, defined in qapplication_pm.cpp, stores move,
// resize and setGeometry requests for a widget that is already
// processing a config event. The purpose is to avoid recursion.
//
void qPMRequestConfig(WId, int, int, int, int, int);

void QWidgetPrivate::setGeometry_sys(int x, int y, int w, int h, bool isMove)
{
    Q_Q(QWidget);
    Q_ASSERT(q->testAttribute(Qt::WA_WState_Created));
    if (extra) { // any size restrictions?
        w = qMin(w,extra->maxw);
        h = qMin(h,extra->maxh);
        w = qMax(w,extra->minw);
        h = qMax(h,extra->minh);
    }
    if (q->isWindow())
        topData()->normalGeometry = QRect(0, 0, -1, -1);

    QSize  oldSize(q->size());
    QPoint oldPos(q->pos());

    if (!q->isWindow())
        isMove = (data.crect.topLeft() != QPoint(x, y));
    bool isResize = w != oldSize.width() || h != oldSize.height();

    if (!isMove && !isResize)
        return;

    HWND fId = frameWinId();

    if (isResize && !q->testAttribute(Qt::WA_StaticContents) &&
        q->internalWinId() != NULLHANDLE) {
        RECTL rcl = { 0, 0, data.crect.width(), data.crect.height() };
        WinValidateRect(q->internalWinId(), &rcl, FALSE);
    }

    if (isResize)
        data.window_state &= ~Qt::WindowMaximized;

    if (data.window_state & Qt::WindowFullScreen) {
        data.window_state &= ~Qt::WindowFullScreen;
    }

    QTLWExtra *tlwExtra = q->window()->d_func()->maybeTopData();
    const bool inTopLevelResize = tlwExtra ? tlwExtra->inTopLevelResize : false;

    if (q->testAttribute(Qt::WA_WState_ConfigPending)) {        // processing config event
        if (q->internalWinId() != NULLHANDLE)
            qPMRequestConfig(q->internalWinId(), isMove ? 2 : 1, x, y, w, h);
    } else {
        if (!q->testAttribute(Qt::WA_DontShowOnScreen))
            q->setAttribute(Qt::WA_WState_ConfigPending);
        if (q->windowType() == Qt::Desktop) {
            data.crect.setRect(x, y, w, h);
        } else if (q->isWindow()) {
            int sh = qt_display_height();
            QRect fs(frameStrut());
            if (extra) {
                fs.setLeft(x - fs.left());
                fs.setTop(y - fs.top());
                fs.setRight((x + w - 1) + fs.right());
                fs.setBottom((y + h - 1) + fs.bottom());
            }
            if (w == 0 || h == 0) {
                q->setAttribute(Qt::WA_OutsideWSRange, true);
                if (q->isVisible() && q->testAttribute(Qt::WA_Mapped))
                    hide_sys();
                data.crect = QRect(x, y, w, h);
            } else if (q->isVisible() && q->testAttribute(Qt::WA_OutsideWSRange)) {
                q->setAttribute(Qt::WA_OutsideWSRange, false);

                // put the window in its place and show it
                WinSetWindowPos(fId, 0, fs.x(),
                                // flip y coordinate
                                sh - (fs.y() + fs.height()),
                                fs.width(), fs.height(), SWP_MOVE | SWP_SIZE);
                data.crect.setRect(x, y, w, h);

                show_sys();
            } else if (!q->testAttribute(Qt::WA_DontShowOnScreen)) {
                q->setAttribute(Qt::WA_OutsideWSRange, false);
                // If the window is hidden and in maximized state or minimized, instead of moving the
                // window, set the normal position of the window.
                SWP swp;
                WinQueryWindowPos(fId, &swp);
                if (((swp.fl & SWP_MAXIMIZE) && !WinIsWindowVisible(fId)) ||
                    (swp.fl & SWP_MINIMIZE)) {
                    WinSetWindowUShort(fId, QWS_XRESTORE, fs.x());
                    WinSetWindowUShort(fId, QWS_YRESTORE, // flip y coordinate
                                       sh - (fs.y() + fs.height()));
                    WinSetWindowUShort(fId, QWS_CXRESTORE, fs.width());
                    WinSetWindowUShort(fId, QWS_CYRESTORE, fs.height());
                } else {
                    WinSetWindowPos(fId, 0, fs.x(),
                                    // flip y coordinate
                                    sh - (fs.y() + fs.height()),
                                    fs.width(), fs.height(), SWP_MOVE | SWP_SIZE);
                }
                if (!q->isVisible())
                    WinInvalidateRect(q->internalWinId(), NULL, FALSE);
                if (!(swp.fl & SWP_MINIMIZE)) {
                    // If the layout has heightForWidth, the WinSetWindowPos() above can
                    // change the size/position, so refresh them. Note that if the
                    // widget is minimized, we don't update its size in Qt (see
                    // QApplication::translateConfigEvent()).
                    WinQueryWindowPos(fId, &swp);
                    // flip y coordinate
                    swp.y = sh - (swp.y + swp.cy);
                    QRect fs(frameStrut());
                    data.crect.setRect(swp.x + fs.left(),
                                       swp.y + fs.top(),
                                       swp.cx - fs.left() - fs.right(),
                                       swp.cy - fs.top() - fs.bottom());
                    isResize = data.crect.size() != oldSize;
                }
            } else {
                q->setAttribute(Qt::WA_OutsideWSRange, false);
                data.crect.setRect(x, y, w, h);
            }
        } else {
            QRect oldGeom(data.crect);
            data.crect.setRect(x, y, w, h);
            if (q->isVisible() && (!inTopLevelResize || q->internalWinId())) {
                // Top-level resize optimization does not work for native child widgets;
                // disable it for this particular widget.
                if (inTopLevelResize)
                    tlwExtra->inTopLevelResize = false;

                if (!isResize)
                    moveRect(QRect(oldPos, oldSize), x - oldPos.x(), y - oldPos.y());
                else
                    invalidateBuffer_resizeHelper(oldPos, oldSize);

                if (inTopLevelResize)
                    tlwExtra->inTopLevelResize = true;
            }
            if (q->testAttribute(Qt::WA_WState_Created))
                setWSGeometry();
        }
        q->setAttribute(Qt::WA_WState_ConfigPending, false);
    }

    if (q->isWindow() && q->isVisible() && isResize && !inTopLevelResize) {
        invalidateBuffer(q->rect()); //after the resize
    }

    // Process events immediately rather than in translateConfigEvent to
    // avoid windows message process delay.
    if (q->isVisible()) {
        if (isMove && q->pos() != oldPos) {
            // in QMoveEvent, pos() and oldPos() exclude the frame, adjust them
            QRect fs(frameStrut());
            QMoveEvent e(q->pos() + fs.topLeft(), oldPos + fs.topLeft());
            QApplication::sendEvent(q, &e);
        }
        if (isResize) {
            static bool slowResize = qgetenv("QT_SLOW_TOPLEVEL_RESIZE").toInt();
            // If we have a backing store with static contents, we have to disable the top-level
            // resize optimization in order to get invalidated regions for resized widgets.
            // The optimization discards all invalidateBuffer() calls since we're going to
            // repaint everything anyways, but that's not the case with static contents.
            const bool setTopLevelResize = !slowResize && q->isWindow() && extra && extra->topextra
                                           && !extra->topextra->inTopLevelResize
                                           && (!extra->topextra->backingStore
                                               || !extra->topextra->backingStore->hasStaticContents());
            if (setTopLevelResize)
                extra->topextra->inTopLevelResize = true;
            QResizeEvent e(q->size(), oldSize);
            QApplication::sendEvent(q, &e);
            if (setTopLevelResize)
                extra->topextra->inTopLevelResize = false;
        }
    } else {
        if (isMove && q->pos() != oldPos)
            q->setAttribute(Qt::WA_PendingMoveEvent, true);
        if (isResize)
            q->setAttribute(Qt::WA_PendingResizeEvent, true);
    }
}

void QWidgetPrivate::setConstraints_sys()
{
    // @todo is there a way to show/hide the Maximize button according to
    // the shouldShowMaximizeButton() return value?
}

HWND QWidgetPrivate::effectiveFrameWinId() const
{
    Q_Q(const QWidget);
    HWND fid = frameWinId();
    if (fid != NULLHANDLE || !q->testAttribute(Qt::WA_WState_Created))
        return fid;
    QWidget *realParent = q->nativeParentWidget();
    Q_ASSERT(realParent);
    Q_ASSERT(realParent->d_func()->frameWinId());
    return realParent->d_func()->frameWinId();
}

void QWidgetPrivate::scroll_sys(int dx, int dy)
{
    Q_Q(QWidget);
    scrollChildren(dx, dy);

    if (!paintOnScreen()) {
        scrollRect(q->rect(), dx, dy);
    } else {
        // @todo ask qt_WinScrollWindowWell() to erase background if
        // WA_OpaquePaintEvent is reset?
        //if (!q->testAttribute(Qt::WA_OpaquePaintEvent))
        //    ;
        Q_ASSERT(q->testAttribute(Qt::WA_WState_Created));
        qt_WinScrollWindowWell(q->internalWinId(), dx, -dy, NULL);
    }
}

void QWidgetPrivate::scroll_sys(int dx, int dy, const QRect &r)
{
    Q_Q(QWidget);

    if (!paintOnScreen()) {
        scrollRect(r, dx, dy);
    } else {
        int h = data.crect.height();
        // flip y coordinate (all coordinates are inclusive)
        RECTL rcl = { r.left(), h - (r.bottom() + 1), r.right(), h - (r.top() + 1) };

        // @todo ask qt_WinScrollWindowWell() to erase background if
        // WA_OpaquePaintEvent is reset?
        //if (!q->testAttribute(Qt::WA_OpaquePaintEvent))
        //    ;
        Q_ASSERT(q->testAttribute(Qt::WA_WState_Created));
        qt_WinScrollWindowWell(q->internalWinId(), dx, -dy, &rcl);
    }
}

int QWidget::metric(PaintDeviceMetric m) const
{
    Q_D(const QWidget);
    LONG val;
    if (m == PdmWidth) {
        val = data->crect.width();
    } else if (m == PdmHeight) {
        val = data->crect.height();
    } else {
        HDC hdc = GpiQueryDevice(qt_display_ps());
        switch (m) {
            case PdmDpiX:
            case PdmPhysicalDpiX:
                if (d->extra && d->extra->customDpiX)
                    val = d->extra->customDpiX;
                else if (d->parent)
                    val = static_cast<QWidget *>(d->parent)->metric(m);
                else
                    DevQueryCaps(hdc, CAPS_HORIZONTAL_FONT_RES, 1, &val);
                break;
            case PdmDpiY:
            case PdmPhysicalDpiY:
                if (d->extra && d->extra->customDpiY)
                    val = d->extra->customDpiY;
                else if (d->parent)
                    val = static_cast<QWidget *>(d->parent)->metric(m);
                else
                    DevQueryCaps(hdc, CAPS_VERTICAL_FONT_RES, 1, &val);
                break;
            case PdmWidthMM:
                DevQueryCaps(hdc, CAPS_HORIZONTAL_RESOLUTION, 1, &val);
                val = data->crect.width() * 1000 / val;
                break;
            case PdmHeightMM:
                DevQueryCaps(hdc, CAPS_VERTICAL_RESOLUTION, 1, &val);
                val = data->crect.height() * 1000 / val;
                break;
            case PdmNumColors:
                DevQueryCaps(hdc, CAPS_COLORS, 1, &val);
                break;
            case PdmDepth:
                LONG colorInfo[2];
                DevQueryCaps(hdc, CAPS_COLOR_PLANES, 2, colorInfo);
                val = colorInfo[0] * colorInfo[1];
                break;
            default:
                val = 0;
                qWarning("QWidget::metric: Invalid metric command");
        }
    }
    return val;
}

void QWidgetPrivate::createSysExtra()
{
}

void QWidgetPrivate::deleteSysExtra()
{
}

void QWidgetPrivate::createTLSysExtra()
{
    extra->topextra->fId = NULLHANDLE;
    extra->topextra->swEntry = NULLHANDLE;
    extra->topextra->iconPointer = NULLHANDLE;
    extra->topextra->modalBlocker = 0;
}

void QWidgetPrivate::deleteTLSysExtra()
{
    extra->topextra->modalBlocker = 0;
    if (extra->topextra->iconPointer != NULLHANDLE)
        WinDestroyPointer(extra->topextra->iconPointer);
    if (extra->topextra->swEntry != NULLHANDLE)
        WinRemoveSwitchEntry(extra->topextra->swEntry);
    // Note: extra->topextra->fId is cleaned up in QWidget::destroy()
}

void QWidgetPrivate::registerDropSite(bool on)
{
    // @todo implement
}

void QWidgetPrivate::setMask_sys(const QRegion &region)
{
#ifdef QT_PM_NATIVEWIDGETMASK
    // @todo implement
#endif
}

void QWidgetPrivate::updateFrameStrut()
{
    Q_Q(QWidget);

    if (!q->testAttribute(Qt::WA_WState_Created))
        return;

    if (q->internalWinId() == NULLHANDLE) {
        data.fstrut_dirty = false;
        return;
    }

    QTLWExtra *top = maybeTopData();
    if (!top || top->fId == NULLHANDLE) {
        data.fstrut_dirty = false;
        return;
    }

    // this widget has WC_FRAME
    SWP swp;
    WinQueryWindowPos(top->fId, &swp);
    SWP cswp;
    WinQueryWindowPos(data.winid, &cswp);
    // flip y coordinates
    swp.y = qt_display_height() - (swp.y + swp.cy);
    cswp.y = swp.cy - (cswp.y + cswp.cy);
    QRect &fs = top->frameStrut;
    fs.setCoords(cswp.x, cswp.y, swp.cx - cswp.x - cswp.cx,
                                 swp.cy - cswp.y - cswp.cy);
    data.crect.setRect(swp.x + cswp.x, swp.y + cswp.y, cswp.cx, cswp.cy);

    data.fstrut_dirty = false;
}

void QWidgetPrivate::setWindowOpacity_sys(qreal level)
{
    // @todo implement
}

QPaintEngine *QWidget::paintEngine() const
{
    // @todo this is a place to return some direct on-screen PaintEngine once
    // we decide to support it

    // We set this bit which is checked in setAttribute for
    // Qt::WA_PaintOnScreen. We do this to allow these two scenarios:
    //
    // 1. Users accidentally set Qt::WA_PaintOnScreen on X and port to
    // OS/2 which would mean suddenly their widgets stop working.
    //
    // 2. Users set paint on screen and subclass paintEngine() to
    // return 0, in which case we have a "hole" in the backingstore
    // allowing use of GPI directly.
    //
    // 1 is WRONG, but to minimize silent failures, we have set this
    // bit to ignore the setAttribute call. 2. needs to be
    // supported because its our only means of embeddeding native
    // graphics stuff.
    const_cast<QWidgetPrivate *>(d_func())->noPaintOnScreen = 1;

    return 0;
}

QWindowSurface *QWidgetPrivate::createDefaultWindowSurface_sys()
{
    Q_Q(QWidget);
    QWindowSurface *surface = QPMDiveWindowSurface::create(q);
    if (!surface)
        surface = new QRasterWindowSurface(q);
    return surface;
}

void QWidgetPrivate::setModal_sys()
{
}

#ifdef QT_PM_NATIVEWIDGETMASK

/*!
    \internal

    Validates areas of this widget covered by (intersected with) its children
    and sibling widgets.

    Clip regions of all relative widgets (set by WinSetClipRegion()) are taken
    into account.
 */
void QWidgetPrivate::validateObstacles()
{
    Q_ASSERT(data.winid != NULLHANDLE);

    RECTL updateRcl;
    if (WinQueryUpdateRect(data.winid, &updateRcl)) {
        // the update rectangle may be empty
        if (updateRcl.xLeft != updateRcl.xRight &&
            updateRcl.yBottom != updateRcl.yTop) {
            qt_WinProcessWindowObstacles(data.winid, &updateRcl, 0, 0, PWO_Default);
        }
    }
}

#endif // QT_PM_NATIVEWIDGETMASK

QT_END_NAMESPACE
