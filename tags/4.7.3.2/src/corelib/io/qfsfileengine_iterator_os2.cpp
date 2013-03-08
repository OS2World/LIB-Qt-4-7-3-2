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

// temporary, until struct dirent in kLIBC gets creation and access time fields
#define QT_OS2_USE_DOSFINDFIRST

#include "qplatformdefs.h"
#include "qfsfileengine_iterator_p.h"

#include <qt_os2.h>

#include <QtCore/qvariant.h>

#include <unistd.h>
#ifndef QT_OS2_USE_DOSFINDFIRST
#include <dirent.h>
#else
#include <QtCore/qdatetime.h>
#include "qfsfileengine.h"
#include "qfileinfo_p.h"
#endif

#ifndef QT_NO_FSFILEENGINE

QT_BEGIN_NAMESPACE

#ifndef QT_OS2_USE_DOSFINDFIRST

class QFSFileEngineIteratorPlatformSpecificData
{
public:
    inline QFSFileEngineIteratorPlatformSpecificData()
        : dir(0), dirEntry(0), root(false), seenDot(false), seenDotDot(false)
        , done(false)
#if defined(_POSIX_THREAD_SAFE_FUNCTIONS)
          , mt_file(0)
#endif
    { }

    DIR *dir;
    dirent *dirEntry;
    bool root : 1;
    bool seenDot : 1;
    bool seenDotDot : 1;
    bool done : 1;

#if defined(_POSIX_THREAD_SAFE_FUNCTIONS)
    // for readdir_r
    dirent *mt_file;
#endif
};

#else // !QT_OS2_USE_DOSFINDFIRST

class QFSFileEngineIteratorPlatformSpecificData : public QFileInfo
{
public:
    inline QFSFileEngineIteratorPlatformSpecificData()
        : root(false), seenDot(false), seenDotDot(false)
        , done(false), hdir(HDIR_CREATE)
    {
        memset(&findBuf, 0, sizeof(findBuf));
    }

    APIRET filterOutDots();

    static QDateTime convertFileDateTime(FDATE fdate, FTIME ftime);
    void updateFileInfo(const QString &path, FILEFINDBUF3L *ffb);
    void resetFileInfo() { d_ptr->clear(); }

    bool root : 1;
    bool seenDot : 1;
    bool seenDotDot : 1;
    bool done : 1;
    HDIR hdir;
    FILEFINDBUF3L findBuf;
};

#endif // !QT_OS2_USE_DOSFINDFIRST

void QFSFileEngineIterator::advance()
{
#ifndef QT_OS2_USE_DOSFINDFIRST

    currentEntry = platform->dirEntry ? QFile::decodeName(QByteArray(platform->dirEntry->d_name)) : QString();

    if (!platform->dir)
        return;

#if defined(_POSIX_THREAD_SAFE_FUNCTIONS)
    if (::readdir_r(platform->dir, platform->mt_file, &platform->dirEntry) != 0)
        platform->dirEntry = 0;
    // filter out "." and ".." from root to have consistent behavior across
    // different IFSes (some report them, some don't).
    while (platform->root && platform->dirEntry &&
           ((!platform->seenDot &&
             (platform->seenDot = qstrcmp(platform->dirEntry->d_name, ".")) == 0) ||
            (!platform->seenDotDot &&
             (platform->seenDotDot = qstrcmp(platform->dirEntry->d_name, "..")) == 0)))
        if (::readdir_r(platform->dir, platform->mt_file, &platform->dirEntry) != 0)
            platform->dirEntry = 0;
#else
    // ### add local lock to prevent breaking reentrancy
    platform->dirEntry = ::readdir(platform->dir);
    // filter out "." and ".." from root to have consistent behavior across
    // different IFSes (some report them, some don't).
    while (platform->root && platform->dirEntry &&
           ((!platform->seenDot &&
             (platform->seenDot = qstrcmp(platform->dirEntry->d_name, ".")) == 0) ||
            (!platform->seenDotDot &&
             (platform->seenDotDot = qstrcmp(platform->dirEntry->d_name, "..")) == 0)))
        platform->dirEntry = ::readdir(platform->dir);
#endif // _POSIX_THREAD_SAFE_FUNCTIONS
    if (!platform->dirEntry) {
        ::closedir(platform->dir);
        platform->dir = 0;
        platform->done = true;
#if defined(_POSIX_THREAD_SAFE_FUNCTIONS)
        delete [] platform->mt_file;
        platform->mt_file = 0;
#endif
    }

#else // !QT_OS2_USE_DOSFINDFIRST

    if (platform->hdir == HDIR_CREATE) {
        platform->resetFileInfo();
        currentEntry.clear();
        return;
    }

    if (platform->root && platform->filterOutDots() != NO_ERROR)
        return;

    // set QFileInfo from find buffer
    platform->updateFileInfo(path(), &platform->findBuf);
    currentEntry = platform->fileName();

    ULONG count = 1;
    if (DosFindNext(platform->hdir, &platform->findBuf,
                    sizeof(platform->findBuf), &count) != NO_ERROR) {
        DosFindClose(platform->hdir);
        platform->hdir = HDIR_CREATE;
        platform->done = true;
    }

#endif // !QT_OS2_USE_DOSFINDFIRST
}

void QFSFileEngineIterator::newPlatformSpecifics()
{
    platform = new QFSFileEngineIteratorPlatformSpecificData;
}

void QFSFileEngineIterator::deletePlatformSpecifics()
{
#ifndef QT_OS2_USE_DOSFINDFIRST

    if (platform->dir) {
        ::closedir(platform->dir);
#if defined(_POSIX_THREAD_SAFE_FUNCTIONS)
        delete [] platform->mt_file;
        platform->mt_file = 0;
#endif
    }

#else // !QT_OS2_USE_DOSFINDFIRST

    if (platform->hdir != HDIR_CREATE)
        DosFindClose(platform->hdir);

#endif // !QT_OS2_USE_DOSFINDFIRST

    delete platform;
    platform = 0;
}

bool QFSFileEngineIterator::hasNext() const
{
#ifndef QT_OS2_USE_DOSFINDFIRST
    if (!platform->done && !platform->dir) {
#else
    if (!platform->done && platform->hdir == HDIR_CREATE) {
#endif
        QFSFileEngineIterator *that = const_cast<QFSFileEngineIterator *>(this);
        QString path = that->path();
        // ensure the directory ends with a slash, otherwise for paths like 'X:'
        // we'll get the current directory's listing instead of the expected root
        if (!path.endsWith(QLatin1Char('/')))
            path.append(QLatin1Char('/'));
        APIRET arc = NO_ERROR;
        if (path.length() >= 2 && path.at(0).isLetter() && path.at(1) == QLatin1Char(':')) {
            that->platform->root = path.length() == 3 && (path.at(2) == QLatin1Char('/'));
            // check that the drive is ready before trying to open it which reduces
            // delays and noise for drives with no media inserted
            BYTE drv[3] = { path.at(0).cell(), ':', '\0' };
            HFILE hdrv;
            ULONG action;
            arc = DosOpen(drv, &hdrv, &action, 0, FILE_NORMAL, FILE_OPEN,
                          OPEN_ACCESS_READONLY | OPEN_SHARE_DENYNONE |
                          OPEN_FLAGS_DASD | OPEN_FLAGS_FAIL_ON_ERROR, NULL);
            if (arc == NO_ERROR) {
                // sometimes, 0 is returned for drive A: with no media in it
                // but SYS0045 pops up when doing stat(). The trick is to
                // read from the handle to detect this case.
                char buf;
                arc = DosRead(hdrv, &buf, sizeof(buf), &action);
                if (arc != ERROR_SECTOR_NOT_FOUND)
                    arc = NO_ERROR;
                DosClose(hdrv);
            }
        } else {
            that->platform->root = path.length() == 1 && (path.at(0) == QLatin1Char('/'));
        }

        if (arc == ERROR_NOT_READY || arc == ERROR_DISK_CHANGE ||
            arc == ERROR_SECTOR_NOT_FOUND || arc == ERROR_INVALID_DRIVE) {
            that->platform->done = true;
#ifndef QT_OS2_USE_DOSFINDFIRST
        } else if ((that->platform->dir = ::opendir(QFile::encodeName(path).data())) == 0) {
            that->platform->done = true;
        } else {
            long maxPathName = CCHMAXPATH;
            maxPathName += sizeof(dirent) + 1;
#if defined(_POSIX_THREAD_SAFE_FUNCTIONS)
            if (that->platform->mt_file)
                delete [] that->platform->mt_file;
            that->platform->mt_file = (dirent *)new char[maxPathName];
#endif
            that->advance();
#else // !QT_OS2_USE_DOSFINDFIRST
        } else {
            // DosFindFirst is unaware of symlinks; help it with canonicalFilePath()
            QString mask = QFileInfo(path).canonicalFilePath().append(QLatin1String("/*"));
            mask = QDir::toNativeSeparators(QDir::cleanPath(mask));
            ULONG count = 1;
            if (DosFindFirst(QFile::encodeName(mask).constData(),
                             &that->platform->hdir,
                             FILE_NORMAL | FILE_READONLY | FILE_HIDDEN |
                             FILE_SYSTEM | FILE_DIRECTORY | FILE_ARCHIVED,
                             &that->platform->findBuf, sizeof(that->platform->findBuf),
                             &count, FIL_STANDARDL) != NO_ERROR ||
                (platform->root && platform->filterOutDots() != NO_ERROR)) {
                that->platform->done = true;
            } else {
                that->platform->resetFileInfo();
                that->currentEntry.clear();
            }
#endif // !QT_OS2_USE_DOSFINDFIRST
        }
    }

    return !platform->done;
}

QFileInfo QFSFileEngineIterator::currentFileInfo() const
{
#ifndef QT_OS2_USE_DOSFINDFIRST
    return QAbstractFileEngineIterator::currentFileInfo();
#else
    // return a copy of our cached file info
    return *platform;
#endif
}

#ifdef QT_OS2_USE_DOSFINDFIRST

// Used to filter out "." and ".." from root to have consistent behavior across
// different IFSes (some report them, some don't).
APIRET QFSFileEngineIteratorPlatformSpecificData::filterOutDots()
{
    while ((!seenDot && (seenDot = qstrcmp(findBuf.achName, ".")) == 0) ||
           (!seenDotDot && (seenDotDot = qstrcmp(findBuf.achName, "..") == 0))) {
        APIRET arc;
        ULONG count = 1;
        if ((arc = DosFindNext(hdir, &findBuf,
                               sizeof(findBuf), &count)) != NO_ERROR) {
            DosFindClose(hdir);
            hdir = HDIR_CREATE;
            done = true;
            return arc;
        }
    }
    return NO_ERROR;
}

//static
QDateTime QFSFileEngineIteratorPlatformSpecificData::convertFileDateTime(FDATE fdate,
                                                                         FTIME ftime)
{
    QDateTime dt;

    dt.setDate(QDate(fdate.year + 1980, fdate.month, fdate.day));
    dt.setTime(QTime(ftime.hours, ftime.minutes, ftime.twosecs * 2));

    return dt;
}

void QFSFileEngineIteratorPlatformSpecificData::updateFileInfo(const QString &path,
                                                               FILEFINDBUF3L *ffb)
{
    QString fileName = path;
    if (!fileName.endsWith(QLatin1Char('/')))
        fileName.append(QLatin1Char('/'));
    fileName.append(QFile::decodeName(QByteArray(ffb->achName)));
    d_ptr = new QFileInfoPrivate(fileName);

    d_ptr->fileFlags = QAbstractFileEngine::ExistsFlag    |
                       QAbstractFileEngine::LocalDiskFlag |
                       QAbstractFileEngine::ReadOwnerPerm |
                       QAbstractFileEngine::ReadUserPerm  |
                       QAbstractFileEngine::ReadGroupPerm |
                       QAbstractFileEngine::ReadOtherPerm;

    if ((ffb->attrFile & FILE_READONLY) == 0) {
        d_ptr->fileFlags |= QAbstractFileEngine::WriteOwnerPerm |
                            QAbstractFileEngine::WriteUserPerm  |
                            QAbstractFileEngine::WriteGroupPerm |
                            QAbstractFileEngine::WriteOtherPerm;
    }

    // we have to use stat() to see if it's a kLIBC-like symlink
    QByteArray fn = QFile::encodeName(fileName);
    QT_STATBUF st;
    if ((QT_LSTAT(fn.constData(), &st) == 0) && S_ISLNK(st.st_mode))
    {
        char buf[PATH_MAX];
        if (realpath(fn.constData(), buf) != NULL)
        {
            d_ptr->fileFlags |= QAbstractFileEngine::LinkType;
            // query the real file instead of the symlink
            FILESTATUS3L fst;
            APIRET arc = DosQueryPathInfo(buf, FIL_STANDARDL, &fst, sizeof(fst));
            if (arc == NO_ERROR)
            {
                // replace the information in the find buffer
                ffb->fdateCreation = fst.fdateCreation;
                ffb->ftimeCreation = fst.ftimeCreation;
                ffb->fdateLastAccess = fst.fdateLastAccess;
                ffb->ftimeLastAccess = fst.ftimeLastAccess;
                ffb->fdateLastWrite = fst.fdateLastWrite;
                ffb->ftimeLastWrite = fst.ftimeLastWrite;
                ffb->cbFile = fst.cbFile;
                ffb->cbFileAlloc = fst.cbFileAlloc;
                ffb->attrFile = fst.attrFile;
            }
            else
            {
                // mark as broken symlink
                d_ptr->fileFlags &= ~QAbstractFileEngine::ExistsFlag;
            }
        }
    }

    if (ffb->attrFile & FILE_DIRECTORY)
        d_ptr->fileFlags |= QAbstractFileEngine::DirectoryType;
    else
        d_ptr->fileFlags |= QAbstractFileEngine::FileType;

    if (ffb->attrFile & FILE_HIDDEN)
        d_ptr->fileFlags |= QAbstractFileEngine::HiddenFlag;

    d_ptr->fileTimes[QAbstractFileEngine::CreationTime] =
            convertFileDateTime(ffb->fdateCreation, ffb->ftimeCreation);

    d_ptr->fileTimes[QAbstractFileEngine::ModificationTime] =
            convertFileDateTime(ffb->fdateLastWrite, ffb->ftimeLastWrite);

    d_ptr->fileTimes[QAbstractFileEngine::AccessTime] =
            convertFileDateTime(ffb->fdateLastAccess, ffb->ftimeLastAccess);

    d_ptr->fileSize = ffb->cbFile;

    // mark fields as "cached" and "present"
    d_ptr->cachedFlags  = QFileInfoPrivate::CachedSize |
                          QFileInfoPrivate::CachedFileFlags |
                          QFileInfoPrivate::CachedLinkTypeFlag |
                          QFileInfoPrivate::CachedBundleTypeFlag |
                          QFileInfoPrivate::CachedMTime |
                          QFileInfoPrivate::CachedCTime |
                          QFileInfoPrivate::CachedATime;
    d_ptr->cache_enabled = 1;
}

#endif // QT_OS2_USE_DOSFINDFIRST

QT_END_NAMESPACE

#endif // QT_NO_FSFILEENGINE
