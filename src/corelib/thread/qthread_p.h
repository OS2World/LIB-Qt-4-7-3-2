/****************************************************************************
**
** Copyright (C) 2011 Nokia Corporation and/or its subsidiary(-ies).
** All rights reserved.
** Contact: Nokia Corporation (qt-info@nokia.com)
**
** This file is part of the QtCore module of the Qt Toolkit.
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

#ifndef QTHREAD_P_H
#define QTHREAD_P_H

//
//  W A R N I N G
//  -------------
//
// This file is not part of the Qt API.  It exists purely as an
// implementation detail.  This header file may change from version to
// version without notice, or even be removed.
//
// We mean it.
//
//

#include "qplatformdefs.h"
#include "QtCore/qthread.h"
#include "QtCore/qmutex.h"
#include "QtCore/qstack.h"
#include "QtCore/qwaitcondition.h"
#include "QtCore/qmap.h"
#include "private/qobject_p.h"

#ifdef Q_OS_SYMBIAN
#include <e32base.h>
#endif

#if defined(Q_OS_OS2)
# include <qt_os2.h>
#endif

QT_BEGIN_NAMESPACE

class QAbstractEventDispatcher;
class QEventLoop;

class QPostEvent
{
public:
    QObject *receiver;
    QEvent *event;
    int priority;
    inline QPostEvent()
        : receiver(0), event(0), priority(0)
    { }
    inline QPostEvent(QObject *r, QEvent *e, int p)
        : receiver(r), event(e), priority(p)
    { }
};
inline bool operator<(int priority, const QPostEvent &pe)
{
    return pe.priority < priority;
}
inline bool operator<(const QPostEvent &pe, int priority)
{
    return priority < pe.priority;
}

class QPostEventList : public QList<QPostEvent>
{
public:
    // recursion == recursion count for sendPostedEvents()
    int recursion;

    // sendOffset == the current event to start sending
    int startOffset;
    // insertionOffset == set by sendPostedEvents to tell postEvent() where to start insertions
    int insertionOffset;

    QMutex mutex;

    inline QPostEventList()
        : QList<QPostEvent>(), recursion(0), startOffset(0), insertionOffset(0)
    { }
};

#ifndef QT_NO_THREAD

class QThreadPrivate : public QObjectPrivate
{
    Q_DECLARE_PUBLIC(QThread)

public:
    QThreadPrivate(QThreadData *d = 0);
    ~QThreadPrivate();

    mutable QMutex mutex;

    bool running;
    bool finished;
    bool terminated;

    bool exited;
    int returnCode;

    uint stackSize;
    QThread::Priority priority;

    static QThread *threadForId(int id);

#ifdef Q_OS_UNIX
    pthread_t thread_id;
    QWaitCondition thread_done;

    static void *start(void *arg);
#if defined(Q_OS_SYMBIAN)
    static void finish(void *arg, bool lockAnyway=true, bool closeNativeHandle=true);
#else
    static void finish(void *);
#endif
#endif // Q_OS_UNIX

#if defined(Q_OS_WIN32) || defined(Q_OS_WINCE)
    HANDLE handle;
    unsigned int id;
    int waiters;

    static unsigned int __stdcall start(void *);
    static void finish(void *, bool lockAnyway=true);
#endif // Q_OS_WIN32

#if defined(Q_OS_WIN32) || defined(Q_OS_WINCE) || defined (Q_OS_SYMBIAN)
    bool terminationEnabled, terminatePending;
# endif

#if defined(Q_OS_OS2)
    TID tid;
    bool terminationEnabled, terminatePending;

    void setPriority(QThread::Priority priority);

    static void start(void *);
    static void finish(void *, bool lockAnyway=true);
#endif

    QThreadData *data;

    static void createEventDispatcher(QThreadData *data);
};

#else // QT_NO_THREAD

class QThreadPrivate : public QObjectPrivate
{
public:
    QThreadPrivate(QThreadData *d = 0) : data(d ? d : new QThreadData) {}
    ~QThreadPrivate() { delete data; }

    QThreadData *data;

    static void setCurrentThread(QThread*) {}
    static QThread *threadForId(int) { return QThread::currentThread(); }
    static void createEventDispatcher(QThreadData *data);

    Q_DECLARE_PUBLIC(QThread)
};

#endif // QT_NO_THREAD

class QThreadData
{
    QAtomicInt _ref;

public:
    QThreadData(int initialRefCount = 1);
    ~QThreadData();

    static QThreadData *current();
    static QThreadData *get2(QThread *thread)
    { Q_ASSERT_X(thread != 0, "QThread", "internal error"); return thread->d_func()->data; }


    void ref();
    void deref();

    QThread *thread;
    bool quitNow;
    int loopLevel;
    QAbstractEventDispatcher *eventDispatcher;
    QStack<QEventLoop *> eventLoops;
    QPostEventList postEventList;
    bool canWait;
    QVector<void *> tls;

    QMutex mutex;

# ifdef Q_OS_SYMBIAN
    RThread symbian_thread_handle;
# endif
};

// thread wrapper for the main() thread
class QAdoptedThread : public QThread
{
    Q_DECLARE_PRIVATE(QThread)

public:
    QAdoptedThread(QThreadData *data = 0);
    ~QAdoptedThread();
    void init();

    static QThread *createThreadForAdoption();
private:
    void run();
};

QT_END_NAMESPACE

#endif // QTHREAD_P_H
