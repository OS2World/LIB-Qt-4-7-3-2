/****************************************************************************
**
** Copyright (C) 2010 Nokia Corporation and/or its subsidiary(-ies).
** All rights reserved.
** Contact: Nokia Corporation (qt-info@nokia.com)
**
** Copyright (C) 2010 netlabs.org. OS/2 parts.
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

//#define QFILESYSTEMWATCHER_DEBUG

#include "qfilesystemwatcher.h"
#include "qfilesystemwatcher_os2_p.h"

#ifndef QT_NO_FILESYSTEMWATCHER

#include "qdir.h"
#include "qfileinfo.h"

#include "qdebug.h"
#ifdef QFILESYSTEMWATCHER_DEBUG
#define DEBUG(a) qDebug a
#else
#define DEBUG(a) do {} while(0)
#endif

// OS/2 has the Dos*ChangeNotify API for file system change notifications but
// this API may be only used once (i.e. by a single process on the system,
// all other attempt will return the "access denied" error) and this process
// is normally the Workplace Shell so there is no way for other applications
// to use this handy API. In order to overcome this limitation, XWorkplace
// (a Workplace Shell extension which lives in the same process as the WPS)
// takes over the WPS change notification function and extends it by sinking
// all changes to a named pipe. This pipe is utilized by this class to get
// notifications about file and directory changes.
//
// Note that when XWorkplace is not installed or when its "Enable folder auto-
// refresh" feature is disabled, the notification pipe will not be available and
// we will fall back to the polling filesystem watcher implementation.

// Notification server pipe name (taken from xworkplace/src/filesys/refresh.c)
#define PIPE_CHANGENOTIFY       "\\PIPE\\XNOTIFY"

// Taken from xworkplace/include/filesys/refresh.c
#pragma pack(1)
typedef struct _CNINFO {      /* CHANGENOTIFYINFO */
    ULONG   oNextEntryOffset;
    CHAR    bAction;
    USHORT  cbName;
    CHAR    szName[1];
} CNINFO;
typedef CNINFO *PCNINFO;
#pragma pack()

#define  RCNF_FILE_ADDED        0x0001
#define  RCNF_FILE_DELETED      0x0002
#define  RCNF_DIR_ADDED         0x0003
#define  RCNF_DIR_DELETED       0x0004
#define  RCNF_MOVED_IN          0x0005
#define  RCNF_MOVED_OUT         0x0006
#define  RCNF_CHANGED           0x0007
#define  RCNF_OLDNAME           0x0008
#define  RCNF_NEWNAME           0x0009
#define  RCNF_DEVICE_ATTACHED   0x000A
#define  RCNF_DEVICE_DETACHED   0x000B

QT_BEGIN_NAMESPACE

// -----------------------------------------------------------------------------

// we don't actually get RCNF_CHANGED from OS/2 when the file modification date
// or size changes (some really silly bug), so we need a poller thread that
// will query this information from time to time
class QOS2FileSysPollerThread : public QThread
{
private:

    enum { PollingInterval = 2000 };

    friend class QOS2FileSysWatcherThread;

    QOS2FileSysPollerThread();
    ~QOS2FileSysPollerThread();

    void addPaths(const QStringList &paths);
    void removePaths(const QStringList &paths);

    void run();

    void stop(QMutexLocker &locker);
    void stop() { QMutexLocker locker(&mutex); stop(locker); }

    bool finish;
    HEV eventSem;

    typedef QHash<QString, FILESTATUS3> PathMap;
    PathMap watchedPaths;
    QMutex mutex;
};

class QOS2FileSysWatcherThread : public QThread
{
public:

    static void addRef();
    static void release();

    static QStringList addPaths(QOS2FileSystemWatcherEngine *engine,
                                const QStringList &paths,
                                QStringList *files,
                                QStringList *directories);

    static QStringList removePaths(QOS2FileSystemWatcherEngine *engine,
                                   const QStringList &paths,
                                   QStringList *files,
                                   QStringList *directories);

    static void removePaths(QOS2FileSystemWatcherEngine *engine);

    static void reportChanges(const QStringList &paths);

    static bool isOk() { return instance->notifyPipe != NULLHANDLE; }

private:

    QOS2FileSysWatcherThread();
    ~QOS2FileSysWatcherThread();

    void run();

    void stop(QMutexLocker &locker);

    int refcnt;

    bool finish;
    HFILE notifyPipe;
    HEV eventSem;

    QOS2FileSysPollerThread poller;

    enum Type { None = 0, Dir, File };
    struct PathInfo
    {
        PathInfo(QString &p, Type t, QOS2FileSystemWatcherEngine *e = 0)
            : path(p), type(t) { if (e) engines.append(e); }
        QString path;
        Type type;
        QList<QOS2FileSystemWatcherEngine *> engines;
    };
    QHash<QString, QList<PathInfo> > watchedPaths;

    static QOS2FileSysWatcherThread *instance;
    static QMutex mutex;
};

// -----------------------------------------------------------------------------

QOS2FileSysPollerThread::QOS2FileSysPollerThread()
    : finish(false), eventSem(NULLHANDLE)
{
    APIRET arc;
    arc = DosCreateEventSem(NULL, &eventSem,
                            DC_SEM_SHARED | DCE_AUTORESET | DCE_POSTONE,
                            FALSE);
    Q_ASSERT(arc == NO_ERROR);
    Q_UNUSED(arc);
}

QOS2FileSysPollerThread::~QOS2FileSysPollerThread()
{
    Q_ASSERT(watchedPaths.isEmpty());

    if (eventSem)
        DosCloseEventSem(eventSem);
}

void QOS2FileSysPollerThread::addPaths(const QStringList &paths)
{
    APIRET arc;
    FILESTATUS3 newInfo;

    QMutexLocker locker(&mutex);

    foreach (const QString &path, paths) {
        arc = DosQueryPathInfo(QFile::encodeName(path), FIL_STANDARD,
                               &newInfo, sizeof(newInfo));
        DEBUG(() << "POLLER ADD" << path << arc);
        if (arc == NO_ERROR)
            watchedPaths.insert(path, newInfo);
    }

    if (!watchedPaths.isEmpty() && !isRunning()) {
        // (re)start the thread
        finish = false;
        start(IdlePriority);
    }
}

void QOS2FileSysPollerThread::removePaths(const QStringList &paths)
{
    QMutexLocker locker(&mutex);

    foreach (const QString &path, paths) {
        DEBUG(() << "POLLER REMOVE" << path);
        watchedPaths.remove(path);
    }

    if (watchedPaths.isEmpty())
        stop(locker);
}

void QOS2FileSysPollerThread::run()
{
    APIRET arc;
    QStringList changedPaths;

    QMutexLocker locker(&mutex);

    forever {
        locker.unlock();

        if (!changedPaths.isEmpty()) {
            // important: must be done outside the lock to avoid the deadlock
            // with QOS2FileSysWatcherThread that may be calling us
            QOS2FileSysWatcherThread::reportChanges(changedPaths);
            changedPaths.clear();
        }

        qDosNI(arc = DosWaitEventSem(eventSem, PollingInterval));

        locker.relock();

        //DEBUG(() << "*** POLLER" << watchedPaths.size() << "items");

        for (PathMap::iterator it = watchedPaths.begin();
             it != watchedPaths.end(); ++it) {
            QString path = it.key();
            FILESTATUS3 &info = it.value();

            FILESTATUS3 newInfo;
            arc = DosQueryPathInfo(QFile::encodeName(path), FIL_STANDARD,
                                   &newInfo, sizeof(newInfo));
            if (arc != NO_ERROR)
                continue;

            if (memcmp(&newInfo.fdateLastWrite,
                       &info.fdateLastWrite, sizeof(FDATE)) != 0 ||
                newInfo.cbFile != info.cbFile ||
                newInfo.cbFileAlloc != info.cbFileAlloc ||
                newInfo.attrFile != info.attrFile) {
                // there was a change, memorize it and update the info
                DEBUG(() << "POLLER CHANGE!" << path);
                changedPaths << path;
                info = newInfo;
            }
        }

        if (finish)
            break;
    }
}

void QOS2FileSysPollerThread::stop(QMutexLocker &locker)
{
    if (isRunning()) {
        finish = true;
        DosPostEventSem(eventSem);
        locker.unlock();
        wait();
    }
}

// -----------------------------------------------------------------------------

// static
QOS2FileSysWatcherThread *QOS2FileSysWatcherThread::instance = 0;
QMutex QOS2FileSysWatcherThread::mutex;

// static
void QOS2FileSysWatcherThread::addRef()
{
    QMutexLocker locker(&mutex);

    if (instance == 0) {
        instance = new QOS2FileSysWatcherThread();
    }

    ++instance->refcnt;
}

// static
void QOS2FileSysWatcherThread::release()
{
    QMutexLocker locker(&mutex);

    Q_ASSERT(instance);

    if (--instance->refcnt == 0) {
        // make sure we don't globally exist anymore before leaving the lock
        QOS2FileSysWatcherThread *instance = QOS2FileSysWatcherThread::instance;
        QOS2FileSysWatcherThread::instance = 0;
        instance->poller.stop();
        instance->stop(locker);
        delete instance;
    }
}

QOS2FileSysWatcherThread::QOS2FileSysWatcherThread()
    : refcnt(0), finish(false), notifyPipe(NULLHANDLE), eventSem(NULLHANDLE)
{
    ULONG dummy, retries = 3;
    APIRET arc;

    // Try to grab a free pipe instance. Retries are necessary because
    // DosWaitNPipe() is not "atomic" (even if it returns NO_ERROR, our next
    // DosOpen() can still fail due to some client being faster). Note that we
    // wait for max 1000ms on each try because the XWP pipe server may perform
    // a 1000ms delay between client connections under some circumstances.
    forever {
        arc = DosOpen(PIPE_CHANGENOTIFY, &notifyPipe, &dummy, 0, FILE_OPEN,
                      OPEN_ACTION_OPEN_IF_EXISTS,
                      OPEN_SHARE_DENYNONE | OPEN_ACCESS_READONLY |
                      OPEN_FLAGS_NOINHERIT, NULL);
        if (arc == ERROR_PIPE_BUSY && --retries) {
            arc = DosWaitNPipe(PIPE_CHANGENOTIFY, 1000);
            if (arc == NO_ERROR)
                continue;
        }
        break;
    }

    if (arc == NO_ERROR) {
        // make sure the pipe is non-blocking so that we can get ERROR_NO_DATA
        arc = DosSetNPHState(notifyPipe, NP_NOWAIT);
        Q_ASSERT(arc == NO_ERROR);
        // connect the pipe to the semaphore
        arc = DosCreateEventSem(NULL, &eventSem,
                                DC_SEM_SHARED | DCE_AUTORESET | DCE_POSTONE,
                                FALSE);
        Q_ASSERT(arc == NO_ERROR);
        arc = DosSetNPipeSem(notifyPipe, (HSEM)eventSem, 0);
        Q_ASSERT(arc == NO_ERROR);
    } else {
        notifyPipe = NULLHANDLE; // sanity (used by isOk())
        if (arc != ERROR_FILE_NOT_FOUND &&
            arc != ERROR_PATH_NOT_FOUND)
            qWarning("QOS2FileSystemWatcherEngine:: "
                     "DosOpen("PIPE_CHANGENOTIFY") returned %lu", arc);
    }
}

QOS2FileSysWatcherThread::~QOS2FileSysWatcherThread()
{
    Q_ASSERT(!refcnt);
    Q_ASSERT(watchedPaths.isEmpty());

    if (notifyPipe != NULLHANDLE) {
        DosCloseEventSem(eventSem);
        DosClose(notifyPipe);
    }
}

// static
QStringList QOS2FileSysWatcherThread::addPaths(QOS2FileSystemWatcherEngine *engine,
                                               const QStringList &paths,
                                               QStringList *files,
                                               QStringList *directories)
{
    QMutexLocker locker(&mutex);

    DEBUG(() << "WATCHER ADD" << paths);

    QStringList p = paths, pollerPaths;
    QMutableListIterator<QString> it(p);
    while (it.hasNext()) {
        QString path = it.next();
        QFileInfo fi(path);
        if (!fi.exists())
            continue;

        QString normalPath = fi.absoluteFilePath();
        normalPath = QDir::toNativeSeparators(QDir::cleanPath(path)).toUpper();
        Type type;
        if (fi.isDir()) {
            type = Dir;
            if (!directories->contains(path))
                directories->append(path);
        } else {
            type = File;
            if (!files->contains(path))
                files->append(path);
        }

        if (!instance->watchedPaths.contains(normalPath))
            pollerPaths << normalPath;

        QList<PathInfo> &variants = instance->watchedPaths[normalPath];
        bool found = false, alreadyAdded = false;
        for (QList<PathInfo>::iterator pathIt = variants.begin();
             pathIt != variants.end(); ++pathIt) {
            if (pathIt->path == path) {
                found = true;
                if (pathIt->engines.contains(engine))
                    alreadyAdded = true;
                else
                    pathIt->engines.append(engine);
                break;
            }
        }
        if (alreadyAdded)
            continue;

        if (!found)
            variants << PathInfo(path, type, engine);
        it.remove();
    }

    if (!instance->isRunning()) {
        // (re)start the watcher thread
        instance->finish = false;
        instance->start(IdlePriority);
    }

    // add new normal paths to the poller
    if (!pollerPaths.isEmpty())
        instance->poller.addPaths(pollerPaths);

    return p;
}

// static
QStringList QOS2FileSysWatcherThread::removePaths(QOS2FileSystemWatcherEngine *engine,
                                                  const QStringList &paths,
                                                  QStringList *files,
                                                  QStringList *directories)
{
    QMutexLocker locker(&mutex);

    DEBUG(() << "WATCHER REMOVE" << paths);

    QStringList p = paths, pollerPaths;
    QMutableListIterator<QString> it(p);
    while (it.hasNext()) {
        QString path = it.next();
        QString normalPath = QDir::current().absoluteFilePath(path);
        normalPath = QDir::toNativeSeparators(QDir::cleanPath(path)).toUpper();

        if (instance->watchedPaths.contains(normalPath)) {
            QList<PathInfo> &variants = instance->watchedPaths[normalPath];

            for (QList<PathInfo>::iterator pathIt = variants.begin();
                 pathIt != variants.end(); ++pathIt) {
                if (pathIt->path == path && pathIt->engines.contains(engine)) {
                    files->removeAll(path);
                    directories->removeAll(path);
                    pathIt->engines.removeAll(engine);
                    if (pathIt->engines.isEmpty())
                        variants.erase(pathIt);
                    it.remove();
                    break;
                }
            }

            if (variants.isEmpty()) {
                instance->watchedPaths.remove(normalPath);
                pollerPaths << normalPath;
            }
        }
    }

    // remove unwatched normal paths from the poller
    if (!pollerPaths.isEmpty())
        instance->poller.removePaths(pollerPaths);

    if (instance->watchedPaths.isEmpty())
        instance->stop(locker);

    return p;
}

// static
void QOS2FileSysWatcherThread::removePaths(QOS2FileSystemWatcherEngine *engine)
{
    QMutexLocker locker(&mutex);

    DEBUG(() << "WATCHER REMOVE" << engine);

    QStringList pollerPaths;
    foreach (const QString &normalPath, instance->watchedPaths.keys()) {
        QList<PathInfo> &variants = instance->watchedPaths[normalPath];
        for (QList<PathInfo>::iterator pathIt = variants.begin();
             pathIt != variants.end(); ++pathIt) {
            if (pathIt->engines.contains(engine)) {
                pathIt->engines.removeAll(engine);
                if (pathIt->engines.isEmpty())
                    variants.erase(pathIt);
                break;
            }
        }

        if (variants.isEmpty()) {
            instance->watchedPaths.remove(normalPath);
            pollerPaths << normalPath;
        }
    }

    // remove unwatched normal paths from the poller
    if (!pollerPaths.isEmpty())
        instance->poller.removePaths(pollerPaths);

    if (instance->watchedPaths.isEmpty())
        instance->stop(locker);
}

// static
void QOS2FileSysWatcherThread::reportChanges(const QStringList &paths)
{
    QMutexLocker locker(&mutex);

    foreach (const QString &path, paths) {
        // signal the change
        if (instance->watchedPaths.contains(path)) {
            QList<PathInfo> &variants = instance->watchedPaths[path];
            foreach(const PathInfo &pi, variants) {
                foreach(QOS2FileSystemWatcherEngine *e, pi.engines) {
                    if (pi.type == File) {
                        DEBUG(() << "WATCHER EMIT FILE"  << pi.path);
                        e->doFileChanged(pi.path, false);
                    } else {
                        DEBUG(() << "WATCHER EMIT DIR" << pi.path);
                        e->doDirectoryChanged(pi.path, false);
                    }
                }
            }
        }
    }
}

void QOS2FileSysWatcherThread::run()
{
    QByteArray buffer(sizeof(CNINFO) + _MAX_PATH + 2, 0);
    PCNINFO	info = (PCNINFO)buffer.data();
    APIRET arc;
    ULONG cbActual;

    QMutexLocker locker(&mutex);

    forever {
        locker.unlock();
        qDosNI(arc = DosWaitEventSem(eventSem, SEM_INDEFINITE_WAIT));
        locker.relock();

        if (finish)
            break;

        forever {
            buffer.fill(0);
            arc = DosRead(notifyPipe, buffer.data(), buffer.size(), &cbActual);
            if (arc == ERROR_NO_DATA) // no more records?
                break;
            if (arc == NO_ERROR && cbActual >= sizeof(CNINFO) && info->cbName) {
                Type type = None;
                switch (info->bAction) {
                case RCNF_FILE_ADDED:
                case RCNF_FILE_DELETED:
                    type = File;
                    break;
                case RCNF_DIR_ADDED:
                case RCNF_DIR_DELETED:
                    type = Dir;
                    break;
                case RCNF_CHANGED:
                case RCNF_NEWNAME: {
                    FILESTATUS3 stat;
                    arc = DosQueryPathInfo(info->szName, FIL_STANDARD, &stat, sizeof(stat));
                    if (arc == NO_ERROR)
                        type = (stat.attrFile & FILE_DIRECTORY) ? Dir : File;
                    break;
                }
                default:
                    break;
                }

                QString normalPath = QString::fromLocal8Bit(info->szName).toUpper();

                //DEBUG(() << "WATCHER NOTIFY" << (int) info->bAction << normalPath);

                switch (info->bAction) {
                case RCNF_FILE_DELETED:
                case RCNF_DIR_DELETED:
                case RCNF_OLDNAME:
                case RCNF_CHANGED:
                    // signal the change or deletion
                    if (watchedPaths.contains(normalPath)) {
                        QList<PathInfo> &variants = watchedPaths[normalPath];
                        QList<PathInfo> deletedVariants;
                        bool deleted = info->bAction != RCNF_CHANGED;
                        if (deleted) {
                            // make a copy as we will need to iterate over it
                            deletedVariants = variants;
                            variants = deletedVariants;
                            // remove the deleted path from watching
                            watchedPaths.remove(normalPath);
                            instance->poller.removePaths(QStringList(normalPath));
                        }
                        foreach(const PathInfo &pi, variants) {
                            if (type == None)
                                type = pi.type;
                            if (type == pi.type) {
                                foreach(QOS2FileSystemWatcherEngine *e, pi.engines) {
                                    if (type == File) {
                                        DEBUG(() << "WATCHER EMIT FILE"
                                                 << (int) info->bAction << pi.path
                                                 << deleted);
                                        e->doFileChanged(pi.path, deleted);
                                    } else {
                                        DEBUG(() << "WATCHER EMIT DIR"
                                                << (int) info->bAction << pi.path
                                                << deleted);
                                        e->doDirectoryChanged(pi.path, deleted);
                                    }
                                }
                            }
                        }
                    }
                    // deliberately fall through:
                case RCNF_FILE_ADDED:
                case RCNF_DIR_ADDED:
                case RCNF_NEWNAME:
                    // signal a change if we watch the parent directory
                    normalPath = normalPath.
                        left(qMax(normalPath.lastIndexOf(QLatin1Char('\\')), 0));
                    if (watchedPaths.contains(normalPath)) {
                        foreach(const PathInfo &pi, watchedPaths[normalPath]) {
                            if (pi.type == Dir) {
                                foreach(QOS2FileSystemWatcherEngine *e, pi.engines) {
                                    DEBUG(() << "WATCHER EMIT DIR"
                                             << (int) info->bAction << pi.path);
                                    e->doDirectoryChanged(pi.path, false);
                                }
                            }
                        }
                    }
                    break;
                default:
                    break;
                }
            } else {
                qWarning() << "QOS2FileSystemWatcherEngine: "
                              "DosRead("PIPE_CHANGENOTIFY") failed:"
                           << "arc" << arc << "cbActual" << cbActual
                           << "info->cbName" << info->cbName;
                break;
            }
        }
    }
}

void QOS2FileSysWatcherThread::stop(QMutexLocker &locker)
{
    if (isRunning()) {
        // stop the thread
        finish = true;
        DosPostEventSem(eventSem);
        locker.unlock();
        wait();
    }
}

// -----------------------------------------------------------------------------

QOS2FileSystemWatcherEngine::QOS2FileSystemWatcherEngine()
{
    QOS2FileSysWatcherThread::addRef();
}

QOS2FileSystemWatcherEngine::~QOS2FileSystemWatcherEngine()
{
    QOS2FileSysWatcherThread::release();
}

QStringList QOS2FileSystemWatcherEngine::addPaths(const QStringList &paths,
                                                  QStringList *files,
                                                  QStringList *directories)
{
    return QOS2FileSysWatcherThread::addPaths(this, paths, files, directories);
}

QStringList QOS2FileSystemWatcherEngine::removePaths(const QStringList &paths,
                                                     QStringList *files,
                                                     QStringList *directories)
{
    return QOS2FileSysWatcherThread::removePaths(this, paths, files, directories);
}

void QOS2FileSystemWatcherEngine::stop()
{
    QOS2FileSysWatcherThread::removePaths(this);
}

bool QOS2FileSystemWatcherEngine::isOk() const
{
    return QOS2FileSysWatcherThread::isOk();
}

QT_END_NAMESPACE

#endif // QT_NO_FILESYSTEMWATCHER
