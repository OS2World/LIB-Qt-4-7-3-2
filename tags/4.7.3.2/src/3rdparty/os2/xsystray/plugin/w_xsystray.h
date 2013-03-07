
/*
 *@@sourcefile w_xsystray.h:
 *      Extended system tray widget for XCenter/eCenter.
 *
 *      Private declarations.
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

#ifndef W_XSYSTRAY_HEADER_INCLUDED
#define W_XSYSTRAY_HEADER_INCLUDED

#include <sys/builtin.h>        // atomics

// primitive debug logging to a file (usage: #define ENABLE_LOG_TO "some_file")
#ifdef ENABLE_LOG_TO
#include <stdio.h>
#include <stdarg.h>
static void __LOG_WORKER(const char *fmt, ...)
{
    static FILE *f = NULL;
    if (f == NULL)
    {
        f = fopen(ENABLE_LOG_TO, "w");
        setbuf(f, NULL);
    }
    if (f != NULL)
    {
        va_list vl;
        va_start(vl, fmt);
        vfprintf(f, fmt, vl);
        va_end(vl);
    }
}
#define LOG(m)  do { __LOG_WORKER m; } while(0)
#define LOGF(m) do { __LOG_WORKER("%s: ", __FUNCTION__); __LOG_WORKER m; } while(0)
#else
#define LOG(m)  do {} while (0)
#define LOGF(m) do {} while (0)
#endif

#include "xsystray.h"

#define XSYSTRAY_VERSION_MAJOR 0
#define XSYSTRAY_VERSION_MINOR 1
#define XSYSTRAY_VERSION_REVISION 1

#define WNDCLASS_WIDGET_XSYSTRAY_SERVER "XWPCenterExtendedSysTrayServer"

#define WNDCLASS_WIDGET_XSYSTRAY        "XWPCenterExtendedSysTray"
#define INTCLASS_WIDGET_XSYSTRAY        "ExtendedSysTray"
#define HUMANSTR_WIDGET_XSYSTRAY        "Extended system tray"

#define WM_XST_CREATED_ATOM             "ExtendedSysTray.WM_XST_CREATED"
#define WM_XST_NOTIFY_ATOM              "ExtendedSysTray.WM_XST_NOTIFY"

#define WM_XST_CONTROL  (WM_USER + 0)
        // message sent to the system tray server to request an action

// server action commands
typedef enum
{
    SYSTRAYCMD_GETVERSION,
    SYSTRAYCMD_ADDICON,
    SYSTRAYCMD_REPLACEICON,
    SYSTRAYCMD_REMOVEICON,
    SYSTRAYCMD_SETTOOLTIP,
    SYSTRAYCMD_SHOWBALLOON,
    SYSTRAYCMD_HIDEBALLOON,
    SYSTRAYCMD_QUERYRECT,
} SYSTRAYCMD;

// server responses to WM_XST_CONTROL
#define XST_OK          0   // command succeeded
#define XST_FAIL        1   // command failed
#define XST_REPLACED    2   // SYSTRAYCMD_ADDICON replaced the existing icon

/*
 *@@ SYSTRAYCTLDATA:
 *      Structure holding information accompanying WM_XST_CONTROL messages sent
 *      to the system tray server by clients (windows associated with system
 *      tray icons).
 *
 *      NOTE: When you change the size of this structure, you may also need to
 *      change CLIENT_MEMORYPOOL_SIZE value (see the comments there for
 *      details).
 */

typedef struct
{
    SYSTRAYCMD  ulCommand;
                // command to execute, must always be set
    HWND        hwndSender;
                // sender window, a must for SYSTRAYCMD_ADDICON, _CHANGEICON,
                // _REMOVEICON, _SETTOOLTIP, _SHOWBALLOON, _HIDEBALLOON,
                // _QUERYRECT
    union
    {
        struct
        {
            ULONG ulMajor;
            ULONG ulMinor;
            ULONG ulRevision;
        } version;
          // used by SYSTRAYCMD_GETVERSION

        struct
        {
            USHORT      usId;
            HPOINTER    hIcon;
            CHAR        szToolTip[512];
            ULONG       ulMsgId;
        } icon;
          // used by SYSTRAYCMD_ADDICON, _CHANGEICON, _REMOVEICON, _SETTOOLTIP

        struct
        {
            RECTL rclIcon;
        } rect;
          // used by SYSTRAYCMD_QUERYRECT
    } u;

    BOOL    bAcknowledged : 1;
            // set to true by the recipient if it processes the message

} SYSTRAYCTLDATA, *PSYSTRAYCTLDATA;

/*
 *@@ NOTIFYDATA:
 *      Structure holding information acompanying notification messages
 *      posted to clients (windows associated with system tray icons) about
 *      icon events. This structure unions all public notification code
 *      dependent structures defined in xsystray.h (starting with XST*).
 *
 *      All messages posted to the client have an ID corresponding to the
 *      WM_XST_NOTIFY_ATOM in the system atom table. The client-side API
 *      implementation intercepts these messages (using HK_INPUT), composes a
 *      new message given the information in NOTIFYDATA, frees the NOTIFYDATA
 *      pointer using FreeNotifyDataPtr() and then sends the composed message to
 *      the appropriate window.
 *
 *      The layout of the XST_NOTIFY message is as follows:
 *
 *          param1
 *              PNOTIFYDATA pNotifyData     pointer to the NOTIFYDATA structure
 *
 *          param2
 *              PVOID       pvMemoryPool    server memory pool (for the
 *                                          FreeNotifyDataPtr() call)
 *
 *      NOTE: Structures in the union should only contain values; passing
 *      pointers to arbitrary data to the client side is not supported (yet).
 *
 *      NOTE: When you change the size of this structure, you may also need to
 *      change SERVER_MEMORYPOOL_SIZE value in w_xsystray.c (see the comments
 *      there for details).
 */

typedef struct
{
    ULONG   msg;
            // ID of the message that is to be sent to the target window
    MPARAM  mp1;
            // message parameter (USHORT usIconId, USHORT usNotifyCode)
    MPARAM  mp2;
            // message parameter (a pointer to a struct from the union)
    union
    {
        XSTMOUSEMSG     MouseMsg;
        XSTCONTEXTMSG   ContextMsg;
        XSTWHEELMSG     WheelMsg;
    } u;

} NOTIFYDATA, *PNOTIFYDATA;

// Header of the server-side memory pool
typedef struct
{
    volatile HWND   hwnd;        // owner of the block or NULLHANDLE if free
    NOTIFYDATA      NotifyData;  // data

} MEMPOOLBLK, *PMEMPOOLBLK;

// allocation unit in the server-side memory pool
typedef struct
{
    ULONG ulBeyond;         // address of the first byte beyond the memory pool

    volatile ULONG ulNeedsCommit;   // address of the first decommitted byte
    volatile ULONG ulNext;          // address of next possibly free block

    MEMPOOLBLK aBlocks[0];          // fake array for easier addressing

} MEMPOOLHDR, *PMEMPOOLHDR;

/*
 *@@ FreeNotifyDataPtr:
 *      Frees the NOTIFYDATA structure allocated by AllocNotifyDataPtr().
 *
 *      See AllocNotifyDataPtr() for more details about allocating these
 *      structures.
 */

inline
VOID FreeNotifyDataPtr(PVOID pvMemoryPool,  // in: memory pool base address
                       HWND hwndOwner,      // in: owner of the struct to free
                       PNOTIFYDATA pData)   // in: address of the struct to free
{
    PMEMPOOLHDR pHdr = (PMEMPOOLHDR)pvMemoryPool;
    PMEMPOOLBLK pBlk = (PMEMPOOLBLK)((ULONG)pData - sizeof(HWND));

    ULONG ulNext = pHdr->ulNext;

    __atomic_cmpxchg32((uint32_t *)&pBlk->hwnd, NULLHANDLE, hwndOwner);

    // if the next possible free block is greater than we just freed,
    // set it to us (to minimize the amount of committed pages)
    if (ulNext > (ULONG)pBlk)
        __atomic_cmpxchg32((uint32_t *)&pHdr->ulNext, (ULONG)pBlk, ulNext);
}

#endif // W_XSYSTRAY_HEADER_INCLUDED


