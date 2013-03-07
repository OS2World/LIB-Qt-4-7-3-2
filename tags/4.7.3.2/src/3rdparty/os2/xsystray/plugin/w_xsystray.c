
/*
 *@@sourcefile w_xsystray.c:
 *      Extended system tray widget for XCenter/eCenter.
 *
 *      Implementation.
 *
 *      Copyright (C) 2009-2011 Dmitriy Kuminov
 *
 *      This file is part of the Extended system tray widget source package.
 *      Extended system tray widget is free software; you can redistribute it
 *      and/or modify it under the terms of the GNU General Public License as
 *      published by the Free Software Foundation, in version 2 as it comes in
 *      the "COPYING" file of the Extended system tray widget distribution. This
 *      program is distributed in the hope that it will be useful, but WITHOUT
 *      ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *      FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 *      more details.
 */

#if defined(__IBMC__) || defined(__IBMCPP__)
#pragma strings(readonly)
#endif

/*
 *  Suggested #include order:
 *  1)  os2.h
 *  2)  C library headers
 *  3)  setup.h (code generation and debugging options)
 *  4)  headers in helpers\
 *  5)  at least one SOM implementation header (*.ih)
 *  6)  dlgids.h, headers in shared\ (as needed)
 *  7)  headers in implementation dirs (e.g. filesys\, as needed)
 *  8)  #pragma hdrstop and then more SOM headers which crash with precompiled headers
 */

#if defined(__GNUC__) && defined(__EMX__)
#define OS2EMX_PLAIN_CHAR
#endif

#define INCL_BASE
#define INCL_PM
#include <os2.h>

// C library headers
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// generic headers
// If this file were part of the XWorkplace sources, we'd now include
// the generic "setup.h" file, which has common set up code for every
// single XWorkplace code file. But it's not, so we won't include that.
// #include "setup.h"                      // code generation and debugging options

// headers in /helpers
// This would be the place to include headers from the "XWorkplace helpers".
// But since we do a minimal sample here, we can't include those helpful
// routines... again, see the src\widgets in the XWorkplace source code
// for how these functions can be imported from XFLDR.DLL to avoid duplicate
// code.
// #include "helpers\dosh.h"               // Control Program helper routines
// #include "helpers\gpih.h"               // GPI helper routines
// #include "helpers\prfh.h"               // INI file helper routines;
                                        // this include is required for some
                                        // of the structures in shared\center.h
// #include "helpers\winh.h"               // PM helper routines
// #include "helpers\xstring.h"            // extended string helpers

// XWorkplace implementation headers
// If this file were part of the XCenter sources, we'd now include
// "center.h" from the "include\shared" directory. Since we're not
// part of the XCenter sources here, we include that file from the
// "toolkit" directory in the binary release. That file is identical
// to "include\shared\center.h" in the XWorkplace sources.
#include "shared\center.h"              // public XCenter interfaces

//#define ENABLE_LOG_TO "c:\\xsystray_plugin.dbg"

#include "w_xsystray.h"

#if defined(__IBMC__) || defined(__IBMCPP__)
#pragma hdrstop                     // VAC++ keeps crashing otherwise
#endif

// copy paste from helpers\comctl.h
// @todo not necessary when we become part of XWorkplace

#define TTN_NEEDTEXT        1000
#define TTN_SHOW            1001
#define TTN_POP             1002

#define TTFMT_PSZ           0x01
#define TTFMT_STRINGRES     0x02

typedef struct _TOOLTIPTEXT
{
    HWND    hwndTooltip;
    HWND    hwndTool;
    ULONG   ulFormat;
    PSZ     pszText;
    HMODULE hmod;
    ULONG   idResource;
} TOOLTIPTEXT, *PTOOLTIPTEXT;

#define TTM_FIRST                   (WM_USER + 1000)
#define TTM_UPDATETIPTEXT           (TTM_FIRST + 9)
#define TTM_SHOWTOOLTIPNOW          (TTM_FIRST + 17)

/* ******************************************************************
 *
 *   Private definitions
 *
 ********************************************************************/

/*
 *@@ ICONDATA:
 *      Per-icon data.
 */

typedef struct
{
    HWND        hwnd;
                // associated window
    USHORT      usId;
                // icon ID
    HPOINTER    hIcon;
                // icon handle
    ULONG       ulMsgId;
                // message ID for notifications
    PSZ         pszToolTip;
                // icon tooltip (NULL if none)
    BOOL        bIsToolTipShowing;
                // whether the tooltip is currently shown
    BOOL        bMemoryPoolGiven;
                // TRUE if SYSTRAYDATA::pvMemoryPool is already given to
                // the process hwnd belongs to

} ICONDATA, *PICONDATA;

/*
 *@@ SYSTRAYDATA:
 *      Global system tray data.
 */

typedef struct
{
    HWND        hwndServer;
                // systtem tray server window handle
    LONG        lIconWidth;
                // system icon width in px
    LONG        lIconHeight;
                // system icon height in px
    LONG        lIconPad;
                // padding around each icon in px
    PICONDATA   pIcons;
                // array of icons currently shown in the system tray
    size_t      cIcons;
                // number of icons in the pIcons array
    size_t      cIconsMax;
                // maximum number of icons pIcons can fit
    CHAR        szToolTip[sizeof(((PSYSTRAYCTLDATA)0)->u.icon.szToolTip)];
                // "static" buffer for the tooltip (XCenter requirement)
    PVOID       pvMemoryPool;
                // memory pool for NOTIFYDATA structures

} SYSTRAYDATA, *PSYSTRAYDATA;

#define ICONARRAY_GROW  4
        // number of element the icon array is increased by when there is no
        // space for the newly added icon

#define SERVER_MEMORYPOOL_SIZE 65536
        // taking NOTIFYDATA size into account (<=32 B), this is enough for at
        // least 2048 simultaneous notification messages, which (even taking
        // slowly responsing clients into account) sounds sane since in most
        // cases the structure is freed once it reaches the target event queue
        // and before a message created as a copy of it is sent to the target
        // window procedure

#define TID_CHECKALIVE          1
        // timer that checks if windows associated with icons are still alive
#define TID_CHECKALIVE_TIMEOUT  2000 // ms
        // how often to perform alive checks

static ULONG WM_XST_CREATED = 0;
             // identity of the WM_XST_CREATED message taken from the atom table
static ULONG WM_XST_NOTIFY = 0;
             // identity of the WM_XST_NOTIFY message taken from the atom table

static ULONG QWL_USER_SERVER_DATA = 0;
             // offset to the PXCENTERWIDGET pointer in the widget data array

static
VOID WgtXSysTrayUpdateAfterIconAddRemove(PXCENTERWIDGET pWidget);

/* ******************************************************************
 *
 *   XCenter widget class definition
 *
 ********************************************************************/

/*
 *      This contains the name of the PM window class and
 *      the XCENTERWIDGETCLASS definition(s) for the widget
 *      class(es) in this DLL.
 *
 *      The address of this structure (or an array of these
 *      structures, if there were several widget classes in
 *      this plugin) is returned by the "init" export
 *      (WgtInitModule).
 */

static const XCENTERWIDGETCLASS G_WidgetClasses[] =
{
    {
        WNDCLASS_WIDGET_XSYSTRAY,   // PM window class name
        0,                          // additional flag, not used here
        INTCLASS_WIDGET_XSYSTRAY,   // internal widget class name
        HUMANSTR_WIDGET_XSYSTRAY,   // widget class name displayed to user
        WGTF_UNIQUEGLOBAL |         // widget class flags
        WGTF_TOOLTIP,
        NULL                        // no settings dialog
    }
};

/* ******************************************************************
 *
 *   Function imports from XFLDR.DLL
 *
 ********************************************************************/

/*
 *      To reduce the size of the widget DLL, it can
 *      be compiled with the VAC subsystem libraries.
 *      In addition, instead of linking frequently
 *      used helpers against the DLL again, you can
 *      import them from XFLDR.DLL, whose module handle
 *      is given to you in the INITMODULE export.
 *
 *      Note that importing functions from XFLDR.DLL
 *      is _not_ a requirement. We can't do this in
 *      this minimal sample anyway without having access
 *      to the full XWorkplace source code.
 *
 *      If you want to know how you can import the useful
 *      functions from XFLDR.DLL to use them in your widget
 *      plugin, again, see src\widgets in the XWorkplace sources.
 *      The actual imports would then be made by WgtInitModule.
 */

// @todo the function declarations should become not necessary when we move into
// XWorkplace. Let's hope the prototypes will not change until then. Let's also
// pray that XFLDRxxx.DLL has the _Optlink calling convention for exports and
// not the default __cdecl (which happens if it builds with GCC according to the
// XWPENTRY definition in the current xwphelpers sources)

#define XWPENTRY _Optlink

typedef VOID XWPENTRY GPIHDRAW3DFRAME(HPS hps,
                                      PRECTL prcl,
                                      USHORT usWidth,
                                      LONG lColorLeft,
                                      LONG lColorRight);
typedef GPIHDRAW3DFRAME *PGPIHDRAW3DFRAME;
PGPIHDRAW3DFRAME pgpihDraw3DFrame = NULL;

typedef struct _RESOLVEFUNCTION
{
    const char  *pcszFunctionName;
    PFN         *ppFuncAddress;
} RESOLVEFUNCTION, *PRESOLVEFUNCTION;
RESOLVEFUNCTION G_aImports[] =
{
    { "gpihDraw3DFrame", (PFN*)&pgpihDraw3DFrame },
};

/* ******************************************************************
 *
 *   Private widget instance data
 *
 ********************************************************************/

// None presently. The samples in src\widgets in the XWorkplace
// sources cleanly separate setup string data from other widget
// instance data to allow for easier manipulation with settings
// dialogs. We have skipped this for the minimal sample.

/* ******************************************************************
 *
 *   Widget setup management
 *
 ********************************************************************/

// None presently. See above.

/* ******************************************************************
 *
 *   Widget settings dialog
 *
 ********************************************************************/

// None currently. To see how a setup dialog can be done,
// see the "window list" widget in the XWorkplace sources
// (src\widgets\w_winlist.c).

/* ******************************************************************
 *
 *   Callbacks stored in XCENTERWIDGETCLASS
 *
 ********************************************************************/

// If you implement a settings dialog, you must write a
// "show settings dlg" function and store its function pointer
// in XCENTERWIDGETCLASS.

/* ******************************************************************
 *
 *   Helper methods
 *
 ********************************************************************/

/*
 *@@ FreeIconData:
 *      Frees all members of the ICONDATA structure and resets them to 0.
 */

static
VOID FreeIconData(PICONDATA pData)
{
    pData->hwnd = NULLHANDLE;
    pData->usId = 0;
    if (pData->hIcon != NULLHANDLE)
    {
        WinDestroyPointer(pData->hIcon);
        pData->hIcon = NULLHANDLE;

    }
    pData->ulMsgId = 0;
    if (pData->pszToolTip)
    {
        free(pData->pszToolTip);
        pData->pszToolTip = NULL;
    }
}

/*
 *@@ FindIconData:
 *      Searches for the icon with the given identity. Returns NULL if not
 *      found.
 */

static
PICONDATA FindIconData(PSYSTRAYDATA pSysTrayData,
                       HWND hwnd,       // in: associated window handle
                       USHORT usId,     // in: icon ID
                       size_t *pIdx)    // out: index of the icon in the icon array
                                        // (optional, may be NULL)
{
    size_t i;
    for (i = 0; i < pSysTrayData->cIcons; ++i)
    {
        if (pSysTrayData->pIcons[i].hwnd == hwnd &&
            pSysTrayData->pIcons[i].usId == usId)
        {
            if (pIdx)
                *pIdx = i;
            return &pSysTrayData->pIcons[i];
        }
    }

    if (pIdx)
        *pIdx = i;
    return NULL;
}

/*
 *@@ FindIconDataAtPt:
 *      Searches for the icon under the given point.
 *      Returns NULL if no icon found (e.g. the pad space).
 *
 *      Refer to WgtPaint() for system tray geometry description.
 */

static
PICONDATA FindIconDataAtPt(PXCENTERWIDGET pWidget,
                           PPOINTL pptl,    // point coordinates (relative to systray)
                           size_t *pIdx)    // out: index of the icon in the icon array
                                            // (optional, may be NULL)
{
    PSYSTRAYDATA pSysTrayData = (PSYSTRAYDATA)pWidget->pUser;

    SWP     swp;
    RECTL   rcl;
    BOOL    bLeftToRight;
    LONG    y, lIconStep;
    size_t  i;

    // start with invalid index index
    if (pIdx)
        *pIdx = pSysTrayData->cIcons;

    WinQueryWindowPos(pWidget->hwndWidget, &swp);
    WinQueryWindowRect(pWidget->pGlobals->hwndClient, &rcl);

    y = (swp.cy - pSysTrayData->lIconHeight) / 2;
    if (pptl->y < y || pptl->y >= y + pSysTrayData->lIconHeight)
        return NULL; // hit pad space

    // detect the direction
    bLeftToRight = swp.x + swp.cx / 2 < (rcl.xRight / 2);

    lIconStep = pSysTrayData->lIconWidth + pSysTrayData->lIconPad;

    // which icon is that?
    if (bLeftToRight)
    {
        i = pptl->x / lIconStep;
        if (pptl->x % lIconStep < pSysTrayData->lIconPad)
            return NULL; // hit pad space
    }
    else
    {
        i = (swp.cx - pptl->x - 1) / lIconStep;
        if ((swp.cx - pptl->x - 1) % lIconStep < pSysTrayData->lIconPad)
            return NULL; // hit pad space
    }
    if (i >= pSysTrayData->cIcons)
        return NULL; // hit pad space

    if (pIdx)
        *pIdx = i;
    return &pSysTrayData->pIcons[i];
}

static
VOID FreeSysTrayData(PSYSTRAYDATA pSysTrayData)
{
    // destroy the server
    if (pSysTrayData->hwndServer != NULLHANDLE)
    {
        WinDestroyWindow(pSysTrayData->hwndServer);
        pSysTrayData->hwndServer = NULLHANDLE;
    }

    // free all system tray data
    if (pSysTrayData->pvMemoryPool)
    {
        DosFreeMem(pSysTrayData->pvMemoryPool);
    }
    if (pSysTrayData->pIcons)
    {
        size_t i;
        for (i = 0; i < pSysTrayData->cIcons; ++i)
            FreeIconData(&pSysTrayData->pIcons[i]);
        pSysTrayData->cIcons = 0;
        free(pSysTrayData->pIcons);
        pSysTrayData->pIcons = NULL;
    }

    free(pSysTrayData);
}

/*
 *@@ AllocNotifyDataPtr:
 *      Allocates a SYSTRAYCTLDATA struct in the pool of shared memory on belalf
 *      of the client window identified by pIconData->hwnd.
 *
 *      If there is no free space in the pool, it returns NULL. On success,
 *      fills in msg and mp1 fields of the returned structure using the
 *      information provided in pIconData.
 *
 *      Note that the allocated structure is supposed to be freed using
 *      FreeNotifyDataPtr by the client-side API implementation in another
 *      process.
 */
static
PNOTIFYDATA AllocNotifyDataPtr(PVOID pvMemoryPool,  // in: memory pool base address
                               PICONDATA pIconData) // in: icon data
{
    // NOTE: we cannot use DosSubAllocMem() and friends since we want to be able
    // to free blocks allocated to clients which death we detect but we cannot
    // be sure about the layout of memory DosSub API uses. Therefore, we provide
    // our own sub-allocation scheme which is rather simple because we need to
    // allocate equally sized blocks (each is NOTIFYDATA struct). The memory
    // pool is laid out as follows: MEMPOOLHDR followed by a number of
    // MEMPOOLBLK.

    APIRET arc;
    PID pid;
    TID tid;
    PMEMPOOLHDR pHdr;
    ULONG ulMax, ulNeedsCommit, ulNext, ulCurr;
    PNOTIFYDATA pData;

    LOGF(("pvMemoryPool  %p\n", pvMemoryPool));
    LOGF(("hwnd          %lx\n", pIconData->hwnd));

    if (!pIconData->bMemoryPoolGiven)
    {
        LOGF(("Giving memory pool to %lx\n", pIconData->hwnd));

        arc = ERROR_INVALID_HANDLE;
        if (WinQueryWindowProcess(pIconData->hwnd, &pid, &tid))
            arc = DosGiveSharedMem(pvMemoryPool, pid, PAG_READ | PAG_WRITE);
        if (arc != NO_ERROR)
            return NULL;

        pIconData->bMemoryPoolGiven = TRUE;
    }

    pHdr = (PMEMPOOLHDR)pvMemoryPool;

    // maximum address that is still enough for a block
    ulMax = pHdr->ulBeyond - sizeof(MEMPOOLBLK);

    ulNeedsCommit = pHdr->ulNeedsCommit;
    ulNext = pHdr->ulNext;
    ulCurr = ulNext;

    LOGF(("ulNeedsCommit %p\n", ulNeedsCommit));
    LOGF(("ulNext        %p\n", ulNext));

    do
    {
        if (ulCurr >= ulNeedsCommit)
        {
            // commit more memory; it's OK two or more threads will do the same
            DosSetMem((PVOID)ulNeedsCommit, 4096,
                      PAG_COMMIT | PAG_READ | PAG_WRITE);
            // advance the address (only if nobody has already done so -- they
            // could already commit more than we did)
            __atomic_cmpxchg32((volatile uint32_t *)&pHdr->ulNeedsCommit,
                               ulNeedsCommit + 4096, ulNeedsCommit);
        }

        if (__atomic_cmpxchg32((volatile uint32_t *)ulCurr,
                               pIconData->hwnd, NULLHANDLE))
            break;

        ulCurr += sizeof(MEMPOOLBLK);
        if (ulCurr > ulMax)
            // start over
            ulCurr = ((ULONG)pvMemoryPool) + sizeof(MEMPOOLHDR);

        if (ulCurr == ulNext)
            return NULL; // no free blocks!
    }
    while (1);

    LOGF(("ulCurr        %p\n", ulCurr));

    // memorize a pointer to the new struct
    pData = &((PMEMPOOLBLK)ulCurr)->NotifyData;

    // advance to the next possibly free block
    ulCurr += sizeof(MEMPOOLBLK);
    if (ulCurr > ulMax)
        // start over
        ulCurr = ((ULONG)pvMemoryPool) + sizeof(MEMPOOLHDR);

    // store the new next address until someone else has already done that
    __atomic_cmpxchg32((volatile uint32_t *)&pHdr->ulNext, ulCurr, ulNext);

    // fill in parts of the allocated NOTIFYDATA
    memset(pData, 0, sizeof(*pData));

    pData->msg = pIconData->ulMsgId;
    pData->mp1 = MPFROMSHORT(pIconData->usId);

    return pData;
}

/*
 *@@ PostNotifyMsg:
 *      Posts WM_XST_NOTIFY to the given client window. Frees pNotifyData if
 *      posting fails.
 */

VOID PostNotifyMsg(PSYSTRAYDATA pSysTrayData, HWND hwnd,
                   PNOTIFYDATA pNotifyData)
{
    LOGF(("hwnd          %lx\n", hwnd));
    LOGF(("pNotifyData   %p\n", pNotifyData));

    if (!WinPostMsg(hwnd, WM_XST_NOTIFY, pNotifyData, pSysTrayData->pvMemoryPool))
    {
        LOGF(("WinPostMsg() failed, last error %lx\n", WinGetLastError(0)));
        FreeNotifyDataPtr(pSysTrayData->pvMemoryPool, hwnd, pNotifyData);
    }
}

/*
 *@@ DrawPointer:
 *      Draws a pointer in a presentation space.
 */

static
BOOL DrawPointer(HPS hps, LONG lx, LONG ly, HPOINTER hptrPointer, BOOL bMini)
{
    return WinDrawPointer(hps, lx, ly, hptrPointer, bMini ? DP_MINI : DP_NORMAL);
    // @todo: for icons with real alpha, do manual alpha blending
}

/* ******************************************************************
 *
 *   PM window class implementation
 *
 ********************************************************************/

/*
 *      This code has the actual PM window class.
 *
 */

/*
 *@@ MwgtControl:
 *      implementation for WM_CONTROL in fnwpXSysTray.
 *
 *      The XCenter communicates with widgets thru
 *      WM_CONTROL messages. At the very least, the
 *      widget should respond to XN_QUERYSIZE because
 *      otherwise it will be given some dumb default
 *      size.
 */

static
BOOL WgtControl(PXCENTERWIDGET pWidget,
                MPARAM mp1,
                MPARAM mp2)
{
    PSYSTRAYDATA pSysTrayData = (PSYSTRAYDATA)pWidget->pUser;
    BOOL brc = FALSE;

    USHORT  usID = SHORT1FROMMP(mp1),
            usNotifyCode = SHORT2FROMMP(mp1);

    // is this from the XCenter client?
    if (usID == ID_XCENTER_CLIENT)
    {
        // yes:

        switch (usNotifyCode)
        {
            /*
             * XN_QUERYSIZE:
             *      XCenter wants to know our size.
             */

            case XN_QUERYSIZE:
            {
                PSIZEL pszl = (PSIZEL)mp2;
                LONG pad = pSysTrayData->lIconPad;
                size_t cnt = pSysTrayData->cIcons;
                // desired width
                if (cnt)
                    pszl->cx = pad + (pSysTrayData->lIconWidth + pad) * cnt;
                else
                    pszl->cx = pad;
                // desired minimum height
                pszl->cy = pSysTrayData->lIconHeight + pad * 2;
                brc = TRUE;
            }
            break;

        }
    }
    else if (usID == ID_XCENTER_TOOLTIP)
    {
        PICONDATA pIconData;
        POINTL ptl;

        WinQueryMsgPos(pWidget->habWidget, &ptl);
        // make the coordinates systray-relative
        WinMapWindowPoints(HWND_DESKTOP, pWidget->hwndWidget, &ptl, 1);

        pIconData = FindIconDataAtPt(pWidget, &ptl, NULL);

        switch (usNotifyCode)
        {
            case TTN_NEEDTEXT:
            {
                LOGF(("TTN_NEEDTEXT\n"));

                PTOOLTIPTEXT pttt = (PTOOLTIPTEXT)mp2;
                pttt->ulFormat = TTFMT_PSZ;

                if (!pIconData || !pIconData->pszToolTip)
                {
                    pttt->pszText = pSysTrayData->cIcons ?
                        "Indicator area" : "Indicator area (empty)";
                }
                else
                {
                    strncpy(pSysTrayData->szToolTip, pIconData->pszToolTip,
                            sizeof(pSysTrayData->szToolTip) - 1);
                    // be on the safe side
                    pSysTrayData->szToolTip[sizeof(pSysTrayData->szToolTip) - 1] = '\0';

                    pttt->pszText = pSysTrayData->szToolTip;
                }

                LOGF((" pszText '%s'\n", pttt->pszText));
            }
            break;

            case TTN_SHOW:
                if (pIconData)
                    pIconData->bIsToolTipShowing = TRUE;
            break;

            case TTN_POP:
                if (pIconData)
                    pIconData->bIsToolTipShowing = FALSE;
            break;
        }
    }

    return brc;
}

/*
 *@@ WgtPaint:
 *      implementation for WM_PAINT in fnwpXSysTray.
 *
 *      Draws all the icons. If the widget's center is located to the left from
 *      the XCenter's center, icons go left to right. Otherwise, they go right
 *      to left.
 *
 *      NOTE: This function must be keept in sync with FindIconDataAtPt() and
 *      SYSTRAYCMD_QUERYRECT in terms of system tray geometry.
 */
/*
        +---------------------------+  p = lIconPad
        |     p                     |  w = lIconWidth
        |   +-------+   +-------+   |  h = lIconHeight
        | p |   w   | p |   w   | p |
        |   |      h|   |      h|   |
        |   |       |   |       |   |  If "Frame around statics" is on in XCenter
        |   +-------+   +-------+   |  properties, then a 1 px 3D frame is drawn
        |     p                     |  within the pad area. So, lIconPad must
        +---------------------------+  be at least 2 px.
 */

static
VOID WgtPaint(HWND hwnd,
              PXCENTERWIDGET pWidget)
{
    PSYSTRAYDATA pSysTrayData = (PSYSTRAYDATA)pWidget->pUser;
    HPS hps;
    RECTL rclPaint;

    if ((hps = WinBeginPaint(hwnd, NULLHANDLE, &rclPaint)))
    {
        SWP     swp;
        RECTL   rcl;
        BOOL    bLeftToRight;
        LONG    x, y, lIconStep;
        size_t  i;

        WinQueryWindowPos(hwnd, &swp);
        WinQueryWindowRect(pWidget->pGlobals->hwndClient, &rcl);

        // correct the paint area
        // @todo find out why it exceeds the window bounds
        if (rclPaint.xLeft < 0)
            rclPaint.xLeft = 0;
        if (rclPaint.xRight > swp.cx)
            rclPaint.xRight = swp.cx;
        if (rclPaint.yBottom < 0)
            rclPaint.yBottom = 0;
        if (rclPaint.yTop > swp.cy)
            rclPaint.yTop = swp.cy;

        LOGF(("rclPaint %d,%d-%d,%d\n",
              rclPaint.xLeft, rclPaint.yBottom, rclPaint.xRight, rclPaint.yTop));

        // switch HPS to RGB mode
        GpiCreateLogColorTable(hps, 0, LCOLF_RGB, 0, 0, NULL);

        // draw icons left to right if our center is closer to the left edge
        // of XCenter and right to left otherwise
        bLeftToRight = swp.x + swp.cx / 2 < (rcl.xRight / 2);

        WinFillRect(hps, &rclPaint,
                    WinQuerySysColor(HWND_DESKTOP, SYSCLR_DIALOGBACKGROUND, 0));

        if ((pWidget->pGlobals->flDisplayStyle & XCS_SUNKBORDERS))
        {
            rcl.xLeft = 0;
            rcl.yBottom = 0;
            rcl.xRight = swp.cx - 1;
            rcl.yTop = swp.cy - 1;
            pgpihDraw3DFrame(hps, &rcl, 1,
                             pWidget->pGlobals->lcol3DDark,
                             pWidget->pGlobals->lcol3DLight);
        }

        // always center the icon vertically (we may be given more height than
        // we requested)
        y = (swp.cy - pSysTrayData->lIconHeight) / 2;

        if (bLeftToRight)
            x = pSysTrayData->lIconPad;
        else
            x = swp.cx - pSysTrayData->lIconPad - pSysTrayData->lIconWidth;

        lIconStep = pSysTrayData->lIconWidth + pSysTrayData->lIconPad;

        // where to start from?
        if (bLeftToRight)
        {
            i = rclPaint.xLeft / lIconStep;
            x = pSysTrayData->lIconPad + i * lIconStep;
        }
        else
        {
            i = (swp.cx - rclPaint.xRight) / lIconStep;
            x = swp.cx - (i + 1) * lIconStep;
            // negate the step, for convenience
            lIconStep = -lIconStep;
        }

        // draw as many icons as we can / need
        for (; i < pSysTrayData->cIcons; ++i)
        {
            if (x >= rclPaint.xRight)
                break;

            // just leave an empty box if the icon is NULL, this is what
            // Windows and Linux tray widgets do
            if (pSysTrayData->pIcons[i].hIcon != NULLHANDLE)
                DrawPointer(hps, x, y, pSysTrayData->pIcons[i].hIcon, DP_MINI);
            x += lIconStep;
        }

        WinEndPaint(hps);
    }
}

/*
 *@@ WgtMouse:
 *      implementation for WM_BUTTONxyyy in fnwpXSysTray.
 *
 *      Posts a notification to the window associated with the icon and returns
 *      TRUE if this mouse message is within the icon bounds. Otherwise returns
 *      FALSE.
 *
 *      Refer to WgtPaint for more details about the widget geometry.
 */

static
BOOL WgtMouse(HWND hwnd, ULONG msg, MRESULT mp1, MRESULT mp2,
              PXCENTERWIDGET pWidget)
{
    PSYSTRAYDATA pSysTrayData = (PSYSTRAYDATA)pWidget->pUser;

    POINTL  ptl;

    PICONDATA   pIconData;
    PNOTIFYDATA pNotifyData;

    ptl.x = ((PPOINTS)&mp1)->x;
    ptl.y = ((PPOINTS)&mp1)->y;

    LOGF(("msg %x ptl %ld,%ld\n", msg, ptl.x, ptl.y));

    pIconData = FindIconDataAtPt(pWidget, &ptl, NULL);
    if (!pIconData)
        return FALSE; // hit pad space

    LOGF(("hwnd  %x\n", pIconData->hwnd));
    LOGF(("usId  %d\n", pIconData->usId));
    LOGF(("hIcon %x\n", pIconData->hIcon));

    // make the coordinates global
    WinMapWindowPoints(hwnd, HWND_DESKTOP, &ptl, 1);

    // allocate a NOTIFYDATA struct
    pNotifyData = AllocNotifyDataPtr(pSysTrayData->pvMemoryPool, pIconData);
    if (!pNotifyData)
        return FALSE;

    switch (msg)
    {
        case WM_HSCROLL:
        case WM_VSCROLL:
            pNotifyData->mp1 += XST_IN_WHEEL << 16;
            pNotifyData->u.WheelMsg.ulWheelMsg = msg;
            pNotifyData->u.WheelMsg.ptsPointerPos.x = ptl.x;
            pNotifyData->u.WheelMsg.ptsPointerPos.y = ptl.y;
            pNotifyData->u.WheelMsg.usCmd = SHORT2FROMMP(mp2);
            pNotifyData->mp2 = &pNotifyData->u.WheelMsg;
        break;

        case WM_CONTEXTMENU:
            pNotifyData->mp1 += XST_IN_CONTEXT << 16;
            pNotifyData->u.ContextMsg.ptsPointerPos.x = ptl.x;
            pNotifyData->u.ContextMsg.ptsPointerPos.y = ptl.y;
            pNotifyData->u.ContextMsg.fPointer = TRUE;
            pNotifyData->mp2 = &pNotifyData->u.ContextMsg;
        break;

        default:
            pNotifyData->mp1 += XST_IN_MOUSE << 16;
            pNotifyData->u.MouseMsg.ulMouseMsg = msg;
            pNotifyData->u.MouseMsg.ptsPointerPos.x = ptl.x;
            pNotifyData->u.MouseMsg.ptsPointerPos.y = ptl.y;
            pNotifyData->u.MouseMsg.fsHitTestRes = SHORT1FROMMP(mp2);
            pNotifyData->u.MouseMsg.fsFlags = SHORT2FROMMP(mp2);
            pNotifyData->mp2 = &pNotifyData->u.MouseMsg;
        break;
    }

    PostNotifyMsg(pSysTrayData, pIconData->hwnd, pNotifyData);

    return TRUE;
}

/*
 *@@ fnwpXSysTray:
 *      window procedure for the Extended system tray widget class.
 *
 *      There are a few rules which widget window procs
 *      must follow. See XCENTERWIDGETCLASS in center.h
 *      for details.
 *
 *      Other than that, this is a regular window procedure
 *      which follows the basic rules for a PM window class.
 */

static
MRESULT EXPENTRY fnwpXSysTray(HWND hwnd, ULONG msg, MPARAM mp1, MPARAM mp2)
{
    // get widget data from QWL_USER (stored there by WM_CREATE)
    PXCENTERWIDGET pWidget = (PXCENTERWIDGET)WinQueryWindowPtr(hwnd, QWL_USER);
                    // this ptr is valid after WM_CREATE

    switch (msg)
    {
        /*
         * WM_CREATE:
         *      as with all widgets, we receive a pointer to the
         *      XCENTERWIDGET in mp1, which was created for us.
         *
         *      The first thing the widget MUST do on WM_CREATE
         *      is to store the XCENTERWIDGET pointer (from mp1)
         *      in the QWL_USER window word by calling:
         *
         *          WinSetWindowPtr(hwnd, QWL_USER, mp1);
         *
         *      We could use XCENTERWIDGET.pUser for allocating
         *      another private memory block for our own stuff,
         *      for example to be able to store fonts and colors.
         *      We ain't doing this in the minimal sample.
         */

        case WM_CREATE:
        {
            LOGF(("WM_CREATE\n"));

            PSYSTRAYDATA pSysTrayData = NULL;
            APIRET arc;

            WinSetWindowPtr(hwnd, QWL_USER, mp1);
            if (    (!(pWidget = (PXCENTERWIDGET)mp1))
                 || (!pWidget->pfnwpDefWidgetProc)
               )
                // shouldn't happen... stop window creation!!
                return (MRESULT)TRUE;

            pSysTrayData = malloc(sizeof(*pSysTrayData));
            if (pSysTrayData == NULL)
                return (MRESULT)TRUE;

            // initialize the SYSTRAYDATA structure
            memset(pSysTrayData, 0, sizeof(*pSysTrayData));
            pSysTrayData->lIconWidth = WinQuerySysValue(HWND_DESKTOP, SV_CXICON) / 2;
            pSysTrayData->lIconHeight = WinQuerySysValue(HWND_DESKTOP, SV_CYICON) / 2;
            pSysTrayData->lIconPad = pSysTrayData->lIconHeight / 8;
            pSysTrayData->cIconsMax = ICONARRAY_GROW;
            pSysTrayData->pIcons = malloc(sizeof(*pSysTrayData->pIcons) *
                                          pSysTrayData->cIconsMax);
            if (pSysTrayData->pIcons == NULL)
            {
                FreeSysTrayData(pSysTrayData);
                return (MRESULT)TRUE;
            }
            pSysTrayData->cIcons = 0;

            // Allocate the memory pool for NOTIFYDATA structs (we don't
            // PAG_COMMIT all memory, AllocNotifyDataPtr() will do so as needed)
            arc = DosAllocSharedMem((PVOID)&pSysTrayData->pvMemoryPool, NULL,
                                    SERVER_MEMORYPOOL_SIZE,
                                    PAG_READ | PAG_WRITE | OBJ_GIVEABLE);
            if (arc == NO_ERROR)
            {
                PMEMPOOLHDR pHdr = (PMEMPOOLHDR)pSysTrayData->pvMemoryPool;
                arc = DosSetMem(pSysTrayData->pvMemoryPool, 4096,
                                PAG_COMMIT | PAG_READ | PAG_WRITE);
                if (arc == NO_ERROR)
                {
                    pHdr->ulBeyond = (ULONG)pSysTrayData->pvMemoryPool +
                                     SERVER_MEMORYPOOL_SIZE;
                    pHdr->ulNeedsCommit = (ULONG)pSysTrayData->pvMemoryPool +
                                           4096;
                    pHdr->ulNext = (ULONG)pSysTrayData->pvMemoryPool +
                                   sizeof(MEMPOOLHDR);
                }
            }
            if (arc != NO_ERROR)
            {
                FreeSysTrayData(pSysTrayData);
                return (MRESULT)TRUE;
            }

            // create the "server" window (note that we pass the XCENTERWIDGET
            // pointer on to it)
            pSysTrayData->hwndServer =
                WinCreateWindow(HWND_DESKTOP, WNDCLASS_WIDGET_XSYSTRAY_SERVER,
                                NULL, WS_MINIMIZED,
                                0, 0, 0, 0,
                                HWND_DESKTOP, HWND_BOTTOM,
                                0, mp1, NULL);
            if (pSysTrayData->hwndServer == NULLHANDLE)
            {
                FreeSysTrayData(pSysTrayData);
                return (MRESULT)TRUE;
            }

            pWidget->pUser = pSysTrayData;

            // inform all interested parties that we are fired up
            WinBroadcastMsg(HWND_DESKTOP, WM_XST_CREATED,
                            NULL, NULL, BMSG_POST);

            return FALSE; // confirm success
        }
        break;

        /*
         * WM_DESTROY:
         *      clean up. This _must_ be passed on to
         *      ctrDefWidgetProc.
         */

        case WM_DESTROY:
        {
            LOGF(("WM_DESTROY\n"));

            PSYSTRAYDATA pSysTrayData = (PSYSTRAYDATA)pWidget->pUser;

            // stop the check alive timer
            WinStopTimer(pWidget->habWidget, pSysTrayData->hwndServer,
                         TID_CHECKALIVE);

            FreeSysTrayData(pSysTrayData);
            pWidget->pUser = NULL;

            // We _MUST_ pass this on, or the default widget proc
            // cannot clean up, so break
        }
        break;

        /*
         * WM_CONTROL:
         *      process notifications/queries from the XCenter.
         */

        case WM_CONTROL:
            return (MPARAM)WgtControl(pWidget, mp1, mp2);
        break;

        /*
         * WM_PAINT:
         *      well, paint the widget.
         */

        case WM_PAINT:
            WgtPaint(hwnd, pWidget);
            return (MRESULT)TRUE;
        break;

        /*
         * WM_PRESPARAMCHANGED:
         *      A well-behaved widget would intercept
         *      this and store fonts and colors.
         */

        /* case WM_PRESPARAMCHANGED:
        break; */

        /*
         * All mouse click and wheel events:
         *      Note that we hide WM_CONTEXTMENU from XCenter when it is within
         *      the icon bounds as it's a responsibility of the application
         *      owning the icon to show it.
         */

        case WM_BUTTON1UP:
        case WM_BUTTON1DOWN:
        case WM_BUTTON1CLICK:
        case WM_BUTTON1DBLCLK:
        case WM_BUTTON2UP:
        case WM_BUTTON2DOWN:
        case WM_BUTTON2CLICK:
        case WM_BUTTON2DBLCLK:
        case WM_BUTTON3UP:
        case WM_BUTTON3DOWN:
        case WM_BUTTON3CLICK:
        case WM_BUTTON3DBLCLK:
        case WM_CONTEXTMENU:
        case WM_VSCROLL:
        case WM_HSCROLL:
        {
            if (WgtMouse(hwnd, msg, mp1, mp2, pWidget))
                return (MRESULT)TRUE;
            // we didn't hit the icon, pass it on to XCenter
        }
        break;

        default:
            break;

    } // end switch(msg)

    return pWidget->pfnwpDefWidgetProc(hwnd, msg, mp1, mp2);
}

/*
 *@@ WgtXSysTrayUpdateAfterIconAddRemove:
 *      Name says it all.
 */

static
VOID WgtXSysTrayUpdateAfterIconAddRemove(PXCENTERWIDGET pWidget)
{
    PSYSTRAYDATA pSysTrayData = (PSYSTRAYDATA)pWidget->pUser;

    if (pSysTrayData->cIcons == 1)
    {
        // start a timer to perform "is window alive" checks
        WinStartTimer(pWidget->habWidget, pSysTrayData->hwndServer,
                      TID_CHECKALIVE,
                      TID_CHECKALIVE_TIMEOUT);
    }
    else
    if (pSysTrayData->cIcons == 0)
    {
        // stop the check alive timer
        WinStopTimer(pWidget->habWidget, pSysTrayData->hwndServer,
                     TID_CHECKALIVE);
    }

    // ask XCenter to take our new size into account (this will also
    // invalidate us)
    WinPostMsg(pWidget->pGlobals->hwndClient,
               XCM_REFORMAT,
               (MPARAM)XFMF_GETWIDGETSIZES,
               0);
}

/*
 *@@ WgtXSysTrayControl:
 *      implementation for WM_XST_CONTROL in fnwpXSysTrayServer.
 *
 *      Serves as an entry point for all client-side API requests to the
 *      Extended system tray.
 *
 *      Note that this message is being sent from another process which is
 *      awaiting for an answer, so it must return as far as possible (by
 *      rescheduling any potentially long term operation for another cycle).
 */

static
ULONG WgtXSysTrayControl(HWND hwnd, PXCENTERWIDGET pWidget,
                         PSYSTRAYCTLDATA pCtlData)
{
    BOOL    brc = FALSE;
    ULONG   xrc = XST_FAIL;

    PSYSTRAYDATA pSysTrayData = (PSYSTRAYDATA)pWidget->pUser;

    switch (pCtlData->ulCommand)
    {
        case SYSTRAYCMD_GETVERSION:
        {
            LOGF(("SYSTRAYCMD_GETVERSION\n"));

            pCtlData->bAcknowledged = TRUE;
            pCtlData->u.version.ulMajor = XSYSTRAY_VERSION_MAJOR;
            pCtlData->u.version.ulMinor = XSYSTRAY_VERSION_MINOR;
            pCtlData->u.version.ulRevision = XSYSTRAY_VERSION_REVISION;
            xrc = XST_OK;
        }
        break;

        case SYSTRAYCMD_ADDICON:
        {
            POINTERINFO Info;
            HPOINTER hIcon = NULLHANDLE;
            size_t i;
            PICONDATA pData;

            LOGF(("SYSTRAYCMD_ADDICON\n"));
            LOGF((" hwnd       %x\n", pCtlData->hwndSender));
            LOGF((" usId       %d\n", pCtlData->u.icon.usId));
            LOGF((" hIcon      %x\n", pCtlData->u.icon.hIcon));
            LOGF((" szToolTip  '%s'\n", pCtlData->u.icon.szToolTip));

            pCtlData->bAcknowledged = TRUE;

            // make a private copy of the provided icon (it will get lost after
            // we return from this message)
            if (pCtlData->u.icon.hIcon != NULLHANDLE)
            {
                brc = WinQueryPointerInfo(pCtlData->u.icon.hIcon, &Info);
                if (!brc)
                    break;
                hIcon = WinCreatePointerIndirect(HWND_DESKTOP, &Info);
                if (hIcon == NULLHANDLE)
                    break;
            }

            pData = FindIconData(pSysTrayData, pCtlData->hwndSender,
                                 pCtlData->u.icon.usId, &i);
            if (pData)
            {
                LOGF((" Replacing with hIcon %x\n", hIcon));

                // try update the tooltip first
                free(pData->pszToolTip);
                pData->pszToolTip = NULL;
                if (pCtlData->u.icon.szToolTip[0] != '\0')
                {
                    pData->pszToolTip = strdup(pCtlData->u.icon.szToolTip);
                    if (!pData->pszToolTip)
                    {
                        if (hIcon != NULLHANDLE)
                            WinDestroyPointer(hIcon);
                        break;
                    }
                }

                if (pData->bIsToolTipShowing)
                {
                    if (pData->pszToolTip)
                        // update the tooltip on screen
                        WinSendMsg(pWidget->pGlobals->hwndTooltip,
                                   TTM_UPDATETIPTEXT,
                                   (MPARAM)pData->pszToolTip, 0);
                    else
                        // hide the tooltip
                        WinSendMsg(pWidget->pGlobals->hwndTooltip,
                                   TTM_SHOWTOOLTIPNOW,
                                   (MPARAM)FALSE, 0);
                }

                // now update the icon
                if (pData->hIcon != NULLHANDLE)
                    WinDestroyPointer(pData->hIcon);
                pData->hIcon = hIcon;
                pData->ulMsgId = pCtlData->u.icon.ulMsgId;

                // we didn't change the number of icons so simply invalidate
                WinInvalidateRect(pWidget->hwndWidget, NULL, FALSE);

                xrc = XST_REPLACED;
            }
            else
            {
                LOGF((" Adding new hIcon %x\n", hIcon));

                if (pSysTrayData->cIcons == pSysTrayData->cIconsMax)
                {
                    PICONDATA pNewIcons;

                    LOGF((" Allocating more memory (new icon count is %u)!\n",
                          pSysTrayData->cIcons + 1));

                    pNewIcons = realloc(pSysTrayData->pIcons,
                                        sizeof(*pSysTrayData->pIcons) *
                                        pSysTrayData->cIconsMax + ICONARRAY_GROW);
                    if (pNewIcons == NULL)
                    {
                        if (hIcon != NULLHANDLE)
                            WinDestroyPointer(hIcon);
                        break;
                    }

                    pSysTrayData->pIcons = pNewIcons;
                    pSysTrayData->cIconsMax += ICONARRAY_GROW;
                }

                i = pSysTrayData->cIcons;

                pData = &pSysTrayData->pIcons[i];
                memset(pData, 0, sizeof(*pData));

                pData->hwnd = pCtlData->hwndSender;
                pData->usId = pCtlData->u.icon.usId;
                pData->hIcon = hIcon;
                pData->ulMsgId = pCtlData->u.icon.ulMsgId;

                if (pCtlData->u.icon.szToolTip[0] != '\0')
                {
                    pData->pszToolTip = strdup(pCtlData->u.icon.szToolTip);
                    if (!pData->pszToolTip)
                    {
                        if (hIcon != NULLHANDLE)
                            WinDestroyPointer(hIcon);
                        break;
                    }
                }

                ++pSysTrayData->cIcons;

                WgtXSysTrayUpdateAfterIconAddRemove(pWidget);

                xrc = XST_OK;
            }
        }
        break;

        case SYSTRAYCMD_REPLACEICON:
        {
            POINTERINFO Info;
            HPOINTER hIcon = NULLHANDLE;
            size_t i;
            PICONDATA pData;

            LOGF(("SYSTRAYCMD_REPLACEICON\n"));
            LOGF((" hwnd  %x\n", pCtlData->hwndSender));
            LOGF((" usId  %d\n", pCtlData->u.icon.usId));

            pCtlData->bAcknowledged = TRUE;

            // make a private copy of the provided icon (it will get lost after
            // we return from this message)
            if (pCtlData->u.icon.hIcon != NULLHANDLE)
            {
                brc = WinQueryPointerInfo(pCtlData->u.icon.hIcon, &Info);
                if (!brc)
                    break;
                hIcon = WinCreatePointerIndirect(HWND_DESKTOP, &Info);
                if (hIcon == NULLHANDLE)
                    break;
            }

            pData = FindIconData(pSysTrayData, pCtlData->hwndSender,
                                 pCtlData->u.icon.usId, &i);
            if (pData)
            {
                LOGF((" Replacing with hIcon %x\n", hIcon));

                if (pData->hIcon != NULLHANDLE)
                    WinDestroyPointer(pData->hIcon);
                pData->hIcon = hIcon;
                pData->ulMsgId = pCtlData->u.icon.ulMsgId;

                // we didn't change the number of icons so simply invalidate
                WinInvalidateRect(pWidget->hwndWidget, NULL, FALSE);

                xrc = XST_OK;
            }
            else
                LOGF((" Icon not found!\n"));
        }
        break;

        case SYSTRAYCMD_REMOVEICON:
        {
            size_t i;
            PICONDATA pData;

            LOGF(("SYSTRAYCMD_REMOVEICON\n"));
            LOGF((" hwnd  %x\n", pCtlData->hwndSender));
            LOGF((" usId  %d\n", pCtlData->u.icon.usId));

            pCtlData->bAcknowledged = TRUE;

            pData = FindIconData(pSysTrayData, pCtlData->hwndSender,
                                 pCtlData->u.icon.usId, &i);
            if (pData)
            {
                LOGF((" Removing hIcon %x\n", pData->hIcon));

                FreeIconData(pData);

                --pSysTrayData->cIcons;
                if (pSysTrayData->cIcons > 0)
                {
                    memcpy(&pSysTrayData->pIcons[i],
                           &pSysTrayData->pIcons[i + 1],
                           sizeof(*pSysTrayData->pIcons) * (pSysTrayData->cIcons - i));
                }

                WgtXSysTrayUpdateAfterIconAddRemove(pWidget);

                xrc = XST_OK;
            }
            else
                LOGF((" Icon not found!\n"));
        }
        break;

        case SYSTRAYCMD_SETTOOLTIP:
        {
            size_t i;
            PICONDATA pData;

            LOGF(("SYSTRAYCMD_SETTOOLTIP\n"));
            LOGF((" hwnd  %x\n", pCtlData->hwndSender));
            LOGF((" usId  %d\n", pCtlData->u.icon.usId));

            pCtlData->bAcknowledged = TRUE;

            pData = FindIconData(pSysTrayData, pCtlData->hwndSender,
                                 pCtlData->u.icon.usId, &i);
            if (pData)
            {
                LOGF((" Replacing with szToolTip '%s'\n",
                      pCtlData->u.icon.szToolTip));

                free(pData->pszToolTip);
                pData->pszToolTip = NULL;
                if (pCtlData->u.icon.szToolTip[0] != '\0')
                {
                    pData->pszToolTip = strdup(pCtlData->u.icon.szToolTip);
                    if (!pData->pszToolTip)
                        break;
                }

                if (pData->bIsToolTipShowing)
                {
                    if (pData->pszToolTip)
                        // update the tooltip on screen
                        WinSendMsg(pWidget->pGlobals->hwndTooltip,
                                   TTM_UPDATETIPTEXT,
                                   (MPARAM)pData->pszToolTip, 0);
                    else
                        // hide the tooltip
                        WinSendMsg(pWidget->pGlobals->hwndTooltip,
                                   TTM_SHOWTOOLTIPNOW,
                                   (MPARAM)FALSE, 0);
                }

                xrc = XST_OK;
            }
            else
                LOGF((" Icon not found!\n"));
        }
        break;

        case SYSTRAYCMD_QUERYRECT:
        {
            size_t i;
            PICONDATA pData;

            LOGF(("SYSTRAYCMD_QUERYRECT\n"));
            LOGF((" hwnd  %x\n", pCtlData->hwndSender));
            LOGF((" usId  %d\n", pCtlData->u.icon.usId));

            pCtlData->bAcknowledged = TRUE;

            pData = FindIconData(pSysTrayData, pCtlData->hwndSender,
                                 pCtlData->u.icon.usId, &i);
            if (pData)
            {
                // Refer to FindIconDataAtPt() for details

                SWP     swp;
                RECTL   rcl;
                BOOL    bLeftToRight;
                LONG    y, lIconStep;

                WinQueryWindowPos(pWidget->hwndWidget, &swp);
                WinQueryWindowRect(pWidget->pGlobals->hwndClient, &rcl);

                y = (swp.cy - pSysTrayData->lIconHeight) / 2;

                // detect the direction
                bLeftToRight = swp.x + swp.cx / 2 < (rcl.xRight / 2);

                lIconStep = pSysTrayData->lIconWidth + pSysTrayData->lIconPad;

                pCtlData->u.rect.rclIcon.yBottom = y;
                pCtlData->u.rect.rclIcon.yTop = y + pSysTrayData->lIconHeight;

                if (bLeftToRight)
                {
                    pCtlData->u.rect.rclIcon.xLeft =
                        pSysTrayData->lIconPad + (lIconStep) * i;
                    pCtlData->u.rect.rclIcon.xRight =
                        pCtlData->u.rect.rclIcon.xLeft +
                        pSysTrayData->lIconWidth;
                }
                else
                {
                    pCtlData->u.rect.rclIcon.xLeft =
                        swp.cx - (lIconStep) * (i + 1);
                    pCtlData->u.rect.rclIcon.xRight =
                        pCtlData->u.rect.rclIcon.xLeft +
                        pSysTrayData->lIconWidth;
                }

                // convert to screen coordinates
                WinMapWindowPoints(pWidget->hwndWidget, HWND_DESKTOP,
                                   (PPOINTL)&pCtlData->u.rect.rclIcon, 2);
                xrc = XST_OK;
            }
            else
                LOGF((" Icon not found!\n"));
        }
        break;

        default:
            break;
    }

    LOGF(("return %d (WinGetLastError is %x)\n",
          xrc, WinGetLastError(pWidget->habWidget)));

    return xrc;
}

/*
 *@@ WgtXSysTrayTimer:
 *      implementation for WM_TIMER in fnwpXSysTrayServer.
 */

static
VOID WgtXSysTrayTimer(HWND hwnd, PXCENTERWIDGET pWidget,
                      USHORT usTimerId)
{
    PSYSTRAYDATA pSysTrayData = (PSYSTRAYDATA)pWidget->pUser;
    PMEMPOOLHDR pMemPoolHdr = (PMEMPOOLHDR)pSysTrayData->pvMemoryPool;
    PMEMPOOLBLK pMemPoolBlk;
    ULONG ulMemPoolMax;

    if (usTimerId == TID_CHECKALIVE)
    {
        // check if windows associated with the icons are still alive
        // and remove those icons whose windows are invalid
        BOOL bAnyDead = FALSE;
        size_t i;
        for (i = 0; i < pSysTrayData->cIcons; ++i)
        {
            if (!WinIsWindow(pWidget->habWidget, pSysTrayData->pIcons[i].hwnd))
            {
                PICONDATA pData = &pSysTrayData->pIcons[i];

                LOGF(("Removing icon of dead window!\n"));
                LOGF((" hwnd  %x\n", pData->hwnd));
                LOGF((" usId  %ld\n", pData->usId));
                LOGF((" hIcon %x\n", pData->hIcon));

                // free memory blocks from the pool allocated for this client
                ulMemPoolMax = pMemPoolHdr->ulBeyond;
                if (ulMemPoolMax > pMemPoolHdr->ulNeedsCommit)
                    ulMemPoolMax = pMemPoolHdr->ulNeedsCommit;
                ulMemPoolMax -= sizeof(MEMPOOLBLK);

                pMemPoolBlk = pMemPoolHdr->aBlocks;
                while ((ULONG)pMemPoolBlk <= ulMemPoolMax)
                {
                    if (pMemPoolBlk->hwnd == pData->hwnd)
                    {
                        LOGF((" freeing memory block %p\n", pMemPoolBlk));
                        FreeNotifyDataPtr(pSysTrayData->pvMemoryPool,
                                          pData->hwnd,
                                          &pMemPoolBlk->NotifyData);
                    }
                    ++pMemPoolBlk;
                }

                bAnyDead = TRUE;
                FreeIconData(pData);
                // pData->hwnd is NULLHANDLE here
            }
        }

        if (bAnyDead)
        {
            // compact the icon array
            i = 0;
            while (i < pSysTrayData->cIcons)
            {
                if (pSysTrayData->pIcons[i].hwnd == NULLHANDLE)
                {
                    --pSysTrayData->cIcons;
                    if (pSysTrayData->cIcons > 0)
                    {
                        memcpy(&pSysTrayData->pIcons[i],
                               &pSysTrayData->pIcons[i + 1],
                               sizeof(*pSysTrayData->pIcons) * (pSysTrayData->cIcons - i));
                    }
                }
                else
                    ++i;
            }

            WgtXSysTrayUpdateAfterIconAddRemove(pWidget);
        }
    }
}

/*
 *@@ fnwpXSysTrayServer:
 *      window procedure for the Extended system tray server window class.
 *
 *      A separate "server" class is necessary because we need a CS_FRAME
 *      top-level window for DDE (which we need to support to be backward
 *      compatible with the System tray wdget from the SysTray/WPS package) and
 *      also to make ourselves discoverable for the client-side implementation
 *      of our new extended API (which queries the class of each top-level
 *      window to find the system tray server).
 */

static
MRESULT EXPENTRY fnwpXSysTrayServer(HWND hwnd, ULONG msg, MPARAM mp1, MPARAM mp2)
{
    // get widget data from QWL_USER_SERVER_DATA (stored there by WM_CREATE)
    PXCENTERWIDGET pWidget =
        (PXCENTERWIDGET)WinQueryWindowPtr(hwnd, QWL_USER_SERVER_DATA);
                    // this ptr is valid after WM_CREATE

    switch (msg)
    {
        case WM_CREATE:
            LOGF(("WM_CREATE\n"));
            WinSetWindowPtr(hwnd, QWL_USER_SERVER_DATA, mp1);
            return FALSE; // confirm success
        break;

        case WM_DESTROY:
            LOGF(("WM_DESTROY\n"));
        break;

        /*
         * WM_XST_CONTROL:
         *      This is the message sent to us by the clinet-side implementation
         *      of the API to request some function. mp1 points to a
         *      SYSTRAYCTLDATA structure.
         */

        case WM_XST_CONTROL:
            return (MRESULT)WgtXSysTrayControl(hwnd, pWidget,
                                               (PSYSTRAYCTLDATA)mp1);
        break;

        /*
         * WM_TIMER:
         *      timer event.
         */

        case WM_TIMER:
            WgtXSysTrayTimer(hwnd, pWidget, SHORT1FROMMP(mp1));
            return (MRESULT)TRUE;
        break;

        default:
            break;
    } // end switch(msg)

    return WinDefWindowProc(hwnd, msg, mp1, mp2);
}

/* ******************************************************************
 *
 *   Exported procedures
 *
 ********************************************************************/

/*
 *@@ WgtInitModule:
 *      required export with ordinal 1, which must tell
 *      the XCenter how many widgets this DLL provides,
 *      and give the XCenter an array of XCENTERWIDGETCLASS
 *      structures describing the widgets.
 *
 *      With this call, you are given the module handle of
 *      XFLDR.DLL. For convenience, and if you have the full
 *      XWorkplace source code, you could resolve imports
 *      for some useful functions which are exported thru
 *      src\shared\xwp.def. We don't do this here.
 *
 *      This function must also register the PM window classes
 *      which are specified in the XCENTERWIDGETCLASS array
 *      entries. For this, you are given a HAB which you
 *      should pass to WinRegisterClass. For the window
 *      class style (4th param to WinRegisterClass),
 *      you should specify
 *
 +          CS_PARENTCLIP | CS_SIZEREDRAW | CS_SYNCPAINT
 *
 *      Your widget window _will_ be resized by the XCenter,
 *      even if you're not planning it to be.
 *
 *      This function only gets called _once_ when the widget
 *      DLL has been successfully loaded by the XCenter. If
 *      there are several instances of a widget running (in
 *      the same or in several XCenters), this function does
 *      not get called again. However, since the XCenter unloads
 *      the widget DLLs again if they are no longer referenced
 *      by any XCenter, this might get called again when the
 *      DLL is re-loaded.
 *
 *      There will ever be only one load occurence of the DLL.
 *      The XCenter manages sharing the DLL between several
 *      XCenters. As a result, it doesn't matter if the DLL
 *      has INITINSTANCE etc. set or not.
 *
 *      If this returns 0, this is considered an error, and the
 *      DLL will be unloaded again immediately.
 *
 *      If this returns any value > 0, *ppaClasses must be
 *      set to a static array (best placed in the DLL's
 *      global data) of XCENTERWIDGETCLASS structures,
 *      which must have as many entries as the return value.
 */

ULONG EXPENTRY WgtInitModule(HAB hab,               // XCenter's anchor block
                             HMODULE hmodPlugin, // module handle of the widget DLL
                             HMODULE hmodXFLDR,     // XFLDR.DLL module handle
                             PCXCENTERWIDGETCLASS *ppaClasses,
                             PSZ pszErrorMsg)       // if 0 is returned, 500 bytes of error msg
{
    ULONG       ulrc = 0, ul = 0;
    CLASSINFO   ClassInfo;

    LOGF(("hmodPlugin %x\n", hmodPlugin));

    do
    {
        // resolve imports from XFLDR.DLL (this is basically
        // a copy of the doshResolveImports code, but we can't
        // use that before resolving...)
        for (ul = 0;
             ul < sizeof(G_aImports) / sizeof(G_aImports[0]);
             ul++)
        {
            APIRET arc;
            if ((arc = DosQueryProcAddr(hmodXFLDR,
                                        0,               // ordinal, ignored
                                        (PSZ)G_aImports[ul].pcszFunctionName,
                                        G_aImports[ul].ppFuncAddress))
                        != NO_ERROR)
            {
                snprintf(pszErrorMsg, 500,
                         "Import %s failed with %ld.",
                         G_aImports[ul].pcszFunctionName, arc);
                break;
            }
        }
        if (ul < sizeof(G_aImports) / sizeof(G_aImports[0]))
            break;

        // register our PM window class
        if (!WinRegisterClass(hab,
                             WNDCLASS_WIDGET_XSYSTRAY,
                             fnwpXSysTray,
                             CS_PARENTCLIP | CS_SIZEREDRAW | CS_SYNCPAINT,
                             sizeof(PVOID))
                                // extra memory to reserve for QWL_USER
            )
        {
            snprintf(pszErrorMsg, 500,
                     "WinRegisterClass(%s) failed with %lX.",
                     WNDCLASS_WIDGET_XSYSTRAY, WinGetLastError(hab));
            break;
        }

        // get the window data size for the WC_FRAME class (any window class
        // that specifies CS_FRAME must have at least this number, otherise
        // WinRegisterClass returns 0x1003
        if (!WinQueryClassInfo(hab, (PSZ)WC_FRAME, &ClassInfo))
            break;
        QWL_USER_SERVER_DATA = ClassInfo.cbWindowData;

        if (!WinRegisterClass(hab,
                              WNDCLASS_WIDGET_XSYSTRAY_SERVER,
                              fnwpXSysTrayServer,
                              CS_FRAME,
                              QWL_USER_SERVER_DATA + sizeof(PVOID))
                                // extra memory to reserve for QWL_USER
            )
        {
            // error registering class: report error then
            snprintf(pszErrorMsg, 500,
                     "WinRegisterClass(%s) failed with %lX",
                     WNDCLASS_WIDGET_XSYSTRAY_SERVER, WinGetLastError(hab));
            break;
        }

        if (WM_XST_CREATED == 0)
            WM_XST_CREATED = WinAddAtom(WinQuerySystemAtomTable(),
                                        WM_XST_CREATED_ATOM);
        if (WM_XST_NOTIFY == 0)
            WM_XST_NOTIFY = WinAddAtom(WinQuerySystemAtomTable(),
                                       WM_XST_NOTIFY_ATOM);

        // no error:
        // return widget classes array
        *ppaClasses = G_WidgetClasses;

        // return no. of classes in this DLL (one here):
        ulrc = sizeof(G_WidgetClasses) / sizeof(G_WidgetClasses[0]);
    }
    while (0);

    LOGF(("pszErrorMsg '%s'\n", pszErrorMsg));
    LOGF(("ulrc %d\n", ulrc));

    return ulrc;
}

/*
 *@@ WgtUnInitModule:
 *      optional export with ordinal 2, which can clean
 *      up global widget class data.
 *
 *      This gets called by the XCenter right before
 *      a widget DLL gets unloaded. Note that this
 *      gets called even if the "init module" export
 *      returned 0 (meaning an error) and the DLL
 *      gets unloaded right away.
 */

VOID EXPENTRY WgtUnInitModule(VOID)
{
    LOGF(("\n"));
}

/*
 *@@ WgtQueryVersion:
 *      this new export with ordinal 3 can return the
 *      XWorkplace version number which is required
 *      for this widget to run. For example, if this
 *      returns 0.9.10, this widget will not run on
 *      earlier XWorkplace versions.
 *
 *      NOTE: This export was mainly added because the
 *      prototype for the "Init" export was changed
 *      with V0.9.9. If this returns 0.9.9, it is
 *      assumed that the INIT export understands
 *      the new FNWGTINITMODULE_099 format (see center.h).
 */

VOID EXPENTRY WgtQueryVersion(PULONG pulMajor,
                              PULONG pulMinor,
                              PULONG pulRevision)
{
    *pulMajor = 0;
    *pulMinor = 9;
    *pulRevision = 9;
}

