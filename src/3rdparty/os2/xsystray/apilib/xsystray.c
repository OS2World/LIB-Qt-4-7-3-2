
/*
 *@@sourcefile xsystray.c:
 *      Extended system tray widget for XCenter/eCenter.
 *
 *      Implementation of the public API.
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

#define OS2EMX_PLAIN_CHAR
#define INCL_DOSERRORS
#define INCL_DOSPROCESS
#define INCL_WINWINDOWMGR
#define INCL_WINERRORS
#define INCL_WINATOM
#define INCL_WINPOINTERS
#define INCL_WINHOOKS
#define INCL_WINWINDOWMGR
#include <os2.h>

#define XSTAPI_IMPL
#include "xsystray.h"

//#define ENABLE_LOG_TO "c:\\xsystray_api.dbg"

#include "w_xsystray.h"

#include <string.h>
#include <sys/builtin.h>        // atomics
#include <sys/fmutex.h>         // fast mutex
#include <sys/smutex.h>         // simple mutex

static ULONG WM_XST_CREATED = 0;
             // identity of the WM_XST_CREATED message taken from the atom table
static ULONG WM_XST_NOTIFY = 0;
             // identity of the WM_XST_NOTIFY message taken from the atom table

static
volatile HWND G_hwndSysTray = NULLHANDLE;
              // window handle of the system tray server

static
volatile PVOID G_pvMemoryPool = NULL;
               // shared memory pool for SYSTRAYCTLDATA structs used by
               // WM_XST_CONTROL messages. Note that once allocated, this memory
               // is never freed: it is intentional since the memory is assumed
               // to be always in need and that the system will free it when the
               // application terminates

#define CLIENT_MEMORYPOOL_SIZE 65536
        // taking SYSTRAYCTLDATA size into account (<=1024 B), this is enough
        // for at least 64 threads sending WM_XST_CONTROL simultaneously, which
        // sounds sane

typedef struct
{
    HMQ         hmq;
    unsigned    cRefs;
} HMQREFS, *PHMQREFS;

static PHMQREFS G_pHmqRefs = NULL;
                // array of references to each HMQ that we make
static size_t   G_cHmqRefs = 0;
                // number of elements in G_pHmqRefs
static size_t   G_cHmqRefsMax = 0;
                // maximum number of elements in G_pHmqRefs
static _fmutex  G_fmtx;
                // fast mutex to protect
static _smutex  G_smtx = 0;
                // simple mutex for xstAddSysTrayIcon()
#define         HMQREFS_GROW 4
                // how fast G_pHmqRefs grows when more space is necessary

// @todo to be on the safe side with casting in __atomic_cmpxchg32() we need
// compile-time assertions like this:
// AssertCompile(sizeof(uint32_t) == sizeof(HWND));
// AssertCompile(sizeof(uint32_t) == sizeof(PVOID));

static HWND FindSysTrayServerWindow()
{
    char buf[sizeof(WNDCLASS_WIDGET_XSYSTRAY_SERVER) + 1];
    HWND hwnd;
    HENUM henum = WinBeginEnumWindows(HWND_DESKTOP);
    while ((hwnd = WinGetNextWindow(henum)) != NULLHANDLE)
    {
        LONG len = WinQueryClassName(hwnd, sizeof(buf), buf);
        buf[len] = '\0';
        if (strcmp(WNDCLASS_WIDGET_XSYSTRAY_SERVER, buf) == 0)
            break;
    }
    WinEndEnumWindows(henum);

    return hwnd;
}

static ULONG SendSysTrayCtlMsg(PSYSTRAYCTLDATA pData)
{
    APIRET arc;
    PID pid;
    TID tid;
    MRESULT mrc;

    BOOL bTriedFind = FALSE;

    do
    {
        if (G_hwndSysTray == NULLHANDLE)
        {
            bTriedFind = TRUE;
            HWND hwnd = FindSysTrayServerWindow();
            __atomic_cmpxchg32((volatile uint32_t *)&G_hwndSysTray,
                               hwnd, NULLHANDLE);
            if (G_hwndSysTray == NULLHANDLE)
                break;
        }

        if (bTriedFind)
        {
            arc = ERROR_INVALID_HANDLE;
            if (WinQueryWindowProcess(G_hwndSysTray, &pid, &tid))
                arc = DosGiveSharedMem(G_pvMemoryPool,
                                       pid, PAG_READ | PAG_WRITE);
            if (arc != NO_ERROR)
                break;
        }

        pData->bAcknowledged = FALSE;

        mrc = WinSendMsg(G_hwndSysTray, WM_XST_CONTROL, pData, NULL);
        if (pData->bAcknowledged)
            return (ULONG)mrc;

        // if we failed to send the message, it may mean that XCenter was restarted
        // or the system tray was re-enabled. Try to get a new handle (only if we
        // didn't already do it in this call)
        if (!bTriedFind)
        {
            G_hwndSysTray = NULLHANDLE;
            continue;
        }

        break;
    }
    while (1);

    return XST_FAIL;
}

/*
 *@@ AllocSysTrayCtlDataPtr:
 *      Allocates a SYSTRAYCTLDATA struct in the pool of shared memory.
 *
 *      If there is no free space in the pool, it returns NULL. The allocated
 *      memory must be freed by FreeSysTrayCtlDataPtr() when not needed.
 */

static PSYSTRAYCTLDATA AllocSysTrayCtlDataPtr()
{
    APIRET arc;
    PVOID pvPool;
    PSYSTRAYCTLDATA pData;

    if (!G_pvMemoryPool)
    {
        // Note: we don't PAG_COMMIT, DosSubAllocMem will do so when needed
        arc = DosAllocSharedMem((PVOID)&pvPool, NULL, CLIENT_MEMORYPOOL_SIZE,
                                PAG_READ | PAG_WRITE | OBJ_GIVEABLE);
        if (arc == NO_ERROR)
            arc = DosSubSetMem(pvPool,
                               DOSSUB_INIT | DOSSUB_SPARSE_OBJ,
                               CLIENT_MEMORYPOOL_SIZE);
        if (!__atomic_cmpxchg32((volatile uint32_t *)&G_pvMemoryPool,
                                (uint32_t)pvPool, (uint32_t)NULL))
        {
            // another thread has already got an entry, discard our try
            if (pvPool)
                DosFreeMem(pvPool);
        }
        else
        {
            // we could fail to allocate while being the first... give up
            if (arc != NO_ERROR)
                return NULL;
        }
    }

    arc = DosSubAllocMem(G_pvMemoryPool, (PVOID)&pData, sizeof(*pData));
    if (arc != NO_ERROR)
        return NULL;

    return pData;
}

static VOID FreeSysTrayCtlDataPtr(PSYSTRAYCTLDATA pData)
{
    DosSubFreeMem(G_pvMemoryPool, pData, sizeof(*pData));
}

/*
 *@@ InputHook:
 *      This is used to intercept posted WM_XST_NOTIFY messages and apply
 *      special processing to them (compose a client window-specific
 *      notification message, free the NOTIFYDATA structure and post the
 *      composed message to the target window).
 */

static BOOL EXPENTRY InputHook(HAB hab, PQMSG pQmsg, ULONG fs)
{
    if (pQmsg->msg == WM_XST_NOTIFY)
    {
        PNOTIFYDATA pNotifyData = (PNOTIFYDATA)pQmsg->mp1;
        PVOID pvMemoryPool = (PVOID)pQmsg->mp2;

        // 1) create a local copy of NOTIFYDATA
        NOTIFYDATA NotifyData = *pNotifyData;
        // 2) fix the mp2 pointer in it (which is always to one of u's structs)
        NotifyData.mp2 -= (ULONG)pNotifyData;
        NotifyData.mp2 += (ULONG)&NotifyData;
        // 3) free the original to let it be reused by other processes ASAP
        FreeNotifyDataPtr(pvMemoryPool, pQmsg->hwnd, pNotifyData);

        // start with a copy of the message and change the fields we need
        QMSG newMsg = *pQmsg;
        newMsg.msg = NotifyData.msg;
        newMsg.mp1 = NotifyData.mp1;
        newMsg.mp2 = NotifyData.mp2;

        LOGF(("Dispatching msg %08lx to hwnd %lx\n", newMsg.msg, newMsg.hwnd));

        // deliver the message
        WinDispatchMsg(hab, &newMsg);

        return TRUE;
    }

    return FALSE;
}

BOOL xstQuerySysTrayVersion(PULONG pulMajor,
                            PULONG pulMinor,
                            PULONG pulRevision)
{
    BOOL brc;
    PSYSTRAYCTLDATA pData = AllocSysTrayCtlDataPtr();
    if (!pData)
        return FALSE;

    pData->ulCommand = SYSTRAYCMD_GETVERSION;
    pData->hwndSender = NULLHANDLE;

    brc = SendSysTrayCtlMsg(pData) == XST_OK;
    if (brc)
    {
        if (pulMajor)
            *pulMajor = pData->u.version.ulMajor;
        if (pulMinor)
            *pulMinor = pData->u.version.ulMinor;
        if (pulRevision)
            *pulRevision = pData->u.version.ulRevision;
    }

    FreeSysTrayCtlDataPtr(pData);

    return brc;
}

BOOL xstAddSysTrayIcon(HWND hwnd,
                       USHORT usId,
                       HPOINTER hIcon,
                       PCSZ pcszToolTip,
                       ULONG ulMsgId,
                       ULONG ulFlags)
{
    BOOL    brc;
    ULONG   xrc = XST_FAIL;
    PPIB    ppib;
    HAB     hab;
    HMQ     hmq;
    size_t  i;

    PSYSTRAYCTLDATA pData = AllocSysTrayCtlDataPtr();
    if (!pData)
        return FALSE;

    if (WM_XST_NOTIFY == 0)
        WM_XST_NOTIFY = WinAddAtom(WinQuerySystemAtomTable(),
                                   WM_XST_NOTIFY_ATOM);

    hab = WinQueryAnchorBlock(hwnd);
    hmq = WinQueryWindowULong(hwnd, QWL_HMQ);
    if (hmq == NULLHANDLE)
        return FALSE;

    // initialize the HMQ refs array
    // @todo remove _smutex usage when we get into the DLL and initialize
    // _fmutex + array in the DLL init routine
    _smutex_request(&G_smtx);
    if (!G_pHmqRefs)
    {
        if (_fmutex_create(&G_fmtx, 0))
            return FALSE;
        G_pHmqRefs = malloc(sizeof(*G_pHmqRefs) * HMQREFS_GROW);
        if (!G_pHmqRefs)
            return FALSE;
        G_cHmqRefs = 0;
        G_cHmqRefsMax = HMQREFS_GROW;
    }
    _smutex_release(&G_smtx);

    // give all processes temporary access to hIcon
    brc = hIcon != NULLHANDLE ? WinSetPointerOwner(hIcon, 0, FALSE) : TRUE;
    if (brc)
    {
        pData->ulCommand = SYSTRAYCMD_ADDICON;
        pData->hwndSender = hwnd;

        pData->u.icon.usId = usId;
        pData->u.icon.hIcon = hIcon;
        pData->u.icon.ulMsgId = ulMsgId;

        if (!pcszToolTip)
            pData->u.icon.szToolTip[0] = '\0';
        else
        {
            strncpy(pData->u.icon.szToolTip, pcszToolTip,
                    sizeof(pData->u.icon.szToolTip) - 1);
            // be on the safe side
            pData->u.icon.szToolTip[sizeof(pData->u.icon.szToolTip) - 1] = '\0';
        }

        xrc = SendSysTrayCtlMsg(pData);
        brc = xrc == XST_OK || xrc == XST_REPLACED;

        // revoke temporary access to hIcon
        if (hIcon != NULLHANDLE)
        {
            DosGetInfoBlocks(NULL, &ppib);
            WinSetPointerOwner(hIcon, ppib->pib_ulpid, TRUE);
        }
    }

    FreeSysTrayCtlDataPtr(pData);

    if (xrc == XST_OK)
    {
        // install the message hook for the new icon to intercept WM_XST_NOTIFY
        // messages or increase the reference count if already done so
        brc = FALSE;
        _fmutex_request(&G_fmtx, _FMR_IGNINT);
        do
        {
            for (i = 0; i < G_cHmqRefs; ++i)
                if (G_pHmqRefs[i].hmq == hmq)
                    break;
            if (i < G_cHmqRefs)
                ++G_pHmqRefs[i].cRefs;
            else
            {
                if (i == G_cHmqRefsMax)
                {
                    PHMQREFS pNewRefs = realloc(G_pHmqRefs,
                                                sizeof(*G_pHmqRefs) *
                                                (G_cHmqRefsMax + HMQREFS_GROW));
                    if (!pNewRefs)
                        break;
                    G_pHmqRefs = pNewRefs;
                    G_cHmqRefsMax += HMQREFS_GROW;
                }
                brc = WinSetHook(hab, hmq, HK_INPUT, (PFN)InputHook, NULLHANDLE);
                if (!brc)
                    break;
                ++G_cHmqRefs;
                G_pHmqRefs[i].hmq = hmq;
                G_pHmqRefs[i].cRefs = 1;
            }
            brc = TRUE;
        }
        while (0);
        _fmutex_release(&G_fmtx);

        if (!brc)
            xstRemoveSysTrayIcon(hwnd, usId);
    }

    return brc;
}

BOOL xstReplaceSysTrayIcon(HWND hwnd,
                           USHORT usId,
                           HPOINTER hIcon)
{
    BOOL    brc;
    PPIB    ppib;

    PSYSTRAYCTLDATA pData = AllocSysTrayCtlDataPtr();
    if (!pData)
        return FALSE;

    // give all processes temporary access to hIcon
    brc = hIcon != NULLHANDLE ? WinSetPointerOwner(hIcon, 0, FALSE) : TRUE;
    if (brc)
    {
        pData->ulCommand = SYSTRAYCMD_REPLACEICON;
        pData->hwndSender = hwnd;

        pData->u.icon.usId = usId;
        pData->u.icon.hIcon = hIcon;

        brc = SendSysTrayCtlMsg(pData) == XST_OK;

        // revoke temporary access to hIcon
        if (hIcon != NULLHANDLE)
        {
            DosGetInfoBlocks(NULL, &ppib);
            WinSetPointerOwner(hIcon, ppib->pib_ulpid, TRUE);
        }
    }

    FreeSysTrayCtlDataPtr(pData);

    return brc;
}

BOOL xstRemoveSysTrayIcon(HWND hwnd,
                          USHORT usId)
{
    BOOL brc;
    HAB     hab;
    HMQ     hmq;
    size_t  i;

    PSYSTRAYCTLDATA pData = AllocSysTrayCtlDataPtr();
    if (!pData)
        return FALSE;

    hab = WinQueryAnchorBlock(hwnd);
    hmq = WinQueryWindowULong(hwnd, QWL_HMQ);
    if (hmq == NULLHANDLE)
        return FALSE;

    pData->ulCommand = SYSTRAYCMD_REMOVEICON;
    pData->hwndSender = hwnd;
    pData->u.icon.usId = usId;

    brc = SendSysTrayCtlMsg(pData) == XST_OK;

    FreeSysTrayCtlDataPtr(pData);

    if (brc)
    {
        // remove the message hook if it's the last reference to the HMQ
        _fmutex_request(&G_fmtx, _FMR_IGNINT);
        do
        {
            for (i = 0; i < G_cHmqRefs; ++i)
                if (G_pHmqRefs[i].hmq == hmq)
                    break;
            if (i == G_cHmqRefs)
                // unknown HMQ??
                break;

            if (--G_pHmqRefs[i].cRefs == 0)
                WinReleaseHook(hab, hmq, HK_INPUT, (PFN)InputHook, NULLHANDLE);
        }
        while (0);
        _fmutex_release(&G_fmtx);
    }

    return brc;
}

BOOL xstSetSysTrayIconToolTip(HWND hwnd,
                              USHORT usId,
                              PCSZ pcszToolTip)
{
    BOOL brc;
    PSYSTRAYCTLDATA pData = AllocSysTrayCtlDataPtr();
    if (!pData)
        return FALSE;

    pData->ulCommand = SYSTRAYCMD_SETTOOLTIP;
    pData->hwndSender = hwnd;
    pData->u.icon.usId = usId;

    if (!pcszToolTip)
        pData->u.icon.szToolTip[0] = '\0';
    else
    {
        strncpy(pData->u.icon.szToolTip, pcszToolTip,
                sizeof(pData->u.icon.szToolTip) - 1);
        // be on the safe side
        pData->u.icon.szToolTip[sizeof(pData->u.icon.szToolTip) - 1] = '\0';
    }

    brc = SendSysTrayCtlMsg(pData) == XST_OK;

    FreeSysTrayCtlDataPtr(pData);

    return brc;
}

BOOL xstShowSysTrayIconBalloon(HWND hwnd, USHORT usId, PCSZ pcszTitle,
                               PCSZ pcszText, ULONG ulFlags, ULONG ulTimeout)
{
    // @todo implement
    return FALSE;
}

BOOL xstHideSysTrayIconBalloon(HWND hwnd, USHORT usId)
{
    // @todo implement
    return FALSE;
}

BOOL xstQuerySysTrayIconRect(HWND hwnd, USHORT usId, PRECTL prclRect)
{
    BOOL brc;
    PSYSTRAYCTLDATA pData = AllocSysTrayCtlDataPtr();
    if (!pData)
        return FALSE;

    pData->ulCommand = SYSTRAYCMD_QUERYRECT;
    pData->hwndSender = hwnd;
    pData->u.icon.usId = usId;

    brc = SendSysTrayCtlMsg(pData) == XST_OK;
    if (brc)
    {
        *prclRect = pData->u.rect.rclIcon;
    }

    FreeSysTrayCtlDataPtr(pData);

    return brc;
}

ULONG xstGetSysTrayCreatedMsgId()
{
    if (WM_XST_CREATED == 0)
        WM_XST_CREATED = WinAddAtom(WinQuerySystemAtomTable(),
                                    WM_XST_CREATED_ATOM);
    return WM_XST_CREATED;
}

ULONG xstGetSysTrayMaxTextLen()
{
    return sizeof(((PSYSTRAYCTLDATA)0)->u.icon.szToolTip);
}

