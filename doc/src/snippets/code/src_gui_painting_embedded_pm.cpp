/****************************************************************************
**
** Copyright (C) 2011 Nokia Corporation and/or its subsidiary(-ies).
** All rights reserved.
** Contact: Nokia Corporation (qt-info@nokia.com)
**
** This file is part of the documentation of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:BSD$
** You may use this file under the terms of the BSD license as follows:
**
** "Redistribution and use in source and binary forms, with or without
** modification, are permitted provided that the following conditions are
** met:
**   * Redistributions of source code must retain the above copyright
**     notice, this list of conditions and the following disclaimer.
**   * Redistributions in binary form must reproduce the above copyright
**     notice, this list of conditions and the following disclaimer in
**     the documentation and/or other materials provided with the
**     distribution.
**   * Neither the name of Nokia Corporation and its Subsidiary(-ies) nor
**     the names of its contributors may be used to endorse or promote
**     products derived from this software without specific prior written
**     permission.
**
** THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
** "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
** LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
** A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
** OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
** SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
** LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
** DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
** THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
** (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
** OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE."
** $QT_END_LICENSE$
**
****************************************************************************/

//! [0]
#define INCL_WINWINDOWMGR
#define INCL_GPIREGIONS
#include <os2.h>

// although the prototype is available in <QWidget>, external applications
// may need to declare it manually and link to QtGui4.lib directly, or even
// query it from QtGui4.dll dynamically at runtime
LONG APIENTRY qt_WinProcessWindowObstacles(HWND hwnd, RECTL *prcl, HRGN hrgn,
                                           LONG op, LONG flags);

void externalPaint(HWND hwnd)
{
    // hwnd is the value returned by QWidget::winId()

    RECTL rcl;
    WinQueryWindowRect(hwnd, &rcl);
    HPS hps = WinGetPS(hwnd);

    HRGN hrgn = GpiCreateRegion(hps, 1L, &rcl);
    ULONG rc = qt_WinProcessWindowObstacles(hwnd, NULL, hrgn, CRGN_DIFF,
                                            0 /* PWO_Default */);
    if (rc == RGN_RECT || rc == RGN_COMPLEX) {
        HRGN hrgnOld;
        GpiSetClipRegion (hps, hrgn, &hrgnOld);
        hrgn = hrgnOld;

        // Paint to hps using regular PM and GPI calls
    }

    GpiDestroyRegion (hps, hrgn);

    WinReleasePS (hps);
}
//! [0]

