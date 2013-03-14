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

#include "qclipboard.h"

#ifndef QT_NO_CLIPBOARD

#include "qapplication.h"
#include "qapplication_p.h"
#include "qeventloop.h"
#include "qwidget.h"
#include "qevent.h"
#include "qmime.h"
#include "qdnd_p.h"
#include "qclipboard_p.h"

#include "qt_os2.h"
#include "private/qpmobjectwindow_pm_p.h"

//#define QCLIPBOARD_DEBUG

#ifdef QCLIPBOARD_DEBUG
#   include "qdebug.h"
#   define DEBUG(a) qDebug a
#else
#   define DEBUG(a) do {} while(0)
#endif

QT_BEGIN_NAMESPACE

extern bool qt_sendSpontaneousEvent(QObject*, QEvent*); // qapplication.cpp

////////////////////////////////////////////////////////////////////////////////

class QClipboardWatcher : public QInternalMimeData
{
public:
    QClipboardWatcher() {}

    bool hasFormat_sys(const QString &mimetype) const;
    QStringList formats_sys() const;
    QVariant retrieveData_sys(const QString &mimetype, QVariant::Type preferredType) const;

private:

    bool peekData(bool leaveOpen = false) const;

    mutable QList<ULONG> formats;
    mutable QList<QPMMime::Match> matches;
};

bool QClipboardWatcher::peekData(bool leaveOpen) const
{
    if (!WinOpenClipbrd(NULLHANDLE)) {
#ifndef QT_NO_DEBUG
        qWarning("QClipboardWatcher::peekData: WinOpenClipbrd "
                 "failed with 0x%lX", WinGetLastError(NULLHANDLE));
#endif
        return false;
    }

    QList<ULONG> newFormats;
    ULONG cf = 0;
    while ((cf = WinEnumClipbrdFmts(NULLHANDLE, cf)))
        newFormats << cf;

    if (!leaveOpen)
        WinCloseClipbrd(NULLHANDLE);

    // optimization: we don't want to call the potentially expensive
    // allConvertersFromFormats() unlesss we really got a different set
    if (newFormats == formats)
        return true;

    formats = newFormats;
    matches = QPMMime::allConvertersFromFormats(formats);

#ifdef QCLIPBOARD_DEBUG
    foreach(ULONG cf, formats)
        DEBUG(("QClipboardWatcher::peekData: have format 0x%lX (%ls)",
               cf, QPMMime::formatName(cf).utf16()));
    foreach(QPMMime::Match match, matches)
        DEBUG(("QClipboardWatcher::peekData: converter %p mime \"%ls\" "
               "format 0x%lX priority %d", match.converter, match.mime.utf16(),
               match.format, match.priority));
#endif

    return true;
}

bool QClipboardWatcher::hasFormat_sys(const QString &mime) const
{
    if (!peekData())
        return false;

    foreach (QPMMime::Match match, matches)
        if (match.mime == mime)
            return true;

    return false;
}

QStringList QClipboardWatcher::formats_sys() const
{
    QStringList fmts;

    if (!peekData())
        return fmts;

    foreach (QPMMime::Match match, matches)
        fmts << match.mime;

    return fmts;
}

QVariant QClipboardWatcher::retrieveData_sys(const QString &mime,
                                             QVariant::Type type) const
{
    QVariant result;

    if (!peekData(true /*leaveOpen*/))
        return result;

    foreach (QPMMime::Match match, matches) {
        if (match.mime == mime) {
            ULONG flags;
            if (WinQueryClipbrdFmtInfo(NULLHANDLE, match.format, &flags)) {
                ULONG data = WinQueryClipbrdData(NULLHANDLE, match.format);
                result = match.converter->convertFromFormat(match.format, flags,
                                                            data, match.mime, type);
            }
            break;
        }
    }

    WinCloseClipbrd(NULLHANDLE);

    return result;
}

////////////////////////////////////////////////////////////////////////////////

class QClipboardData : public QPMObjectWindow
{
public:
    QClipboardData();
    ~QClipboardData();

    void setSource(QMimeData *s);

    void setAsClipboardViewer();
    bool ownsClipboard() const;
    void putAllMimeToClipboard(bool isDelayed);
    void flushClipboard();

    const QMimeData *mimeData() const;

    static QClipboardData *instance()
    {
        if (instancePtr == 0) {
            instancePtr = new QClipboardData;
        }
        Q_ASSERT(instancePtr);
        return instancePtr;
    }

    static void deleteInstance()
    {
        delete instancePtr;
        instancePtr = 0;
    }

private:
    bool setClipboard(QPMMime *converter, ULONG format, bool isDelayed);

    MRESULT message(ULONG msg, MPARAM mp1, MPARAM mp2);

    QMimeData *src;
    QList<QPMMime::Match> matches;
    HWND prevClipboardViewer;

    QClipboardWatcher watcher;

    bool ignore_WM_DESTROYCLIPBOARD;

    static QClipboardData *instancePtr;
};

// static
QClipboardData *QClipboardData::instancePtr = 0;

QClipboardData::QClipboardData()
    : src(0), prevClipboardViewer(NULLHANDLE)
    , ignore_WM_DESTROYCLIPBOARD(false)
{
}

QClipboardData::~QClipboardData()
{
    setSource(0);

    // make sure we are not the clipboard viewer any more
    if (hwnd() == WinQueryClipbrdViewer(NULLHANDLE))
        WinSetClipbrdViewer(NULLHANDLE, prevClipboardViewer);
}

void QClipboardData::setSource(QMimeData *s)
{
    if (s == src)
        return;
    delete src;
    src = s;

    // build the list of all mime <-> cf matches
    matches.clear();
    if (src)
        matches = QPMMime::allConvertersFromMimeData(src);

#ifdef QCLIPBOARD_DEBUG
    if (src) {
        DEBUG(() << "QClipboardData::setSource: mimes" << src->formats());
        foreach(QPMMime::Match match, matches)
            DEBUG(("QClipboardData::setSource: match: converter %p format 0x%lX "
                   "(%ls) priority %d", match.converter, match.format,
                   QPMMime::formatName(match.format).utf16(), match.priority));
    }
#endif
}

void QClipboardData::setAsClipboardViewer()
{
    DEBUG(("QClipboardData::setAsClipboardViewer"));

    HWND clipboardViewer = WinQueryClipbrdViewer(NULLHANDLE);
    if (hwnd() != clipboardViewer) {
        prevClipboardViewer = clipboardViewer;
        BOOL ok = WinSetClipbrdViewer(NULLHANDLE, hwnd());
        Q_UNUSED(ok);
#ifndef QT_NO_DEBUG
        if (!ok)
            qWarning("QClipboardData::setAsClipboardViewer: WinSetClipbrdViewer "
                     " failed with 0x%lX", WinGetLastError(NULLHANDLE));
#endif
    }
}

bool QClipboardData::ownsClipboard() const
{
    return src && hwnd() == WinQueryClipbrdOwner(NULLHANDLE);
}

bool QClipboardData::setClipboard(QPMMime *converter, ULONG format,
                                  bool isDelayed)
{
    Q_ASSERT(src);
    if (!src)
        return false;

    bool ok, ok2;
    ULONG flags = 0, data = 0;

    if (isDelayed) {
        // setup delayed rendering of clipboard data
        ok = converter->convertFromMimeData(src, format, flags, 0);
        if (ok) {
            WinSetClipbrdOwner(NULLHANDLE, hwnd());
            ok2 = WinSetClipbrdData(NULLHANDLE, 0, format, flags);
        }
    } else {
        // render now
        ok = converter->convertFromMimeData(src, format, flags, &data);
        if (ok)
            ok2 = WinSetClipbrdData(NULLHANDLE, data, format, flags);
    }
    DEBUG(("QClipboardData::setClipboard: convert to format 0x%lX (%ls) "
           "flags 0x%lX data 0x%lX delayed %d ok %d", format,
           QPMMime::formatName(format).utf16(), flags, data, isDelayed, ok));
#ifndef QT_NO_DEBUG
    if (!ok2) {
        qWarning("QClipboardData::setClipboard: WinSetClipbrdData "
                 "failed with 0x%lX", WinGetLastError(NULLHANDLE));
    }
#endif

    return ok && ok2;
}

void QClipboardData::putAllMimeToClipboard(bool isDelayed)
{
    DEBUG(() << "QClipboardData::putAllMimeToClipboard: isDelayed" << isDelayed);

    if (!WinOpenClipbrd(NULLHANDLE)) {
#ifndef QT_NO_DEBUG
        qWarning("QClipboardData::putAllMimeToClipboard: WinOpenClipbrd "
                 "failed with 0x%lX", WinGetLastError(NULLHANDLE));
#endif
        return;
    }

    // delete the clipboard contents before we render everything to make sure
    // nothing is left there from another owner
    ignore_WM_DESTROYCLIPBOARD = true;
    BOOL ok = WinEmptyClipbrd(NULLHANDLE);
    ignore_WM_DESTROYCLIPBOARD = false;
    if (!ok) {
#ifndef QT_NO_DEBUG
        qWarning("QClipboardData::putAllMimeToClipboard: WinEmptyClipbrd "
                 "failed with 0x%lX", WinGetLastError(NULLHANDLE));
#endif
        WinCloseClipbrd(NULLHANDLE);
        return;
    }

    if (src) {
        foreach(QPMMime::Match match, matches)
            setClipboard(match.converter, match.format, isDelayed);
    }

    WinCloseClipbrd(NULLHANDLE);
}

void QClipboardData::flushClipboard()
{
    if (ownsClipboard()) {
        putAllMimeToClipboard(false);
        // make sure we won't be doing this again if asked in WM_RENDERALLFMTS
        setSource(0);
    }
}

const QMimeData *QClipboardData::mimeData() const
{
    // short cut for local copy / paste
    if (ownsClipboard() && src)
        return src;
    return &watcher;
}

MRESULT QClipboardData::message(ULONG msg, MPARAM mp1, MPARAM mp2)
{
    DEBUG(("QClipboardData::message: msg %08lX, mp1 %p, mp2 %p",
           msg, mp1, mp2));

    switch (msg) {

        case WM_DRAWCLIPBOARD: {
            DEBUG(("QClipboardData::message: WM_DRAWCLIPBOARD"));

            if (hwnd() != WinQueryClipbrdOwner(NULLHANDLE) && src) {
                // we no longer own the clipboard, clean up the clipboard object
                setSource(0);
            }

            // ask QClipboard to emit changed() signals
            QClipboardEvent e(reinterpret_cast<QEventPrivate *>(1));
            qt_sendSpontaneousEvent(QApplication::clipboard(), &e);

            // PM doesn't inform the previous clipboard viewer if another
            // app changes it (nor does it support viewer chains in some other
            // way). The best we can do is to propagate the message to the
            // previous clipboard viewer ourselves (though there is no guarantee
            // that all non-Qt apps will do the same).
            if (prevClipboardViewer) {
                // propagate the message to the previous clipboard viewer
                BOOL ok = WinPostMsg(prevClipboardViewer, msg, mp1, mp2);
                if (!ok)
                    prevClipboardViewer = NULLHANDLE;
            }
        }
        break;

        case WM_DESTROYCLIPBOARD: {
            DEBUG(("QClipboardData::message: WM_DESTROYCLIPBOARD:"));
            if (!ignore_WM_DESTROYCLIPBOARD)
                setSource(0);
        }
        break;

        case WM_RENDERFMT: {
            DEBUG(("QClipboardData::message: WM_RENDERFMT: CF 0x%lX (%ls)",
                   (ULONG)mp1, QPMMime::formatName((ULONG)mp1).utf16()));
            if (src) {
                foreach(QPMMime::Match match, matches) {
                    if (match.format == (ULONG)mp1) {
                        setClipboard(match.converter, match.format, false);
                        break;
                    }
                }
            }
        }
        break;

        case WM_RENDERALLFMTS: {
            DEBUG(("QClipboardData::message: WM_RENDERALLFMTS:"));
            if (src) {
                foreach(QPMMime::Match match, matches)
                    setClipboard(match.converter, match.format, false);
            }
        }
        break;

        default:
            break;
    }

    DEBUG(("QClipboardData::message: END"));
    return FALSE;
}

////////////////////////////////////////////////////////////////////////////////

QClipboard::~QClipboard()
{
    QClipboardData::deleteInstance();
}

void QClipboard::setMimeData(QMimeData *src, Mode mode)
{
    DEBUG(() << "QClipboard::setMimeData: src" << src << "mode" << mode);

    if (mode != Clipboard) {
        delete src;
        return;
    }

    QClipboardData *d = QClipboardData::instance();
    d->setSource(src);

    if (!src)
        return; // nothing to do

    // use delayed rendering only if the application runs the event loop
    bool runsEventLoop = d_func()->threadData->loopLevel != 0;

    d->putAllMimeToClipboard(runsEventLoop);
}

void QClipboard::clear(Mode mode)
{
    setMimeData(0, Clipboard);
}

bool QClipboard::event(QEvent *e)
{
    if (e->type() != QEvent::Clipboard)
        return QObject::event(e);

    if (!((QClipboardEvent*)e)->data()) {
        // this is sent by QApplication to render all formats at app shut down
        QClipboardData::instance()->flushClipboard();
    } else {
        // this is sent by QClipboardData to notify about clipboard data change
        emitChanged(QClipboard::Clipboard);
    }

    return true;
}

void QClipboard::connectNotify(const char *signal)
{
    if (qstrcmp(signal, SIGNAL(dataChanged())) == 0) {
        // ensure we are up and running (by instantiating QClipboardData and
        // setting it as the clipboard viewer to receive notifications on
        // clipboard contents chages) but block signals so the dataChange signal
        // is not emitted while being connected to.
        bool blocked = blockSignals(true);
        QClipboardData::instance()->setAsClipboardViewer();
        blockSignals(blocked);
    }
}

const QMimeData *QClipboard::mimeData(Mode mode) const
{
    if (mode != Clipboard)
        return 0;

    return QClipboardData::instance()->mimeData();
}

bool QClipboard::supportsMode(Mode mode) const
{
    return (mode == Clipboard);
}

bool QClipboard::ownsMode(Mode mode) const
{
    if (mode == Clipboard) {
        return QClipboardData::instance()->ownsClipboard();
    }
    return false;
}

void QClipboard::ownerDestroyed()
{
    // not used
}

QT_END_NAMESPACE

#endif // QT_NO_CLIPBOARD
