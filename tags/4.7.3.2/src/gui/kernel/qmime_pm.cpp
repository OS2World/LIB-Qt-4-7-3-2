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

#include "qmime.h"

#include "qimagereader.h"
#include "qimagewriter.h"
#include "qdatastream.h"
#include "qbuffer.h"
#include "qt_os2.h"
#include "qapplication_p.h"
#include "qtextcodec.h"
#include "qregexp.h"
#include "qalgorithms.h"
#include "qmap.h"
#include "qdnd_p.h"
#include "qurl.h"
#include "qvariant.h"
#include "qtextdocument.h"
#include "qdir.h"

#include "qt_os2.h"
#include "private/qpmobjectwindow_pm_p.h"

//#define QDND_DEBUG // in pair with qdnd_pm.cpp

#ifdef QDND_DEBUG
#   include "qdebug.h"
#   define DEBUG(a) qDebug a
#else
#   define DEBUG(a) do {} while(0)
#endif

QT_BEGIN_NAMESPACE

#if !defined(QT_NO_DRAGANDDROP)

// Undoc'd DC_PREPAREITEM, see
// http://lxr.mozilla.org/seamonkey/source/widget/src/os2/nsDragService.cpp
#if !defined (DC_PREPAREITEM)
#define DC_PREPAREITEM 0x40
#endif

/*! \internal
  According to my tests, DrgFreeDragtransfer() appears to be bogus: when the
  drag source attempts to free the DRAGTRANSFER structure passed to it in
  DM_RENDERPREPARE/DM_RENDER by another process, the shared memory object is not
  actually released until DrgFreeDragtransfer() is called for the second time.
  This method tries to fix this problem.

  \note The problem (and the solution) was not tested on platforms other than
  eCS!
*/
void qt_DrgFreeDragtransfer(DRAGTRANSFER *xfer)
{
    Q_ASSERT(xfer);
    if (xfer) {
        BOOL ok = DrgFreeDragtransfer(xfer);
        Q_ASSERT(ok);
        if (ok) {
            ULONG size = ~0, flags = 0;
            APIRET rc = DosQueryMem(xfer, &size, &flags);
            Q_ASSERT(rc == 0);
            if (rc == 0 && !(flags & PAG_FREE)) {
                PID pid;
                TID tid;
                ok = WinQueryWindowProcess(xfer->hwndClient, &pid, &tid);
                Q_ASSERT(ok);
                if (ok) {
                    PPIB ppib = 0;
                    DosGetInfoBlocks(0, &ppib);
                    if (ppib->pib_ulpid != pid) {
                        DEBUG(() << "qt_DrgFreeDragtransfer: Will free xfer"
                                 << xfer << "TWICE (other process)!");
                        DrgFreeDragtransfer(xfer);
                    }
                }
            }
        }
    }
}

#define SEA_TYPE ".TYPE"

/*! \internal
  Sets a single .TYPE EA vaule on a given fle.
*/
static void qt_SetFileTypeEA(const char *name, const char *type)
{
    #pragma pack(1)

    struct MY_FEA2 {
        ULONG   oNextEntryOffset;  /*  Offset to next entry. */
        BYTE    fEA;               /*  Extended attributes flag. */
        BYTE    cbName;            /*  Length of szName, not including NULL. */
        USHORT  cbValue;           /*  Value length. */
        CHAR    szName[0];         /*  Extended attribute name. */
        /* EA value follows here */
    };

    struct MY_FEA2LIST {
        ULONG   cbList;            /*  Total bytes of structure including full list. */
        MY_FEA2 list[0];           /*  Variable-length FEA2 structures. */
    };

    struct MY_FEA2_VAL {
        USHORT  usEAType;          /* EA value type (one of EAT_* constants) */
        USHORT  usValueLen;        /* Length of the value data following */
        CHAR    aValueData[0];     /* value data */
    };

    struct MY_FEA2_MVMT {
        USHORT      usEAType;      /* Always EAT_MVMT */
        USHORT      usCodePage;    /* 0 - default */
        USHORT      cbNumEntries;  /* Number of MYFEA2_VAL structs following */
        MY_FEA2_VAL aValues[0];    /* MYFEA2_VAL value structures */
    };

    #pragma pack()

    uint typeLen = qstrlen(type);
    uint valLen = sizeof(MY_FEA2_VAL) + typeLen;
    uint mvmtLen = sizeof(MY_FEA2_MVMT) + valLen;
    uint fea2Len = sizeof(MY_FEA2) + sizeof(SEA_TYPE);
    uint fullLen = sizeof(MY_FEA2LIST) + fea2Len + mvmtLen;

    uchar *eaData = new uchar[fullLen];

    MY_FEA2LIST *fea2List = (MY_FEA2LIST *)eaData;
    fea2List->cbList = fullLen;

    MY_FEA2 *fea2 = fea2List->list;
    fea2->oNextEntryOffset = 0;
    fea2->fEA = 0;
    fea2->cbName = sizeof(SEA_TYPE) - 1;
    fea2->cbValue = mvmtLen;
    strcpy(fea2->szName, SEA_TYPE);

    MY_FEA2_MVMT *mvmt = (MY_FEA2_MVMT *)(fea2->szName + sizeof(SEA_TYPE));
    mvmt->usEAType = EAT_MVMT;
    mvmt->usCodePage = 0;
    mvmt->cbNumEntries = 1;

    MY_FEA2_VAL *val = mvmt->aValues;
    val->usEAType = EAT_ASCII;
    val->usValueLen = typeLen;
    memcpy(val->aValueData, type, typeLen);

    EAOP2 eaop2;
    eaop2.fpGEA2List = 0;
    eaop2.fpFEA2List = (FEA2LIST *)fea2List;
    eaop2.oError = 0;

    APIRET rc = DosSetPathInfo(name, FIL_QUERYEASIZE,
                               &eaop2, sizeof(eaop2), 0);
    Q_UNUSED(rc);
#ifndef QT_NO_DEBUG
    if (rc)
        qWarning("qt_SetFileTypeEA: DosSetPathInfo failed with %ld", rc);
#endif

    delete[] eaData;
}

static int hex_to_int(uchar c)
{
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= '0' && c <= '9') return c - '0';
    return -1;
}

static inline int hex_to_int(char c)
{
    return hex_to_int((uchar) c);
}

//------------------------------------------------------------------------------

struct QPMMime::DefaultDragWorker::Data : public QPMObjectWindow
{
    Data(DefaultDragWorker *worker, bool excl) : q(worker), exclusive(excl) {}

    struct Request
    {
        Request(ULONG i, Provider *p, const char *m, const char *f)
            : index(i), provider(p), drm(m), drf(f)
            , xfer(0), rendered(false), sharedMem(0) {}

        ~Request()
        {
            // free memory allocated for the target that requested DRM_SHAREDMEM
            if (sharedMem)
                DosFreeMem(sharedMem);
            Q_ASSERT(!xfer);
        }

        ULONG index;
        Provider *provider;
        QByteArray drm;
        QByteArray drf;
        DRAGTRANSFER *xfer;
        bool rendered;
        PVOID sharedMem;
    };

    inline bool isInitialized() { return providers.count() != 0; }
    void cleanupRequests();

    // QPMObjectWindow interface
    MRESULT message(ULONG msg, MPARAM mp1, MPARAM mp2);

    struct DrfProvider
    {
        DrfProvider() : prov(0) {}
        DrfProvider(const QByteArray &d, Provider *p) : drf(d), prov(p) {}
        QByteArray drf;
        Provider *prov;
    };

    typedef QList<DrfProvider> DrfProviderList;

    Provider *providerFor(const char *drf)
    {
        foreach (const DrfProvider &dp, providers)
            if (qstrcmp(dp.drf, drf) == 0)
                return dp.prov;
        return 0;
    }

    DefaultDragWorker *q;

    const bool exclusive : 1;
    DrfProviderList providers;

    ULONG itemCnt;
    QHash<ULONG, Request*> requests;
    bool renderOk : 1;
};

void QPMMime::DefaultDragWorker::Data::cleanupRequests()
{
    if (requests.count()) {
        DEBUG(("In the previous DnD session, the drop target sent "
               "DM_RENDERPREPARE/DM_RENDER\n"
               "for some drag item but didn't send DM_ENDCONVERSATION!"));
        qDeleteAll(requests);
        requests.clear();
    }
}

/*!
    Constructs a new default drag worker instance. The \a exclusive flag
    defines if this worker is exclusive (see isExclusive() for details).
*/
QPMMime::DefaultDragWorker::DefaultDragWorker(bool exclusive)
    : d(new Data(this, exclusive))
{
    d->itemCnt = 0;
    d->renderOk = true;
}

/*!
    Destroys the instance.
*/
QPMMime::DefaultDragWorker::~DefaultDragWorker()
{
    d->cleanupRequests();
    delete d;
}

/*!
    \reimp
*/
bool QPMMime::DefaultDragWorker::cleanup(bool isCancelled)
{
    // the return value is: true if the source-side Move for the given
    // drag object should be *dis*allowed, false otherwise (including cases
    // when this DragWorker didn't participate to the conversation at all)

    // sanity check
    Q_ASSERT(d->isInitialized());
    if (!d->isInitialized())
        return true;

    bool moveDisallowed = false;

    DEBUG(() << "DefaultDragWorker: Session ended ( cancelled" << isCancelled
             << "requests.left" << d->requests.count() << ")");

    if (d->requests.count()) {
        // always disallow Move if not all requests got DM_ENDCONVERSATION
        moveDisallowed = true;
    } else {
        // disallow Move if rendering of some item failed
        moveDisallowed = !d->renderOk;
    }

    DEBUG(() << "DefaultDragWorker: moveDisallowed" << moveDisallowed);

    // Note: remaining requests will be lazily deleted by cleanupRequests()
    // when a new DND session is started

    d->renderOk = true;
    d->itemCnt = 0;

    // Indicate we're cleaned up (i.e. the DND session is finished)
    d->providers.clear();

    return moveDisallowed;
}

/*!
    \reimp
*/
bool QPMMime::DefaultDragWorker::isExclusive() const
{
    return d->exclusive;
}

/*!
    \reimp
*/
ULONG QPMMime::DefaultDragWorker::itemCount() const
{
    return d->itemCnt;
}

/*!
    \reimp
*/
ULONG QPMMime::DefaultDragWorker::hwnd() const
{
    return d->hwnd();
}

/*!
    \reimp
*/
QByteArray QPMMime::DefaultDragWorker::composeFormatString()
{
    QByteArray formats;

    // sanity checks
    Q_ASSERT(d->isInitialized());
    if (!d->isInitialized())
        return formats;

    bool first = true;
    foreach(const Data::DrfProvider &p, d->providers) {
        if (first)
            first = false;
        else
            formats += ",";
        formats += p.drf;
    }

    Q_ASSERT(!formats.isNull());
    if (formats.isNull())
        return formats;

    // DRM_SHAREDMEM comes first to prevent native DRM_OS2FILE
    // rendering on the target side w/o involving the source.
    // Also, we add <DRM_SHAREDMEM,DRF_POINTERDATA> just like WPS does it
    // (however, it doesn't help when dropping objects to it -- WPS still
    // chooses DRM_OS2FILE).
    formats = "(DRM_SHAREDMEM,DRM_OS2FILE)x(" + formats + "),"
              "<DRM_SHAREDMEM,DRF_POINTERDATA>";

    DEBUG(() << "DefaultDragWorker: formats" << formats
             << ", itemCnt" << d->itemCnt);

    return formats;
}

/*!
    \reimp
*/
bool QPMMime::DefaultDragWorker::prepare(const char *drm, const char *drf,
                                          DRAGITEM *item, ULONG itemIndex)
{
    // sanity checks
    Q_ASSERT(d->isInitialized());
    if (!d->isInitialized())
        return false;

    Q_ASSERT(item && itemIndex < d->itemCnt);
    if (!item || itemIndex >= d->itemCnt)
        return false;

    DEBUG(() << "DefaultDragWorker: Preparing item" <<  itemIndex << "( id "
             << item->ulItemID << ") for <" << drm << "," << drf << ">");

    Provider *p = d->providerFor(drf);

    if (!canRender(drm) || p == NULL) {
        DEBUG(() << "DefaultDragWorker: Cannot render the given RMF");
        return false;
    }

    Data::Request *req = d->requests.value(item->ulItemID);

    if (req) {
        // this item has been already prepared, ensure it has also been
        // rendered already
        Q_ASSERT(req->index == itemIndex);
        Q_ASSERT(req->rendered);
        if (req->index != itemIndex || !req->rendered)
            return false;
        // remove the old request to free resources
        delete d->requests.take(item->ulItemID);
    }

    // store the request
    req = new Data::Request(itemIndex, p, drm, drf);
    d->requests.insert(item->ulItemID, req);

    return true;
}

/*!
    \reimp
*/
void QPMMime::DefaultDragWorker::defaultFileType(QString &type,
                                                 QString &ext)
{
    Q_ASSERT(d->providers.count());
    if (d->providers.count()) {
        Provider *p = d->providers.first().prov;
        Q_ASSERT(p);
        if (p)
            p->fileType(d->providers.first().drf, type, ext);
    }
}

MRESULT QPMMime::DefaultDragWorker::Data::message(ULONG msg, MPARAM mp1, MPARAM mp2)
{
    if (msg == DM_RENDER) {
        DRAGTRANSFER *xfer = (DRAGTRANSFER *) mp1;

        // sanity checks
        Q_ASSERT(isInitialized());
        Q_ASSERT(xfer);
        if (!isInitialized() || !xfer)
            return (MRESULT)FALSE;

        Q_ASSERT(xfer->hwndClient && xfer->pditem);
        if (!xfer->hwndClient || !xfer->pditem)
            return (MRESULT)FALSE;

        Data::Request *req = requests.value(xfer->pditem->ulItemID);

        // check that this item has been prepared (should always be the case
        // because the target would never know our hwnd() otherwise)
        Q_ASSERT(req); // prepared
        Q_ASSERT(!req->xfer); // no DM_RENDER requested
        if (!req || req->xfer)
            return (MRESULT)FALSE;

        DEBUG(() << "DefaultDragWorker: Got DM_RENDER to"
                 << queryHSTR(xfer->hstrSelectedRMF) << "for item" << req->index
                 << "( id " << xfer->pditem->ulItemID << ")");

        QByteArray drm, drf;
        if (!parseRMF(xfer->hstrSelectedRMF, drm, drf))
            Q_ASSERT(/* parseRMF() = */ FALSE);

        if (req->drm != drm || req->drf != drf) {
            xfer->fsReply = DMFL_RENDERRETRY;
            return (MRESULT)FALSE;
        }

        // indicate that DM_RENDER was requested
        req->xfer = xfer;

        DEBUG(() << "DefaultDragWorker: Will render from ["
                 << req->provider->format(drf) << "] using provider"
                 << req->provider);

        // We would like to post WM_USER to ourselves to do actual rendering
        // after we return from DM_RENDER. But we are inside DrgDrag() at this
        // point (our DND implementation is fully synchronous by design), so
        // PM will not  deliver this message to us until we return from
        // DrgDrag(). Thus, we have to send it.

        WinSendMsg(hwnd(), WM_USER,
                   MPFROMLONG(xfer->pditem->ulItemID), MPFROMP(req));

        return (MRESULT)TRUE;
    }

    if (msg == WM_USER) {
        // sanity checks
        Q_ASSERT(isInitialized());
        if (!isInitialized())
            return (MRESULT)FALSE;

        ULONG itemId = LONGFROMMP(mp1);

        // sanity checks
        Data::Request *req = requests.value(itemId);
        Q_ASSERT(req); // prepared
        Q_ASSERT(req->xfer != NULL); // DM_RENDER requested
        Q_ASSERT(!req->rendered); // not yet rendered
        Q_ASSERT((Data::Request *) PVOIDFROMMP(mp2) == req);
        if (!req || req->xfer == NULL || req->rendered ||
             (Data::Request *) PVOIDFROMMP(mp2) != req)
            return (MRESULT)FALSE;

        Q_ASSERT(q->source() && req->provider && req->index < itemCnt);
        if (!q->source() || !req->provider || req->index >= itemCnt)
            return (MRESULT)FALSE;

        DEBUG(() << "DefaultDragWorker: Got DO_RENDER for item " << req->index
                 << "( id " << req->xfer->pditem->ulItemID << ")"
                 << "provider"<< req->provider << "drm" << req->drm.data()
                 << "drf" << req->drf.data());

        bool ok = false;

        QByteArray allData = q->source()->data(req->provider->format(req->drf));
        QByteArray itemData;

        ok = req->provider->provide(req->drf, allData, req->index, itemData);

        if (ok) {
            enum DRM { OS2File, SharedMem } drmType;
            if (qstrcmp(req->drm, "DRM_SHAREDMEM") == 0) drmType = SharedMem;
            else drmType = OS2File;

            if (drmType == OS2File) {
                QByteArray renderToName = queryHSTR(req->xfer->hstrRenderToName);
                Q_ASSERT(!renderToName.isEmpty());
                ok = !renderToName.isEmpty();
                if (ok) {
                    DEBUG(() << "DefaultDragWorker: Will write to" << renderToName);
                    QFile file(QFile::decodeName(renderToName));
                    ok = file.open(QIODevice::WriteOnly);
                    if (ok) {
                        qint64 written = file.write(itemData, itemData.size());
                        ok = written == itemData.size();
                        file.close();
                        if (ok && req->xfer->pditem->hstrType) {
                            // since WPS ignores hstrType, write it manually
                            // to the .TYPE EA of the created file
                            qt_SetFileTypeEA(renderToName,
                                queryHSTR(req->xfer->pditem->hstrType));
                        }
                    }
                }
            } else {
                PID pid;
                TID tid;
                bool isSameProcess = false;
                ok = WinQueryWindowProcess(req->xfer->hwndClient, &pid, &tid);
                if (ok) {
                    PPIB ppib = NULL;
                    DosGetInfoBlocks(NULL, &ppib);
                    isSameProcess = ppib->pib_ulpid == pid;

                    ULONG sz = itemData.size() + sizeof (ULONG);
                    char *ptr = NULL;
                    APIRET rc = isSameProcess ?
                                DosAllocMem((PPVOID) &ptr, sz,
                                            PAG_COMMIT | PAG_READ | PAG_WRITE) :
                                DosAllocSharedMem((PPVOID) &ptr, NULL, sz,
                                                  OBJ_GIVEABLE | PAG_COMMIT |
                                                  PAG_READ | PAG_WRITE);
                    ok = rc == 0;
                    if (ok && !isSameProcess) {
                        rc = DosGiveSharedMem(ptr, pid, PAG_READ);
                        ok = rc == 0;
                    }
                    if (ok) {
                        *(ULONG *) ptr = itemData.size();
                        memcpy(ptr + sizeof (ULONG), itemData.data(),
                               itemData.size());
                        req->xfer->hstrRenderToName = (HSTR) ptr;
                        req->sharedMem = ptr;
                        DEBUG(() << "DefaultDragWorker: Created shared memory "
                                    "object" << (void *)ptr);
#ifndef QT_NO_DEBUG
                    } else {
                        qWarning("DefaultDragWorker: DosAllocSharedMem/"
                                 "DosGiveSharedMem failed with %ld", rc);
#endif
                    }
#ifndef QT_NO_DEBUG
                } else {
                    qWarning("DefaultDragWorker: WinQueryWindowProcess failed"
                             "with 0x%lX", WinGetLastError(NULLHANDLE));
#endif
                }
            }
        }

        req->rendered = true;
        // cumulative render result
        renderOk &= ok;

        DEBUG(() << "DefaultDragWorker: ok" << ok
                 << "overall.renderOk" << renderOk);

        // note that we don't allow the target to retry
        USHORT reply = ok ? DMFL_RENDEROK : DMFL_RENDERFAIL;
        DrgPostTransferMsg(req->xfer->hwndClient, DM_RENDERCOMPLETE,
                           req->xfer, reply, 0, false);

        // DRAGTRANSFER is no more necessary, free it early
        qt_DrgFreeDragtransfer(req->xfer);
#if defined(QT_DEBUG_DND)
        {
            ULONG size = ~0, flags = 0;
            DosQueryMem(req->xfer, &size, &flags);
            DEBUG(("DefaultDragWorker: Freed DRAGTRANSFER: "
                   "req->xfer %p size %lu (0x%08lX) flags 0x%08lX",
                   req->xfer, size, size, flags));
        }
#endif
        req->xfer = NULL;

        return (MRESULT)FALSE;
    }

    if (msg == DM_ENDCONVERSATION) {
        // we don't check that isInitialized() is true here, because WPS
        // (and probably some other apps) may send this message after
        // cleanup() is called up on return from DrgDrag

        ULONG itemId = LONGFROMMP(mp1);
        ULONG flags = LONGFROMMP(mp2);

        // sanity check (don't assert, see above)
        Data::Request *req = requests.value(itemId);
        Q_ASSERT(req);
        if (!req)
            return (MRESULT)FALSE;

        DEBUG(() << "DefaultDragWorker: Got DM_ENDCONVERSATION for item" << req->index
                 << "(id " << itemId << ") provider" << req->provider
                 << "drm" << req->drm << "drf" << req->drf
                 << "rendered" << req->rendered << "outdated" << !isInitialized());

        // proceed further only if it's not an outdated request
        // from the previous DND session
        if (isInitialized()) {
            if (!req->rendered) {
                // we treat cancelling the render request (for any reason)
                // as a failure
                renderOk = false;
            } else {
                // the overall success is true only if target says Okay
                renderOk &= flags == DMFL_TARGETSUCCESSFUL;
            }
        }

        // delete the request
        delete requests.take(itemId);

        return (MRESULT)FALSE;
    }

    return (MRESULT)FALSE;
}

/*!
    Adds \a provider that understands the \a drf format and provides \a itemCnt
    items to the list of providers.

    Returns \c true on success or \c false otherwise (e.g. on attempts to add
    more than one a provider to the exclusive worker or to add a provider
    supporting a different count of items compared to providers already in the
    list).
*/
bool QPMMime::DefaultDragWorker::addProvider(const QByteArray &drf, Provider *provider,
                                             ULONG itemCnt /* = 1 */)
{
    // make sure remaining requests from the previous DND session are deleted
    d->cleanupRequests();

    Q_ASSERT(!drf.isEmpty() && provider && itemCnt >= 1);
    if (!drf.isEmpty() && provider && itemCnt >= 1) {
        if (d->providers.count() == 0) {
            // first provider
            d->itemCnt = itemCnt;
            d->providers.append(Data::DrfProvider(drf, provider));
            return true;
        }
        // next provider, must not be exclusive and itemCnt must match
        if (!d->exclusive && d->itemCnt == itemCnt) {
            // ensure there are no dups (several providers for the same drf)
            if (!d->providerFor(drf))
                d->providers.append(Data::DrfProvider(drf, provider));
            return true;
        }
    }
    return false;
}

/*!
    Returns \c true if the default drag worker can render using the mechanism
    specified in \a drm.
*/
// static
bool QPMMime::DefaultDragWorker::canRender(const char *drm)
{
    return qstrcmp(drm, "DRM_SHAREDMEM") == 0 ||
           qstrcmp(drm, "DRM_OS2FILE") == 0;
}

//------------------------------------------------------------------------------

struct QPMMime::DefaultDropWorker::Data : public QPMObjectWindow
{
    struct MimeProvider
    {
        MimeProvider() : prov(NULL) {}
        MimeProvider(const QString &m, Provider *p) : mime(m), prov(p) {}
        QString mime;
        Provider *prov;
    };

    Data(DefaultDropWorker *worker) : q(worker) {}

    // QPMObjectWindow interface
    MRESULT message(ULONG msg, MPARAM mp1, MPARAM mp2);

    typedef QList<MimeProvider> MimeProviderList;

    Provider *providerFor(const QString &mime)
    {
        foreach (const MimeProvider &p, providers) {
            if (p.mime == mime)
                return p.prov;
        }
        return NULL;
    }

    DefaultDropWorker *q;

    bool exclusive : 1;
    MimeProviderList providers;

    bool sending_DM_RENDER : 1;
    bool got_DM_RENDERCOMPLETE : 1;
    USHORT flags_DM_RENDERCOMPLETE;

    QEventLoop eventLoop;
};

/*!
    Constructs a new default drop worker instance.
*/
QPMMime::DefaultDropWorker::DefaultDropWorker() : d(new Data(this))
{
    d->exclusive = false;
    d->sending_DM_RENDER = d->got_DM_RENDERCOMPLETE = false;
    d->flags_DM_RENDERCOMPLETE = 0;
}

/*!
    Destroys the instance.
*/
QPMMime::DefaultDropWorker::~DefaultDropWorker()
{
    delete d;
}

/*!
    \reimp
*/
void QPMMime::DefaultDropWorker::cleanup(bool isAccepted)
{
    if (d->eventLoop.isRunning()) {
#ifndef QT_NO_DEBUG
        qWarning("The previous drag source didn't post DM_RENDERCOMPLETE!\n"
                 "Contact the drag source developer.");
#endif
        d->eventLoop.exit(1);
    }

    d->providers.clear();
    d->exclusive = false;
    d->sending_DM_RENDER = d->got_DM_RENDERCOMPLETE = false;
    d->flags_DM_RENDERCOMPLETE = 0;
}

/*!
    \reimp
*/
bool QPMMime::DefaultDropWorker::isExclusive() const
{
    return d->exclusive;
}

/*!
    \reimp
*/
bool QPMMime::DefaultDropWorker::hasFormat(const QString &mimeType) const
{
    return d->providerFor(mimeType) != NULL;
}

/*!
    \reimp
*/
QStringList QPMMime::DefaultDropWorker::formats() const
{
    QStringList mimes;
    foreach(const Data::MimeProvider &p, d->providers)
        mimes << p.mime;
    return mimes;
}

static QByteArray composeTempFileName()
{
    QByteArray tmpDir =
        QFile::encodeName(QDir::toNativeSeparators(QDir::tempPath()));

    static bool srandDone = false;
    if (!srandDone) {
        srand(time(NULL));
        srandDone = true;
    }

    ULONG num = rand();
    enum { Attempts = 100 };
    int attempts = Attempts;

    QString tmpName;
    do {
        tmpName.sprintf("%s\\%08lX.tmp", tmpDir.constData(), num);
        if (!QFile::exists(tmpName))
            break;
        num = rand();
    } while (--attempts > 0);

    Q_ASSERT(attempts > 0);
    if (attempts <= 0)
        tmpName.clear();

    return QFile::encodeName(tmpName);
}

/*!
    \reimp
*/
QVariant QPMMime::DefaultDropWorker::retrieveData(const QString &mimeType,
                                                  QVariant::Type preferredType) const
{
    Q_UNUSED(preferredType);

    DEBUG(() << "DefaultDropWorker::retrieveData: mimeType" << mimeType);

    QVariant ret;

    Q_ASSERT(info());
    if (!info())
        return ret;

    ULONG itemCount = DrgQueryDragitemCount(info());
    Q_ASSERT(itemCount);
    if (!itemCount)
        return ret;

    Provider *provider = d->providerFor(mimeType);
    if (!provider)
        return ret;

    QByteArray drf = provider->drf(mimeType);
    Q_ASSERT(!drf.isEmpty());
    if (drf.isEmpty())
        return ret;

    // Note: Allocating and freeing DRAGTRANSFER structures is a real mess. It's
    // absolutely unclear how they can be reused for multiple items and/or render
    // requests. My practice shows, that they cannot be reused at all, especially
    // when the source and the target are the same process: if we have multiple
    // items and use the same DRAGTRANSFER for all of them, the source will call
    // DrgFreeDragtransfer() every time that will eventually destroy the memory
    // object before the target finishes to work with it, so that the next
    // DrgFreeDragtransfer() will generate a segfault in PMCTLS. Also note that
    // using a number > 1 as an argument to DrgAllocDragtransfer() won't help
    // because that will still allocate a single memory object. Thus, we will
    // always allocate a new struct per every item. It seems to work.

    QByteArray renderToName = composeTempFileName();
    HSTR hstrRenderToName = DrgAddStrHandle(renderToName);

    HSTR rmfOS2File =
        DrgAddStrHandle(QString().sprintf("<DRM_OS2FILE,%s>",
                                          drf.data()).toLocal8Bit());
    HSTR rmfSharedMem =
        DrgAddStrHandle(QString().sprintf("<DRM_SHAREDMEM,%s>",
                                          drf.data()).toLocal8Bit());

    MRESULT mrc;
    bool renderOk = false;

    DRAGTRANSFER *xfer = NULL;
    QByteArray srcFileName;

    QByteArray allData, itemData;

    DEBUG(() << "DefaultDropWorker::retrieveData: itemCount" << itemCount);

    for (ULONG i = 0; i < itemCount; ++i) {
        DRAGITEM *item = DrgQueryDragitemPtr(info(), i);
        Q_ASSERT(item);
        if (!item) {
            renderOk = false;
            break;
        }

        DEBUG(() << "DefaultDropWorker::retrieveData: item" << i
                 << "hstrRMF" << queryHSTR(item->hstrRMF));

        enum { None, OS2File, SharedMem } drm = None;
        bool needToTalk = true;

        // determine the mechanism to use (prefer DRM_SHAREDMEM)

        if (DrgVerifyRMF(item, "DRM_SHAREDMEM", drf) &&
             DrgVerifyRMF(item, "DRM_SHAREDMEM", "DRF_POINTERDATA"))
            drm = SharedMem;
        if (DrgVerifyRMF(item, "DRM_OS2FILE", drf)) {
            srcFileName = querySourceNameFull(item);
            // If the source provides the full file name, we prefer DRM_OS2FILE
            // even if there is also DRM_SHAREDMEM available because we don't
            // need to do any communication in this case at all. This will help
            // with some native drag sources (such as DragText) that cannot send
            // DM_RENDERCOMPLETE synchronously (before we return from DM_DROP)
            // and would hang otherwise.
            if (!srcFileName.isEmpty()) {
                needToTalk = false;
                drm = OS2File;
            } else if (drm == None) {
                srcFileName = renderToName;
                drm = OS2File;
            }
        }
        Q_ASSERT(drm != None);
        if (drm == None) {
            renderOk = false;
            break;
        }

        if (needToTalk) {
            // need to perform a conversation with the source,
            // allocate a new DRAGTRANSFER structure for each item
            xfer = DrgAllocDragtransfer(1);
            Q_ASSERT(xfer);
            if (!xfer) {
                renderOk = false;
                break;
            }

            xfer->cb = sizeof(DRAGTRANSFER);
            xfer->hwndClient = d->hwnd();
            xfer->ulTargetInfo = (ULONG) info();
            xfer->usOperation = info()->usOperation;

            xfer->pditem = item;
            if (drm == OS2File) {
                xfer->hstrSelectedRMF = rmfOS2File;
                xfer->hstrRenderToName = hstrRenderToName;
            } else {
                xfer->hstrSelectedRMF = rmfSharedMem;
                xfer->hstrRenderToName = 0;
            }

            DEBUG(() << "DefaultDropWorker: Will use"
                     << queryHSTR(xfer->hstrSelectedRMF) << "to render item" << item);

            mrc = (MRESULT)TRUE;
            if ((item->fsControl & DC_PREPARE) ||
                (item->fsControl & DC_PREPAREITEM)) {
                DEBUG(("DefaultDropWorker: Sending DM_RENDERPREPARE to 0x%08lX...",
                       info()->hwndSource));
                mrc = DrgSendTransferMsg(info()->hwndSource, DM_RENDERPREPARE,
                                         MPFROMP (xfer), 0);
                DEBUG(("DefaultDropWorker: Finisned sending DM_RENDERPREPARE\n"
                       " mrc %p xfer->fsReply 0x%08hX", mrc, xfer->fsReply));
                renderOk = (BOOL) mrc;
            }

            if ((BOOL) mrc) {
                DEBUG(("DefaultDropWorker: Sending DM_RENDER to 0x%08lX...",
                       item->hwndItem));
                d->sending_DM_RENDER = true;
                mrc = DrgSendTransferMsg(item->hwndItem, DM_RENDER,
                                         MPFROMP(xfer), 0);
                d->sending_DM_RENDER = false;
                DEBUG(("DefaultDropWorker: Finisned Sending DM_RENDER\n"
                       " mrc %p xfer->fsReply 0x%hX got_DM_RENDERCOMPLETE %d",
                       mrc, xfer->fsReply, d->got_DM_RENDERCOMPLETE));

                if (!(BOOL) mrc || d->got_DM_RENDERCOMPLETE) {
                    if (d->got_DM_RENDERCOMPLETE)
                        renderOk = (d->flags_DM_RENDERCOMPLETE & DMFL_RENDEROK);
                    else
                        renderOk = false;
                } else {
                    // synchronously wait for DM_RENDERCOMPLETE
                    DEBUG(() << "DefaultDropWorker: Waiting for DM_RENDERCOMPLETE...");
                    int result = d->eventLoop.exec();
                    DEBUG(("DefaultDropWorker: Finished waiting for "
                           "DM_RENDERCOMPLETE (result %d)\n"
                           " got_DM_RENDERCOMPLETE %d usFS 0x%hX",
                           result, d->got_DM_RENDERCOMPLETE, d->flags_DM_RENDERCOMPLETE));
                    Q_UNUSED(result);
                    // JFTR: at this point, cleanup() might have been called,
                    // as a result of either program exit or getting another
                    // DM_DRAGOVER (if the source app has crashed) before getting
                    // DM_RENDERCOMPLETE from the source. Use data members with
                    // care!
                    renderOk = d->got_DM_RENDERCOMPLETE &&
                               (d->flags_DM_RENDERCOMPLETE & DMFL_RENDEROK);
                }

                d->got_DM_RENDERCOMPLETE = false;
            }
        } else {
            DEBUG(() << "DefaultDropWorker: Source supports < DRM_OS2FILE,"
                     << drf << "> and provides a file" << srcFileName
                     << "for item" << item << "(no need to render)");
            renderOk = true;
        }

        if (renderOk) {
            if (drm == OS2File) {
                DEBUG(() << "DefaultDragWorker: Will read from" << srcFileName);
                QFile file(QFile::decodeName(srcFileName));
                renderOk = file.open(QIODevice::ReadOnly);
                if (renderOk) {
                    itemData = file.readAll();
                    renderOk = file.error() == QFile::NoError;
                    file.close();
                }
                if (needToTalk) {
                    // only delete the file if we provided it for rendering
                    bool ok = file.remove();
                    Q_ASSERT((ok = ok));
                    Q_UNUSED(ok);
                }
            } else {
                Q_ASSERT(xfer->hstrRenderToName);
                renderOk = xfer->hstrRenderToName != 0;
                if (renderOk) {
                    const char *ptr = (const char *) xfer->hstrRenderToName;
                    ULONG size = ~0;
                    ULONG flags = 0;
                    APIRET rc = DosQueryMem((PVOID) ptr, &size, &flags);
                    renderOk = rc == 0;
                    if (renderOk) {
                        DEBUG(("DefaultDropWorker: Got shared data %p size %lu "
                               "(0x%08lX) flags 0x%08lX", ptr, size, size, flags));
                        Q_ASSERT((flags & (PAG_COMMIT | PAG_READ | PAG_BASE)) ==
                                 (PAG_COMMIT | PAG_READ | PAG_BASE));
                        renderOk = (flags & (PAG_COMMIT | PAG_READ | PAG_BASE)) ==
                                   (PAG_COMMIT | PAG_READ | PAG_BASE);
#ifndef QT_NO_DEBUG
                    } else {
                        qWarning("DefaultDropWorker: DosQueryMem failed with %ld", rc);
#endif
                    }
                    if (renderOk) {
                        ULONG realSize = *(ULONG *) ptr;
                        DEBUG(() << "DefaultDropWorker: realSize" << realSize);
                        Q_ASSERT(realSize <= size);
                        renderOk = realSize <= size;
                        if (renderOk) {
                            itemData.resize(realSize);
                            memcpy(itemData.data(), ptr + sizeof(ULONG), realSize);
                        }
                    }
                    // free memory only if it is given by another process,
                    // otherwise DefaultDragWorker will free it
                    if (flags & PAG_SHARED)
                        DosFreeMem((PVOID) xfer->hstrRenderToName);
                }
            }
        }

        if (renderOk)
            renderOk = provider->provide(mimeType, i, itemData, allData);

        if (needToTalk) {
            // free the DRAGTRANSFER structure
            DrgFreeDragtransfer(xfer);
#if defined(QT_DEBUG_DND)
            {
                ULONG size = ~0, flags = 0;
                DosQueryMem(xfer, &size, &flags);
                DEBUG(("DefaultDropWorker: Freed DRAGTRANSFER: "
                       "xfer=%p, size=%lu(0x%08lX), flags=0x%08lX",
                       xfer, size, size, flags));
            }
#endif
            xfer = NULL;
        }

        if (!renderOk)
            break;
    }

    DEBUG(() << "DefaultDropWorker: renderOk" << renderOk);

    DrgDeleteStrHandle(rmfSharedMem);
    DrgDeleteStrHandle(rmfOS2File);
    DrgDeleteStrHandle(hstrRenderToName);

    if (renderOk)
        ret = allData;

    return ret;
}

MRESULT QPMMime::DefaultDropWorker::Data::message(ULONG msg, MPARAM mp1, MPARAM mp2)
{
    switch (msg) {
        case DM_RENDERCOMPLETE: {
            // sanity check
            Q_ASSERT(q->info());
            if (!q->info())
                return (MRESULT)FALSE;

            DEBUG(("DefaultDropWorker: Got DM_RENDERCOMPLETE"));
            got_DM_RENDERCOMPLETE = true;
            flags_DM_RENDERCOMPLETE = SHORT1FROMMP(mp2);

            if (sending_DM_RENDER)
            {
#ifndef QT_NO_DEBUG
                DRAGTRANSFER *xfer = (DRAGTRANSFER *) mp1;
                qWarning("Drag item 0x%08lX sent DM_RENDERCOMPLETE w/o first "
                         "replying to DM_RENDER!\n"
                         "Contact the drag source developer.",
                         xfer->pditem->hwndItem);
#endif
                return (MRESULT)FALSE;
            }

            // stop synchronous waiting for DM_RENDERCOMPLETE
            if (eventLoop.isRunning())
                eventLoop.exit();
            return (MRESULT)FALSE;
        }
        default:
            break;
    }

    return (MRESULT)FALSE;
}

/*!
    Adds \a provider that understands the \a mimeType format to the list of
    providers.

    Returns \c true on success or \c false otherwise (e.g. if the list already
    contains an exclusive provider or other providers supporting the given MIME
    type).
*/
bool QPMMime::DefaultDropWorker::addProvider(const QString &mimeType,
                                             Provider *provider)
{
    Q_ASSERT(!mimeType.isEmpty() && provider);
    if (!mimeType.isEmpty() && provider && !d->exclusive) {
        // ensure there are no dups (several providers for the same mime)
        if (!d->providerFor(mimeType))
            d->providers.append(Data::MimeProvider(mimeType, provider));
        return true;
    }
    return false;
}

/*!
    Adds \a provider that understands the \a mimeType format to the list of
    providers. The provider is marked as exclusive (responsive for converting
    the whole DRAGITEM structure).

    Returns \c true on success or \c false otherwise (e.g. if the list already
    contains an exclusive provider).
*/
bool QPMMime::DefaultDropWorker::addExclusiveProvider(const QString &mimeType,
                                                      Provider *provider)
{
    Q_ASSERT(!mimeType.isEmpty() && provider);
    if (!mimeType.isEmpty() && provider && !d->exclusive) {
        d->exclusive = true;
        d->providers.clear();
        d->providers.append(Data::MimeProvider(mimeType, provider));
        return true;
    }
    return false;
}

/*!
    Returns \c true if the given \a item can be rendered by the default drop
    worker can render in the \a drf rendering format.
*/
// static
bool QPMMime::DefaultDropWorker::canRender(DRAGITEM *item, const char *drf)
{
    return DrgVerifyRMF(item, "DRM_OS2FILE", drf) ||
           (DrgVerifyRMF(item, "DRM_SHAREDMEM", drf) &&
            DrgVerifyRMF(item, "DRM_SHAREDMEM", "DRF_POINTERDATA"));
}

/*! \internal

    Parses the rendering mechanism/format specification of the given \a item
    and stores only those mechanism branches in the given \a list that represent
    mechanisms supported by this worker. Returns false if fails to parse the
    RMF specification. Note that if no supported mechanisms are found, true is
    returned but the \a list will simply contain zero items.

    \note The method clears the given \a list variable before proceeding.

    \sa canRender(), PMMime::parseRMFs()
*/
// static
bool QPMMime::DefaultDropWorker::getSupportedRMFs(DRAGITEM *item,
                                                  QList<QByteArrayList> &list)
{
    if (!parseRMFs(item->hstrRMF, list))
        return false;

    for (QList<QByteArrayList>::iterator rmf = list.begin(); rmf != list.end();) {
        QByteArrayList::iterator mf = rmf->begin();
        Q_ASSERT(mf != rmf->end());
        const char *drm = *mf;
        if (qstrcmp(drm, "DRM_OS2FILE") == 0) {
            ++rmf;
            continue;
        }
        if (qstrcmp(drm, "DRM_SHAREDMEM") == 0) {
            // accept DRM_SHAREDMEM only if there is DRF_POINTERDATA
            for(; mf != rmf->end(); ++mf) {
                const char *drf = *mf;
                if (qstrcmp(drf, "DRF_POINTERDATA") == 0)
                    break;
            }
            if (mf != rmf->end()) {
                ++rmf;
                continue;
            }
        }
        // remove the unsupported mechanism branch from the list
        rmf = list.erase(rmf);
    }

    return true;
}

#endif // !QT_NO_DRAGANDDROP

//------------------------------------------------------------------------------

class QPMMimeList
{
public:
    QPMMimeList();
    ~QPMMimeList();
    void addMime(QPMMime *mime);
    void removeMime(QPMMime *mime);
    QList<QPMMime*> mimes();

private:
    void init();
    bool initialized;
    QList<QPMMime*> list;
};

Q_GLOBAL_STATIC(QPMMimeList, theMimeList);


/*!
    \class QPMMime
    \brief The QPMMime class maps open-standard MIME to OS/2 PM Clipboard
    formats.
    \ingroup io
    \ingroup draganddrop
    \ingroup misc

    Qt's drag-and-drop and clipboard facilities use the MIME standard.
    On X11, this maps trivially to the Xdnd protocol, but on OS/2
    although some applications use MIME types to describe clipboard
    formats, others use arbitrary non-standardized naming conventions,
    or unnamed built-in formats of the Presentation Manager.

    By instantiating subclasses of QPMMime that provide conversions between OS/2
    PM Clipboard and MIME formats, you can convert proprietary clipboard formats
    to MIME formats.

    Qt has predefined support for the following PM Clipboard formats (custom
    formats registered in the system atom table by name are given in double
    quotes):

    \table
    \header \o PM Format \o Equivalent MIME type
    \row \o \c CF_TEXT          \o \c text/plain (system codepage,
                                   zero-terminated string)
    \row \o \c "text/unicode"   \o \c text/plain (16-bit Unicode,
                                   zero-terminated string, Mozilla-compatible)
    \row \o \c "text/html"      \o \c text/html (16-bit Unicode,
                                   zero-terminated string, Mozilla-compatible)
    \row \o \c CF_BITMAP        \o \c{image/xyz}, where \c xyz is
                                   a \l{QImageWriter::supportedImageFormats()}{Qt image format}
    \row \o \c "x-mime:<mime>"  \o data in the format corresponding to the given
                                   MIME type \c <mime>
    \endtable

    Note that all "x-mime:<mime>" formats use the \c CFI_POINTER storage type.
    That is, the clipboard contains a pointer to the memory block containing the
    MIME data in the corresponding format. The first 4 bytes of this memory
    block always contain the length of the subsequent MIME data array, in bytes.

    An example use of this class by the user application would be to map the
    PM Metafile clipboard format (\c CF_METAFILE) to and from the MIME type
    \c{image/x-metafile}. This conversion might simply be adding or removing a
    header, or even just passing on the data. See \l{Drag and Drop} for more
    information on choosing and definition MIME types.
*/

/*!
Constructs a new conversion object, adding it to the globally accessed
list of available converters.
*/
QPMMime::QPMMime()
{
    theMimeList()->addMime(this);
}

/*!
Destroys a conversion object, removing it from the global
list of available converters.
*/
QPMMime::~QPMMime()
{
    theMimeList()->removeMime(this);
}

/*!
    Registers the MIME type \a mime, and returns an ID number
    identifying the format on OS/2. Intended to be used by QPMMime
    implementations for registering custom clipboard formats they use.
*/
// static
ULONG QPMMime::registerMimeType(const QString &mime)
{
    ULONG cf = WinAddAtom(WinQuerySystemAtomTable(), mime.toLocal8Bit());
    if (!cf) {
#ifndef QT_NO_DEBUG
        qWarning("QPMMime: WinAddAtom failed with 0x%lX",
                 WinGetLastError(NULLHANDLE));
#endif
        return 0;
    }

    return cf;
}

/*!
    Unregisters the MIME type identified by \a mimeId which was previously
    registered with registerMimeType().
*/
// static
void QPMMime::unregisterMimeType(ULONG mimeId)
{
    WinDeleteAtom(WinQuerySystemAtomTable(), mimeId);
}

/*!
    Returns a list of all currently defined QPMMime objects.
*/
// static
QList<QPMMime*> QPMMime::all()
{
    return theMimeList()->mimes();
}

/*!
    Allocates a block of shared memory of the given \a size and returns the
    address of this block. This memory block may be then filled with data and
    returned by convertFromMimeData() as the value of the \c CFI_POINTER type.
*/
// static
ULONG QPMMime::allocateMemory(size_t size)
{
    if (size == 0)
        return 0;

    ULONG data = 0;

    // allocate giveable memory for the array
    APIRET arc = DosAllocSharedMem((PVOID *)&data, NULL, size,
                                   PAG_WRITE  | PAG_COMMIT | OBJ_GIVEABLE);
    if (arc != NO_ERROR) {
#ifndef QT_NO_DEBUG
        qWarning("QPMMime::allocateMemory: DosAllocSharedMem failed with %lu", arc);
#endif
        return 0;
    }

    return data;
}

/*!
    Frees a memory block \a addr allocated by allocateMemory(). Normally, not
    used because the \c CFI_POINTER memory blocks are owned by the system after
    convertFromMimeData() returns.
*/
// static
void QPMMime::freeMemory(ULONG addr)
{
    DosFreeMem((PVOID)addr);
}

/*!
    \typedef QPMMime::QByteArrayList

    A QList of QByteArray elemetns.
*/

/*!
    \class QPMMime::MimeCFPair

    A pair of MIME type and clipboard format values.
*/

/*!
    \fn QPMMime::MimeCFPair::MimeCFPair(const QString &m, ULONG f)

    Construct a new pair given MIME format \a m and clipboard format \a f.
*/

/*!
    \variable QPMMime::MimeCFPair::mime

    MIME type.
*/

/*!
    \variable QPMMime::MimeCFPair::format

    PM clipboard format.
*/

/*!
    \fn QList<MimeCFPair> QPMMime::formatsForMimeData(const QMimeData *mimeData) const

    Returns a list of ULONG values representing the different OS/2 PM
    clipboard formats that can be provided for the \a mimeData, in order of
    precedence (the most suitable format goes first), or an empty list if
    neither of the mime types provided by \a mimeData is supported by this
    converter. Note that each item in the returned list is actually a pair
    consisting of the mime type name and the corresponding format identifier.

    All subclasses must reimplement this pure virtual function.
*/

/*!
    \fn bool QPMMime::convertFromMimeData(const QMimeData *mimeData, ULONG format,
                                          ULONG &flags, ULONG *data) const

    Converts the \a mimeData to the specified \a format.

    If \a data is not NULL, a handle to the converted data should be then placed
    in a variable pointed to by \a data and with the necessary flags describing
    the handle returned in the \a flags variable.

    The following flags describing the data storage type are recognized:

    \table
    \row \o \c CFI_POINTER        \o \a data is a pointer to a block of memory
                                      allocated with QPMMime::allocateMemory()
    \row \o \c CFI_HANDLE         \o \a data is a handle to the appropriate
                                      PM resource
    \endtable

    If \a data is NULL then a delayed conversion is requested by the caller.
    The implementation should return the appropriate flags in the \a flags
    variable and may perform the real data conversion later when this method is
    called again with \a data being non-NULL.

    Return true if the conversion was successful.

    All subclasses must reimplement this pure virtual function.
*/

/*!
    \fn QList<MimeCFPair> QPMMime::mimesForFormats(const QList<ULONG> &formats) const

    Returns a list of mime types that will be created form the specified list of
    \a formats, in order of precedence (the most suitable mime type comes
    first), or an empty list if neither of the \a formats is supported by this
    converter. Note that each item in the returned list is actually a pair
    consisting of the mime type name and the corresponding format identifier.

    All subclasses must reimplement this pure virtual function.
*/

/*!
    \fn QVariant QPMMime::convertFromFormat(ULONG format, ULONG flags, ULONG data,
                                            const QString &mimeType,
                                            QVariant::Type preferredType) const

    Returns a QVariant containing the converted from the \a data in the
    specified \a format with the given \a flags to the requested \a mimeType. If
    possible the QVariant should be of the \a preferredType to avoid needless
    conversions.

    All subclasses must reimplement this pure virtual function.
*/

/*!
    \fn DragWorker *QPMMime::dragWorkerFor(const QString &mimeType,
                                           QMimeData *mimeData)

    Returns a DragWorker instance suitable for converting \a mimeType of the
    given \a mimeData to a set of drag items for the Direct Manipulation (Drag
    And Drop) session. If this converter does not support the given MIME type,
    this method should return 0.

    See the QPMMime::DragWorker class description for more information.

    The default implementation of this method returns 0.
*/

/*!
    \fn DropWorker *QPMMime::dropWorkerFor(DRAGINFO *info)

    Returns a DropWorker instance suitable for converting drag items represented
    by the \a info structure to MIME data when these items are dropped on a Qt
    widget at the end of the Direct manipulation session. If this converter does
    not support the given set of drag items, this method should return 0.

    See the QPMMime::DropWorker class description for more information.

    The default implementation of this method returns 0.
*/

/*!
    \class QPMMime::DragWorker

    This class is responsible for providing the drag items for the Direct
    Manipulation session.

    Drag workers can be super exclusive (solely responsible for converting the
    given mime type to a set of DRAGITEM structures), exclusive (cannot coexist
    with other workers but don't manage the DRAGINFO/DRAGITEM creation), or
    cooperative (can coexist with other drag workers and share the same set of
    DRAGITEM structures in order to represent different mime data types). As
    opposed to super exclusive workers (identified by isExclusive() returning
    \c true and by itemCount() returning 0), exclusive and cooperative workers
    do not create DRAGINFO/DRAGITEM structures on their own, they implement a
    subset of methods that is used by the drag manager to fill drag structures
    it creates.

    If a super exlusive or an exclusive worker is encoundered when starting the
    drag session, it will be used only if there are no any other workers found
    for \b other mime types of the object being dragged. If a cooperative worker
    with the item count greater than one is encountered, it will be used only if
    all other found workers are also cooperative and require the same number of
    items. In both cases, if the above conditions are broken, the respective
    workers are discarded (ignored). Instead, a special fall-back cooperative
    worker (that requires a single DRAGITEM, supports any mime type and can
    coexist with other one-item cooperative workers) will be used for the given
    mime type.

    \note Every exclusive drag worker must implement createDragInfo() and must
    not implement composeFormatSting()/prepare()/defaultFileType(). And vice
    versa, every cooperative drag worker must implement the latter three
    functions but not the former two.
*/

/*!
    \fn QPMMime::DragWorker::DragWorker()

    Constructs a new instance.
*/

/*!
    \fn QPMMime::DragWorker::~DragWorker()

    Destroys the instance.
*/

/*!
    \fn QPMMime::DragWorker::source() const

    Returns the source of the current drag operation.
*/

/*!
    \fn QPMMime::DragWorker::init()

    Initializes the instance before the drag operation.
*/

/*!
    \fn QPMMime::DragWorker::cleanup(bool isCancelled)

    Performs cleanup after the drag operation. If the drag operation was
    cancelled, \a isCancelled will be \c true.

    Returns \c true if the Move operation is disallowed by this worker. Note
    that if this worker doesn't participate in a given DnD session, it should
    return \c false to let other workers allow Move.

    Must be reimplemented in subclasses.
*/

/*!
    \fn QPMMime::DragWorker::isExclusive() const

    Returns \c true if this is an exclusive drag worker.

    Must be reimplemented in subclasses.
*/

/*!
    \fn QPMMime::DragWorker::itemCount() const

    Returns the number of DRAGITEM elements this drag worker needs to represent
    its data. If isExclusive() is \c true and this method returns 0, this drag
    worker will manage creation of the DRAGINFO structure on its onwn.

    Must be reimplemented in subclasses.
*/

/*!
    \fn QPMMime::DragWorker::hwnd() const

    Returns a window handle to be associated with DRAGITEM elemetns of this drag
    worker.

    Must be reimplemented in subclasses.
*/

/*!
    \fn QPMMime::DragWorker::createDragInfo(const QString &targetName, USHORT
                                            supportedOps)

    Creates a new DRAGINFO structure for the drag operation with the suggested
    \a targetName and supported operations represented by \a supportedOps.

    \note The created structure is owned by QPMMime and should be not freed by
    subclasses.

    Must be reimplemented in subclasses if the isExclusive() implementation
    returns \c true and itemCount() returns 0.
*/

/*!
    \fn QPMMime::DragWorker::composeFormatString()

    Returns a format string containing "<mechanism,format>" pairs supported by
    this drag worker.

    Must be reimplemented in subclasses if the isExclusive() implementation
    returns \c false.
*/

/*!
    \fn QPMMime::DragWorker::prepare(const char *drm, const char *drf,
                                     DRAGITEM *item, ULONG itemIndex)

    Prepares this worker for the drop operation of the \a item having
    \a itemIndex using the mechanism and format specified in \a drm and \a drf,
    respectively.

    Must be reimplemented in subclasses if the isExclusive() implementation
    returns \c false.
*/

/*!
    \fn QPMMime::DragWorker::defaultFileType(QString &type, QString &ext)

    Returns the default file \a type (and extension \a ext) for the data
    represented by this worker.

    Must be reimplemented in subclasses if the isExclusive() implementation
    returns \c false.
*/

/*!
    \class QPMMime::DefaultDragWorker

    This class is a DragWorker implementation that supports standard
    \c DRM_SHAREDMEM and \c DRM_OS2FILE and rendering mechanisms. It uses
    QPMMime::DefaultDragWorker::Provider subclasses to map mime types of the
    object being dragged to rendering formats and apply preprocessing of data
    before rendering.
*/

/*!
  \class QPMMime::DefaultDragWorker::Provider

    This class is used to map mime types of the object being dragged to
    rendering formats and apply preprocessing of data before rendering.
*/

/*!
    \fn QPMMime::DefaultDragWorker::Provider::format(const char *drf) const

    Returns a MIME format string for the given \a drf rendering format that this
    provider can provide. Returns a null string if the format is unsupported.
*/

/*!
    \fn QPMMime::DefaultDragWorker::Provider::provide(const char *drf,
                                                      const QByteArray &allData,
                                                      ULONG itemIndex,
                                                      QByteArray &itemData)

    If the \a drf format is supported by this provider, converts \a allData to
    it, stores the result in \a itemData and returns \c true. In case of
    multi-item data, \a itemIndex is an index of the item in question.
*/

/*!
    \fn QPMMime::DefaultDragWorker::Provider::fileType(const char *drf, QString
                                                       &type, QString &ext)

    If the \a drf format is supported by this provider, returns the file type in
    \a type and the extension in \a ext for the data it provides.
*/

/*!
  \class QPMMime::DropWorker

    This class is responsible for interpreting the drag items after the Direct
    Manipulation session ends up in a drop.

    Drop workers can be exclusive (solely responsible for converting the given
    set of DRAGITEM structures) or cooperative (can coexist with other drop
    workers in order to produce different mime data types from the same set of
    DRAGITEM structures). If an exclusive drop worker is encountered when
    processing the drop event, all other workers are silently ignored.

    \note Subclasses must \b not send \c DM_ENDCONVERSATION to the source.
*/

/*!
    \fn QPMMime::DropWorker::DropWorker()

    Constructs a new instance.
*/

/*!
    \fn QPMMime::DropWorker::~DropWorker()

    Destroys the instance.
*/

/*!
    \fn QPMMime::DropWorker::info() const

    Returns a pointer to the DRAGINFO sctructure describing the current
    drag operation.

    \note Subclasses must \b not free this DRAGINFO structure.
*/

/*!
    \fn QPMMime::DropWorker::init()

    Initializes the instance before the drop operation.
*/

/*!
    \fn QPMMime::DropWorker::cleanup(bool isAccepted)

    Performs the cleanup after the drop operation. If the drag was accepted,
    \a isAccepted will be \c true.
*/

/*!
    \fn QPMMime::DropWorker::isExclusive() const

    Returns \c true if this is an exclusive drop worker.

    Must be reimplemented in subclasses.
*/

/*!
    \fn QPMMime::DropWorker::hasFormat(const QString &mimeType) const

    Returns \c true if this drop worker supports the given \a mimeType.

    Must be reimplemented in subclasses.
*/

/*!
    \fn QPMMime::DropWorker::formats() const

    Returns a list of all MIME types supported by this drop worker.

    Must be reimplemented in subclasses.
*/

/*!
    \fn QPMMime::DropWorker::retrieveData(const QString &mimeType,
                                          QVariant::Type preferredType) const

    Returns data represented by this drag worker converted to the given
    \a mimeType. The QVariant type preferred by the caller is indicated by
    \a preferredType.

    Must be reimplemented in subclasses.
*/

/*!
    \class QPMMime::DefaultDropWorker

    This class is a DropWorker implementation that supports standard
    \c DRM_SHAREDMEM and \c DRM_OS2FILE and rendering mechanisms. It uses
    QPMMime::DefaultDropWorker::Provider subclasses to map various rendering
    formats to particular mime types and apply postprocessing of data after
    rendering.
*/

/*!
    \class QPMMime::DefaultDropWorker::Provider

    This class is used to map rendering formats to particular mime and apply
    preprocessing of data after rendering.
*/

/*!
    \fn QPMMime::DefaultDropWorker::Provider::drf(const QString &mimeType) const

    Returns a rendering format string for the given \a mimeType that this
    provider can provide. Returns a null string if the MIME type is unsupported.
*/

/*!
    \fn QPMMime::DefaultDropWorker::Provider::provide(const QString &mimeType,
    ULONG itemIndex, const QByteArray &itemData, QByteArray &allData)

    If the \a mimeType is supported by this provider, converts \a itemData to
    it, stores the result in \a allData and returns \c true. In case of
    multi-item data, \a itemIndex is an index of the item in question.
*/

// static
QList<QPMMime::Match> QPMMime::allConvertersFromFormats(const QList<ULONG> &formats)
{
    QList<Match> matches;

    QList<QPMMime*> mimes = theMimeList()->mimes();
    foreach(QPMMime *mime, mimes) {
        QList<MimeCFPair> fmts = mime->mimesForFormats(formats);
        int priority = 0;
        foreach (MimeCFPair fmt, fmts) {
            ++priority;
            QList<Match>::iterator it = matches.begin();
            for (; it != matches.end(); ++it) {
                Match &match = *it;
                if (match.mime == fmt.mime) {
                    // replace if priority is higher, ignore otherwise
                    if (priority < match.priority) {
                        match.converter = mime;
                        match.format = fmt.format;
                        match.priority = priority;
                    }
                    break;
                }
            }
            if (it == matches.end()) {
                matches += Match(mime, fmt.mime, fmt.format, priority);
            }
        }
    }

    return matches;
}

// static
QList<QPMMime::Match> QPMMime::allConvertersFromMimeData(const QMimeData *mimeData)
{
    QList<Match> matches;

    QList<QPMMime*> mimes = theMimeList()->mimes();
    foreach(QPMMime *mime, mimes) {
        QList<MimeCFPair> fmts = mime->formatsForMimeData(mimeData);
        int priority = 0;
        foreach (MimeCFPair fmt, fmts) {
            ++priority;
            QList<Match>::iterator it = matches.begin();
            for (; it != matches.end(); ++it) {
                Match &match = *it;
                if (mime == mimes.last()) { // QPMMimeAnyMime?
                    if (match.mime == fmt.mime){
                        // we assume that specialized converters (that come
                        // first) provide a more precise conversion than
                        // QPMMimeAnyMime and don't let it get into the list in
                        // order to avoid unnecessary duplicate representations
                        break;
                    }
                }
                if (match.format == fmt.format) {
                    // replace if priority is higher, ignore otherwise
                    if (priority < match.priority) {
                        match.converter = mime;
                        match.mime = fmt.mime;
                        match.priority = priority;
                    }
                    break;
                }
            }
            if (it == matches.end()) {
                matches += Match(mime, fmt.mime, fmt.format, priority);
            }
        }
    }

    return matches;
}


/*!
    Returns a string representation of the given clipboard \a format. The
    string representation is obtained by querying the system atom table.
*/
// static
QString QPMMime::formatName(ULONG format)
{
    QString name;
    HATOMTBL tbl = WinQuerySystemAtomTable();
    if (tbl != NULLHANDLE) {
        ULONG len = WinQueryAtomLength(tbl, format);
        QByteArray atom(len, '\0');
        WinQueryAtomName(tbl, format, atom.data(), atom.size() + 1);
        name = QString::fromLocal8Bit(atom);
    }
    return name;
}

#if !defined(QT_NO_DRAGANDDROP)

/*!
    Returns a string represented by \a hstr.
*/
// static
QByteArray QPMMime::queryHSTR(HSTR hstr)
{
    QByteArray str;
    ULONG len = DrgQueryStrNameLen(hstr);
    if (len) {
        str.resize(len);
        DrgQueryStrName(hstr, str.size() + 1 /* \0 */, str.data());
    }
    return str;
}

/*!
    Returns a string that is a concatenation of \c hstrContainerName and
    \c hstrSourceName fileds of the given \a item structure.
*/
// static
QByteArray QPMMime::querySourceNameFull(DRAGITEM *item)
{
    QByteArray fullName;
    if (!item)
        return fullName;

    ULONG pathLen = DrgQueryStrNameLen(item->hstrContainerName);
    ULONG nameLen = DrgQueryStrNameLen(item->hstrSourceName);
    if (!pathLen || !nameLen)
        return fullName;

    // Take into account that the container name may lack the trailing slash
    fullName.resize(pathLen + nameLen + 1);

    DrgQueryStrName(item->hstrContainerName, pathLen + 1, fullName.data());
    if (fullName.at(pathLen - 1) != '\\') {
        fullName[(size_t)pathLen] = '\\';
        ++pathLen;
    }

    DrgQueryStrName(item->hstrSourceName, nameLen + 1, fullName.data() + pathLen);

    fullName.truncate(qstrlen(fullName));

    return fullName;
}

/*!
    Checks that the given drag \a item supports the \c DRM_OS2FILE rendering
    mechanism and can be rendered by a target w/o involving the source (i.e.,
    \c DRM_OS2FILE is the first supported format and a valid file name with full
    path is provided). If the function returns \c true, \a fullName (if not
    \c NULL) will be assigned the item's full source file name (composed from
    \c hstrContainerName and \c hstrSourceName fields).
 */
// static
bool QPMMime::canTargetRenderAsOS2File(DRAGITEM *item, QByteArray *fullName /*= 0*/)
{
    if (!item)
        return false;

    if (item->fsControl & (DC_PREPARE | DC_PREPAREITEM))
        return false;

    {
        // DrgVerifyNativeRMF doesn't work on my system (ECS 1.2.1 GA):
        // it always returns FALSE regardless of arguments. Use simplified
        // hstrRMF parsing to determine whether \c DRM_OS2FILE is the native
        // mechanism or not (i.e. "^\s*[\(<]\s*DRM_OS2FILE\s*,.*").

        QByteArray rmf = queryHSTR(item->hstrRMF);
        bool ok = false;
        int i = rmf.indexOf("DRM_OS2FILE");
        if (i >= 1) {
            for (int j = i - 1; j >= 0; --j) {
                char ch = rmf[j];
                if (ch == ' ')
                    continue;
                if (ch == '<' || ch == '(') {
                    if (ok)
                        return false;
                    ok = true;
                } else {
                    return false;
                }
            }
        }
        if (ok) {
            ok = false;
            int drmLen = strlen("DRM_OS2FILE");
            for (int j = i + drmLen; j < rmf.size(); ++j) {
                char ch = rmf[j];
                if (ch == ' ')
                    continue;
                if (ch == ',') {
                    ok = true;
                    break;
                }
                return false;
            }
        }
        if (!ok)
            return false;
    }

    QByteArray srcFullName = querySourceNameFull(item);
    if (srcFullName.isEmpty())
        return false;

    QByteArray srcFullName2(srcFullName.size(), '\0');
    APIRET rc = DosQueryPathInfo(srcFullName, FIL_QUERYFULLNAME,
                                 srcFullName2.data(), srcFullName2.size() + 1);
    if (rc != 0)
        return false;

    QString s1 = QFile::decodeName(srcFullName);
    QString s2 = QFile::decodeName(srcFullName2);

    if (s1.compare(s2, Qt::CaseInsensitive) != 0)
        return false;

    if (fullName)
        *fullName = srcFullName;
    return true;
}

/*!
    Parses the given \a rmfs list (the full rendering mechanism/format
    specification) and builds a \a list of mechanism branches. Each mechanism
    branch is also a list, where the first item is the mechahism name and all
    subsequent items are formats supported by this mechanism. Returns false if
    fails to parse \a rmfs.

    \note The method clears the given \a list variable before proceeding.
*/
// static
bool QPMMime::parseRMFs(HSTR rmfs, QList<QByteArrayList> &list)
{
    // The format of the RMF list is "elem {,elem,elem...}"
    // where elem is "(mechanism{,mechanism...}) x (format{,format...})"
    // or "<mechanism,format>".
    // We use a simple FSM to parse it. In terms of FSM, the format is:
    //
    // STRT ( BCM m CMCH echanism CMCH , NCM m CMCH echanism CMCH ) ECM x
    //     SCMF ( BCF f CFCH ormat CFCH , NCF f CFCH ormat CFCH ) ECF , STRT
    // STRT < BP m PMCH echanism PMCH , SPMF f PFCH ormat PFCH > EP , STRT

    QByteArray str = queryHSTR(rmfs);
    uint len = str.length();

    enum {
        // states
        STRT = 0, BCM, CMCH, NCM, ECM, SCMF, BCF, CFCH, NCF, ECF,
        BP, PMCH, SPMF, PFCH, EP,
        STATES_COUNT,
        // pseudo states
        Err, Skip,
        // inputs
        obr = 0, cbr, xx, lt, gt, cm, any, ws,
        INPUTS_COUNT,
    };

    static const char Chars[] =  { '(', ')', 'x', 'X', '<', '>', ',', ' ', 0 };
    static const char Inputs[] = { obr, cbr, xx,  xx,  lt,  gt,  cm,  ws };
    static const uchar Fsm [STATES_COUNT] [INPUTS_COUNT] = {
             /* 0 obr  1 cbr  2 xx   3 lt   4 gt   5 cm   6 any  7 ws */
/* STRT 0  */ { BCM,   Err,   Err,   BP,    Err,   Err,   Err,   Skip },
/* BCM  1  */ { Err,   Err,   Err,   Err,   Err,   Err,   CMCH,  Skip },
/* CMCH 2  */ { Err,   ECM,   CMCH,  Err,   Err,   NCM,   CMCH,  CMCH },
/* NCM  3  */ { Err,   Err,   Err,   Err,   Err,   Err,   CMCH,  Skip },
/* ECM  4  */ { Err,   Err,   SCMF,  Err,   Err,   Err,   Err,   Skip },
/* SCMF 5  */ { BCF,   Err,   Err,   Err,   Err,   Err,   Err,   Skip },
/* BCF  6  */ { Err,   Err,   Err,   Err,   Err,   Err,   CFCH,  Skip },
/* CFCH 7  */ { Err,   ECF,   CFCH,  Err,   Err,   NCF,   CFCH,  CFCH },
/* NCF  8  */ { Err,   Err,   Err,   Err,   Err,   Err,   CFCH,  Skip },
/* ECF  9  */ { Err,   Err,   Err,   Err,   Err,   STRT,  Err,   Skip },
/* BP   10 */ { Err,   Err,   Err,   Err,   Err,   Err,   PMCH,  Skip },
/* PMCH 11 */ { Err,   Err,   PMCH,  Err,   Err,   SPMF,  PMCH,  PMCH  },
/* SPMF 12 */ { Err,   Err,   Err,   Err,   Err,   Err,   PFCH,  Skip },
/* PFCH 13 */ { Err,   Err,   PFCH,  Err,   EP,    Err,   PFCH,  PFCH },
/* EP   14 */ { Err,   Err,   Err,   Err,   Err,   STRT,  Err,   Skip }
    };

    list.clear();

    QList<QByteArrayList*> refList;

    QByteArray buf;
    QList<QByteArrayList>::iterator rmf;

    uint state = STRT;
    uint start = 0, end = 0, space = 0;

    for (uint i = 0; i < len && state != Err ; ++i) {
        char ch = str[i];
        char *p = strchr(Chars, ch);
        uint input = p ? Inputs[p - Chars] : any;
        uint newState = Fsm[state][input];
        switch (newState) {
            case Skip:
                continue;
            case CMCH:
            case CFCH:
            case PMCH:
            case PFCH:
                if (state != newState)
                    start = end = i;
                ++end;
                // accumulate trailing space for truncation
                if (input == ws) ++space;
                else space = 0;
                break;
            case NCM:
            case ECM:
            case SPMF:
                buf = QByteArray(str.data() + start, end - start - space);
                // find the mechanism branch in the output list
                for (rmf = list.begin(); rmf != list.end(); ++rmf) {
                    if (rmf->first() == buf)
                        break;
                }
                if (rmf == list.end()) {
                    // append to the output list if not found
                    QByteArrayList newRmf;
                    newRmf.append(buf);
                    rmf = list.insert(list.end(), newRmf);
                }
                // store a refecence in the helper list for making a cross product
                refList.append(&*rmf);
                start = end = 0;
                break;
            case NCF:
            case ECF:
            case EP:
                buf = QByteArray(str.data() + start, end - start - space);
                // make a cross product with all current mechanisms
                foreach(QByteArrayList *rmfRef, refList)
                    rmfRef->append(buf);
                if (newState != NCF)
                    refList.clear();
                start = end = 0;
                break;
            default:
                break;
        }
        state = newState;
    }

    return state == ECF || state == EP;
}

/*!
    Splits the given \a rmf (rendering mechanism/format pair) to a \a mechanism
    and a \a format string. Returns FALSE if fails to parse \a rmf.
 */
// static
bool QPMMime::parseRMF(HSTR rmf, QByteArray &mechanism, QByteArray &format)
{
    QList<QByteArrayList> list;
    if (!parseRMFs(rmf, list))
        return false;

    if (list.count() != 1 || list.first().count() != 2)
        return false;

    QByteArrayList first = list.first();
    mechanism = first.at(0);
    format = first.at(1);

    return true;
}

/*!
    Returns the default drag worker that works in cooperative mode.

    See the DefaultDragWorker class description for more information.
 */
// static
QPMMime::DefaultDragWorker *QPMMime::defaultCoopDragWorker()
{
    static DefaultDragWorker defCoopDragWorker(false /* exclusive */);
    return &defCoopDragWorker;
}

/*!
    Returns the default drag worker that works in exclusive mode.

    See the DefaultDragWorker class description for more information.
 */
// static
QPMMime::DefaultDragWorker *QPMMime::defaultExclDragWorker()
{
    static DefaultDragWorker defExclDragWorker(true /* exclusive */);
    return &defExclDragWorker;
}

/*!
    Returns the default drop worker.

    See the DefaultDropWorker class description for more information.
 */
// static
QPMMime::DefaultDropWorker *QPMMime::defaultDropWorker()
{
    static DefaultDropWorker defaultDropWorker;
    return &defaultDropWorker;
}

#endif // !QT_NO_DRAGANDDROP

//------------------------------------------------------------------------------

class QPMMimeText : public QPMMime
{
public:
    QPMMimeText();
    ~QPMMimeText();

    // for converting from Qt
    QList<MimeCFPair> formatsForMimeData(const QMimeData *mimeData) const;
    bool convertFromMimeData(const QMimeData *mimeData, ULONG format,
                             ULONG &flags, ULONG *data) const;

    // for converting to Qt
    QList<MimeCFPair> mimesForFormats(const QList<ULONG> &formats) const;
    QVariant convertFromFormat(ULONG format, ULONG flags, ULONG data,
                               const QString &mimeType,
                               QVariant::Type preferredType) const;

#if !defined(QT_NO_DRAGANDDROP)

    // Direct Manipulation (DND) converter interface
    DragWorker *dragWorkerFor(const QString &mimeType, QMimeData *mimeData);
    DropWorker *dropWorkerFor(DRAGINFO *info);

    class NativeFileDrag : public DragWorker, public QPMObjectWindow
    {
    public:
        // DragWorker interface
        bool cleanup(bool isCancelled) { return true; } // always disallow Move
        bool isExclusive() const { return true; }
        ULONG itemCount() const { return 0; } // super exclusive
        HWND hwnd() const { return QPMObjectWindow::hwnd(); }
        DRAGINFO *createDragInfo(const QString &targetName, USHORT supportedOps);
        // QPMObjectWindow interface (dummy implementation, we don't need to interact)
        MRESULT message(ULONG msg, MPARAM mp1, MPARAM mp2) { return 0; }
    };

    class NativeFileDrop : public DropWorker
    {
    public:
        // DropWorker interface
        bool isExclusive() const { return true; }
        bool hasFormat(const QString &mimeType) const;
        QStringList formats() const;
        QVariant retrieveData(const QString &mimeType,
                              QVariant::Type preferredType) const;
    };

    class TextDragProvider : public DefaultDragWorker::Provider
    {
    public:
        TextDragProvider() : exclusive(false) {}
        bool exclusive;
        // Provider interface
        QString format(const char *drf) const;
        bool provide(const char *drf, const QByteArray &allData,
                     ULONG itemIndex, QByteArray &itemData);
        void fileType(const char *drf, QString &type, QString &ext);
    };

    class TextDropProvider : public DefaultDropWorker::Provider
    {
    public:
        // Provider interface
        QByteArray drf(const QString &mimeType) const;
        bool provide(const QString &mimeType, ULONG itemIndex,
                     const QByteArray &itemData, QByteArray &allData);
    };

#endif // !QT_NO_DRAGANDDROP

    const ULONG CF_TextUnicode;
    const ULONG CF_TextHtml;

#if !defined(QT_NO_DRAGANDDROP)
    NativeFileDrag nativeFileDrag;
    NativeFileDrop nativeFileDrop;
    TextDragProvider textDragProvider;
    TextDropProvider textDropProvider;
#endif // !QT_NO_DRAGANDDROP
};

QPMMimeText::QPMMimeText()
    // "text/unicode" is what Mozilla uses to for unicode
    : CF_TextUnicode (registerMimeType(QLatin1String("text/unicode")))
    // "text/html" is what Mozilla uses to for HTML
    , CF_TextHtml (registerMimeType(QLatin1String("text/html")))
{
}

QPMMimeText::~QPMMimeText()
{
    unregisterMimeType(CF_TextHtml);
    unregisterMimeType(CF_TextUnicode);
}

QList<QPMMime::MimeCFPair> QPMMimeText::formatsForMimeData(const QMimeData *mimeData) const
{
    QList<MimeCFPair> fmts;
    // prefer HTML as it's reacher
    if (mimeData->hasHtml())
        fmts << MimeCFPair(QLatin1String("text/html"), CF_TextHtml);
    // prefer unicode over local8Bit
    if (mimeData->hasText())
        fmts << MimeCFPair(QLatin1String("text/plain"), CF_TextUnicode)
             << MimeCFPair(QLatin1String("text/plain"), CF_TEXT);
    return fmts;
}

// text/plain is defined as using CRLF, but so many programs don't,
// and programmers just look for '\n' in strings.
// OS/2 really needs CRLF, so we ensure it here.
bool QPMMimeText::convertFromMimeData(const QMimeData *mimeData, ULONG format,
                                      ULONG &flags, ULONG *data) const
{
    if (!mimeData->hasText() ||
        (format != CF_TEXT && format != CF_TextUnicode && format != CF_TextHtml))
        return false;

    flags = CFI_POINTER;

    if (data == NULL)
        return true; // delayed rendering, nothing to do

    QByteArray r;

    if (format == CF_TEXT) {
        QByteArray str = mimeData->text().toLocal8Bit();
        // Anticipate required space for CRLFs at 1/40
        int maxsize = str.size()+str.size()/40+1;
        r.fill('\0', maxsize);
        char *o = r.data();
        const char *d = str.data();
        const int s = str.size();
        bool cr = false;
        int j = 0;
        for (int i = 0; i < s; i++) {
            char c = d[i];
            if (c == '\r')
                cr = true;
            else {
                if (c == '\n') {
                    if (!cr)
                        o[j++] = '\r';
                }
                cr = false;
            }
            o[j++] = c;
            if (j+1 >= maxsize) {
                maxsize += maxsize/4;
                r.resize(maxsize);
                o = r.data();
            }
        }
        if (j < r.size())
            o[j] = '\0';
    } else if (format == CF_TextUnicode || CF_TextHtml) {
        QString str = format == CF_TextUnicode ?
                      mimeData->text() : mimeData->html();
        const QChar *u = str.unicode();
        QString res;
        const int s = str.length();
        int maxsize = s + s/40 + 3;
        res.resize(maxsize);
        int ri = 0;
        bool cr = false;
        for (int i = 0; i < s; ++i) {
            if (*u == QLatin1Char('\r'))
                cr = true;
            else {
                if (*u == QLatin1Char('\n') && !cr)
                    res[ri++] = QLatin1Char('\r');
                cr = false;
            }
            res[ri++] = *u;
            if (ri+3 >= maxsize) {
                maxsize += maxsize/4;
                res.resize(maxsize);
            }
            ++u;
        }
        res.truncate(ri);
        const int byteLength = res.length()*2;
        r.fill('\0', byteLength + 2);
        memcpy(r.data(), res.unicode(), byteLength);
        r[byteLength] = 0;
        r[byteLength+1] = 0;
    } else{
        return false;
    }

    *data = QPMMime::allocateMemory(r.size());
    if (!*data)
        return false;

    memcpy((void *)*data, r.data(), r.size());
    return true;
}

QList<QPMMime::MimeCFPair> QPMMimeText::mimesForFormats(const QList<ULONG> &formats) const
{
    QList<MimeCFPair> mimes;
    // prefer HTML as it's reacher
    if (formats.contains(CF_TextHtml))
        mimes << MimeCFPair(QLatin1String("text/html"), CF_TextHtml);
    // prefer unicode over local8Bit
    if (formats.contains(CF_TextUnicode))
        mimes << MimeCFPair(QLatin1String("text/plain"), CF_TextUnicode);
    if (formats.contains(CF_TEXT))
        mimes << MimeCFPair(QLatin1String("text/plain"), CF_TEXT);
    return mimes;
}

QVariant QPMMimeText::convertFromFormat(ULONG format, ULONG flags, ULONG data,
                                        const QString &mimeType,
                                        QVariant::Type preferredType) const
{
    QVariant ret;

    if (!mimeType.startsWith(QLatin1String("text/plain")) &&
        !mimeType.startsWith(QLatin1String("text/html")))
        return ret;
    if ((format != CF_TEXT && format != CF_TextUnicode && format != CF_TextHtml) ||
        !(flags & CFI_POINTER) || !data)
        return ret;

    QString str;

    if (format == CF_TEXT) {
        const char *d = (const char *)data;
        QByteArray r("");
        if (*d) {
            const int s = qstrlen(d);
            r.fill('\0', s);
            char *o = r.data();
            int j = 0;
            for (int i = 0; i < s; i++) {
                char c = d[i];
                if (c != '\r')
                    o[j++] = c;
            }
        }
        str = QString::fromLocal8Bit(r);
    } else if (format == CF_TextUnicode || CF_TextHtml) {
        str = QString::fromUtf16((const unsigned short *)data);
        str.replace(QLatin1String("\r\n"), QLatin1String("\n"));
    }

    if (preferredType == QVariant::String)
        ret = str;
    else
        ret = str.toUtf8();

    return ret;
}

#if !defined(QT_NO_DRAGANDDROP)

DRAGINFO *QPMMimeText::NativeFileDrag::createDragInfo(const QString &targetName,
                                                      USHORT supportedOps)
{
    Q_ASSERT(source());
    if (!source())
        return 0;

    // obtain the list of files
    QList<QUrl> list;
    if (source()->hasUrls())
        list = source()->urls();
    ULONG itemCnt = list.count();
    Q_ASSERT(itemCnt);
    if (!itemCnt)
        return 0;

    DEBUG(() << "QPMMimeText::NativeFileDrag: itemCnt" << itemCnt);

    DRAGINFO *info = DrgAllocDraginfo(itemCnt);
    Q_ASSERT(info);
    if (!info)
        return 0;

    bool ok = true;
    QList<QUrl>::iterator it = list.begin();
    for (ULONG i = 0; i < itemCnt; ++i, ++it) {
        DRAGITEM *item = DrgQueryDragitemPtr(info, i);
        Q_ASSERT(item);
        if (!item) {
            ok = false;
            break;
        }

        QByteArray fileName = QFile::encodeName(QDir::convertSeparators(it->toLocalFile()));

        int sep = fileName.lastIndexOf('\\');

        if (sep >= 0) {
            item->hstrSourceName = DrgAddStrHandle(fileName.data() + sep + 1);
            fileName.truncate(sep + 1);
            item->hstrContainerName = DrgAddStrHandle(fileName);
        } else {
            // we got an URL like "file:" which corresponds to the "Computer"
            // bookmark in the side bar of the standard file dialog. We have to
            // deal with that too
            item->hstrSourceName = DrgAddStrHandle(fileName.data());
            item->hstrContainerName = DrgAddStrHandle("");
        }

        DEBUG(() << "QPMMimeText::NativeFileDrag: item" << i
                 << "dir" << queryHSTR(item->hstrContainerName)
                 << "name" << queryHSTR(item->hstrSourceName));

        item->hwndItem = hwnd();
        item->ulItemID = 0;
        item->hstrType = DrgAddStrHandle(DRT_UNKNOWN);
        item->hstrRMF = DrgAddStrHandle("<DRM_OS2FILE,DRF_UNKNOWN>");
        item->hstrTargetName = 0;
        item->cxOffset = 0;
        item->cyOffset = 0;
        item->fsControl = 0;
        item->fsSupportedOps = supportedOps;
    }

    if (!ok) {
        DrgFreeDraginfo(info);
        info = 0;
    }

    return info;
}

bool QPMMimeText::NativeFileDrop::hasFormat(const QString &mimeType) const
{
    return mimeType == QLatin1String("text/uri-list");
}

QStringList QPMMimeText::NativeFileDrop::formats() const
{
    QStringList mimes;
    mimes << QLatin1String("text/uri-list");
    return mimes;
}

QVariant QPMMimeText::NativeFileDrop::retrieveData(const QString &mimeType,
                                                   QVariant::Type preferredType) const
{
    QVariant result;

    Q_ASSERT(info());
    if (!info())
        return result;

    ULONG itemCount = DrgQueryDragitemCount(info());
    Q_ASSERT(itemCount);
    if (!itemCount)
        return result;

    // sanity check
    if (mimeType != QLatin1String("text/uri-list"))
        return result;

    QList<QVariant> urls;

    for (ULONG i = 0; i < itemCount; ++i) {
        DRAGITEM *item = DrgQueryDragitemPtr(info(), i);
        Q_ASSERT(item);
        QByteArray fullName;
        if (!item || !canTargetRenderAsOS2File(item, &fullName))
            return result;
        QString fn = QFile::decodeName(fullName);
        urls += QUrl::fromLocalFile(fn);
    }

    if (preferredType == QVariant::Url && urls.size() == 1)
        result = urls.at(0);
    else if (!urls.isEmpty())
        result = urls;

    return result;
}

QString QPMMimeText::TextDragProvider::format(const char *drf) const
{
    QString result;

    if (qstrcmp(drf, "DRF_TEXT") == 0) {
        if (exclusive)
            result = QLatin1String("text/uri-list");
        else
            result = QLatin1String("text/plain");
    }
    return result;
}

bool QPMMimeText::TextDragProvider::provide(const char *drf,
                                            const QByteArray &allData,
                                            ULONG itemIndex,
                                            QByteArray &itemData)
{
    if (qstrcmp(drf, "DRF_TEXT") == 0) {
        if (exclusive) {
            // locate the required item
            int dataSize = allData.size();
            if (!dataSize)
                return false;
            int begin = 0, end = 0, next = 0;
            do {
                begin = next;
                end = allData.indexOf('\r', begin);
                if (end >= 0) {
                    next = end + 1;
                    if (next < dataSize && allData[next] == '\n')
                        ++next;
                } else {
                    end = allData.indexOf('\n', begin);
                    if (end >= 0)
                        next = end + 1;
                }
            } while (itemIndex-- && end >= 0 && next < dataSize);
            int urlLen = end - begin;
            if (urlLen <= 0)
                return false;
            QUrl url = QUrl(QString::fromUtf8(allData.data() + begin, urlLen));
            if (!url.isValid())
                return false;
            itemData = url.toEncoded();
        } else {
            itemData = QString::fromUtf8(allData).toLocal8Bit();
        }
        return true;
    }
    return false;
}

void QPMMimeText::TextDragProvider::fileType(const char *drf,
                                             QString &type, QString &ext)
{
    if (qstrcmp(drf, "DRF_TEXT") == 0) {
        if (exclusive) {
            type = QLatin1String("UniformResourceLocator");
            // no extension for URLs
            ext = QString::null;
        } else {
            type = QLatin1String(DRT_TEXT);
            ext = QLatin1String("txt");
        }
    }
};

QByteArray QPMMimeText::TextDropProvider::drf(const QString &mimeType) const
{
    // sanity check
    if (mimeType == QLatin1String("text/plain") ||
        mimeType == QLatin1String("text/uri-list"))
        return QByteArray("DRF_TEXT");
    return 0;
}

bool QPMMimeText::TextDropProvider::provide(const QString &mimeType,
                                            ULONG itemIndex,
                                            const QByteArray &itemData,
                                            QByteArray &allData)
{
    if (mimeType == QLatin1String("text/plain")) {
        allData = QString::fromLocal8Bit(itemData).toUtf8();
        return true;
    }

    if (mimeType == QLatin1String("text/uri-list")) {
        QUrl url = QUrl::fromEncoded(itemData);
        if (!url.isValid())
            return false;
        // append the URL to the list
        allData += url.toString().toUtf8();
        allData += "\r\n";
        return true;
    }

    return false;
}

QPMMime::DragWorker *QPMMimeText::dragWorkerFor(const QString &mimeType,
                                                QMimeData *mimeData)
{
    if (mimeType == QLatin1String("text/plain")) {
        // add a cooperative provider
        textDragProvider.exclusive = false;
        DefaultDragWorker *defWorker = defaultCoopDragWorker();
        defWorker->addProvider("DRF_TEXT", &textDragProvider);
        return defWorker;
    }

    if (mimeType == QLatin1String("text/uri-list")) {
        // see what kind of items text/uri-list represents
        QList<QUrl> urls = mimeData->urls();
        int fileCnt = 0;
        foreach (const QUrl &url, urls) {
            if (url.scheme() == QLatin1String("file"))
                ++fileCnt;
        }
        if (fileCnt && fileCnt == urls.count()) {
            // all items are local files, return an exclusive file drag worker
            return &nativeFileDrag;
        }
        if (urls.count() && !fileCnt) {
            // all items are non-files, add an exclusive provider for the
            // specified item count
            textDragProvider.exclusive = true;
            DefaultDragWorker *defWorker = defaultExclDragWorker();
            bool ok = defWorker->addProvider("DRF_TEXT", &textDragProvider,
                                             urls.count());
            return ok ? defWorker : 0;
        }
        // if items are mixed, we return NULL to fallback to QPMMimeAnyMime
    }

    return 0;
}

QPMMime::DropWorker *QPMMimeText::dropWorkerFor(DRAGINFO *info)
{
    ULONG itemCount = DrgQueryDragitemCount(info);
    Q_ASSERT(itemCount);
    if (!itemCount)
        return 0;

    if (itemCount == 1) {
        DRAGITEM *item = DrgQueryDragitemPtr(info, 0);
        Q_ASSERT(item);
        if (!item)
            return 0;
        // proceed only if the target cannot render DRM_OS2FILE on its own
        // and if the item type is not "UniformResourceLocator" (which will be
        // processed below)
        if (!canTargetRenderAsOS2File(item) &&
            !DrgVerifyType(item, "UniformResourceLocator")) {
            DefaultDropWorker *defWorker = defaultDropWorker();
            // check that we support one of DRMs and the format is DRF_TEXT
            if (defWorker->canRender(item, "DRF_TEXT")) {
                // add a cooperative provider (can coexist with others)
                defWorker->addProvider(QLatin1String("text/plain"),
                                        &textDropProvider);
                return defWorker;
            }
            return 0;
        }
    }

    // Either the target can render DRM_OS2FILE on its own (so it's a valid
    // file/directory name), or it's an "UniformResourceLocator", or there is
    // more than one drag item. Check that all items are of either one type
    // or another. If so, we can represent them as 'text/uri-list'.
    bool allAreFiles = true;
    bool allAreURLs = true;
    DefaultDropWorker *defWorker = defaultDropWorker();
    for (ULONG i = 0; i < itemCount; ++i) {
        DRAGITEM *item = DrgQueryDragitemPtr(info, i);
        Q_ASSERT(item);
        if (!item)
            return 0;
        if (allAreFiles)
            allAreFiles &= canTargetRenderAsOS2File(item);
        if (allAreURLs)
            allAreURLs &= DrgVerifyType(item, "UniformResourceLocator") &&
                          defWorker->canRender(item, "DRF_TEXT");
        if (!allAreFiles && !allAreURLs)
            return 0;
    }

    // Note: both allAreFiles and allAreURLs may be true here (e.g. a file on
    // the desktop that represents an URL object). In this case, we will treat
    // the list as files rather than as URLs for similarity with other platforms
    // (e.g. an Internet shortcut on Windows is interpreted as a local file as
    // well).

    if (allAreFiles) {
        // return an exclusive drop worker
        return &nativeFileDrop;
    }

    // add an exclusive provider (can neither coexist with other workers
    // or providers)
    bool ok = defWorker->addExclusiveProvider(QLatin1String("text/uri-list"),
                                              &textDropProvider);
    return ok ? defWorker : 0;
}

#endif // !QT_NO_DRAGANDDROP

//------------------------------------------------------------------------------

class QPMMimeImage : public QPMMime
{
public:
    QPMMimeImage();

    // for converting from Qt
    QList<MimeCFPair> formatsForMimeData(const QMimeData *mimeData) const;
    bool convertFromMimeData(const QMimeData *mimeData, ULONG format,
                             ULONG &flags, ULONG *data) const;
    // for converting to Qt
    QList<MimeCFPair> mimesForFormats(const QList<ULONG> &formats) const;
    QVariant convertFromFormat(ULONG format, ULONG flags, ULONG data,
                               const QString &mimeType,
                               QVariant::Type preferredType) const;
};

QPMMimeImage::QPMMimeImage()
{
}

QList<QPMMime::MimeCFPair> QPMMimeImage::formatsForMimeData(const QMimeData *mimeData) const
{
    QList<MimeCFPair> fmts;
    if (mimeData->hasImage()) {
        // "application/x-qt-image" seems to be used as a single name for all
        // "image/xxx" types in Qt
        fmts << MimeCFPair(QLatin1String("application/x-qt-image"), CF_BITMAP);
    }
    return fmts;
}

bool QPMMimeImage::convertFromMimeData(const QMimeData *mimeData, ULONG format,
                                       ULONG &flags, ULONG *data) const
{
    if (!mimeData->hasImage() || format != CF_BITMAP)
        return false;

    flags = CFI_HANDLE;

    if (data == NULL)
        return true; // delayed rendering, nothing to do

    QImage img = qvariant_cast<QImage>(mimeData->imageData());
    if (img.isNull())
        return false;

    QPixmap pm = QPixmap::fromImage(img);
    if (pm.isNull())
        return false;

    HBITMAP bmp = pm.toPmHBITMAP(0, true);
    if (bmp == NULLHANDLE)
        return false;

    *data = bmp;
    return true;
}

QList<QPMMime::MimeCFPair> QPMMimeImage::mimesForFormats(const QList<ULONG> &formats) const
{
    QList<MimeCFPair> mimes;
    if (formats.contains(CF_BITMAP))
        mimes << MimeCFPair(QLatin1String("application/x-qt-image"), CF_BITMAP);
    return mimes;
}

QVariant QPMMimeImage::convertFromFormat(ULONG format, ULONG flags, ULONG data,
                                         const QString &mimeType,
                                         QVariant::Type preferredType) const
{
    Q_UNUSED(preferredType);

    QVariant ret;

    if (mimeType != QLatin1String("application/x-qt-image"))
        return ret;
    if (format != CF_BITMAP || !(flags & CFI_HANDLE) || !data)
        return ret;

    QPixmap pm = QPixmap::fromPmHBITMAP((HBITMAP)data);
    if (pm.isNull())
        return ret;

    ret = pm.toImage();
    return ret;
}

//------------------------------------------------------------------------------

class QPMMimeAnyMime : public QPMMime
{
public:
    QPMMimeAnyMime();
    ~QPMMimeAnyMime();

    // for converting from Qt
    QList<MimeCFPair> formatsForMimeData(const QMimeData *mimeData) const;
    bool convertFromMimeData(const QMimeData *mimeData, ULONG format,
                             ULONG &flags, ULONG *data) const;
    // for converting to Qt
    QList<MimeCFPair> mimesForFormats(const QList<ULONG> &formats) const;
    QVariant convertFromFormat(ULONG format, ULONG flags, ULONG data,
                               const QString &mimeType,
                               QVariant::Type preferredType) const;

#if !defined(QT_NO_DRAGANDDROP)

    // Direct Manipulation (DND) converter interface
    DragWorker *dragWorkerFor(const QString &mimeType, QMimeData *mimeData);
    DropWorker *dropWorkerFor(DRAGINFO *info);

    class AnyDragProvider : public DefaultDragWorker::Provider
    {
    public:
        AnyDragProvider(QPMMimeAnyMime *am) : anyMime(am) {}
        // Provider interface
        QString format(const char *drf) const;
        bool provide(const char *drf, const QByteArray &allData,
                     ULONG itemIndex, QByteArray &itemData);
        void fileType(const char *drf, QString &type, QString &ext);
    private:
        QPMMimeAnyMime *anyMime;
    };

    class AnyDropProvider : public DefaultDropWorker::Provider
    {
    public:
        AnyDropProvider(QPMMimeAnyMime *am) : anyMime(am) {}
        // Provider interface
        QByteArray drf(const QString &mimeType) const;
        bool provide(const QString &mimeType, ULONG itemIndex,
                     const QByteArray &itemData, QByteArray &allData);
    private:
        QPMMimeAnyMime *anyMime;
    };

#endif // !QT_NO_DRAGANDDROP

private:
    ULONG registerMimeType(const QString &mime) const;
    QString registerFormat(ULONG format) const;

    mutable QMap<QString, ULONG> cfMap;
    mutable QMap<ULONG, QString> mimeMap;

    static QStringList ianaTypes;
    static QString mimePrefix;
    static QString customPrefix;

#if !defined(QT_NO_DRAGANDDROP)

    static ULONG drfToCf(const char *drf);
    static QByteArray cfToDrf(ULONG cf);

    AnyDragProvider anyDragProvider;
    AnyDropProvider anyDropProvider;

//    friend class AnyDragProvider;
//    friend class AnyDropProvider;

#endif // !QT_NO_DRAGANDDROP
};

// static
QStringList QPMMimeAnyMime::ianaTypes;
QString QPMMimeAnyMime::mimePrefix;
QString QPMMimeAnyMime::customPrefix;

QPMMimeAnyMime::QPMMimeAnyMime()
#if !defined(QT_NO_DRAGANDDROP)
    : anyDragProvider(AnyDragProvider(this))
    , anyDropProvider(AnyDropProvider(this))
#endif // !QT_NO_DRAGANDDROP
{
    //MIME Media-Types
    if (!ianaTypes.size()) {
        ianaTypes.append(QLatin1String("application/"));
        ianaTypes.append(QLatin1String("audio/"));
        ianaTypes.append(QLatin1String("example/"));
        ianaTypes.append(QLatin1String("image/"));
        ianaTypes.append(QLatin1String("message/"));
        ianaTypes.append(QLatin1String("model/"));
        ianaTypes.append(QLatin1String("multipart/"));
        ianaTypes.append(QLatin1String("text/"));
        ianaTypes.append(QLatin1String("video/"));

        mimePrefix = QLatin1String("x-mime:");
        customPrefix = QLatin1String("application/x-qt-pm-mime;value=\"");
    }
}

QPMMimeAnyMime::~QPMMimeAnyMime()
{
    foreach(ULONG cf, cfMap.values())
        unregisterMimeType(cf);
}

QList<QPMMime::MimeCFPair> QPMMimeAnyMime::formatsForMimeData(const QMimeData *mimeData) const
{
    QList<MimeCFPair> fmts;

    QStringList mimes = QInternalMimeData::formatsHelper(mimeData);
    foreach (QString mime, mimes) {
        ULONG cf = cfMap.value(mime);
        if (!cf)
            cf = registerMimeType(mime);
        if (cf)
            fmts << MimeCFPair(mime, cf);
    }

    return fmts;
}

bool QPMMimeAnyMime::convertFromMimeData(const QMimeData *mimeData, ULONG format,
                                         ULONG &flags, ULONG *data) const
{
    QString mime = mimeMap.value(format);
    if (mime.isNull())
        return false;

    flags = CFI_POINTER;

    if (data == NULL)
        return true; // delayed rendering, nothing to do

    QByteArray r = QInternalMimeData::renderDataHelper(mime, mimeData);
    if (r.isNull())
        return false;

    *data = QPMMime::allocateMemory(r.size() + sizeof(ULONG));
    if (!*data)
        return false;

    *((ULONG *)(*data)) = r.size();
    memcpy((void *)(*data + sizeof(ULONG)), r.data(), r.size());
    return true;
}

QList<QPMMime::MimeCFPair> QPMMimeAnyMime::mimesForFormats(const QList<ULONG> &formats) const
{
    QList<MimeCFPair> mimes;

    foreach (ULONG format, formats) {
        QString mime = mimeMap.value(format);
        if (mime.isEmpty())
            mime = registerFormat(format);
        if (!mime.isEmpty())
            mimes << MimeCFPair(mime, format);
    }

    return mimes;
}

QVariant QPMMimeAnyMime::convertFromFormat(ULONG format, ULONG flags, ULONG data,
                                           const QString &mimeType,
                                           QVariant::Type preferredType) const
{
    Q_UNUSED(preferredType);

    QVariant ret;

    if (cfMap.value(mimeType) != format)
        return ret;

    if (!(flags & CFI_POINTER) || !data)
        return ret;

    // get the real block size (always rounded to the page boundary (4K))
    ULONG sz = ~0, fl = 0, arc;
    arc = DosQueryMem((PVOID)data, &sz, &fl);
    if (arc != NO_ERROR) {
#ifndef QT_NO_DEBUG
        qWarning("QPMMimeText::convertFromFormat: DosQueryMem failed with %lu", arc);
#endif
        return ret;
    }
    ULONG size = *((ULONG *)data);
    if (!size || size + sizeof(ULONG) > sz)
        return ret;

    // it should be enough to return the data and let QMimeData do the rest.
    ret = QByteArray((const char *)(data + sizeof(ULONG)), size);
    return ret;
}

#if !defined(QT_NO_DRAGANDDROP)

QString QPMMimeAnyMime::AnyDragProvider::format(const char *drf) const
{
    ULONG cf = drfToCf(drf);
    if (cf) {
        QString mime = anyMime->mimeMap.value(cf);
        if (!mime.isEmpty())
            return mime;
    }

    // There must always be a match since the given drf is associated with this
    // provider by dragWorkerFor() and all necessary mappings are there.
    Q_ASSERT(false);
    return QString::null;
}

bool QPMMimeAnyMime::AnyDragProvider::provide(const char *drf,
                                              const QByteArray &allData,
                                              ULONG itemIndex,
                                              QByteArray &itemData)
{
    Q_UNUSED(drf);
    Q_UNUSED(itemIndex);

    // always straight through coversion
    itemData = allData;
    return true;
}

void QPMMimeAnyMime::AnyDragProvider::fileType(const char *drf,
                                               QString &type, QString &ext)
{
    // file type = mime
    type = format(drf);
    Q_ASSERT(!type.isEmpty());

    // no way to determine the extension
    ext = QString::null;
};

QByteArray QPMMimeAnyMime::AnyDropProvider::drf(const QString &mimeType) const
{
    ULONG cf = anyMime->cfMap.value(mimeType);
    if (cf)
        return cfToDrf(cf);

    // There must always be a match since the given drf is associated with this
    // provider by dragWorkerFor() and all necessary mappings are there.
    Q_ASSERT(false);
    return QByteArray();
}

bool QPMMimeAnyMime::AnyDropProvider::provide(const QString &mimeType,
                                              ULONG itemIndex,
                                              const QByteArray &itemData,
                                              QByteArray &allData)
{
    Q_UNUSED(mimeType);
    Q_UNUSED(itemIndex);

    // always straight through coversion
    allData = itemData;
    return true;
}

QPMMime::DragWorker *QPMMimeAnyMime::dragWorkerFor(const QString &mimeType,
                                                   QMimeData *mimeData)
{
    ULONG cf = cfMap.value(mimeType);
    if (!cf)
        cf = registerMimeType(mimeType);
    if (cf) {
        DefaultDragWorker *defWorker = defaultCoopDragWorker();
        // add a cooperative provider
        defWorker->addProvider(cfToDrf(cf), &anyDragProvider);
        return defWorker;
    }

    Q_ASSERT(false);
    return 0;
}

QPMMime::DropWorker *QPMMimeAnyMime::dropWorkerFor(DRAGINFO *info)
{
    ULONG itemCount = DrgQueryDragitemCount(info);
    Q_ASSERT(itemCount);
    if (!itemCount)
        return 0;

    if (itemCount == 1) {
        DRAGITEM *item = DrgQueryDragitemPtr(info, 0);
        Q_ASSERT(item);
        if (!item)
            return 0;

        DefaultDropWorker *defWorker = defaultDropWorker();
        bool atLeastOneSupported = false;

        // check that we support one of DRMs and the format is CF_hhhhhhh
        QList<QByteArrayList> list;
        defWorker->getSupportedRMFs(item, list);
        foreach(const QByteArrayList &mech, list) {
            QByteArrayList::const_iterator it = mech.begin();
            Q_ASSERT(it != mech.end());
            DEBUG(() << "QPMMimeAnyMime: Supported drm:" << *it);
            for (++it; it != mech.end(); ++it) {
                const QByteArray &drf = *it;
                ULONG cf = drfToCf(drf);
                if (cf) {
                    DEBUG(() << "QPMMimeAnyMime: Supported drf:" << drf);
                    QString mime = mimeMap.value(cf);
                    if (mime.isEmpty())
                        mime = registerFormat(cf);
                    Q_ASSERT(!mime.isEmpty());
                    if (!mime.isEmpty()) {
                        DEBUG(() << "QPMMimeAnyMime: Will provide [" << mime
                                 << "] for drf" << drf);
                        // add a cooperative provider (can coexist with others)
                        defWorker->addProvider(mime, &anyDropProvider);
                        atLeastOneSupported = true;
                    }
                }
            }
        }

        if (atLeastOneSupported)
            return defWorker;
    }

    return 0;
}

#endif // !QT_NO_DRAGANDDROP

ULONG QPMMimeAnyMime::registerMimeType(const QString &mime) const
{
    if (mime.isEmpty())
        return 0;

    QString mimeToReg = mime;

    bool ianaType = false;
    foreach(QString prefix, ianaTypes) {
        if (mime.startsWith(prefix)) {
            ianaType = true;
            break;
        }
    }
    if (!ianaType) {
        // prepend the non-standard type with the prefix that makes it comply
        // with the standard
        mimeToReg = customPrefix + mime + QLatin1Char('\"');
    }

    mimeToReg = mimePrefix + mimeToReg;
    ULONG cf = QPMMime::registerMimeType(mimeToReg);
    if (cf) {
        cfMap[mime] = cf;
        mimeMap[cf] = mime;
    }
    return cf;
}

QString QPMMimeAnyMime::registerFormat(ULONG format) const
{
    QString mime;

    if (!format)
        return mime;

    QString atomStr = formatName(format);
    if (atomStr.startsWith(mimePrefix)) {
        // the format represents the mime type we can recognize
        // increase the reference count
        ULONG cf = QPMMime::registerMimeType(atomStr);
        Q_ASSERT(cf == format);
        // extract the real mime type (w/o our prefix)
        mime = atomStr.mid(mimePrefix.size());
        if (!mime.isEmpty()) {
            cfMap[mime] = cf;
            mimeMap[cf] = mime;
        }
    }
    return mime;
}

#if !defined(QT_NO_DRAGANDDROP)

// static
ULONG QPMMimeAnyMime::drfToCf(const char *drf)
{
    if (qstrncmp(drf, "CF_", 3) == 0)
        return QString(QLatin1String(drf + 3)).toULong(0, 16);
    return 0;
}

// static
QByteArray QPMMimeAnyMime::cfToDrf(ULONG cf)
{
    return QString().sprintf("CF_%08lX", cf).toLatin1();
}

#endif // !QT_NO_DRAGANDDROP

//------------------------------------------------------------------------------

QPMMimeList::QPMMimeList()
    : initialized(false)
{
}

QPMMimeList::~QPMMimeList()
{
    while (list.size())
        delete list.first();
}


void QPMMimeList::init()
{
    if (!initialized) {
        initialized = true;
        new QPMMimeAnyMime; // must be the first (used as a fallback)
        new QPMMimeImage;
        new QPMMimeText;
    }
}

void QPMMimeList::addMime(QPMMime *mime)
{
    init();
    list.prepend(mime);
}

void QPMMimeList::removeMime(QPMMime *mime)
{
    init();
    list.removeAll(mime);
}

QList<QPMMime*> QPMMimeList::mimes()
{
    init();
    return list;
}

QT_END_NAMESPACE
