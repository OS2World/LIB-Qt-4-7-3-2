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

#include <qdebug.h>

//#define QDIVE_DEBUG

#if defined QDIVE_DEBUG
#define DEBUG(a) qDebug a
#define DEBUG_VAR(v) DEBUG(() << #v << v)
#define DEBUG_VAR_HEX(v) DEBUG(() << #v << qDebugFmtHex(v))
#else
#define DEBUG(a) do {} while(0)
#define DEBUG_VAR(v) do {} while(0)
#define DEBUG_VAR_HEX(v) do {} while(0)
#endif

#include <qt_os2.h>
#include <qlibrary.h>
#include <qobject.h>
#include <qevent.h>

#include "qwindowsurface_pm_p.h"
#include "private/qnativeimage_p.h"
#include "private/qdnd_p.h"

////////////////////////////////////////////////////////////////////////////////

// The below definitions are stolen from the OS/2 Toolkit 4.5 headers (dive.h)
// to avoid the requirement of having the Toolkit installed when building Qt
// and let it dynamically link to dive.dll.

#define mmioFOURCC( ch0, ch1, ch2, ch3 )                         	\
      ( (ULONG)(BYTE)(ch0) | ( (ULONG)(BYTE)(ch1) << 8 ) |	\
      ( (ULONG)(BYTE)(ch2) << 16 ) | ( (ULONG)(BYTE)(ch3) << 24 ) )

#define FOURCC_SCRN  0
#define FOURCC_BGR4  mmioFOURCC( 'B', 'G', 'R', '4' )
#define FOURCC_RGB4  mmioFOURCC( 'R', 'G', 'B', '4' )
#define FOURCC_BGR3  mmioFOURCC( 'B', 'G', 'R', '3' )
#define FOURCC_RGB3  mmioFOURCC( 'R', 'G', 'B', '3' )
#define FOURCC_R565  mmioFOURCC( 'R', '5', '6', '5' )
#define FOURCC_R555  mmioFOURCC( 'R', '5', '5', '5' )
#define FOURCC_R664  mmioFOURCC( 'R', '6', '6', '4' )

#define FOURCC ULONG
#define HDIVE ULONG

#define DIVE_SUCCESS                                     0x00000000

#define DIVE_BUFFER_SCREEN                               0x00000000
#define DIVE_BUFFER_GRAPHICS_PLANE                       0x00000001
#define DIVE_BUFFER_ALTERNATE_PLANE                      0x00000002

typedef struct _DIVE_CAPS {

   ULONG  ulStructLen;            /* Set equal to sizeof(DIVE_CAPS)          */
   ULONG  ulPlaneCount;           /* Number of defined planes.               */

   /* Info returned in the following fields pertains to ulPlaneID.           */
   BOOL   fScreenDirect;          /* TRUE if can get addressability to vram. */
   BOOL   fBankSwitched;          /* TRUE if vram is bank-switched.          */
   ULONG  ulDepth;                /* Number of bits per pixel.               */
   ULONG  ulHorizontalResolution; /* Screen width in pixels.                 */
   ULONG  ulVerticalResolution;   /* Screen height in pixels.                */
   ULONG  ulScanLineBytes;        /* Screen scan line size in bytes.         */
   FOURCC fccColorEncoding;       /* Colorspace encoding of the screen.      */
   ULONG  ulApertureSize;         /* Size of vram aperture in bytes.         */

   ULONG  ulInputFormats;         /* Number of input color formats.          */
   ULONG  ulOutputFormats;        /* Number of output color formats.         */
   ULONG  ulFormatLength;         /* Length of format buffer.                */
   PVOID  pFormatData;            /* Pointer to color format buffer FOURCC's.*/

   } DIVE_CAPS;
typedef DIVE_CAPS *PDIVE_CAPS;

typedef struct _SETUP_BLITTER {

     /* Set the ulStructLen field equal to the amount of the structure used. */
     /* allowable: blank lines below mark sizes of 8, 28, 32, 52, 60, or 68. */
   ULONG  ulStructLen;
     /* Set the ulInvert flags based on the following:                       */
     /* b0001 = d01 = h01 = flip the image in the horizontal direction.      */
     /* b0010 = d02 = h02 = flip the image in the vertical direction.        */
     /* All other bits ignored.                                              */
   ULONG  fInvert;

     /* This is the color format of the source data.  See "FOURCC.H"         */
   FOURCC fccSrcColorFormat;
     /* This is the width of the source image in pixels.                     */
   ULONG  ulSrcWidth;
     /* This is the height of the source image in pixels.                    */
   ULONG  ulSrcHeight;
     /* This is the horizontal offset from which to start displaying for     */
     /* use in displaying a sub-portion of the source image.                 */
   ULONG  ulSrcPosX;
     /* This is the vertical offset from which to start displaying.          */
   ULONG  ulSrcPosY;

     /* This is the dither type to use.  0 defines no dither and 1           */
     /* defines 2x2 dither (all others ignored).  Note: dithering is only    */
     /* supported in direct color to LUT8 conversions.                       */
   ULONG  ulDitherType;

     /* This is the color format of the destinaion data.  See "FOURCC.H"     */
   FOURCC fccDstColorFormat;
     /* This is the width of the destination image in pixels.                */
   ULONG  ulDstWidth;
     /* This is the height of the destination image in pixels.               */
   ULONG  ulDstHeight;
     /* This is the horizontal offset from which to start displaying for     */
     /* use in displaying to sub-portion of the destination image.           */
   LONG   lDstPosX;
     /* This is the vertical offset from which to start displaying.          */
   LONG   lDstPosY;

     /* This is the world screen horizontal position, where 0 is left.       */
     /* These are ignored if the destination is not the screen.              */
   LONG   lScreenPosX;
     /* This is the world screen vertical position, where 0 is bottom.       */
   LONG   lScreenPosY;

     /* This is the number of visible rectangular regions being passed in.   */
     /* These are ignored if the destination is not the screen.              */
     /* Also, if you application *KNOWS* that the region is fully visible    */
     /* (like not going to the screen), the you can use DIVE_FULLY_VISIBLE   */
     /* instead of making up a bogus visible region structure.               */
   ULONG  ulNumDstRects;
     /* This points to an array of visible regions which defines what        */
     /* portions of the source image are to be displayed.                    */
   PRECTL pVisDstRects;           /* Pointer to array of visible rectangles. */

   } SETUP_BLITTER;
typedef SETUP_BLITTER *PSETUP_BLITTER;

// functions resolved by dive.dll

static
ULONG (APIENTRY *DiveQueryCaps) ( PDIVE_CAPS pDiveCaps,
                                  ULONG      ulPlaneBufNum ) = 0;

static
ULONG (APIENTRY *DiveOpen) ( HDIVE *phDiveInst,
                             BOOL   fNonScreenInstance,
                             PVOID  ppFrameBuffer ) = 0;

static
ULONG (APIENTRY *DiveSetupBlitter) ( HDIVE          hDiveInst,
                                     PSETUP_BLITTER pSetupBlitter ) = 0;

static
ULONG (APIENTRY *DiveBlitImage) ( HDIVE hDiveInst,
                                  ULONG ulSrcBufNumber,
                                  ULONG ulDstBufNumber ) = 0;

static
ULONG (APIENTRY *DiveClose) ( HDIVE hDiveInst ) = 0;

static
ULONG (APIENTRY *DiveAcquireFrameBuffer) ( HDIVE   hDiveInst,
                                           PRECTL  prectlDst ) = 0;

static
ULONG (APIENTRY *DiveDeacquireFrameBuffer) ( HDIVE hDiveInst ) = 0;

static
ULONG (APIENTRY *DiveAllocImageBuffer) ( HDIVE  hDiveInst,
                                         PULONG pulBufferNumber,
                                         FOURCC fccColorSpace,
                                         ULONG  ulWidth,
                                         ULONG  ulHeight,
                                         ULONG  ulLineSizeBytes,
                                         PBYTE  pbImageBuffer ) = 0;

static
ULONG (APIENTRY *DiveFreeImageBuffer) ( HDIVE hDiveInst,
                                        ULONG ulBufferNumber ) = 0;

static
ULONG (APIENTRY *DiveBeginImageBufferAccess) ( HDIVE  hDiveInst,
                                               ULONG  ulBufferNumber,
                                               PBYTE *ppbImageBuffer,
                                               PULONG pulBufferScanLineBytes,
                                               PULONG pulBufferScanLines ) = 0;

static
ULONG (APIENTRY *DiveEndImageBufferAccess) ( HDIVE hDiveInst,
                                             ULONG ulBufferNumber ) = 0;

// function table

#define FUNC_ENTRY(name) { #name, (void **)&name }

static struct { const char *name; void **entry; } diveDllFuncs[] =
{
    FUNC_ENTRY(DiveQueryCaps),
    FUNC_ENTRY(DiveOpen),
    FUNC_ENTRY(DiveSetupBlitter),
    FUNC_ENTRY(DiveBlitImage),
    FUNC_ENTRY(DiveClose),
    FUNC_ENTRY(DiveAcquireFrameBuffer),
    FUNC_ENTRY(DiveDeacquireFrameBuffer),
    FUNC_ENTRY(DiveAllocImageBuffer),
    FUNC_ENTRY(DiveFreeImageBuffer),
    FUNC_ENTRY(DiveBeginImageBufferAccess),
    FUNC_ENTRY(DiveEndImageBufferAccess),
};

static QLibrary diveDll(QLatin1String("dive"));
static bool diveDllResolved = false;
static bool diveDllOK = false;

static DIVE_CAPS diveCaps = { 0 };
static bool diveUseFB = false;
static ULONG diveColorMap[3][256] = { { 0 } };
static HDIVE diveHandle = NULLHANDLE;
static char *diveFrameBuf = NULL;
static bool diveHideMouse = false;
static FOURCC diveBestBufFormat = 0;

static LONG mousePtrSize = 0;

////////////////////////////////////////////////////////////////////////////////

QT_BEGIN_NAMESPACE

#ifdef Q_CC_GNU
extern inline unsigned bswap32_p(unsigned u)
{
    __asm__ __volatile__ ("bswap %0\n"
                          : "=r" (u)
                          : "0" (u));
    return u;
}
#else
#define bswap32_p(a) \
    ((((ULONG)(a)) >> 24) | (((ULONG)(a)) << 24) | \
     (((ULONG)(a) << 8) & 0x00ff0000) | (((ULONG)(a) >> 8) & 0x0000ff00))
#endif

// Returns a directly matching QImage format for the given FOURCC (including
// bit order and depth, scan line size etc.) or Format_Invalid.
static QImage::Format fourccToFormat(FOURCC fourcc)
{
    switch (fourcc) {
        case FOURCC_R555: return QImage::Format_RGB555;
        case FOURCC_R565: return QImage::Format_RGB16;
        case FOURCC_RGB3: return QImage::Format_RGB888;
        case FOURCC_BGR4: return QImage::Format_RGB32;
        default: return QImage::Format_Invalid;
    }
}

// Returns a FOURCC that is best for the buffer used to blit to the given
// screen FOURCC. Returns 0 (FOURC_SCRN) if there is no suitable conversion,
// otherwise it is guaranteed that the returned value is accepted by
// fourccToFormat(). If isFB is true, the selection is made for the direct
// framebuffer access mode.
static FOURCC fourccScreenToBuffer(FOURCC fourcc, bool isFB)
{
    // return it as is if supported by fourccToFormat()
    if (fourccToFormat(fourcc) != QImage::Format_Invalid)
        return fourcc;

    if (!isFB) {
        // otherwise, use FOURCC_RGB3 (which in theory should always work; if not,
        // we will add exceptions here and return 0 in such cases). Note that
        // although working with 32-bit pixels would be faster, we cannot return
        // FOURCC_BGR4 here because DiveBlitImage() is known to crahsh when the
        // source buffer is BGR4 and the screen is not (at least, it's the case
        // with recent SNAP versions)
        return FOURCC_RGB3;
    }

    // in direct framebuffer access mode, we use BGR4 which should be faster
    // (note that our color conversion table built in QPMDiveWindowSurface::create()
    // only works with BGR4 for now so we must return it here otherwise the FB
    // mode will be disabled)
    return FOURCC_BGR4;
}

class QPMDiveWindowSurfaceFB : public QPMDiveWindowSurface
{
public:
    QPMDiveWindowSurfaceFB(QWidget *widget);
    ~QPMDiveWindowSurfaceFB();
    void doFlush(QWidget *widget, const QRect &from, const QPoint &to);
};

struct QPMDiveWindowSurfacePrivate : public QObject, private QWidget::PmEventFilter
{
    QPMDiveWindowSurface *that;

    QImage *image;
    HDIVE hDive;
    bool useFB;
    ULONG bufNum;
    bool posDirty;
    bool inDrag;
    SETUP_BLITTER setup;

    struct FlushArgs
    {
        QWidget *widget;
        QRect from;
        QPoint to;
    };
    QList<FlushArgs> pending;

    struct WidgetData
    {
        int widgetHeight;
        bool vrnDirty : 1;
        bool vrnDisabled : 1;
        size_t rclCount;
        QVector<RECTL> rcls;
    };

    WidgetData data;
    QMap<QWidget *, WidgetData> *subWidgets;

    void addWidget(QWidget *widget);
    void removeWidget(QWidget *widget);

    WidgetData *widgetData(QWidget *widget) {
        // check for the most common case (no sub-widgets with own HWNDs)
        if (widget == that->window())
            return &data;
        if (!subWidgets || !subWidgets->contains(widget))
            return 0;
        return &(*subWidgets)[widget];
    }

    bool eventFilter(QObject *obj, QEvent *event);
    bool pmEventFilter(QMSG *msg, MRESULT *result);
};

void QPMDiveWindowSurfacePrivate::addWidget(QWidget *widget)
{
    Q_ASSERT(widget->internalWinId());

    WidgetData *wd = &data;
    if (widget != that->window()) {
        // lazily create the sub-widget map (only when really necessary)
        if (!subWidgets)
            subWidgets = new QMap<QWidget *, WidgetData>();
        wd = &(*subWidgets)[widget];
    }

    wd->vrnDirty = true;
    wd->vrnDisabled = false;
    wd->widgetHeight = 0;
    wd->rclCount = 0;

    // receive visible region change messages and other PM messages
    widget->addPmEventFilter(this);
    WinSetVisibleRegionNotify(widget->internalWinId(), TRUE);

    if (widget != that->window()) {
        // receive reparent messages from children to cleanup the map
        widget->installEventFilter(this);
    }
}

void QPMDiveWindowSurfacePrivate::removeWidget(QWidget *widget)
{
    if (widget != that->window()) {
        widget->removeEventFilter(this);
        subWidgets->remove(widget);
    }

    WinSetVisibleRegionNotify(widget->internalWinId(), FALSE);
    widget->removePmEventFilter(this);
}

bool QPMDiveWindowSurfacePrivate::eventFilter(QObject *obj, QEvent *event)
{
    QWidget *widget = qobject_cast<QWidget *>(obj);
    if (event->type() == QEvent::ParentAboutToChange ||
        event->type() == QEvent::Destroy) {
        removeWidget(widget);
    }

    return false;
}

bool QPMDiveWindowSurfacePrivate::pmEventFilter(QMSG *msg, MRESULT *result)
{
    switch (msg->msg) {
        case WM_VRNDISABLED: {
            DEBUG(() << "WM_VRNDISABLED:" << qDebugHWND(msg->hwnd));
            if (msg->hwnd == that->window()->internalWinId()) {
                if (!useFB)
                    DiveSetupBlitter(hDive, NULL);
                data.vrnDisabled = true;
            } else {
                WidgetData *wd = widgetData(QWidget::find(msg->hwnd));
                Q_ASSERT(wd);
                if (wd)
                    wd->vrnDisabled = true;
            }
            *result = 0;
            return true;
        }
        case DM_DRAGOVER: {
            inDrag = true;
            break;
        }
        case DM_DRAGLEAVE:
        case DM_DROP: {
            inDrag = false;
            break;
        }
        case WM_VRNENABLED: {
            DEBUG(() << "WM_VRNENABLED:" << qDebugHWND(msg->hwnd));
            QWidget *widget = msg->hwnd == that->window()->internalWinId() ?
                              that->window() : QWidget::find(msg->hwnd);
            WidgetData *wd = widgetData(widget);
            Q_ASSERT(wd);
            wd->vrnDisabled = false;
            // Note that when an overlapping window of *other* process is moved
            // over this window, PM still sends WM_VRNENABLED to it but with
            // ffVisRgnChanged set to FALSE although the visible region *does*
            // actually change (it doesn't seem to be the case for windows of
            // the same process). The solution is to ignore this flag and always
            // assume that the visible region was changed when we get this msg
#if 0
            if (LONGFROMMP(msg->mp1)) // window's visible region changed
#endif
                wd->vrnDirty = true;

            if (widget == that->window())
                posDirty = true;

            // process pending flush events for this widget
            for (QList<QPMDiveWindowSurfacePrivate::FlushArgs>::iterator
                 it = pending.begin(); it != pending.end();) {
                if (it->widget == widget) {
                    that->doFlush(it->widget, it->from, it->to);
                    it = pending.erase(it);
                } else {
                    ++it;
                }
            }

            *result = 0;
            return true;
        }
        default:
            break;
    }

    return false;
}

QPMDiveWindowSurface::QPMDiveWindowSurface(QWidget* widget)
    : QWindowSurface(widget), d(new QPMDiveWindowSurfacePrivate)
{
    d->that = this;
    d->image = 0;
    d->hDive = NULLHANDLE;
    d->useFB = false;
    d->bufNum = 0;
    d->posDirty = true;
    d->inDrag = false;
    d->subWidgets = 0;

    memset(&d->setup, 0, sizeof(SETUP_BLITTER));

    // note that fccSrcColorFormat overrides the value specified during the
    // source buffer creation, so they must match
    Q_ASSERT(diveBestBufFormat != 0);
    d->setup.fccSrcColorFormat = diveBestBufFormat;
    d->setup.fccDstColorFormat = FOURCC_SCRN;

    // add self to the map of participating widgets
    d->addWidget(window());

    setStaticContentsSupport(true);

    DEBUG(() << "QPMDiveWindowSurface:" << widget);
}

QPMDiveWindowSurface::~QPMDiveWindowSurface()
{
    if (d->subWidgets) {
        QList<QWidget *> keys = d->subWidgets->keys();
        foreach(QWidget *w, keys)
            d->removeWidget(w);
        Q_ASSERT(d->subWidgets->count() == 0);
        delete d->subWidgets;
    }

    d->removeWidget(window());

    if (d->bufNum)
        DiveFreeImageBuffer(d->hDive, d->bufNum);
    if (d->hDive != NULLHANDLE)
        DiveClose(d->hDive);
    if (d->image)
        delete d->image;
}

QPaintDevice *QPMDiveWindowSurface::paintDevice()
{
    return d->image;
}

void QPMDiveWindowSurface::flush(QWidget *widget, const QRegion &rgn,
                                 const QPoint &offset)
{
    // Not ready for painting yet, bail out. This can happen in
    // QWidget::create_sys()
    if (!d->image || rgn.rectCount() == 0)
        return;

    // make sure the widget is known to us
    if (!d->widgetData(widget))
        d->addWidget(widget);

    QRect br = rgn.boundingRect();

    br.translate(offset);

    if (d->inDrag || QDragManager::self()->isInOwnDrag()) {
        // In drag, DIVE seems to not synchronize well with the mouse buffer
        // that preserves the contents under the mouse pointer and the drag
        // image while dragging (even when we use DiveBlitImage()). As a result,
        // we will get some areas unpainted under if painting is done during
        // drag. Use the WinGetPS()/GpiDrawBits() way to fix it. The below code
        // is merely a copy of QT_BITMAP_MIRROR == 3 from qwindowsurface_raster.cpp

        HPS wps = widget->getPS();

        MATRIXLF m;
        m.fxM11 = MAKEFIXED(1, 0);
        m.fxM12 = 0;
        m.lM13 = 0;
        m.fxM21 = 0;
        m.fxM22 = MAKEFIXED(-1, 0);
        m.lM23 = 0;
        m.lM31 = 0;
        m.lM32 = widget->height() - 1;
        GpiSetDefaultViewMatrix(wps, 8, &m, TRANSFORM_REPLACE);

        // make sure br doesn't exceed the backing storage size (it may happen
        // during resize & move due to the different event order)
        br = br.intersected(QRect(0, 0, d->image->width(), d->image->height()));

        QPoint wOffset = qt_qwidget_data(widget)->wrect.topLeft();
        // note that we remove offset from wbr because the widget's HPS has a proper
        // origin already that includes this offset (which is in fact a position of
        // the widget relative to its top-level parent)
        QRect wbr = br.translated(-offset - wOffset);

        BITMAPINFOHEADER2 bmh;
        memset(&bmh, 0, sizeof(BITMAPINFOHEADER2));
        bmh.cbFix = sizeof(BITMAPINFOHEADER2);
        bmh.cPlanes = 1;
        bmh.cBitCount = d->image->depth();
        bmh.cx = d->image->width();
        bmh.cy = d->image->height();

#ifdef QT_PM_NATIVEWIDGETMASK
        int wh = widget->height();

        // produce a clip region that excludes all descending obstacles
        // (like child widgets with real HWNDs which are not excluded by Qt)
        HWND wwnd = widget->internalWinId();
        RECTL wrcl = { wbr.left(), wh - wbr.bottom() - 1,
                       wbr.right() + 1, wh - wbr.top() };
        HRGN wrgn = GpiCreateRegion(wps, 1L, &wrcl);
        ULONG rc = qt_WinProcessWindowObstacles(wwnd, NULL, wrgn, CRGN_DIFF,
                                                PWO_Children);
        if (rc == RGN_RECT || rc == RGN_COMPLEX) {
            // set the clip region
            HRGN oldRgn;
            GpiSetClipRegion(wps, wrgn, &oldRgn);
            wrgn = oldRgn;
#endif
            // Note: target is inclusive-inclusive, source is inclusive-exclusive
            POINTL ptls[] = { { wbr.left(), wbr.top() },
                              { wbr.right(), wbr.bottom() },
                              { br.left(), br.top() },
                              { br.right() + 1, br.bottom() + 1 } };
            GpiDrawBits(wps, (PVOID) const_cast<const QImage *>(d->image)->bits(),
                        (PBITMAPINFO2) &bmh, 4, ptls, ROP_SRCCOPY, BBO_IGNORE);

#ifdef QT_PM_NATIVEWIDGETMASK
        }

        if (wrgn != NULLHANDLE)
            GpiDestroyRegion(wps, wrgn);
#endif

        widget->releasePS(wps);

        return;
    }

    QPoint wOffset = qt_qwidget_data(widget)->wrect.topLeft();
    // note that we leave offset in wbr since in DIVE mode the origin of the
    // blit target is always the top level window so we need to properly offset
    // the target position if we are flushing its child widget
    QRect wbr = br.translated(-wOffset);

    QPMDiveWindowSurfacePrivate::WidgetData *wd = d->widgetData(widget);
    Q_ASSERT(wd);

    DEBUG(() << "QPMDiveWindowSurface::flush:" << window() << widget
             << "vrnDisabled" << wd->vrnDisabled);

    if (wd->vrnDisabled) {
        // defer the flush
        QPMDiveWindowSurfacePrivate::FlushArgs args = { widget,
                                                        br, wbr.topLeft() };
        d->pending.append(args);
        return;
    }

    doFlush(widget, br, wbr.topLeft());
}

bool QPMDiveWindowSurface::adjustSetup(QWidget *widget)
{
    DEBUG(() << "QPMDiveWindowSurface::adjustSetup:" << window() << widget);

    HWND hwnd = window()->internalWinId();

    // don't include lScreenPosX and the rest by default
    d->setup.ulStructLen = offsetof(SETUP_BLITTER, lScreenPosX);
    bool setupDirty = false;

    QPMDiveWindowSurfacePrivate::WidgetData *wd = d->widgetData(widget);
    Q_ASSERT(wd);

    if (d->posDirty || wd->vrnDirty) {
        setupDirty = true;
        d->posDirty = false;
        // the main widget moved, adjust the target poition
        POINTL ptl = { 0, 0 };
        WinMapWindowPoints(hwnd, HWND_DESKTOP, &ptl, 1);
        d->setup.lScreenPosX = ptl.x;
        d->setup.lScreenPosY = ptl.y;
        d->setup.ulStructLen = offsetof(SETUP_BLITTER, ulNumDstRects);

        DEBUG(() << "QPMDiveWindowSurface::adjustSetup:" << "posDirty"
                 << ptl.x << ptl.y);
    }

    bool wasVrnDirty = wd->vrnDirty;

    if (wd->vrnDirty) {
        setupDirty = true;
        wd->vrnDirty = false;

        HPS hps = widget->getPS();
        HRGN hrgn = GpiCreateRegion(hps, 0L, NULL);

        HWND hwndWidget = widget->internalWinId();
        ULONG rc = WinQueryVisibleRegion(hwndWidget, hrgn);
#ifdef QT_PM_NATIVEWIDGETMASK
        if (rc != RGN_ERROR) {
            // substract children from the visible region, if any
            rc = qt_WinProcessWindowObstacles(hwndWidget, NULL, hrgn,
                                              CRGN_DIFF, PWO_Children);
#endif
            if (rc != RGN_ERROR && hwnd != hwndWidget) {
                // translate coords to the main widget's coordinate space
                POINTL ptlOffset = { 0, 0 };
                WinMapWindowPoints(hwnd, hwndWidget, &ptlOffset, 1);
                ptlOffset.x = -ptlOffset.x;
                ptlOffset.y = -ptlOffset.y;
                GpiOffsetRegion(hps, hrgn, &ptlOffset);
            }
#ifdef QT_PM_NATIVEWIDGETMASK
        }
#endif

        if (rc == RGN_RECT || rc == RGN_COMPLEX) {
            RGNRECT rgnCtl;
            rgnCtl.ircStart = 1;
            rgnCtl.crc = rgnCtl.crcReturned = 0;
            rgnCtl.ulDirection = RECTDIR_LFRT_TOPBOT;
            if (GpiQueryRegionRects(hps, hrgn, NULL, &rgnCtl, NULL) &&
                rgnCtl.crcReturned) {
                rgnCtl.ircStart = 1;
                rgnCtl.crc = rgnCtl.crcReturned;
                rgnCtl.ulDirection = RECTDIR_LFRT_TOPBOT;
                if (wd->rcls.size() < (int)rgnCtl.crc)
                    wd->rcls.resize((int)rgnCtl.crc);
                wd->rclCount = rgnCtl.crc;
                GpiQueryRegionRects(hps, hrgn, NULL, &rgnCtl, wd->rcls.data());
            }
        } else if (rc == RGN_NULL) {
            wd->rclCount = 0;
        }

        GpiDestroyRegion(hps, hrgn);
        widget->releasePS(hps);

        // memorize the window height used for the additional visible region
        // validation in doFlush()
        wd->widgetHeight = widget->height();

#if defined(QDIVE_DEBUG)
        DEBUG(() << "QPMDiveWindowSurface::adjustSetup:" << "vrnDirty");
        for (size_t i = 0; i < wd->rclCount; ++i)
            DEBUG(() << " " << i << ":" << wd->rcls[i]);
#endif
    }

    // make sure setup points to the correct visible rectangle array (note that
    // we switch it even vrnDirty is false since the widget may change)
    if (wasVrnDirty || d->setup.ulNumDstRects != wd->rclCount ||
        (wd->rclCount && d->setup.pVisDstRects != wd->rcls.data()) ||
        (!wd->rclCount && d->setup.pVisDstRects != NULL)) {
        setupDirty = true;
        d->setup.ulNumDstRects = wd->rclCount;
        d->setup.pVisDstRects = wd->rclCount ? wd->rcls.data() : NULL;
        d->setup.ulStructLen = sizeof(SETUP_BLITTER);
    }

    return setupDirty;
}

void QPMDiveWindowSurface::doFlush(QWidget *widget, const QRect &from, const QPoint &to)
{
    DEBUG(() << "QPMDiveWindowSurface::doFlush:" << window() << widget
             << "from" << from << "to" << to);

    // make sure from doesn't exceed the backing storage size (it may happen
    // during resize & move due to the different event order)
    QRect src = from.intersected(QRect(0, 0, d->image->width(), d->image->height()));
    QPoint dst = to + (src.topLeft() - from.topLeft());

    bool setupDirty = adjustSetup(widget);

    // note that the source image is expected to be top-left oriented
    // by DiveSetupBlitter() so we don't perform y coordinate flip

    // flip destination y coordinate
    dst.setY(window()->height() - dst.y() - src.height());

    SETUP_BLITTER setupTmp = d->setup;
    setupTmp.ulSrcWidth = src.width();
    setupTmp.ulSrcHeight = src.height();
    setupTmp.ulSrcPosX = src.left();
    setupTmp.ulSrcPosY = src.top();
    setupTmp.ulDstWidth = setupTmp.ulSrcWidth;
    setupTmp.ulDstHeight = setupTmp.ulSrcHeight;
    setupTmp.lDstPosX = dst.x();
    setupTmp.lDstPosY = dst.y();

    if (memcmp(&d->setup, &setupTmp, d->setup.ulStructLen)) {
        setupDirty = true;
        memcpy(&d->setup, &setupTmp, d->setup.ulStructLen);
    }

    if (setupDirty) {
        DiveSetupBlitter(d->hDive, &d->setup);
    }

    DiveBlitImage(d->hDive, d->bufNum, DIVE_BUFFER_SCREEN);
}

void QPMDiveWindowSurface::setGeometry(const QRect &rect)
{
    // this method is mostly like QRasterWindowSurface::prepareBuffer()

    QWindowSurface::setGeometry(rect);

    if (d->image == 0 || d->image->width() < rect.width() || d->image->height() < rect.height()) {
        int width = window()->width();
        int height = window()->height();
        if (d->image) {
            width = qMax(d->image->width(), width);
            height = qMax(d->image->height(), height);
        }

        if (width == 0 || height == 0) {
            delete d->image;
            d->image = 0;
            return;
        }

        QImage *oldImage = d->image;

        QImage::Format format = fourccToFormat(d->setup.fccSrcColorFormat);
        Q_ASSERT(format != QImage::Format_Invalid);
        d->image = new QImage(width, height, format);

        if (!d->useFB) {
            // associate the image data pointer with the buffer number
            if (d->bufNum)
                DiveFreeImageBuffer(d->hDive, d->bufNum);
            d->bufNum = 0;
            ULONG rc = DiveAllocImageBuffer(d->hDive, &d->bufNum,
                                            d->setup.fccSrcColorFormat,
                                            width, height,
                                            d->image->bytesPerLine(),
                                            (PBYTE)const_cast<const QImage *>(d->image)->bits());
            if (rc != DIVE_SUCCESS) {
                qWarning("QPMDiveWindowSurface::setGeometry: DiveAllocImageBuffer "
                         "returned 0x%08lX", rc);
                delete d->image;
                delete oldImage;
                return;
            }
        }

        if (oldImage && hasStaticContents()) {
            // Make sure we use the const version of bits() (no detach).
            const uchar *src = const_cast<const QImage *>(oldImage)->bits();
            uchar *dst = d->image->bits();

            const int srcBytesPerLine = oldImage->bytesPerLine();
            const int dstBytesPerLine = d->image->bytesPerLine();
            const int bytesPerPixel = oldImage->depth() >> 3;

            QRegion staticRegion(staticContents());
            // Make sure we're inside the boundaries of the old image.
            staticRegion &= QRect(0, 0, oldImage->width(), oldImage->height());
            const QVector<QRect> &rects = staticRegion.rects();
            const QRect *srcRect = rects.constData();

            // Copy the static content of the old image into the new one.
            int numRectsLeft = rects.size();
            do {
                const int bytesOffset = srcRect->x() * bytesPerPixel;
                const int dy = srcRect->y();

                // Adjust src and dst to point to the right offset.
                const uchar *s = src + dy * srcBytesPerLine + bytesOffset;
                uchar *d = dst + dy * dstBytesPerLine + bytesOffset;
                const int numBytes = srcRect->width() * bytesPerPixel;

                int numScanLinesLeft = srcRect->height();
                do {
                    ::memcpy(d, s, numBytes);
                    d += dstBytesPerLine;
                    s += srcBytesPerLine;
                } while (--numScanLinesLeft);

                ++srcRect;
            } while (--numRectsLeft);
        }

        delete oldImage;
    }
}

// from qwindowsurface.cpp
extern void qt_scrollRectInImage(QImage &img, const QRect &rect, const QPoint &offset);

bool QPMDiveWindowSurface::scroll(const QRegion &area, int dx, int dy)
{
    if (!d->image || d->image->isNull())
        return false;

    const QVector<QRect> rects = area.rects();
    for (int i = 0; i < rects.size(); ++i)
        qt_scrollRectInImage(*d->image, rects.at(i), QPoint(dx, dy));

    return true;
}

// static
QPMDiveWindowSurface *QPMDiveWindowSurface::create(QWidget *widget)
{
    if (!diveDllResolved) {
        diveDllResolved = true;
        diveDllOK = qgetenv("QT_PM_NO_DIVE").isEmpty();
        if (diveDllOK) {
            QByteArray diveEnv = qgetenv("QT_PM_DIVE").trimmed().toUpper();
            if (diveEnv == "BLIT") {
                // use DiveBlitImage()
                diveUseFB = false;
            } else if (diveEnv == "FB") {
                // use direct framebuffer access
                diveUseFB = true;
            } else if (diveEnv == "FBSWM") {
                // use direct framebuffer access, hide the mouse pointer
                diveUseFB = true;
                diveHideMouse = true;
            } else {
                // dedect the Panorama video driver presence
                HMODULE hmod;
                bool isPanorama =
                    DosQueryModuleHandle("VBE2GRAD", &hmod) == NO_ERROR &&
                    DosQueryModuleHandle("PANOGREX", &hmod) == NO_ERROR;
                if (isPanorama) {
                    // if Panorama is detected, disable DIVE by default due to
                    // bugs in the DIVE implementation
                    diveDllOK = false;
                } else {
                    // FBSWM is a safe default for SNAP and probably other drivers
                    diveUseFB = true;
                    diveHideMouse = true;
                }
            }
        }

        if (diveDllOK) {
            // resolve Dive functions
            for (size_t i = 0; i < sizeof(diveDllFuncs) / sizeof(diveDllFuncs[0]); ++i) {
                *diveDllFuncs[i].entry = diveDll.resolve(diveDllFuncs[i].name);
                if (!*diveDllFuncs[i].entry) {
                    diveDllOK = false;
                    break;
                }
            }

            if (diveDllOK) {
                diveCaps.ulStructLen = sizeof(diveCaps);
                DiveQueryCaps(&diveCaps, DIVE_BUFFER_SCREEN);

                DEBUG_VAR(diveCaps.fScreenDirect);
                DEBUG_VAR(diveCaps.fBankSwitched);
                DEBUG_VAR(diveCaps.ulDepth);
                DEBUG_VAR(diveCaps.ulHorizontalResolution);
                DEBUG_VAR(diveCaps.ulVerticalResolution);
                DEBUG_VAR(diveCaps.ulScanLineBytes);
                DEBUG(() << "diveCaps.fccColorEncoding"
                         << ((char*)&diveCaps.fccColorEncoding)[0]
                         << ((char*)&diveCaps.fccColorEncoding)[1]
                         << ((char*)&diveCaps.fccColorEncoding)[2]
                         << ((char*)&diveCaps.fccColorEncoding)[3]);

                diveBestBufFormat = fourccScreenToBuffer(diveCaps.fccColorEncoding,
                                                         diveUseFB);
                DEBUG(() << "diveBestBufFormat"
                         << ((char*)&diveBestBufFormat)[0]
                         << ((char*)&diveBestBufFormat)[1]
                         << ((char*)&diveBestBufFormat)[2]
                         << ((char*)&diveBestBufFormat)[3]);

                if (diveUseFB) {
                    if (!diveCaps.fScreenDirect || diveCaps.fBankSwitched) {
                        // direct framebuffer is not supported by the driver
                        // (and switching banks is not supported by our code)
                        diveUseFB = false;
                    } else {
                        if (diveBestBufFormat == diveCaps.fccColorEncoding) {
                            // no color conversion is required
                        } else if (diveBestBufFormat == FOURCC_BGR4) {
                            // build the color conversion table
                            switch (diveCaps.fccColorEncoding) {
#if 0
                            // FOURCC_R565/FOURCC_R555 should be handled directly
                            case FOURCC_R565: {
                                for (ULONG u = 0; u < 256; ++u) {
                                    diveColorMap[0][u] = (u >> 3) << 0;
                                    diveColorMap[1][u] = (u >> 2) << 5;
                                    diveColorMap[2][u] = (u >> 3) << 11;
                                }
                                break;
                            }
                            case FOURCC_R555: {
                                for (ULONG u = 0; u < 256; ++u) {
                                    diveColorMap[0][u] = (u >> 3) << 0;
                                    diveColorMap[1][u] = (u >> 3) << 5;
                                    diveColorMap[2][u] = (u >> 3) << 10;
                                }
                                break;
                            }
#endif
                            case FOURCC_R664: {
                                for (ULONG u = 0; u < 256; ++u) {
                                    diveColorMap[0][u] = (u >> 2) << 0;
                                    diveColorMap[1][u] = (u >> 2) << 6;
                                    diveColorMap[2][u] = (u >> 4) << 12;
                                }
                                break;
                            }
#if 0
                            // FOURCC_BGR4 should be handled directly
                            case FOURCC_BGR4:
#endif
                            case FOURCC_RGB4:
                            case FOURCC_BGR3:
                            case FOURCC_RGB3:
                                break;
                            default:
                                // screen pixel format is not supported
                                diveUseFB = false;
                                break;
                            }
                        } else {
                            Q_ASSERT(false);
                            diveUseFB = false;
                        }
                    }

                    if (!diveUseFB) {
                        // we disabled FB, recalculate the best format
                        diveBestBufFormat = fourccScreenToBuffer(diveCaps.fccColorEncoding,
                                                                 diveUseFB);
                        DEBUG(() << "diveBestBufFormat (final)"
                                 << ((char*)&diveBestBufFormat)[0]
                                 << ((char*)&diveBestBufFormat)[1]
                                 << ((char*)&diveBestBufFormat)[2]
                                 << ((char*)&diveBestBufFormat)[3]);
                    }
                }

                if (!diveUseFB) {
                    if (diveBestBufFormat == 0) {
                        // there is no working pixel format for the buffer for
                        // DiveBlitImage() to work correctly with the current screen
                        // format, give up
                        diveDllOK = false;
                    }
                }

                mousePtrSize = WinQuerySysValue(HWND_DESKTOP, SV_CXPOINTER);
            }
        }

        DEBUG_VAR(diveDllOK);
        DEBUG_VAR(diveUseFB);
        DEBUG_VAR(diveHideMouse);
    }

    // Note: returning 0 from this method will cause using QRasterWindowSurface
    // as a fallback

    if (!diveDllOK)
        return 0;

    HDIVE hDive = NULLHANDLE;
    ULONG rc = DIVE_SUCCESS;

    if (diveUseFB) {
        // we use a shared DIVE instance for all widgets
        if (diveHandle == NULLHANDLE)
            rc = DiveOpen(&diveHandle, FALSE, &diveFrameBuf);
        hDive = diveHandle;
    } else {
        // we need a new DIVE instance to reduce the number of calls to
        // DiveSetupBlitter() (as recommended by MMAPG)
        rc = DiveOpen(&hDive, FALSE, 0);
    }

    if (rc != DIVE_SUCCESS) {
        qWarning("QPMDiveWindowSurface::create: DiveOpen returned 0x%08lX", rc);
        return 0;
    }

    QPMDiveWindowSurface *surface = diveUseFB ?
        new QPMDiveWindowSurfaceFB(widget) : new QPMDiveWindowSurface(widget);

    if (surface)
        surface->d->hDive = hDive;
    else if (!diveUseFB)
        DiveClose(hDive);

    return surface;
}

////////////////////////////////////////////////////////////////////////////////

QPMDiveWindowSurfaceFB::QPMDiveWindowSurfaceFB(QWidget* widget)
    : QPMDiveWindowSurface(widget)
{
    d->useFB = true;
}

QPMDiveWindowSurfaceFB::~QPMDiveWindowSurfaceFB()
{
    // prevent the shared DIVE handle from closing
    d->hDive = NULLHANDLE;
}

void QPMDiveWindowSurfaceFB::doFlush(QWidget *widget, const QRect &from, const QPoint &to)
{
    DEBUG(() << "QPMDiveWindowSurfaceFB::doFlush:" << window() << widget
             << "from" << from << "to" << to);

    // make sure from doesn't exceed the backing storage size (it may happen
    // during resize & move due to the different event order)
    QRect src = from.intersected(QRect(0, 0, d->image->width(), d->image->height()));

    // convert the "to" coordinate to the delta
    QPoint dstDelta = from.topLeft() - to;

    QPMDiveWindowSurfacePrivate::WidgetData *wd = d->widgetData(widget);
    Q_ASSERT(wd);

    int widgetHeight = widget->height();

    // sometimes PM does not send WM_VRNENABLED although the window size has
    // been changed (this is known to happen in FB mode with the Qt Assistant
    // application) which leads to screen corruption due to outdated visible
    // regions. A fix is to memorize the window height and check it here
    if (widgetHeight != wd->widgetHeight)
        wd->vrnDirty = true;

    bool wasPosDirty = d->posDirty;
    bool wasVrnDirty = wd->vrnDirty;

    adjustSetup(widget);

    int windowHeight = window()->height();

    if (wasVrnDirty) {
        // flip the y coordinate of all rectangles (both the image and the frame
        // buffer are top-left oriented) and also make all points inclusive
        for (ULONG i = 0; i < d->setup.ulNumDstRects; ++i) {
            RECTL &rcl = d->setup.pVisDstRects[i];
            --rcl.xRight;
            --rcl.yTop;
            rcl.yBottom = windowHeight - rcl.yBottom - 1;
            rcl.yTop = windowHeight - rcl.yTop - 1;
        }
    }
    if (wasPosDirty || wasVrnDirty) {
        // the same flip for the window position
        d->setup.lScreenPosY = diveCaps.ulVerticalResolution -
                               d->setup.lScreenPosY - windowHeight;
    }

    // just assume that the rectangle in DiveAcquireFrameBuffer() is in PM
    // coordinates (bottom-left based, top-right exclusive), MMREF doesn't
    // mention anything particular...
    RECTL rclDst = { src.left(),
                     diveCaps.ulVerticalResolution -
                     (d->setup.lScreenPosY + src.bottom() + dstDelta.y()) - 1,
                     src.right() + 1,
                     diveCaps.ulVerticalResolution -
                     (d->setup.lScreenPosY + src.top() + dstDelta.y()) };

    const int srcBpp = d->image->depth() >> 3;
    const int dstBpp = diveCaps.ulDepth >> 3;
    Q_ASSERT(srcBpp > 0); // we don't expect color depths < 1 byte here

    const int srcBytesPerLine = d->image->bytesPerLine();

    bool ptrHidden = false;
    if (diveHideMouse) {
        // DIVE is not able to preserve the software mouse pointer when
        // painting over it using the direct framebuffer access. We have to
        // hide it to avoid screen corruption underneath
        POINTL ptrPos;
        if (WinQueryPointerPos(HWND_DESKTOP, &ptrPos)) {
            ptrPos.x -= d->setup.lScreenPosX;
            ptrPos.y = diveCaps.ulVerticalResolution - ptrPos.y - 1 -
                       d->setup.lScreenPosY;
            // keep a safety zone around the rectangle to each direction
            // as we don't know the exact mouse pointer configuration
            if (ptrPos.x >= (src.left() - mousePtrSize) &&
                ptrPos.x <= (src.right() + mousePtrSize) &&
                ptrPos.y >= (src.top() - mousePtrSize) &&
                ptrPos.y <= (src.bottom() + mousePtrSize))
                ptrHidden = WinShowPointer(HWND_DESKTOP, FALSE);
        }
    }

    if (DiveAcquireFrameBuffer(d->hDive, &rclDst) == DIVE_SUCCESS) {
        // take each visible rectangle and blit it
        for (ULONG i = 0; i < d->setup.ulNumDstRects; ++i) {
            RECTL rcl = d->setup.pVisDstRects[i];

            if (rcl.xLeft < src.left())
              rcl.xLeft = src.left();
            if (rcl.xRight > src.right())
              rcl.xRight = src.right();
            if (rcl.yTop < src.top())
              rcl.yTop = src.top();
            if (rcl.yBottom > src.bottom())
              rcl.yBottom = src.bottom();

            int rows = rcl.yBottom - rcl.yTop + 1;
            int cols = rcl.xRight - rcl.xLeft + 1;
            int i;

            if (cols > 0 && rows > 0) {
                const uchar *srcBits =
                    d->image->scanLine(rcl.yTop) + rcl.xLeft * srcBpp;
                char *dstBits = diveFrameBuf +
                    (d->setup.lScreenPosY + rcl.yTop + dstDelta.y()) * diveCaps.ulScanLineBytes +
                    (d->setup.lScreenPosX + rcl.xLeft + dstDelta.x()) * dstBpp;

                if (d->setup.fccSrcColorFormat == diveCaps.fccColorEncoding) {
                    // no color conversion is required
                    do {
                        memcpy(dstBits, srcBits, srcBpp * cols);
                        srcBits += srcBytesPerLine;
                        dstBits += diveCaps.ulScanLineBytes;
                    } while (--rows);
                } else {
                    Q_ASSERT(d->setup.fccSrcColorFormat == FOURCC_BGR4);
                    Q_ASSERT(d->image->format() == QImage::Format_RGB32);
                    Q_ASSERT(srcBpp == 4);
                    switch (diveCaps.fccColorEncoding) {

#if 0
                    // FOURCC_BGR4 is covered by memcpy()
                    case FOURCC_BGR4:
                        do {
                            for (i = 0; i < cols; i++ ) {
                                *(PULONG)dstBits = *(PULONG)srcBits;
                                srcBits += 4;
                                dstBits += 4;
                            }
                            srcBits += srcBytesPerLine - (cols * 4);
                            dstBits += diveCaps.ulScanLineBytes - (cols * 4);
                        } while (--rows);
                        break;
#endif

                    case FOURCC_RGB4:
                        do {
                            for (i = 0; i < cols; i++ ) {
                                *(PULONG)dstBits = bswap32_p(*(PULONG)srcBits) >> 8;
                                srcBits += 4;
                                dstBits += 4;
                            }
                            srcBits += srcBytesPerLine - (cols * 4);
                            dstBits += diveCaps.ulScanLineBytes - (cols * 4);
                        } while (--rows);
                        break;

                    case FOURCC_BGR3:
                        do {
                            // copy in batches by 4 pixels
                            for (i = cols; i >= 4; i -= 4) {
                                *(PULONG)&dstBits[0] =
                                    (*(PULONG)&srcBits[0] & 0x00ffffff) |
                                    (*(PUCHAR)&srcBits[4] << 24);
                                *(PULONG)&dstBits[4] =
                                    (*(PUSHORT)&srcBits[5]) |
                                    (*(PUSHORT)&srcBits[8] << 16);
                                *(PULONG)&dstBits[8] =
                                    (*(PUCHAR)&srcBits[10]) |
                                    (*(PULONG)&srcBits[12] << 8);
                                srcBits += 16;
                                dstBits += 12;
                            }
                            // copy the rest
                            while (i--) {
                                dstBits[0] = srcBits[0];
                                dstBits[1] = srcBits[1];
                                dstBits[2] = srcBits[2];
                                srcBits += 4;
                                dstBits += 3;
                            }
                            srcBits += srcBytesPerLine - (cols * 4);
                            dstBits += diveCaps.ulScanLineBytes - (cols * 3);
                        } while (--rows);
                        break;

                    case FOURCC_RGB3:
                        do {
                            // copy in batches by 4 pixels
                            for (i = cols; i >= 4; i -= 4) {
                                *(PULONG)&dstBits[0] =
                                    (*(PUCHAR)&srcBits[6] << 24) |
                                    (bswap32_p(*(PULONG)srcBits) >> 8);
                                *(PULONG)&dstBits[4] =
                                    bswap32_p(*(PUSHORT)&srcBits[9]) |
                                    (bswap32_p(*(PUSHORT)&srcBits[4]) >> 16);
                                *(PULONG)&dstBits[8] =
                                    (*(PUCHAR)&srcBits[8]) |
                                    (bswap32_p(*(PULONG)&srcBits[12]) & 0xffffff00);
                                srcBits += 16;
                                dstBits += 12;
                            }
                            // copy the rest
                            while (i--) {
                                dstBits[2] = srcBits[0];
                                dstBits[1] = srcBits[1];
                                dstBits[0] = srcBits[2];
                                srcBits += 4;
                                dstBits += 3;
                            }
                            srcBits += srcBytesPerLine - (cols * 4);
                            dstBits += diveCaps.ulScanLineBytes - (cols * 3);
                        } while (--rows);
                        break;

                    default:
                        // assumes that we initialized the diveColorMap table
                        Q_ASSERT(dstBpp == 2);
                        do {
                            PUSHORT temp = (PUSHORT)dstBits;
                            for (i = 0; i < cols; ++i) {
                               *temp++ = (USHORT)
                                    (diveColorMap[0][srcBits[0]] |
                                     diveColorMap[1][srcBits[1]] |
                                     diveColorMap[2][srcBits[2]]);
                               srcBits += 4;
                            }
                            srcBits += srcBytesPerLine - (cols * 4);
                            dstBits += diveCaps.ulScanLineBytes;
                        } while(--rows);
                        break;
                    }
                }
            }
        }

        DiveDeacquireFrameBuffer(d->hDive);
    }

    if (ptrHidden)
        WinShowPointer(HWND_DESKTOP, TRUE);
}

QT_END_NAMESPACE

