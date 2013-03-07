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

#include "qapplication.h"

#include "qapplication_p.h"
#include "qevent.h"
#include "qpainter.h"
#include "qwidget.h"
#include "qbuffer.h"
#include "qdatastream.h"
#include "qcursor.h"
#include "qdesktopwidget.h"
#include "qfile.h"
#include "qfileinfo.h"
#include "qdnd_p.h"
#include "qdebug.h"

#ifndef QT_NO_ACCESSIBILITY
#include "qaccessible.h"
#endif

#include "qt_os2.h"
#include "private/qpmobjectwindow_pm_p.h"

//#define QDND_DEBUG // in pair with qmime_pm.cpp

#ifdef QDND_DEBUG
#   define DEBUG(a) qDebug a
#else
#   define DEBUG(a) do {} while(0)
#endif

QT_BEGIN_NAMESPACE

#if !defined(QT_NO_DRAGANDDROP) && !defined(QT_NO_CLIPBOARD)

extern void qt_pmMouseButtonUp(); // defined in qapplication_pm.cpp
extern void qt_DrgFreeDragtransfer(DRAGTRANSFER *xfer); // defined in qmime_pm.cpp

#ifdef QDND_DEBUG
extern QString dragActionsToString(Qt::DropActions actions);
#endif

/** \internal
 *  Data for QDragEnterEvent/QDragMoveEvent/QPMDropEvent.
 */
class QPMDragData
{
public:
    QPMDragData();
    ~QPMDragData();

    void initialize(DRAGINFO *di);
    void reset(bool isAccepted);
    void reset() { reset(false); }

    void setInFakeDrop(bool d) { fakeDrop = d; }
    bool inFakeDrop() const { return fakeDrop; }

    DRAGINFO *info() const { return di; }
    bool canSyncRender() const { return canSyncRndr; }

    bool hasFormat_sys(const QString &mimeType);
    QStringList formats_sys();
    QVariant retrieveData_sys(const QString &mimeType,
                              QVariant::Type preferredType);

private:
    friend class QDragManager;

    void initWorkers();

    bool initialized : 1;
    bool fakeDrop : 1;
    bool gotWorkers : 1;
    bool canSyncRndr : 1;

    DRAGINFO *di;
    QList<QPMMime::DropWorker*> workers;

    QWidget *lastDragOverWidget; // last target widget
    USHORT lastDragOverOp; // last DM_DRAGOVER operation

    USHORT supportedOps; // operations supported by all items
    bool sourceAllowsOp : 1; // does source allow requested operation

    USHORT lastDropReply; // last reply to DM_DRAGOVER

    Qt::DropAction lastAction; // last target-accepted action
    QRect lastAnswerRect; // last accepted rectangle from the target
};

QPMDragData::QPMDragData()
    : initialized(false), fakeDrop(false)
    , gotWorkers(false), canSyncRndr(false), di(NULL)
    , lastDragOverWidget(0), lastDragOverOp(0)
    , supportedOps(0), sourceAllowsOp(false)
    , lastDropReply(DOR_NEVERDROP)
    , lastAction(Qt::CopyAction)
{
}

QPMDragData::~QPMDragData()
{
    reset();
}

void QPMDragData::initialize(DRAGINFO *info)
{
    Q_ASSERT(info);
    if (initialized || !info)
        return;

    initialized = true;
    di = info;

    // Detect if the source supports synchronous rendering which means it can
    // process DM_RENDER and reply with DM_RENDERCOMPLETE from within both
    // DM_DRAGOVER and DM_DROP (i.e. while it is running the DnD session with
    // DrgDrag()). Mozilla apps are known to only support synchronous rendering
    // (an attempt to send DM_RENDER to them after DM_DROP will crash them).
    // By the contrast, EPM only supports asyncronous rendering (it will hang
    // if we send DM_RENDER and/or wait for DM_RENDERCOMPLETE before returning
    // from DM_DRAGOVER/DM_DROP). WPS (e.g. desktop folders) seem to support
    // both (we prefer the syncrhonous rendering in such cases since it lets
    // Qt apps access dragged data right from QDragEnterEvent/QDragLeaveEvent
    // and QDropEvent message handlers which is used by many of them for better
    // visualization.

    // get class name
    QByteArray className(256, '\0');
    LONG classNameLen = WinQueryClassName(info->hwndSource,
                                          className.size(), className.data());
    className.truncate(classNameLen);

    DEBUG(("QPMDragData::initialize: info->hwndSource %08lX className \"%s\"",
           info->hwndSource, QString::fromLocal8Bit(className).toUtf8().data()));

    // get EXE name
    PID pid = 0;
    TID tid = 0;
    QByteArray exePath(CCHMAXPATH, '\0');
    BOOL ok = WinQueryWindowProcess(info->hwndSource, &pid, &tid);
    if (ok) {
        QByteArray buf(4192, '\0');
        APIRET arc = DosQuerySysState(QS_PROCESS, 0, pid, tid,
                                      buf.data(), buf.size());
        if (arc == NO_ERROR) {
            qsGrec_t *grec = *(qsGrec_t **)buf.data();
            qsPrec_t *prec = (qsPrec_t *)((ULONG)grec + sizeof(qsGrec_t));
            arc = DosQueryModuleName(prec->hMte, exePath.size(), exePath.data());
            if (arc == NO_ERROR)
                exePath.truncate(qstrlen(exePath));
        }
    }

    DEBUG(("QPMDragData::initialize: pid %lu exePath \"%s\"",
           pid, QString::fromLocal8Bit(exePath).toUtf8().data()));

    QString exeName = QFileInfo(QFile::decodeName(exePath)).fileName();

    // start with a positive
    canSyncRndr = true;

    if (exeName.toUpper() == QLatin1String("EPM.EXE") &&
        qstrcmp(className, "FileWnd") == 0) {
        // EPM only supports async rendering
        canSyncRndr = false;
    }

    DEBUG(("QPMDragData::initialize: canSyncRender %d", canSyncRndr));
}

void QPMDragData::initWorkers()
{
    Q_ASSERT(initialized);
    if (!initialized || gotWorkers)
        return;

    gotWorkers = true;

    // go through all converters and collect DropWorkers to use
    foreach (QPMMime *mime, QPMMime::all()) {
        QPMMime::DropWorker *wrk = mime->dropWorkerFor(di);
        if (wrk) {
            if (wrk->isExclusive()) {
                // ignore all other workers if some of them identified itself
                // as exclusive
                workers.clear();
                workers.append(wrk);
                break;
            }
            // ensure there are no duplicateseed
            if (!workers.contains(wrk))
                workers.append(wrk);
        }
    }

    DEBUG(() << "QPMDragData:" << workers.count() << "drop workers for DRAGINFO" << di);

    // init all workers
    foreach (QPMMime::DropWorker *w, workers) {
        w->nfo = di;
        w->init();
    }
}

void QPMDragData::reset(bool isAccepted)
{
    if (!initialized)
        return;

    // cleanup all workers
    foreach (QPMMime::DropWorker *w, workers) {
        w->cleanup(isAccepted);
        w->nfo = 0;
    }

    workers.clear();
    di = 0;
    initialized = fakeDrop = gotWorkers = canSyncRndr = false;
}

bool QPMDragData::hasFormat_sys(const QString &mimeType)
{
    if (!gotWorkers)
        initWorkers();

    foreach (QPMMime::DropWorker *w, workers)
        if (w->hasFormat(mimeType))
            return true;

    return false;
}

QStringList QPMDragData::formats_sys()
{
    QStringList mimes;

    if (!gotWorkers)
        initWorkers();

    foreach (QPMMime::DropWorker *w, workers)
        mimes << w->formats();

    return mimes;
}

QVariant QPMDragData::retrieveData_sys(const QString &mimeType,
                                       QVariant::Type preferredType)
{
    QVariant result;

    // If the source doesn't support synchronous rendering, we may only do data
    // transfer after DM_DROP is processed by us. Return shortly.
    if (!canSyncRender() && !inFakeDrop())
        return result;

    if (!gotWorkers)
        initWorkers();

    foreach (QPMMime::DropWorker *w, workers) {
        if (w->hasFormat(mimeType)) {
            result = w->retrieveData(mimeType, preferredType);
            break;
        }
    }

    return result;
}

static Qt::DropActions toQDragDropActions(USHORT ops)
{
    Qt::DropActions actions = Qt::IgnoreAction;
    if (ops & DO_LINKABLE)
        actions |= Qt::LinkAction;
    if (ops & DO_COPYABLE)
        actions |= Qt::CopyAction;
    if (ops & DO_MOVEABLE)
        actions |= Qt::MoveAction;
    return actions;
}

static Qt::DropAction toQDragDropAction(USHORT op)
{
    if (op == DO_LINK)
        return Qt::LinkAction;
    if (op == DO_COPY)
        return Qt::CopyAction;
    if (op == DO_MOVE)
        return Qt::MoveAction;
    return Qt::IgnoreAction;
}

static USHORT toPmDragDropOp(Qt::DropActions action)
{
    if (action & Qt::LinkAction)
        return DO_LINK;
    if (action & Qt::CopyAction)
        return DO_COPY;
    if (action & Qt::MoveAction)
        return DO_MOVE;
    return 0;
}

static USHORT toPmDragDropOps(Qt::DropActions actions)
{
    USHORT op = 0;
    if (actions & Qt::LinkAction)
        op |= DO_LINKABLE;
    if (actions & Qt::CopyAction)
        op |= DO_COPYABLE;
    if (actions & Qt::MoveAction)
        op |= DO_MOVEABLE;
    return op;
}

// Equivalent of QApplication::mouseButtons() (mouse events aren't delivered
// to the window during DnD in PM so this metnod returns an incorrect value).
static Qt::MouseButtons mouseButtons()
{
    Qt::MouseButtons buttons = Qt::NoButton;

    if (WinGetKeyState(HWND_DESKTOP, VK_BUTTON1) & 0x8000)
        buttons |= Qt::LeftButton;
    if (WinGetKeyState(HWND_DESKTOP, VK_BUTTON2) & 0x8000)
        buttons |= Qt::RightButton;
    if (WinGetKeyState(HWND_DESKTOP, VK_BUTTON3) & 0x8000)
        buttons |= Qt::MidButton;

    return buttons;
}

// Equivalent of QApplication::keyboardModifiers() (keyboard events aren't delivered
// to the window during DnD in PM so this metnod returns an incorrect value).
static Qt::KeyboardModifiers keyboardModifiers()
{
    Qt::KeyboardModifiers mods = Qt::NoModifier;

    if (WinGetKeyState(HWND_DESKTOP, VK_SHIFT ) & 0x8000)
        mods |= Qt::ShiftModifier;
    if ((WinGetKeyState(HWND_DESKTOP, VK_ALT) & 0x8000) ||
        (WinGetKeyState(HWND_DESKTOP, VK_ALTGRAF) & 0x8000))
        mods |= Qt::AltModifier;
    if (WinGetKeyState(HWND_DESKTOP, VK_CTRL) & 0x8000)
        mods |= Qt::ControlModifier;

    // Note: we cannot properly detect LWIN/RWIN (Meta) key state here as it
    // doesn't have a corresponding VK_ value. The best we can do is to check
    // the physical keyboard state using the hardware scancode but it has the
    // following problems:
    //
    //   1. The physical keyboard state may be out of sync with the current
    //      input message we are processing.
    //   2. The scancode may be hardware dependent
    //
    // 0x7E is what I get as a scancode value for LWIN from PM here.
    // Unfortunately, I have no keyboard with RWIN around so I can't get its
    // scan code.

    if (WinGetPhysKeyState(HWND_DESKTOP, 0x7E) & 0x8000)
        mods |= Qt::MetaModifier;

    return mods;
}

void QDragManager::init_sys()
{
    inDrag = false;

    Q_ASSERT(dropData);
    Q_ASSERT(!dropData->d);
    dropData->d = new QPMDragData();
}

void QDragManager::uninit_sys()
{
    Q_ASSERT(dropData);
    Q_ASSERT(dropData->d);
    delete dropData->d;
    dropData->d = 0;
}

void QDragManager::sendDropEvent(QWidget *receiver, QEvent *event)
{
    // Note: inDrag = true causes QWidget::getPS() to return a special HPS for
    // drawing during DnD. However, if we send the message from a fake DM_DROP
    // that we post to ourselves to render data after the DnD session ends
    // (inFakeDrop = true), a normal HPS has to be used.

    bool inFakeDrop = dropData->d->inFakeDrop();

    Q_ASSERT(!inDrag);
    if (!inFakeDrop)
        inDrag = true;

    QApplication::sendEvent(receiver, event);

    if (!inFakeDrop) {
        // make sure that all issued update requests are handled before we
        // return from the DM_DRAGOVER/DM_DRAGLEAVE/DM_DROP message; otherwise
        // we'll get screen corruption due to unexpected screen updates while
        // dragging
        QApplication::sendPostedEvents(0, QEvent::UpdateRequest);
        inDrag = false;
    }
}

/*!
 *  \internal
 *  Direct manipulation (Drag & Drop) event handler
 */
MRESULT QDragManager::dispatchDragAndDrop(QWidget *widget, const QMSG &qmsg)
{
    Q_ASSERT(widget);

    BOOL ok = FALSE;

    QDragManager *manager = QDragManager::self();
    QPMDragData *dragData = manager->dropData->d;
    Q_ASSERT(dragData);

    // @todo use dragData->lastAnswerRect to compress DM_DRAGOVER events

    switch (qmsg.msg) {
        case DM_DRAGOVER: {
            DEBUG(("DM_DRAGOVER: hwnd %lX di %p x %d y %d", qmsg.hwnd, qmsg.mp1,
                   SHORT1FROMMP(qmsg.mp2), SHORT2FROMMP(qmsg.mp2)));

            DRAGINFO *info = (DRAGINFO *)qmsg.mp1;
            ok = DrgAccessDraginfo(info);
            Q_ASSERT(ok && info);
            if (!ok || !info)
                return MRFROM2SHORT(DOR_NEVERDROP, 0);

            // flip y coordinate
            QPoint pnt(info->xDrop, info->yDrop);
            pnt.setY(qt_display_height() - (pnt.y() + 1));
            pnt = widget->mapFromGlobal(pnt);

            QWidget *dragOverWidget = widget->childAt(pnt);
            if (!dragOverWidget)
                dragOverWidget = widget;
            else
                pnt = dragOverWidget->mapFrom(widget, pnt);

            bool first = dragData->lastDragOverWidget != dragOverWidget;
            if (first) {
                // the first DM_DRAGOVER message
                if (dragData->lastDragOverWidget != 0) {
                    // send drag leave to the old widget
                    dragData->reset();
                    QPointer<QWidget> dragOverWidgetGuard(dragOverWidget);
                    QDragLeaveEvent dle;
                    sendDropEvent(dragData->lastDragOverWidget, &dle);
                    if (!dragOverWidgetGuard) {
                        dragOverWidget = widget->childAt(pnt);
                        if (!dragOverWidget)
                            dragOverWidget = widget;
                        else
                            pnt = dragOverWidget->mapFrom(widget, pnt);
                    }
                }
                dragData->lastDragOverWidget = dragOverWidget;
                dragData->lastDragOverOp = 0;
                dragData->supportedOps = DO_COPYABLE | DO_MOVEABLE | DO_LINKABLE;
                dragData->sourceAllowsOp = false;
                dragData->lastDropReply = DOR_NEVERDROP;
                dragData->lastAction =
                    manager->defaultAction(toQDragDropActions(dragData->supportedOps),
                                           Qt::NoModifier);
                dragData->lastAnswerRect = QRect();
                // ensure drag data is reset (just in case of a wrong msg flow...)
                dragData->reset();
            }

            // Note: we don't check if dragOverWidget->acceptDrops() here since
            // this will be checked in QApplication::notify() and the
            // appropriate action will be taken (the message will be delivered
            // to the parent)

            USHORT dropReply = dragData->lastDropReply;

            if (first) {
                // determine the set of operations supported by *all* items
                // (this implies that DRAGITEM::fsSupportedOps is a bit field)
                dropReply = DOR_DROP;
                ULONG itemCount = DrgQueryDragitemCount(info);
                for (ULONG i = 0; i < itemCount; ++ i) {
                    PDRAGITEM item = DrgQueryDragitemPtr(info, i);
                    Q_ASSERT(item);
                    if (!item) {
                        dropReply = DOR_NEVERDROP;
                        break;
                    }
                    dragData->supportedOps &= item->fsSupportedOps;
                }
                if (dropReply != DOR_NEVERDROP) {
                    Q_ASSERT(itemCount);
                    if (!itemCount || !dragData->supportedOps) {
                        // items don't have even a single common operation...
                        dropReply = DOR_NEVERDROP;
                    }
                }
            }

            if (dropReply != DOR_NEVERDROP) {

                if (first || dragData->lastDragOverOp != info->usOperation) {
                    // the current drop operation was changed by a modifier key
                    USHORT realOp = info->usOperation;
                    if (realOp == DO_DEFAULT || realOp == DO_UNKNOWN) {
                        // the default operation is requested
                        realOp = toPmDragDropOp(dragData->lastAction);
                    } else {
                        dragData->lastAction = toQDragDropAction(realOp);
                    }
                    dragData->sourceAllowsOp =
                        ((dragData->supportedOps & DO_MOVEABLE) && realOp == DO_MOVE) ||
                        ((dragData->supportedOps & DO_COPYABLE) && realOp == DO_COPY) ||
                        ((dragData->supportedOps & DO_LINKABLE) && realOp == DO_LINK);
                    dragData->lastDragOverOp = realOp;
                    // make sure the default accepted state for the next event
                    // correlates with the set of supported operations
                    if (dropReply == DOR_DROP)
                        dropReply = dragData->sourceAllowsOp ? DOR_DROP : DOR_NODROP;
                }

                // Note that if sourceAllowsOp = false here, we have to deliver
                // events anyway (stealing them from Qt would be confusing), but
                // we will silently ignore any accept commands and always reject
                // the drop. Other platforms seem to do the same.

                QMimeData *data = manager->source() ? manager->dragPrivate()->data : manager->dropData;

                if (first) {
                    dragData->initialize(info);
                    QDragEnterEvent dee(pnt,
                                        toQDragDropActions(dragData->supportedOps),
                                        data, mouseButtons(), keyboardModifiers());
                    dee.setDropAction(dragData->lastAction);
                    sendDropEvent(dragOverWidget, &dee);
                    // if not allowed or not accepted, always reply DOR_NODROP
                    // to have DM_DRAGOVER delivered to us again for a new test
                    if (dragData->sourceAllowsOp && dee.isAccepted()) {
                        dropReply = DOR_DROP;
                        dragData->lastAction = dee.dropAction();
                        dragData->lastAnswerRect = dee.answerRect();
                    } else {
                        dropReply = DOR_NODROP;
                    }
                    DEBUG(() << "DM_DRAGOVER: QDragEnterEvent: accepted" << dee.isAccepted());
                }

                // note: according to the Qt documentation, the move event
                // is sent immediately after the enter event, do so (only if
                // the target accepts it)
                if (!first || dropReply == DOR_DROP) {
                    QDragMoveEvent dme(pnt,
                                       toQDragDropActions(dragData->supportedOps),
                                       data, mouseButtons(), keyboardModifiers());
                    dme.setDropAction(dragData->lastAction);
                    // accept by default if the enter event was accepted
                    if (dropReply == DOR_DROP)
                        dme.accept();
                    sendDropEvent(dragOverWidget, &dme);
                    if (dragData->sourceAllowsOp && dme.isAccepted()) {
                        dropReply = DOR_DROP;
                        dragData->lastAction = dme.dropAction();
                        dragData->lastAnswerRect = dme.answerRect();
                    } else {
                        dropReply = DOR_NODROP;
                    }
                    DEBUG(() << "DM_DRAGOVER: QDragMoveEvent: accepted" << dme.isAccepted());
                }

                dragData->lastDropReply = dropReply;
            }

            DrgFreeDraginfo(info);

            DEBUG(("DM_DRAGOVER: return %08x default %08x",
                   dropReply, toPmDragDropOp(dragData->lastAction)));
            return MRFROM2SHORT(dropReply, toPmDragDropOp(dragData->lastAction));
        }
        case DM_DRAGLEAVE: {
            DEBUG(("DM_DRAGLEAVE: hwnd %lX di %p", qmsg.hwnd, qmsg.mp1));

            // Odin32 apps produce incorrect message flow, ignore
            Q_ASSERT(dragData->lastDragOverWidget != 0);
            if (dragData->lastDragOverWidget == 0)
                return 0;

            QDragLeaveEvent de;
            sendDropEvent(dragData->lastDragOverWidget, &de);

            dragData->lastDragOverWidget = 0;
            dragData->reset();

            return 0;
        }
        case DM_DROP: {
            DEBUG(("DM_DROP: hwnd %lX di %p", qmsg.hwnd, qmsg.mp1));

            // sanity
            Q_ASSERT(!dragData->canSyncRender() || qmsg.mp1);

            DRAGINFO *info = 0;

            if (dragData->canSyncRender() || qmsg.mp1) {
                // Odin32 apps produce incorrect message flow, ignore
                Q_ASSERT(dragData->lastDragOverWidget != 0);
                if (dragData->lastDragOverWidget == 0)
                    return 0;

                // Odin32 apps send DM_DROP even if we replied DOR_NEVERDROP or
                // DOR_NODROP, simulate DM_DRAGLEAVE
                Q_ASSERT(dragData->lastDropReply == DOR_DROP);
                if (dragData->lastDropReply != DOR_DROP) {
                    QMSG qmsg2 = qmsg;
                    qmsg2.msg = DM_DRAGLEAVE;
                    dispatchDragAndDrop(widget, qmsg2);
                    return 0;
                }

                // sanity
                Q_ASSERT((DRAGINFO *)qmsg.mp1 == dragData->info());

                info = (DRAGINFO *)qmsg.mp1;
                ok = DrgAccessDraginfo(info);
                Q_ASSERT(ok && info);
                if (!ok || !info)
                    return 0;

                if (!dragData->canSyncRender()) {
                    // The source doesn't support syncrhonous rendering (i.e.
                    // it won't issue DM_RENDERCOMPLETE until we return from
                    // DM_DROP) so we have to post a message to ourselves to do
                    // it asynchronously. For simplicity, we reuse DM_DRAG with
                    // mp1 = 0 for this purpose.
                    WinPostMsg(qmsg.hwnd, DM_DROP, 0, 0);
                    return 0;
                }
            } else {
                // we're in a fake DM_DROP we posted above
                dragData->setInFakeDrop(true);
                info = dragData->info();
                Q_ASSERT(info);
            }

            // flip y coordinate
            QPoint pnt(info->xDrop, info->yDrop);
            pnt.setY(qt_display_height() - (pnt.y() + 1));
            pnt = widget->mapFromGlobal(pnt);
            if (dragData->lastDragOverWidget != widget)
                pnt = dragData->lastDragOverWidget->mapFrom(widget, pnt);

            QMimeData *data = manager->source() ? manager->dragPrivate()->data : manager->dropData;

            QDropEvent de(pnt, toQDragDropActions(dragData->supportedOps),
                          data, mouseButtons(), keyboardModifiers());
            if (dragData->lastDropReply == DOR_DROP)
                de.setDropAction(dragData->lastAction);
            sendDropEvent(dragData->lastDragOverWidget, &de);

            if (dragData->lastDropReply == DOR_DROP)
                de.accept();

            if (de.isAccepted()) {
                // We must update usOperation so that if the application changes
                // the drop action during processing of the QDropEvent this
                // change will be visible at the DrgDrag() end. This is odd (since
                // it may be easily made out of sync with the visual indication)
                // but needed for compatibility with other platforms.
                info->usOperation = toPmDragDropOp(de.dropAction());
            }

            dragData->lastDragOverWidget = 0;
            dragData->reset(de.isAccepted());

            ULONG targetReply = de.isAccepted() ?
                DMFL_TARGETSUCCESSFUL : DMFL_TARGETFAIL;

            if (de.isAccepted() && de.dropAction() == Qt::TargetMoveAction) {
                // Qt::TargetMoveAction means that the target will move the
                // object (e.g. copy it to itself and delete from the source).
                // In this case, we always send DMFL_TARGETFAIL to the source to
                // prevent it from doing the same on its side.
                targetReply = DMFL_TARGETFAIL;
            }

            // send DM_ENDCONVERSATION for every item
            ULONG itemCount = DrgQueryDragitemCount(info);
            for (ULONG i = 0; i < itemCount; ++ i) {
                PDRAGITEM item = DrgQueryDragitemPtr(info, i);
                Q_ASSERT(item);
                if (!item)
                    continue;
                // it is possible that this item required DM_RENDERPREPARE but
                // returned false in reply to it (so hwndItem may be NULL)
                if (!item->hwndItem)
                    continue;
                DrgSendTransferMsg(item->hwndItem, DM_ENDCONVERSATION,
                                   MPFROMLONG(item->ulItemID),
                                   MPFROMLONG(targetReply));
            }

            DrgDeleteDraginfoStrHandles(info);
            DrgFreeDraginfo(info);

            return 0;
        }
        default:
            break;
    }

    return WinDefWindowProc(qmsg.hwnd, qmsg.msg, qmsg.mp1, qmsg.mp2);
}

//---------------------------------------------------------------------
//                    QDropData
//---------------------------------------------------------------------

bool QDropData::hasFormat_sys(const QString &mimeType) const
{
    Q_ASSERT(d);
    if (d)
        return d->hasFormat_sys(mimeType);
    return false;
}

QStringList QDropData::formats_sys() const
{
    QStringList fmts;
    Q_ASSERT(d);
    if (d)
        fmts = d->formats_sys();
    return fmts;
}

QVariant QDropData::retrieveData_sys(const QString &mimeType, QVariant::Type type) const
{
    QVariant result;
    Q_ASSERT(d);
    if (d)
        result = d->retrieveData_sys(mimeType, type);
    return result;
}

//---------------------------------------------------------------------
//                    QPMCoopDragWorker
//---------------------------------------------------------------------

class QPMCoopDragWorker : public QPMMime::DragWorker, public QPMObjectWindow
{
public:
    QPMCoopDragWorker() : info(0) {}
    bool collectWorkers(QDrag *o);

    // DragWorker interface
    void init();
    bool cleanup(bool isCancelled);
    bool isExclusive() const { return true; }
    ULONG itemCount() const { return 0; }
    HWND hwnd() const { return QPMObjectWindow::hwnd(); }
    DRAGINFO *createDragInfo(const QString &targetName, USHORT supportedOps);

    // QPMObjectWindow interface
    MRESULT message(ULONG msg, MPARAM mp1, MPARAM mp2);

private:
    QList<DragWorker*> workers;
    DRAGINFO *info;
};

bool QPMCoopDragWorker::collectWorkers(QDrag *o)
{
    Q_ASSERT(o);
    if (!o)
        return false;

    bool gotExcl = false; // got isExclusive() worker?
    bool skipExcl = false; // skip isExclusive() or itemCount() > 1 workers?
    ULONG coopLevel = 0; // itemCount() level for !isExclusive() workers

    bool gotExclForMime = false;

    // go through all formats and all converters to collect DragWorkers
    QMimeData *mimeData = o->mimeData();
    foreach (const QString &fmt, mimeData->formats()) {
        DEBUG(() << "QPMCoopDragWorker: Searching for worker for mime" << fmt);
        foreach (QPMMime *mime, QPMMime::all()) {
            DragWorker *wrk = mime->dragWorkerFor(fmt, mimeData);
            if (!wrk)
                continue;
            DEBUG(() << "QPMCoopDragWorker: Got worker" << wrk
                     << "( isExclusive" << wrk->isExclusive()
                     << "itemCount" << wrk->itemCount() << ") from convertor"
                     << mime << "( gotExclForMime" << gotExclForMime
                     << "gotExcl" << gotExcl
                     << "skipExcl" << skipExcl << "coopLevel"
                     << coopLevel << ")");
            if (wrk->isExclusive()) {
                if (!skipExcl && !gotExclForMime) {
                    gotExclForMime = true;
                    if (!gotExcl) {
                        gotExcl = true;
                        workers.append(wrk);
                    } else {
                        // skip everything exclusive unless it's exactly the
                        // same worker
                        skipExcl = !workers.contains(wrk);
                    }
                }
                // continue to search for a fall-back cooperative 1-item worker
                // (like QPMMimeAnyMime) for the case if this worker quits
                // the game
                continue;
            }
            ULONG itemCnt = wrk->itemCount();
            if (itemCnt == 0) {
                DEBUG(() << "QPMCoopDragWorker: Cooperative DragWorker"
                         << wrk << "for mime " << fmt << " has "
                         << "itemCount = 0!");
                continue;
            }
            if (itemCnt > 1) {
                // coop workers with item count > 1 are also considered exclusive
                // here, because may not co-exist with 1-item workers that should
                // always be able to contribute
                if (!gotExcl && !skipExcl && !gotExclForMime) {
                    gotExclForMime = true;
                    workers.append(wrk);
                    if (!coopLevel)
                        coopLevel = itemCnt;
                    // only those for the same number of items can proceed
                    if (itemCnt != coopLevel)
                        skipExcl = true;
                }
                // continue to search for a fall-back cooperative 1-item worker
                // (like QPMMimeAnyMime) for the case if this worker quits
                // the game
                continue;
            }
            workers.append(wrk);
            // Don't search for other workrers for the same mime type --
            // we've already got a drag worker for it and adding another
            // one would just introduce mime type duplicates on the drop
            // target's side (where the first encountered drop worker
            // for the given RMF would be used for actual data transfer
            // anyway). See also QClipboard::setData().
            break;
        }
        if (gotExclForMime) {
            // ensure we have a fall-back coop (should be the last added item)
            DragWorker *w = workers.last();
            if (w->isExclusive() || w->itemCount() > 1) {
                DEBUG(() << "QPMCoopDragWorker: DragWorker" << w
                         << "for" << fmt << "( isExclusive" << w->isExclusive()
                         << "itemCount" << w->itemCount()
                         <<") has no fall-back cooperative 1-item worker!");
                workers.removeLast();
            }
            gotExclForMime = false;
        } else {
            // got a regular (non-fall-back) 1-item coop, skip evreything else
            skipExcl = true;
        }
    }

    // remove either all exclusive workers or all their fall-back workers
    // (depending on skipExcl) and remove duplicates
    for (QList<DragWorker*>::iterator it = workers.begin();
         it < workers.end();) {
        DragWorker *wrk = *it;
        bool excl = wrk->isExclusive() || wrk->itemCount() > 1;
        if (skipExcl == excl || workers.count(wrk) > 1) {
            it = workers.erase(it);
        } else {
            ++it;
        }
    }

#if defined(QDND_DEBUG)
    foreach (DragWorker *wrk, workers) {
        DEBUG(() << "QPMCoopDragWorker: Will use worker" << wrk
                 << "( isExclusive" << wrk->isExclusive()
                 << "itemCount" << wrk->itemCount() << ")");
    }
#endif

    if (!mimeData->formats().count()) {
        // The drag source doesn't provide any format, so we've got no workers.
        // Although it may look strange, it is a supported case: for example,
        // designer uses it a lot for in-process DnD. Instead of MIME data, it
        // works directly with the QMimeData object from the drag source. We
        // will handle this situation by providing a dummy DRAGINFO structure.
        DEBUG(() << "QPMCoopDragWorker: Source provides NO data, will use "
                    "dummy DRAGINFO");
        Q_ASSERT(!workers.count());
        return true;
    }

    Q_ASSERT(workers.count() > 0);
    return workers.count() > 0;
}

void QPMCoopDragWorker::init()
{
    Q_ASSERT(source());
    foreach(DragWorker *wrk, workers) {
        wrk->src = source();
        wrk->init();
    }
}

bool QPMCoopDragWorker::cleanup(bool isCancelled)
{
    bool moveDisallowed = false;

    foreach(DragWorker *wrk, workers) {
        // disallow the Move operation if at least one worker asked so
        moveDisallowed |= wrk->cleanup(isCancelled);
        wrk->src = 0;
    }
    workers.clear();
    info = 0;
    return moveDisallowed;
}

DRAGINFO *QPMCoopDragWorker::createDragInfo(const QString &targetName,
                                            USHORT supportedOps)
{
    Q_ASSERT(!info);
    if (info)
        return 0;

    if (!workers.count()) {
        // The drag source doesn't provide any format; provide a dummy DRAGINFO
        // with a null DRAGITEM that we will never render
        info = DrgAllocDraginfo(1);
        Q_ASSERT(info);
        if (!info)
            return 0;

        DRAGITEM *item = DrgQueryDragitemPtr(info, 0);
        Q_ASSERT(item);
        if (!item) {
            DrgFreeDraginfo(info);
            info = 0;
            return 0;
        }

        item->hwndItem = 0;
        item->ulItemID = 0;
        item->hstrType = 0;
        item->hstrRMF = 0;
        item->hstrContainerName = 0;
        item->hstrSourceName = 0;
        item->hstrTargetName = 0;
        item->cxOffset = 0;
        item->cyOffset = 0;
        item->fsControl = DC_PREPARE; // require DM_RENDERPREPARE from target
        item->fsSupportedOps = supportedOps;

        return info;
    }

    DragWorker *firstWorker = workers.first();
    Q_ASSERT(firstWorker);
    if (!firstWorker)
        return 0;

    ULONG itemCnt = firstWorker->itemCount();

    if (firstWorker->isExclusive() && itemCnt == 0) {
        // this is a super exclusive worker that will do everything on its own
        DEBUG(() << "QPMCoopDragWorker: Will redirect to super worker"
                 << firstWorker);
        return firstWorker->createDragInfo(targetName, supportedOps);
    }

    // note that all workers at this place require the same amount of items
    // (guaranteed by collectWorkers())

    DEBUG(() << "QPMCoopDragWorker: itemCnt" << itemCnt);

    info = DrgAllocDraginfo(itemCnt);
    Q_ASSERT(info);
    if (!info)
        return 0;

    // collect all mechanism/format pairs
    QByteArray allFormats;
    foreach (DragWorker *wrk, workers) {
        QByteArray formats = wrk->composeFormatString();
        Q_ASSERT(!formats.isNull());
        if (!formats.isNull()) {
            if (allFormats.isNull())
                allFormats = formats;
            else {
                allFormats += ",";
                allFormats += formats;
            }
        }
    }

    DEBUG(() << "QPMCoopDragWorker: allFormats" << allFormats);

    static ULONG itemID = 0;

    QString type;
    QString ext;
    firstWorker->defaultFileType(type, ext);

    bool ok = true;
    for (ULONG i = 0; i < itemCnt; ++i) {
        DRAGITEM *item = DrgQueryDragitemPtr(info, i);
        Q_ASSERT(item);
        if (!item) {
            ok = false;
            break;
        }

        QString name;
        if (itemCnt == 1)
            name = targetName;
        else
            name = QString(QLatin1String("%1 %2")).arg(targetName).arg(i + 1);

        if (!ext.isEmpty())
            name += QString(QLatin1String(".%1")).arg(ext);

        DEBUG(() << "QPMCoopDragWorker: item" << i << ": type" << type
                 << " name" << name);

        // Note 1: DRAGITEM::hstrType is actually ignored by WPS,
        // only the target extension matters.

        // Note 2: We're not required to fill in the hwndItem field because we
        // use the DC_PREPARE flag (to indicate it will be filled later, after
        // DM_RENDERPREPARE); however, Mozilla refuses to render if hwndItem
        // is initially 0. Set it to our HWND instead (we'll issue a warning if
        // DM_RENDER or DM_ENDCONVERSATION is erroneously sent to us)

        item->hwndItem = hwnd();
        item->ulItemID = itemID ++;
        item->hstrType = DrgAddStrHandle(!type.isEmpty() ?
                                         QFile::encodeName(type) : DRT_UNKNOWN);
        item->hstrRMF = DrgAddStrHandle(allFormats);
        item->hstrContainerName = 0;
        item->hstrSourceName = 0;
        item->hstrTargetName = DrgAddStrHandle(QFile::encodeName(name));
        item->cxOffset = 0;
        item->cyOffset = 0;
        item->fsControl = DC_PREPARE; // require DM_RENDERPREPARE from target
        item->fsSupportedOps = supportedOps;
    }

    if (!ok) {
        DrgFreeDraginfo(info);
        info = 0;
    }

    return info;
}

MRESULT QPMCoopDragWorker::message(ULONG msg, MPARAM mp1, MPARAM mp2)
{
    if (msg == DM_RENDERPREPARE) {
        if (!info) {
            DEBUG(() << "Drop target sent DM_RENDERPREPARE after the DnD "
                        "session is over!");
            // free the given DRAGTRANSFER structure to avoud memory leak
            DRAGTRANSFER *xfer = (DRAGTRANSFER *)mp1;
            if (xfer)
                qt_DrgFreeDragtransfer(xfer);
            return (MRESULT)FALSE;
        }

        DRAGTRANSFER *xfer = (DRAGTRANSFER *)mp1;
        Q_ASSERT(xfer && xfer->pditem);
        if (!xfer || !xfer->pditem)
            return (MRESULT)FALSE;

        // find the item's index (ordinal number)
        ULONG itemCnt = DrgQueryDragitemCount(info);
        ULONG index = 0;
        for (; index < itemCnt; ++index)
            if (DrgQueryDragitemPtr(info, index) == xfer->pditem)
                break;

        Q_ASSERT(index < itemCnt);
        if (index >= itemCnt)
            return (MRESULT)FALSE;

        DEBUG(() << "QPMCoopDragWorker: Got DM_RENDERPREPARE to"
                 << QPMMime::queryHSTR(xfer->hstrSelectedRMF) << "for item"
                 << index << "( id" << xfer->pditem->ulItemID << ")");

        QByteArray drm, drf;
        if (!QPMMime::parseRMF(xfer->hstrSelectedRMF, drm, drf)) {
            Q_ASSERT(false);
            return (MRESULT)FALSE;
        }

        DragWorker *wrk = 0;
        foreach(wrk, workers)
            if (wrk->prepare(drm, drf, xfer->pditem, index))
                break;
        if (!wrk) {
            DEBUG(() << "QPMCoopDragWorker: No suitable worker found");
            return (MRESULT)FALSE;
        }

        xfer->pditem->hwndItem = wrk->hwnd();
        Q_ASSERT(xfer->pditem->hwndItem);
        return (MRESULT)(xfer->pditem->hwndItem ? TRUE : FALSE);
    }

    if (msg == DM_RENDER) {
        DEBUG(() << "Drop target sent DM_RENDER to the drag source window "
                    "instead of the drag item window, will try to forward!");
        DRAGTRANSFER *xfer = (DRAGTRANSFER *)mp1;
        Q_ASSERT(xfer && xfer->pditem);
        if (xfer && xfer->pditem &&
            xfer->pditem->hwndItem && xfer->pditem->hwndItem != hwnd())
            return WinSendMsg(xfer->pditem->hwndItem, msg, mp1, mp2);
    }
    else
    if (msg == DM_ENDCONVERSATION) {
        DEBUG(() << "Drop target sent DM_CONVERSATION to the drag source window "
                    "instead of the drag item window, will try to forward!");
        if (info) {
            ULONG itemId = LONGFROMMP(mp1);
            // find the item by ID
            ULONG itemCnt = DrgQueryDragitemCount(info);
            for (ULONG index = 0; index < itemCnt; ++index) {
                PDRAGITEM item = DrgQueryDragitemPtr(info, index);
                if (item->ulItemID == itemId &&
                    item->hwndItem && item->hwndItem != hwnd())
                    return WinSendMsg(item->hwndItem, msg, mp1, mp2);
            }
        }
    }

    return (MRESULT)FALSE;
}

//---------------------------------------------------------------------
//                    QDragManager
//---------------------------------------------------------------------

Qt::DropAction QDragManager::drag(QDrag *o)

{
    DEBUG(() << "QDragManager::drag");

    if (object == o || !o || !o->d_func()->source)
        return Qt::IgnoreAction;

    if (object) {
        cancel();
        qApp->removeEventFilter(this);
        beingCancelled = false;
    }

    // detect a mouse button to end dragging
    LONG vkTerminate = 0;
    {
        ULONG msg = WinQuerySysValue(HWND_DESKTOP, SV_BEGINDRAG) & 0xFFFF;
        switch(msg) {
            case WM_BUTTON1MOTIONSTART: vkTerminate = VK_BUTTON1; break;
            case WM_BUTTON2MOTIONSTART: vkTerminate = VK_BUTTON2; break;
            case WM_BUTTON3MOTIONSTART: vkTerminate = VK_BUTTON3; break;
        }

        if (WinGetKeyState(HWND_DESKTOP, vkTerminate) & 0x8000) {
            // prefer the default button if it is pressed
        } else if (WinGetKeyState(HWND_DESKTOP, VK_BUTTON2) & 0x8000) {
            vkTerminate = VK_BUTTON2;
        } else if (WinGetKeyState(HWND_DESKTOP, VK_BUTTON1) & 0x8000) {
            vkTerminate = VK_BUTTON1;
        } else if (WinGetKeyState(HWND_DESKTOP, VK_BUTTON3) & 0x8000) {
            vkTerminate = VK_BUTTON3;
        } else {
            vkTerminate = 0;
        }
    }

    if (!vkTerminate) {
        DEBUG(() << "QDragManager::drag: No valid mouse button pressed, "
                    "dragging cancelled!");
        o->deleteLater();
        return Qt::IgnoreAction;
    }

    USHORT supportedOps = toPmDragDropOps(o->d_func()->possible_actions);

    static QPMCoopDragWorker dragWorker;

    bool ok = dragWorker.collectWorkers(o);
    Q_ASSERT(ok);
    Q_ASSERT(dragWorker.hwnd());
    if (!ok || !dragWorker.hwnd()) {
        o->deleteLater();
        return Qt::IgnoreAction;
    }

    dragWorker.src = o->mimeData();
    dragWorker.init();
    DRAGINFO *info = dragWorker.createDragInfo(o->objectName(), supportedOps);

    Q_ASSERT(info);
    if (!info) {
        dragWorker.cleanup(true /* isCancelled */);
        dragWorker.src = 0;
        o->deleteLater();
        return Qt::IgnoreAction;
    }

    object = o;

    DEBUG(() << "QDragManager::drag: actions"
             << dragActionsToString(dragPrivate()->possible_actions));

    dragPrivate()->target = 0;

#ifndef QT_NO_ACCESSIBILITY
    QAccessible::updateAccessibility(this, 0, QAccessible::DragDropStart);
#endif

    DRAGIMAGE img;
    img.cb = sizeof(DRAGIMAGE);

    QPixmap pm = dragPrivate()->pixmap;
    if (!pm.isNull()) {
        if (pm.hasAlpha()) {
            // replace the black pixels with the neutral default window
            // background color
            QPixmap pm2(pm.width(), pm.height());
            pm2.fill(QApplication::palette().color(QPalette::Window));
            QPainter p(&pm2);
            p.drawPixmap(0, 0, pm);
            p.end();
            pm = pm2;
        }
        QPoint hot = dragPrivate()->hotspot;
        img.fl = DRG_BITMAP;
        img.hImage = pm.toPmHBITMAP();
        img.cxOffset = -hot.x();
        img.cyOffset = -(pm.height() - hot.y() - 1);
    } else {
        img.fl = DRG_ICON;
        img.hImage = WinQuerySysPointer(HWND_DESKTOP, SPTR_FILE, FALSE);
        img.cxOffset = 0;
        img.cyOffset = 0;
    }

    // the mouse is most likely captured by Qt at this point, uncapture it
    // or DrgDrag() will definitely fail
    WinSetCapture(HWND_DESKTOP, 0);

    HWND target = DrgDrag(dragWorker.hwnd(), info, &img, 1, vkTerminate,
                          (PVOID)0x80000000L); // don't lock the desktop PS

    DEBUG(("QDragManager::drag: DrgDrag() returned %08lX (error 0x%08lX)",
            target, WinGetLastError(0)));

    // we won't get any mouse release event, so manually adjust qApp state
    qt_pmMouseButtonUp();

    bool moveDisallowed = dragWorker.cleanup(beingCancelled || target == 0);
    dragWorker.src = 0;

    moveDisallowed |= beingCancelled || target == 0 ||
                      info->usOperation != DO_MOVE;

    DEBUG(() << "QDragManager::drag: moveDisallowed" << moveDisallowed);

    Qt::DropAction ret = Qt::IgnoreAction;
    if (target != 0) {
        ret = toQDragDropAction(info->usOperation);
        if (moveDisallowed && info->usOperation == DO_MOVE)
            ret = Qt::TargetMoveAction;
    }

    DEBUG(() << "QDragManager::drag: result" << dragActionsToString(ret));

    if (target == 0)
        DrgDeleteDraginfoStrHandles(info);
    DrgFreeDraginfo(info);

    if (!beingCancelled) {
        dragPrivate()->target = QWidget::find(target);
        cancel(); // this will delete o (object)
    }

    beingCancelled = false;

#ifndef QT_NO_ACCESSIBILITY
    QAccessible::updateAccessibility(this, 0, QAccessible::DragDropEnd);
#endif

    return ret;
}

void QDragManager::cancel(bool deleteSource)
{
    // Note: the only place where this function is called with
    // deleteSource = false so far is QDrag::~QDrag()

    Q_ASSERT(object && !beingCancelled);
    if (!object || beingCancelled)
        return;

    beingCancelled = true;

    object->setMimeData(0);

    if (deleteSource)
        object->deleteLater();
    object = 0;

#ifndef QT_NO_CURSOR
    // insert cancel code here ######## todo

    if (restoreCursor) {
        QApplication::restoreOverrideCursor();
        restoreCursor = false;
    }
#endif
#ifndef QT_NO_ACCESSIBILITY
    QAccessible::updateAccessibility(this, 0, QAccessible::DragDropEnd);
#endif
}

void QDragManager::updatePixmap()
{
    // not used in PM implementation
}

bool QDragManager::eventFilter(QObject *, QEvent *)
{
    // not used in PM implementation
    return false;
}

void QDragManager::timerEvent(QTimerEvent*)
{
    // not used in PM implementation
}

void QDragManager::move(const QPoint &)
{
    // not used in PM implementation
}

void QDragManager::drop()
{
    // not used in PM implementation
}

QT_END_NAMESPACE

#endif // QT_NO_DRAGANDDROP
