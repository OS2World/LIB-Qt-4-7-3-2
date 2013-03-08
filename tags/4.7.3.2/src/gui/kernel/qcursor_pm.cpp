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

#include <private/qcursor_p.h>
#include <qbitmap.h>
#include <qcursor.h>

#ifndef QT_NO_CURSOR

#include <qicon.h>
#include <qapplication.h>
#include <qdesktopwidget.h>
#include <qt_os2.h>

QT_BEGIN_NAMESPACE

extern QCursorData *qt_cursorTable[Qt::LastCursor + 1]; // qcursor.cpp

extern HPS qt_alloc_mem_ps(int w, int h, HPS compat = 0); // qpixmap_pm.cpp
extern void qt_free_mem_ps(HPS hps); // qpixmap_pm.cpp

/*****************************************************************************
  Internal QCursorData class
 *****************************************************************************/

QCursorData::QCursorData(Qt::CursorShape s)
  : cshape(s), bm(0), bmm(0), hx(0), hy(0), hptr(NULLHANDLE), isSysPtr(false)
{
    ref = 1;
}

QCursorData::~QCursorData()
{
    delete bm;
    delete bmm;
    if (hptr && !isSysPtr)
        WinDestroyPointer(hptr);
}


QCursorData *QCursorData::setBitmap(const QBitmap &bitmap, const QBitmap &mask, int hotX, int hotY)
{
    if (!QCursorData::initialized)
        QCursorData::initialize();
    if (bitmap.depth() != 1 || mask.depth() != 1 || bitmap.size() != mask.size()) {
        qWarning("QCursor: Cannot create bitmap cursor; invalid bitmap(s)");
        QCursorData *c = qt_cursorTable[0];
        c->ref.ref();
        return c;
    }
    QCursorData *d = new QCursorData;
    d->bm  = new QBitmap(bitmap);
    d->bmm = new QBitmap(mask);
    d->hptr = NULLHANDLE;
    d->cshape = Qt::BitmapCursor;
    d->hx = hotX >= 0 ? hotX : bitmap.width()/2;
    d->hy = hotY >= 0 ? hotY : bitmap.height()/2;
    return d;
}

HPOINTER QCursor::handle() const
{
    if (!QCursorData::initialized)
        QCursorData::initialize();
    if (d->hptr == NULLHANDLE)
        d->update();
    return d->hptr;
}

QCursor::QCursor(HPOINTER handle)
{
    d = new QCursorData(Qt::CustomCursor);
    d->hptr = handle;
}

#endif //QT_NO_CURSOR

QPoint QCursor::pos()
{
    POINTL p;
    WinQueryPointerPos(HWND_DESKTOP, &p);
    // flip y coordinate
    p.y = qt_display_height() - (p.y + 1);
    return QPoint(p.x, p.y);
}

void QCursor::setPos(int x, int y)
{
    // flip y coordinate
    y = qt_display_height() - (y + 1);
    WinSetPointerPos(HWND_DESKTOP, x, y);
}

#ifndef QT_NO_CURSOR

enum { cursor_uparrow_x = 11, cursor_uparrow_y = 1 };
static char const * const cursor_uparrow_xpm[] = {
"24 24 3 1",
"       c None",
".      c #000000",
"+      c #FFFFFF",
"                        ",
"           ++           ",
"          +..+          ",
"         +....+         ",
"        +......+        ",
"       +........+       ",
"        +++..+++        ",
"          +..+          ",
"          +..+          ",
"          +..+          ",
"          +..+          ",
"          +..+          ",
"          +..+          ",
"          +..+          ",
"          +..+          ",
"          +..+          ",
"          +..+          ",
"          +..+          ",
"          +..+          ",
"          +..+          ",
"          +..+          ",
"           ++           ",
"                        ",
"                        "};

enum { cursor_cross_x = 11, cursor_cross_y = 11 };
static char const * const cursor_cross_xpm[] = {
"24 24 3 1",
"       c None",
".      c #FFFFFF",
"+      c #000000",
"                        ",
"           ..           ",
"          .++.          ",
"          .++.          ",
"          .++.          ",
"          .++.          ",
"          .++.          ",
"          .++.          ",
"          .++.          ",
"          .++.          ",
"  ........   ........   ",
" .++++++++   ++++++++.  ",
" .++++++++   ++++++++.  ",
"  ........   ........   ",
"          .++.          ",
"          .++.          ",
"          .++.          ",
"          .++.          ",
"          .++.          ",
"          .++.          ",
"          .++.          ",
"          .++.          ",
"           ..           ",
"                        "};

enum { cursor_vsplit_x = 11, cursor_vsplit_y = 11 };
static char const * const cursor_vsplit_xpm[] = {
"24 24 3 1",
"       c None",
".      c #000000",
"+      c #FFFFFF",
"                        ",
"           ++           ",
"          +..+          ",
"         +....+         ",
"        +......+        ",
"       +........+       ",
"        +++..+++        ",
"          +..+          ",
"  +++++++++..+++++++++  ",
" +....................+ ",
" +....................+ ",
"  ++++++++++++++++++++  ",
"  ++++++++++++++++++++  ",
" +....................+ ",
" +....................+ ",
"  +++++++++..+++++++++  ",
"          +..+          ",
"        +++..+++        ",
"       +........+       ",
"        +......+        ",
"         +....+         ",
"          +..+          ",
"           ++           ",
"                        "};

enum { cursor_hsplit_x = 11, cursor_hsplit_y = 11 };
static char const * const cursor_hsplit_xpm[] = {
"24 24 3 1",
"       c None",
".      c #000000",
"+      c #FFFFFF",
"                        ",
"         ++++++         ",
"        +..++..+        ",
"        +..++..+        ",
"        +..++..+        ",
"        +..++..+        ",
"        +..++..+        ",
"     +  +..++..+  +     ",
"    +.+ +..++..+ +.+    ",
"   +..+ +..++..+ +..+   ",
"  +...+++..++..+++...+  ",
" +.........++.........+ ",
" +.........++.........+ ",
"  +...+++..++..+++...+  ",
"   +..+ +..++..+ +..+   ",
"    +.+ +..++..+ +.+    ",
"     +  +..++..+  +     ",
"        +..++..+        ",
"        +..++..+        ",
"        +..++..+        ",
"        +..++..+        ",
"        +..++..+        ",
"         ++++++         ",
"                        "};

enum { cursor_blank_x = 0, cursor_blank_y = 0 };
static char const * const cursor_blank_xpm[] = {
"1 1 1 1",
"       c None",
" "};

enum { cursor_hand_x = 9, cursor_hand_y = 0 };
static char const * const cursor_hand_xpm[] = {
"24 24 3 1",
"       c None",
".      c #FFFFFF",
"+      c #000000",
"         ++             ",
"        +..+            ",
"        +..+            ",
"        +..+            ",
"        +..+            ",
"        +..+            ",
"        +..+++          ",
"        +..+..+++       ",
"        +..+..+..++     ",
"     ++ +..+..+..+.+    ",
"    +..++..+..+..+.+    ",
"    +...+..........+    ",
"     +.............+    ",
"      +............+    ",
"      +............+    ",
"       +..........+     ",
"       +..........+     ",
"        +........+      ",
"        +........+      ",
"        ++++++++++      ",
"        ++++++++++      ",
"        ++++++++++      ",
"                        ",
"                        "};

enum { cursor_whatsthis_x = 0, cursor_whatsthis_y = 0 };
static char const * const cursor_whatsthis_xpm[] = {
"24 24 3 1",
"       c None",
".      c #000000",
"+      c #FFFFFF",
".                       ",
"..          ++++++      ",
".+.        +......+     ",
".++.      +........+    ",
".+++.    +....++....+   ",
".++++.   +...+  +...+   ",
".+++++.  +...+  +...+   ",
".++++++.  +.+  +....+   ",
".+++++++.  +  +....+    ",
".++++++++.   +....+     ",
".+++++.....  +...+      ",
".++.++.      +...+      ",
".+. .++.     +...+      ",
"..  .++.      +++       ",
".    .++.    +...+      ",
"     .++.    +...+      ",
"      .++.   +...+      ",
"      .++.    +++       ",
"       ..               ",
"                        ",
"                        ",
"                        ",
"                        ",
"                        "};

enum { cursor_openhand_x = 7, cursor_openhand_y = 7 };
static char const * const cursor_openhand_xpm[] = {
"16 16 3 1",
"       g None",
".      g #000000",
"+      g #FFFFFF",
"       ..       ",
"   .. .++...    ",
"  .++..++.++.   ",
"  .++..++.++. . ",
"   .++.++.++..+.",
"   .++.++.++.++.",
" ..+.+++++++.++.",
".++..++++++++++.",
".+++.+++++++++. ",
" .++++++++++++. ",
"  .+++++++++++. ",
"  .++++++++++.  ",
"   .+++++++++.  ",
"    .+++++++.   ",
"     .++++++.   ",
"                "};

enum { cursor_closedhand_x = 7, cursor_closedhand_y = 7 };
static char const * const cursor_closedhand_xpm[] = {
"16 16 3 1",
"       g None",
".      g #000000",
"+      g #FFFFFF",
"                ",
"                ",
"                ",
"    .. .. ..    ",
"   .++.++.++..  ",
"   .++++++++.+. ",
"    .+++++++++. ",
"   ..+++++++++. ",
"  .+++++++++++. ",
"  .++++++++++.  ",
"   .+++++++++.  ",
"    .+++++++.   ",
"     .++++++.   ",
"     .++++++.   ",
"                ",
"                "};

static bool qt_alloc_ps_with_bitmap(int w, int h, bool isMono,
                                    HPS &hps, HBITMAP &hbm)
{
    hps = qt_alloc_mem_ps(32, 32);
    if (hps == NULLHANDLE)
        return false;

    BITMAPINFOHEADER2 bmh;
    bmh.cbFix = sizeof(BITMAPINFOHEADER2);
    bmh.cx = w;
    bmh.cy = h;
    bmh.cPlanes = 1;
    bmh.cBitCount = isMono ? 1 : 32;
    hbm = GpiCreateBitmap(hps, &bmh, 0, 0, 0);
    if (hbm == NULLHANDLE) {
        qt_free_mem_ps(hps);
        return false;
    }

    GpiSetBitmap(hps, hbm);
    return true;
}

static void qt_free_ps_with_bitmap(HPS hps, HBITMAP hbm)
{
    GpiSetBitmap(hps, NULLHANDLE);
    GpiDeleteBitmap(hbm);
    qt_free_mem_ps(hps);
}

static HPOINTER combineTwoCursors(LONG idOver, LONG idUnder)
{
    HPOINTER hptr = NULLHANDLE;

    POINTERINFO piOver, piUnder;
    HPOINTER hOver = WinQuerySysPointer(HWND_DESKTOP, idOver, FALSE);
    WinQueryPointerInfo(hOver, &piOver);
    HPOINTER hUnder = WinQuerySysPointer(HWND_DESKTOP, idUnder, FALSE);
    WinQueryPointerInfo(hUnder, &piUnder);
    if (piOver.hbmColor) {
        HPS hpsPtr, hpsTmp, hpsMask;
        HBITMAP hbmPtr, hbmTmp, hbmMask;
        qt_alloc_ps_with_bitmap(32, 32, false, hpsPtr, hbmPtr);
        qt_alloc_ps_with_bitmap(32, 32, false, hpsTmp, hbmTmp);
        qt_alloc_ps_with_bitmap(32, 64, true, hpsMask, hbmMask);
        // copy the overlying pointer
        POINTL ptls[] = { { 0, 0 }, { 31, 31 }, { 0, 0 }, { 32, 32 } };
        GpiWCBitBlt(hpsTmp, piOver.hbmColor, 4, ptls, ROP_SRCCOPY, BBO_IGNORE);
        // make its transparent pixels black
        ptls[2].y += 32; ptls[3].y += 32;
        GpiSetColor(hpsTmp, CLR_TRUE);
        GpiSetBackColor(hpsTmp, CLR_FALSE);
        GpiWCBitBlt(hpsTmp, piOver.hbmPointer, 4, ptls, 0x22, BBO_IGNORE);
        // copy the underlying pointer
        ptls[2].y -= 32; ptls[3].y -= 32;
        GpiWCBitBlt(hpsPtr, piUnder.hbmColor, 4, ptls, ROP_SRCCOPY, BBO_IGNORE);
        // make non-transparent pixels from the overlying pointer black
        ptls[2].y += 32; ptls[3].y += 32;
        GpiSetColor(hpsPtr, CLR_TRUE);
        GpiSetBackColor(hpsPtr, CLR_FALSE);
        GpiWCBitBlt(hpsPtr, piOver.hbmPointer, 4, ptls, ROP_SRCAND, BBO_IGNORE);
        // put the overlying pointer there
        ptls[2].y -= 32; ptls[3].y -= 32;
        ptls[1].x ++; ptls[1].y ++;
        GpiBitBlt(hpsPtr, hpsTmp, 4, ptls, ROP_SRCPAINT, BBO_IGNORE);
        // copy both underlying pointer's masks
        ptls[1].x --; ptls[1].y --;
        ptls[1].y += 32; ptls[3].y += 32;
        GpiWCBitBlt(hpsMask, piUnder.hbmPointer, 4, ptls, ROP_SRCCOPY, BBO_IGNORE);
        // add overlying pointer's XOR mask
        ptls[1].y -= 32; ptls[3].y -= 32;
        GpiWCBitBlt(hpsMask, piOver.hbmPointer, 4, ptls, ROP_SRCPAINT, BBO_IGNORE);
        // add overlying pointer's AND mask
        ptls[0].y += 32; ptls[2].y += 32;
        ptls[1].y += 32; ptls[3].y += 32;
        GpiWCBitBlt(hpsMask, piOver.hbmPointer, 4, ptls, ROP_SRCAND, BBO_IGNORE);
        // create the new pointer
        GpiSetBitmap(hpsPtr, NULLHANDLE);
        GpiSetBitmap(hpsMask, NULLHANDLE);
        piOver.hbmColor = hbmPtr;
        piOver.hbmPointer = hbmMask;
        piOver.hbmMiniColor = NULLHANDLE;
        piOver.hbmMiniPointer = NULLHANDLE;
        hptr = WinCreatePointerIndirect(HWND_DESKTOP, &piOver);
        qt_free_ps_with_bitmap(hpsMask,  hbmMask);
        qt_free_ps_with_bitmap(hpsTmp,  hbmTmp);
        qt_free_ps_with_bitmap(hpsPtr,  hbmPtr);
    } else {
        HPS hps;
        HBITMAP hbm;
        qt_alloc_ps_with_bitmap(32, 64, true, hps, hbm);
        POINTL ptls[] = { { 0, 0 }, { 31, 63 }, { 0, 0 }, { 32, 64 } };
        // make a copy of the underlying pointer
        GpiWCBitBlt(hps, piUnder.hbmPointer, 4, ptls, ROP_SRCCOPY, BBO_IGNORE);
        // combine AND masks
        ptls[0].y += 32; ptls[2].y += 32;
        GpiWCBitBlt(hps, piOver.hbmPointer, 4, ptls, ROP_SRCAND, BBO_IGNORE);
        // apply the overlying AND mask to the underlying XOR mask
        ptls[0].y -= 32; ptls[1].y -= 32;
        GpiWCBitBlt(hps, piOver.hbmPointer, 4, ptls, ROP_SRCAND, BBO_IGNORE);
        // apply the overlying XOR mask to the underlying XOR mask
        ptls[2].y -= 32; ptls[3].y -= 32;
        GpiWCBitBlt(hps, piOver.hbmPointer, 4, ptls, ROP_SRCINVERT, BBO_IGNORE);
        // create the new pointer
        GpiSetBitmap(hps, 0);
        hptr = WinCreatePointer(HWND_DESKTOP, hbm, TRUE,
                                piOver.xHotspot, piOver.yHotspot);
        qt_free_ps_with_bitmap(hps, hbm);
    }

    Q_ASSERT(hptr);
    return hptr;
}

void QCursorData::update()
{
    if (!QCursorData::initialized)
        QCursorData::initialize();

    if (cshape == Qt::BitmapCursor) {
        QPixmap pm = pixmap;
        if (pm.isNull()) {
            Q_ASSERT(bm && bmm);
            pm = *bm;
            pm.setMask(*bmm);
        }
        hptr = QPixmap::toPmHPOINTER(QIcon(pm), true, hx, hy);
        Q_ASSERT(hptr);
        isSysPtr = false;
        return;
    }

    LONG id = 0;
    char const * const * xpm = 0;
    int xpm_hx = 0;
    int xpm_hy = 0;

    switch (cshape) { // map to OS/2 cursor
    case Qt::ArrowCursor:
        id = SPTR_ARROW;
        break;
    case Qt::UpArrowCursor:
        xpm = cursor_uparrow_xpm;
        xpm_hx = cursor_uparrow_x;
        xpm_hy = cursor_uparrow_y;
        break;
    case Qt::CrossCursor:
        xpm = cursor_cross_xpm;
        xpm_hx = cursor_cross_x;
        xpm_hy = cursor_cross_y;
	    break;
    case Qt::WaitCursor:
        id = SPTR_WAIT;
	    break;
    case Qt::IBeamCursor:
    	id = SPTR_TEXT;
    	break;
    case Qt::SizeVerCursor:
    	id = SPTR_SIZENS;
    	break;
    case Qt::SizeHorCursor:
    	id = SPTR_SIZEWE;
    	break;
    case Qt::SizeBDiagCursor:
    	id = SPTR_SIZENESW;
    	break;
    case Qt::SizeFDiagCursor:
    	id = SPTR_SIZENWSE;
    	break;
    case Qt::SizeAllCursor:
    	id = SPTR_MOVE;
    	break;
    case Qt::BlankCursor:
        xpm = cursor_blank_xpm;
        xpm_hx = cursor_blank_x;
        xpm_hy = cursor_blank_y;
        break;
    case Qt::SplitVCursor:
        xpm = cursor_vsplit_xpm;
        xpm_hx = cursor_vsplit_x;
        xpm_hy = cursor_vsplit_y;
        break;
    case Qt::SplitHCursor:
        xpm = cursor_hsplit_xpm;
        xpm_hx = cursor_hsplit_x;
        xpm_hy = cursor_hsplit_y;
        break;
    case Qt::PointingHandCursor:
        xpm = cursor_hand_xpm;
        xpm_hx = cursor_hand_x;
        xpm_hy = cursor_hand_y;
        break;
    case Qt::ForbiddenCursor:
    	id = SPTR_ILLEGAL;
    	break;
    case Qt::WhatsThisCursor:
        xpm = cursor_whatsthis_xpm;
        xpm_hx = cursor_whatsthis_x;
        xpm_hy = cursor_whatsthis_y;
        break;
    case Qt::BusyCursor:
        // we create a busy cursor below as a combination of the standard
        // arrow and wait cursors
        hptr = combineTwoCursors(SPTR_ARROW, SPTR_WAIT);
        isSysPtr = false;
        return;
    case Qt::OpenHandCursor:
        xpm = cursor_openhand_xpm;
        xpm_hx = cursor_openhand_x;
        xpm_hy = cursor_openhand_y;
        break;
    case Qt::ClosedHandCursor:
        xpm = cursor_closedhand_xpm;
        xpm_hx = cursor_closedhand_x;
        xpm_hy = cursor_closedhand_y;
        break;
    default:
        qWarning("QCursor::update: Invalid cursor shape %d", cshape);
        return;
    }

    if (!id) {
#ifndef QT_NO_IMAGEFORMAT_XPM
        QPixmap pm(xpm);
        hptr = QPixmap::toPmHPOINTER(QIcon(pm), true, xpm_hx, xpm_hy);
        Q_ASSERT(hptr);
        isSysPtr = false;
        return;
#else
        id = SPTR_ARROW;
#endif
    }

    Q_ASSERT(id);

    hptr = WinQuerySysPointer(HWND_DESKTOP, id, FALSE);
    Q_ASSERT(hptr);
    isSysPtr = true;
}

#endif // QT_NO_CURSOR

QT_END_NAMESPACE
