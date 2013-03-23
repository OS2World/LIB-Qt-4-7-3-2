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

#include "qpixmap.h"
#include "qpixmap_raster_p.h"

#include "qicon.h"
#include "qbitmap.h"
#include "qpainter.h"

#include "qt_os2.h"

QT_BEGIN_NAMESPACE

HPS qt_alloc_mem_ps(int w, int h, HPS compat = 0)
{
    HDC hdcCompat = NULLHANDLE;
    if (compat)
        hdcCompat = GpiQueryDevice(compat);

    static PCSZ hdcData[4] = { "Display", NULL, NULL, NULL };
    HDC hdc = DevOpenDC(0, OD_MEMORY, "*", 4, (PDEVOPENDATA) hdcData, hdcCompat);
    if (!hdc) {
        qWarning( "alloc_mem_dc: DevOpenDC failed with %08lX!", WinGetLastError(0));
        return NULLHANDLE;
    }
    SIZEL size = { w, h };
    HPS hps = GpiCreatePS(0, hdc, &size, PU_PELS | GPIA_ASSOC | GPIT_MICRO);
    if (hps == NULLHANDLE) {
        qWarning("alloc_mem_dc: GpiCreatePS failed wit %08lX!", WinGetLastError(0));
        return NULLHANDLE;
    }
    // @todo later
//  if (QColor::hPal()) {
//      GpiSelectPalette(hps, QColor::hPal());
//  } else {
        // direct RGB mode
        GpiCreateLogColorTable(hps, 0, LCOLF_RGB, 0, 0, NULL);
//  }
    return hps;
}

void qt_free_mem_ps(HPS hps)
{
    HDC hdc = GpiQueryDevice(hps);
    GpiAssociate(hps, NULLHANDLE);
    GpiDestroyPS(hps);
    DevCloseDC(hdc);
}

/*!
    Creates a \c HBITMAP equivalent to the QPixmap. Returns the \c HBITMAP
    handle.

    If \a mask is not NULL, the mask mode is turned on. In this mode, the bitmap
    mask is also created from the QPixmap's mask and returned in the given
    variable. This bitmap mask will contain two vertically adjacent sections,
    the first of which is the AND mask and the second one is the XOR mask
    (according to WinCreatePointer() specification). Also, in mask mode, the
    HBITMAP returned for the pixmap itself will be prepared for masking (with
    transparent pixels made black). This mode is useful for creating system
    icons and pointers (\sa toPmHPOINTER()).

    if \a embedRealAlpha is \c true, the real alpha chennel (not the 1bpp mask)
    will be embedded in the high 8 bits of the 32-bit pixel value for each pixel
    in the created bitmap (which always has 1 plane and the 32-bit depth). This
    extra information isn't touched by PM/GPI but may be used by custom drawing
    routines to do real alpha blending.

    Note that if \a mask is not NULL but the pixmap does neither have a mask nor
    the alpha channel, an emptpy bitmap mask with no transparency (zeroed AND
    and XOR parts) will be created and returned.

    It is the caller's responsibility to free both returned \c HBITMAP handes
    after use.

    \warning This function is only available on OS/2.

    \sa fromPmHBITMAP(), toPmHPOINTER()
*/
HBITMAP QPixmap::toPmHBITMAP(HBITMAP *mask, bool embedRealAlpha) const
{
    if (data->classId() != QPixmapData::RasterClass) {
        QPixmapData *data = new QRasterPixmapData(depth() == 1 ?
                                                  QPixmapData::BitmapType :
                                                  QPixmapData::PixmapType);
        data->fromImage(toImage(), Qt::AutoColor);
        return QPixmap(data).toPmHBITMAP(mask, embedRealAlpha);
    }

    QRasterPixmapData* d = static_cast<QRasterPixmapData*>(data.data());
    int w = d->image.width();
    int h = d->image.height();

    HPS hps = qt_alloc_mem_ps(w, h * 2);
    if (hps == NULLHANDLE)
        return NULLHANDLE;

    HBITMAP hbm = NULLHANDLE;
    HBITMAP hbmMask = NULLHANDLE;

    // Note that we always use ARGB32 even if embedRealAlpha is false because
    // in this case we will use the alpha channel to dither the 1bpp mask
    QImage image = d->image.convertToFormat(QImage::Format_ARGB32);
    // flip the bitmap top to bottom for PM
    image = image.mirrored();

    // bitmap header + 2 palette entries (for the mask)
    char bmi[sizeof(BITMAPINFOHEADER2) + 4 * 2];
    memset(bmi, 0, sizeof(bmi));
    PBITMAPINFOHEADER2 bmh = (PBITMAPINFOHEADER2)bmi;
    bmh->cbFix = sizeof(BITMAPINFOHEADER2);
    PULONG pal = (PULONG)(bmi + sizeof(BITMAPINFOHEADER2));

    // create the normal bitmap from the pixmap data
    bmh->cx = w;
    bmh->cy = h;
    bmh->cPlanes = 1;
    bmh->cBitCount = 32;
    hbm = GpiCreateBitmap(hps, bmh, CBM_INIT, (PBYTE)(const uchar *)image.bits(),
                          (PBITMAPINFO2)&bmi);

    if (mask) {
        // get the mask
        QImage mask;
        if (hasAlpha()) {
            if (!embedRealAlpha) {
                // We prefer QImage::createAlphaMask() over QPixmap::mask()
                // since the former will dither while the latter will convert any
                // non-zero alpha value to an opaque pixel
                mask = image.createAlphaMask().convertToFormat(QImage::Format_Mono);

                // note: for some strange reason, createAlphaMask() (as opposed to
                // mask().toImage()) returns an image already flipped top to bottom,
                // so take it into account

                // create the AND mask
                mask.invertPixels();
                // add the XOR mask (and leave it zeroed)
                mask = mask.copy(0, -h, w, h * 2);
            } else {
                // if we embedded real alpha, we still need a mask if we are going
                // to create a pointer out of this pixmap (WinCreatePointerIndirect()
                // requirement), but we will use QPixmap::mask() because it won't be
                // able to destroy the alpha channel of non-fully transparent pixels
                // when preparing the color bitmap for masking later. We could also
                // skip this prepare step, but well, let's go this way, it won't hurt.
                mask = this->mask().toImage().convertToFormat(QImage::Format_Mono);

                // create the AND mask
                mask.invertPixels();
                // add the XOR mask (and leave it zeroed)
                mask = mask.copy(0, 0, w, h * 2);
                // flip the bitmap top to bottom for PM
                mask = mask.mirrored(false, true);
            }
        } else {
            mask = QImage(w, h * 2, QImage::Format_Mono);
            mask.fill(0);
        }

        // create the mask bitmap
        bmh->cbFix = sizeof(BITMAPINFOHEADER2);
        bmh->cx = w;
        bmh->cy = h * 2;
        bmh->cPlanes = 1;
        bmh->cBitCount = 1;
        bmh->cclrUsed = 2;
        pal[0] = 0;
        pal[1] = 0x00FFFFFF;
        hbmMask = GpiCreateBitmap(hps, bmh, CBM_INIT,
                                  (PBYTE)(const uchar *)mask.bits(),
                                  (PBITMAPINFO2)&bmi);

        // prepare the bitmap for masking by setting transparent pixels to black
        GpiSetBitmap(hps, hbm);

        POINTL ptls[] = {
            { 0, 0 }, { w - 1, h - 1 },     // dst: inclusive-inclusive
            { 0, h }, { w, h * 2 },         // src: inclusive-exclusive
        };
        ptls[0].y -= h;
        ptls[1].y -= h;
        enum { AllImageAttrs = IBB_COLOR | IBB_BACK_COLOR |
                               IBB_MIX_MODE | IBB_BACK_MIX_MODE };
        IMAGEBUNDLE ib = { CLR_TRUE, CLR_FALSE, FM_OVERPAINT, BM_OVERPAINT };
        GpiSetAttrs(hps, PRIM_IMAGE, AllImageAttrs, 0, (PBUNDLE)&ib);
        GpiDrawBits(hps, (PBYTE)(const uchar *)mask.bits(), (PBITMAPINFO2)&bmi,
                    4, ptls, ROP_SRCAND, BBO_IGNORE);
    }

    qt_free_mem_ps(hps);

    if (mask)
        *mask = hbmMask;

    return hbm;
}

/*!
    Returns a QPixmap that is equivalent to the bitmap given in \a hbm. If \a
    hbmMask is not NULLHANDLE, it should contain vertically adjacent AND and XOR
    masks for the given bitmap which will be used to create a mask for the
    returned QPixmap.

    Note that this method will attempt to auto-detect the presence of the real
    alpha chennel in the high 8 bits of the 32-bit pixel value for each pixel if
    \a hbm has 1 plane and the 32-bit depth. This alpha channel will be used to
    create an alpha channel for the returned QPixmap.

    \warning This function is only available on OS/2.

    \sa toPmHBITMAP(), {QPixmap#Pixmap Conversion}{Pixmap Conversion}

*/
// static
QPixmap QPixmap::fromPmHBITMAP(HBITMAP hbm, HBITMAP hbmMask)
{
    QPixmap res;

    if (hbm == NULLHANDLE)
        return res;

    // bitmap header + 2 palette entries (for the monochrome bitmap)
    char bmi[sizeof(BITMAPINFOHEADER2) + 4 * 2];
    memset(bmi, 0, sizeof(bmi));
    PBITMAPINFOHEADER2 bmh = (PBITMAPINFOHEADER2)bmi;
    bmh->cbFix = sizeof(BITMAPINFOHEADER2);
    PULONG pal = (PULONG)(bmi + sizeof(BITMAPINFOHEADER2));

    if (!GpiQueryBitmapInfoHeader(hbm, bmh))
        return res;

    HPS hps = qt_alloc_mem_ps(bmh->cx, bmh->cy * 2);
    if (hps == NULLHANDLE)
        return res;

    GpiSetBitmap(hps, hbm);

    QImage img;
    bool succeeded = false;

    if (bmh->cPlanes == 1 && bmh->cBitCount == 1) {
        // monochrome bitmap
        img = QImage(bmh->cx, bmh->cy, QImage::Format_Mono);
        if (GpiQueryBitmapBits(hps, 0, img.height(), (PBYTE)img.bits(),
                               (PBITMAPINFO2)&bmi) != GPI_ALTERROR) {
            succeeded = true;
            // take the palette
            QVector<QRgb> colors(2);
            colors[0] = QRgb(pal[0]);
            colors[1] = QRgb(pal[1]);
            img.setColorTable(colors);
        }
    } else {
        // always convert to 32-bit otherwise
        img = QImage(bmh->cx, bmh->cy, QImage::Format_RGB32);
        bmh->cPlanes = 1;
        bmh->cBitCount = 32;
        if (GpiQueryBitmapBits(hps, 0, img.height(), (PBYTE)img.bits(),
                               (PBITMAPINFO2)&bmi) != GPI_ALTERROR) {
            succeeded = true;
            // try to auto-detect if there is a real alpha channel
            bool allZero = true;
            for (int i = 0; i < img.numBytes(); ++i) {
                if (img.bits()[i] & 0xFF000000) {
                    allZero = false;
                    break;
                }
            }
            if (!allZero) {
                // assume we've got the alpha channel
                QImage alphaImg = QImage(bmh->cx, bmh->cy, QImage::Format_ARGB32);
                memcpy(alphaImg.bits(), img.bits(), img.numBytes());
                img = alphaImg;
            }
            // flip the bitmap top to bottom to cancel PM inversion
            img = img.mirrored();
        }
    }

    QImage mask;

    if (hbmMask != NULLHANDLE && GpiQueryBitmapInfoHeader(hbmMask, bmh)) {
        // get the AND+XOR mask
        if ((int)bmh->cx == img.width() &&
            (int)bmh->cy == img.height() * 2 &&
            bmh->cPlanes == 1 && bmh->cBitCount == 1) {
            GpiSetBitmap(hps, hbmMask);
            mask = QImage(bmh->cx, bmh->cy, QImage::Format_Mono);
            if (GpiQueryBitmapBits(hps, 0, mask.height(), (PBYTE)mask.bits(),
                                   (PBITMAPINFO2)&bmi) != GPI_ALTERROR) {
                // take the palette
                QVector<QRgb> colors(2);
                colors[0] = QRgb(pal[0]);
                colors[1] = QRgb(pal[1]);
                mask.setColorTable(colors);
                // flip the bitmap top to bottom to cancel PM inversion
                mask = mask.mirrored(false, true);
                // drop the XOR mask
                mask = mask.copy(0, 0, mask.width(), mask.height() / 2);
                // create a normal mask from the AND mask
                mask.invertPixels();
            } else {
                mask = QImage();
            }
            GpiSetBitmap(hps, NULLHANDLE);
        } else {
            Q_ASSERT(false);
        }
    }

    qt_free_mem_ps(hps);

    if (succeeded) {
        res = QPixmap::fromImage(img);
        if (!mask.isNull())
            res.setMask(QBitmap::fromImage(mask));
    }

    return res;
}

/*!
    Creates a \c HPOINTER from the given \a icon. Returns the \c HPOINTER
    handle.

    If \a isPointer is \c true, an icon size closest to the system pointer size
    is chosen, otherwise to the system icon size. \a hotX and \a hotY define the
    hot spot. Note is that the size of the resulting pointer will exactly match
    the system size no matter what size the matched icon is. Smaller icons will
    be centered in a box corresponding to the system size, larger icons will
    be scaled down.

    If \a embedRealAlpha is \c true, the color bitmap in the pointer will have
    the alpha channel embedded in it (see toPmHBITMAP() for details).

    Note that due to the bug in WinCreatePointerIndirect(), hbmMiniPointer and
    hbmMiniColor field of the POINTERINFO structure are always ignored. For this
    reason, the caller must choose what icon size (normal or half-size) he wants
    to get using the \a isMini argument. A bitmap of the respective size will be
    created and assigned to the hbmColor field.

    It is the caller's responsibility to free the \c HPOINTER data
    after use.

    \note \a isMini is ignored when \a isPointer is \c true.

    \warning This function is only available on OS/2.

    \sa toPmHBITMAP()
*/
// static
HPOINTER QPixmap::toPmHPOINTER(const QIcon &icon, bool isPointer,
                               int hotX, int hotY, bool embedRealAlpha,
                               bool isMini)
{
    if (icon.isNull())
        return NULLHANDLE;

    // get the system icon size
    int w = WinQuerySysValue(HWND_DESKTOP, isPointer ? SV_CXPOINTER : SV_CXICON);
    int h = WinQuerySysValue(HWND_DESKTOP, isPointer ? SV_CYPOINTER : SV_CYICON);
    if (!isPointer && isMini) {
        w = w / 2;
        h = h / 2;
    }

    // obtain the closest (but never larger) icon size we have
    QSize size = icon.actualSize(QSize(w, h));

    QPixmap pm = icon.pixmap(size);
    if (pm.isNull())
        return NULLHANDLE;

    // if we got a smaller pixmap then center it inside the box matching the
    // system size instead of letting WinCreatePointerIndirect() scale (this
    // covers a usual case when we get 32/16 px pixmaps on a 120 DPI system
    // where the icon size is 40/20 px respectively): scaling such small images
    // looks really ugly.
    if (!pm.isNull() && (pm.width() < w || pm.height() < h)) {
        Q_ASSERT(pm.width() <= w && pm.height() <= h);
        QPixmap pmNew(w, h);
        pmNew.fill(Qt::transparent);
        QPainter painter(&pmNew);
        int dx = (w - pm.width()) / 2;
        int dy = (h - pm.height()) / 2;
        painter.drawPixmap(dx, dy, pm);
        pm = pmNew;
        hotX += dx;
        hotY += dy;
    }

    POINTERINFO info;
    info.fPointer = isPointer;
    info.xHotspot = hotX;
    info.yHotspot = pm.height() - hotY - 1;
    info.hbmColor = pm.toPmHBITMAP(&info.hbmPointer, embedRealAlpha);
    info.hbmMiniPointer = NULLHANDLE;
    info.hbmMiniColor = NULLHANDLE;

    HPOINTER hIcon = WinCreatePointerIndirect(HWND_DESKTOP, &info);

    GpiDeleteBitmap(info.hbmPointer);
    GpiDeleteBitmap(info.hbmColor);

    return hIcon;
}

/*!
    Returns a QIcon that is equivalent to the pointer given in \a hpointer.
    Optionally returns pixmaps used to comprise the icon in \a pixmap and
    \a pixmapMini.

    Note that this method will attempt to auto-detect the presence of the real
    alpha chennel in the high 8 bits of the 32-bit pixel value for each pixel if
    the bitmaps in \a hpointer have 1 plane and the 32-bit depth. This alpha
    channel will be used to create an alpha channel for the pixmaps comprising
    the icon.

    \warning This function is only available on OS/2.

    \sa toPmHPOINTER(), {QPixmap#Pixmap Conversion}{Pixmap Conversion}

*/
// static
QIcon QPixmap::fromPmHPOINTER(HPOINTER hpointer, QPixmap *pixmap,
                              QPixmap *pixmapMini)
{
    QIcon res;

    if (hpointer == NULLHANDLE)
        return res;

    POINTERINFO info = { 0 };
    if (!WinQueryPointerInfo(hpointer, &info))
        return res;

    QPixmap pm = fromPmHBITMAP(info.hbmColor, info.hbmPointer);
    if (!pm.isNull())
        res.addPixmap(pm);

    QPixmap pmMini = fromPmHBITMAP(info.hbmMiniColor, info.hbmMiniPointer);
    if (!pmMini.isNull())
        res.addPixmap(pmMini);

    if (pixmap)
        *pixmap = pm;
    if (pixmapMini)
        *pixmapMini = pmMini;

    return res;
}

QPixmap QPixmap::grabWindow(WId winId, int x, int y, int w, int h)
{
    QPixmap pm;

    if (w == 0 || h == 0)
        return pm;

    RECTL rcl;
    if (!WinQueryWindowRect(winId, &rcl))
        return pm;

    if (w < 0)
        w = rcl.xRight;
    if (h < 0)
        h = rcl.yTop;

    // flip y coordinate
    y = rcl.yTop - (y + h);

    HPS hps = qt_alloc_mem_ps(w, h);
    if (hps == NULLHANDLE)
        return pm;

    HBITMAP hbm = NULLHANDLE;

    // bitmap header + 2 palette entries (for the mask)
    BITMAPINFOHEADER2 bmh;
    bmh.cbFix = sizeof(BITMAPINFOHEADER2);

    // create the uninitialized bitmap to hold window pixels
    bmh.cx = w;
    bmh.cy = h;
    bmh.cPlanes = 1;
    bmh.cBitCount = 32;
    hbm = GpiCreateBitmap(hps, &bmh, 0, 0, 0);

    if (hbm != NULLHANDLE) {
        GpiSetBitmap(hps, hbm);
        HPS hpsWin = WinGetPS(winId);
        if (hpsWin != NULLHANDLE) {
            POINTL pnts[] = { {0, 0}, {w, h}, {x, y} };
            if (GpiBitBlt(hps, hpsWin, 3, pnts,
                          ROP_SRCCOPY, BBO_IGNORE) != GPI_ERROR) {
                GpiSetBitmap(hps, NULLHANDLE);
                pm = fromPmHBITMAP(hbm);
            }
            WinReleasePS(hpsWin);
        }
        GpiDeleteBitmap(hbm);
    }

    qt_free_mem_ps(hps);

    return pm;
}

QT_END_NAMESPACE
