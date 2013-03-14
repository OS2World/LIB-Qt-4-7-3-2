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

#include "qplatformdefs.h"
#include "qabstractfileengine.h"
#include "private/qfsfileengine_p.h"

#ifndef QT_NO_FSFILEENGINE

#ifndef QT_NO_REGEXP
# include "qregexp.h"
#endif
#include "qfile.h"
#include "qdir.h"
#include "qdatetime.h"
#include "qdebug.h"
#include "qvarlengtharray.h"

#include <sys/mman.h>
#include <stdlib.h>
#include <limits.h>
#include <errno.h>
#include <unistd.h>

#ifdef __INNOTEK_LIBC__
#include <InnoTekLIBC/pathrewrite.h>
#endif

#include <pwd.h>
#include <grp.h>

#define  INCL_DOSDEVIOCTL
#include "qt_os2.h"

QT_BEGIN_NAMESPACE

/*!
    \internal

    Returns the stdlib open string corresponding to a QIODevice::OpenMode.
*/
static QByteArray openModeToFopenMode(QIODevice::OpenMode flags, const QString &fileName = QString())
{
    QByteArray mode;
    if ((flags & QIODevice::ReadOnly) && !(flags & QIODevice::Truncate)) {
        mode = "rb";
        if (flags & QIODevice::WriteOnly) {
            if (!fileName.isEmpty() &&QFile::exists(fileName))
                mode = "rb+";
            else
                mode = "wb+";
        }
    } else if (flags & QIODevice::WriteOnly) {
        mode = "wb";
        if (flags & QIODevice::ReadOnly)
            mode += "+";
    }
    if (flags & QIODevice::Append) {
        mode = "ab";
        if (flags & QIODevice::ReadOnly)
            mode += "+";
    }
    return mode;
}

/*!
    \internal

    Returns the stdio open flags corresponding to a QIODevice::OpenMode.
*/
static int openModeToOpenFlags(QIODevice::OpenMode mode)
{
    int oflags = QT_OPEN_RDONLY;
#ifdef QT_LARGEFILE_SUPPORT
    oflags |= QT_OPEN_LARGEFILE;
#endif

    if ((mode & QFile::ReadWrite) == QFile::ReadWrite) {
        oflags = QT_OPEN_RDWR | QT_OPEN_CREAT;
    } else if (mode & QFile::WriteOnly) {
        oflags = QT_OPEN_WRONLY | QT_OPEN_CREAT;
    }

    if (mode & QFile::Append) {
        oflags |= QT_OPEN_APPEND;
    } else if (mode & QFile::WriteOnly) {
        if ((mode & QFile::Truncate) || !(mode & QFile::ReadOnly))
            oflags |= QT_OPEN_TRUNC;
    }

    // always use O_BINARY, Qt does all EOL handling on its own
    oflags |= O_BINARY;

    return oflags;
}

/*!
    \internal

    Sets the file descriptor to close on exec. That is, the file
    descriptor is not inherited by child processes.
*/
static bool setCloseOnExec(int fd)
{
    return fd != -1 && fcntl(fd, F_SETFD, FD_CLOEXEC) != -1;
}

static bool isUncRootPath(const QString &path)
{
    QString localPath = QDir::toNativeSeparators(path);
    if (!localPath.startsWith(QLatin1String("\\\\")))
        return false;

    int idx = localPath.indexOf(QLatin1Char('\\'), 2);
    if (idx == -1 || idx + 1 == localPath.length())
        return true;

    localPath = localPath.right(localPath.length() - idx - 1).trimmed();
    return localPath.isEmpty();
}


static bool isRelativePath(const QString &path)
{
    // On OS/2, paths like "A:bbb.txt" and "\ccc.dat" are not absolute (because
    // they may lead to different locations depending on where we are). However,
    // fixing this function will break an unfixable amount of invalid code in Qt
    // and qmake (just search for isRelativePath in there) so that we have to
    // live with it... See also QDir::absoluteFilePath() and
    // isNotReallyAbsolutePath() below.

    return !(path.startsWith(QLatin1Char('/')) || // "\ccc.dat" or "\\server\share..."
             (path.length() >= 2 && path.at(0).isLetter() && // "A:bbb..." or "A:\bbb..."
              path.at(1) == QLatin1Char(':')));
}

static bool isNotReallyAbsolutePath(const QString &path)
{
    // see isRelativePath(). Intended to detect a false absolute case when
    // isRelativePath() returns true

    QChar ch0, ch1, ch2;
    if (path.length()) ch0 = path.at(0);
    if (path.length() > 1) ch1 = path.at(1);
    if (path.length() > 2) ch2 = path.at(2);
    return ((ch0 == QLatin1Char('/') && ch1 != QLatin1Char('/')) || // "\ccc.dat"
            (ch0.isLetter() && ch1 == QLatin1Char(':') && // "A:bbb.txt" (but not A:)
             !ch2.isNull() && ch2 != QLatin1Char('/')));
}

static bool isDriveRootPath(const QString &path)
{
    return (path.length() >= 2 && path.at(0).isLetter() &&
            path.at(1) == QLatin1Char(':') &&
            (path.length() == 2 ||
             (path.length() == 3 && path.at(2) == QLatin1Char('/'))));
}

void QFSFileEnginePrivate::nativeInitFileName()
{
    // On OS/2, trailing spaces in file names are ignored so that "readme " and
    // "readme" refers to the same file. However, kLIBC seems to not handle this
    // case well and returns ENOENT at least when attempting to access a file
    // named "somedir /file" (provided that "somedir" exists of course. To try
    // to seamlessly solve this issue, we remove such trailing spaces from path
    // components here.
    QString fp = filePath.replace(QRegExp(QLatin1String("(?: *)([\\\\/$]|$)")),
                                  QLatin1String("\\1"));
    nativeFilePath = QFile::encodeName(fp);
}

bool QFSFileEnginePrivate::nativeOpen(QIODevice::OpenMode openMode)
{
    Q_Q(QFSFileEngine);

    if (openMode & QIODevice::Unbuffered) {
        int flags = openModeToOpenFlags(openMode);

        // Try to open the file in unbuffered mode.
        do {
            fd = QT_OPEN(nativeFilePath.constData(), flags, 0666);
        } while (fd == -1 && errno == EINTR);

        // On failure, return and report the error.
        if (fd == -1) {
            q->setError(errno == EMFILE ? QFile::ResourceError : QFile::OpenError,
                        qt_error_string(errno));
            return false;
        }

        QT_STATBUF statBuf;
        if (QT_FSTAT(fd, &statBuf) != -1) {
            if ((statBuf.st_mode & S_IFMT) == S_IFDIR) {
                q->setError(QFile::OpenError, QLatin1String("file to open is a directory"));
                QT_CLOSE(fd);
                return false;
            }
        }

        setCloseOnExec(fd);     // ignore failure

        // Seek to the end when in Append mode.
        if (flags & QFile::Append) {
            int ret;
            do {
                ret = QT_LSEEK(fd, 0, SEEK_END);
            } while (ret == -1 && errno == EINTR);

            if (ret == -1) {
                q->setError(errno == EMFILE ? QFile::ResourceError : QFile::OpenError,
                            qt_error_string(int(errno)));
                return false;
            }
        }

        fh = 0;
    } else {
        QByteArray fopenMode = openModeToFopenMode(openMode, filePath);

        // Try to open the file in buffered mode.
        do {
            fh = QT_FOPEN(nativeFilePath.constData(), fopenMode.constData());
        } while (!fh && errno == EINTR);

        // On failure, return and report the error.
        if (!fh) {
            q->setError(errno == EMFILE ? QFile::ResourceError : QFile::OpenError,
                        qt_error_string(int(errno)));
            return false;
        }

        QT_STATBUF statBuf;
        if (QT_FSTAT(fileno(fh), &statBuf) != -1) {
            if ((statBuf.st_mode & S_IFMT) == S_IFDIR) {
                q->setError(QFile::OpenError, QLatin1String("file to open is a directory"));
                fclose(fh);
                return false;
            }
        }

        setCloseOnExec(fileno(fh)); // ignore failure

        // Seek to the end when in Append mode.
        if (openMode & QIODevice::Append) {
            int ret;
            do {
                ret = QT_FSEEK(fh, 0, SEEK_END);
            } while (ret == -1 && errno == EINTR);

            if (ret == -1) {
                q->setError(errno == EMFILE ? QFile::ResourceError : QFile::OpenError,
                            qt_error_string(int(errno)));
                return false;
            }
        }

        fd = -1;
    }

    closeFileHandle = true;
    return true;
}

bool QFSFileEnginePrivate::nativeClose()
{
    return closeFdFh();
}

bool QFSFileEnginePrivate::nativeFlush()
{
    return fh ? flushFh() : fd != -1;
}

qint64 QFSFileEnginePrivate::nativeRead(char *data, qint64 len)
{
    Q_Q(QFSFileEngine);

    if (fh && nativeIsSequential()) {
        size_t readBytes = 0;
        int oldFlags = fcntl(QT_FILENO(fh), F_GETFL);
        for (int i = 0; i < 2; ++i) {
            // Unix: Make the underlying file descriptor non-blocking
            int v = 1;
            if ((oldFlags & O_NONBLOCK) == 0)
                fcntl(QT_FILENO(fh), F_SETFL, oldFlags | O_NONBLOCK, &v, sizeof(v));

            // Cross platform stdlib read
            size_t read = 0;
            do {
                read = fread(data + readBytes, 1, size_t(len - readBytes), fh);
            } while (read == 0 && !feof(fh) && errno == EINTR);
            if (read > 0) {
                readBytes += read;
                break;
            } else {
                if (readBytes)
                    break;
                readBytes = read;
            }

            // Unix: Restore the blocking state of the underlying socket
            if ((oldFlags & O_NONBLOCK) == 0) {
                int v = 1;
                fcntl(QT_FILENO(fh), F_SETFL, oldFlags, &v, sizeof(v));
                if (readBytes == 0) {
                    int readByte = 0;
                    do {
                        readByte = fgetc(fh);
                    } while (readByte == -1 && errno == EINTR);
                    if (readByte != -1) {
                        *data = uchar(readByte);
                        readBytes += 1;
                    } else {
                        break;
                    }
                }
            }
        }
        // Unix: Restore the blocking state of the underlying socket
        if ((oldFlags & O_NONBLOCK) == 0) {
            int v = 1;
            fcntl(QT_FILENO(fh), F_SETFL, oldFlags, &v, sizeof(v));
        }
        if (readBytes == 0 && !feof(fh)) {
            // if we didn't read anything and we're not at EOF, it must be an error
            q->setError(QFile::ReadError, qt_error_string(int(errno)));
            return -1;
        }
        return readBytes;
    }

    return readFdFh(data, len);
}

qint64 QFSFileEnginePrivate::nativeReadLine(char *data, qint64 maxlen)
{
    return readLineFdFh(data, maxlen);
}

qint64 QFSFileEnginePrivate::nativeWrite(const char *data, qint64 len)
{
    return writeFdFh(data, len);
}

qint64 QFSFileEnginePrivate::nativePos() const
{
    return posFdFh();
}

bool QFSFileEnginePrivate::nativeSeek(qint64 pos)
{
    return seekFdFh(pos);
}

int QFSFileEnginePrivate::nativeHandle() const
{
    return fh ? fileno(fh) : fd;
}

bool QFSFileEnginePrivate::nativeIsSequential() const
{
    return isSequentialFdFh();
}

bool QFSFileEngine::remove()
{
    Q_D(QFSFileEngine);
    return unlink(d->nativeFilePath.constData()) == 0;
}

bool QFSFileEngine::copy(const QString &newName)
{
    Q_D(QFSFileEngine);
    return DosCopy(d->nativeFilePath, QFile::encodeName(newName), 0) == NO_ERROR;
}

bool QFSFileEngine::rename(const QString &newName)
{
    Q_D(QFSFileEngine);
    return ::rename(d->nativeFilePath.constData(), QFile::encodeName(newName).constData()) == 0;
}

bool QFSFileEngine::link(const QString &newName)
{
    Q_D(QFSFileEngine);
    return ::symlink(d->nativeFilePath.constData(), QFile::encodeName(newName).constData()) == 0;
}

qint64 QFSFileEnginePrivate::nativeSize() const
{
    return sizeFdFh();
}

bool QFSFileEngine::mkdir(const QString &name, bool createParentDirectories) const
{
    // Note: according to what I see in implementations of this function on
    // other platforms, it expects name to be the absolute path (since no
    // member variables of this class are used in there). Thus, assert.
    Q_ASSERT(!::isRelativePath(name) && !::isNotReallyAbsolutePath(name));
    if (::isRelativePath(name) || ::isNotReallyAbsolutePath(name))
        return false;

    QString dirName = QDir::toNativeSeparators(QDir::cleanPath(name));
    if (createParentDirectories) {
        int sep = -1;
        if (dirName.startsWith(QLatin1String("\\\\"))) {
            // don't attempt to create a share on the server
            sep = dirName.indexOf(QLatin1Char('\\'), 2);
            if (sep)
                sep = dirName.indexOf(QLatin1Char('\\'), sep + 1);
            if (sep < 0) // "\\server" or "\\server\share"?
                return false;
        } else if (dirName.at(1) == QLatin1Char(':')) {
            // don't attempt to create the root dir on drive
            sep = 2;
        }
        while (sep < dirName.length()) {
            sep = dirName.indexOf(QLatin1Char('\\'), sep + 1);
            if (sep < 0)
                sep = dirName.length();
            QByteArray chunk = QFile::encodeName(dirName.left(sep));
            QT_STATBUF st;
            if (QT_STAT(chunk, &st) != -1) {
                if ((st.st_mode & S_IFMT) != S_IFDIR)
                    return false;
            } else if (::mkdir(chunk, 0777) != 0) {
                    return false;
            }
        }
        return true;
    }
    return (::mkdir(QFile::encodeName(dirName), 0777) == 0);
}

bool QFSFileEngine::rmdir(const QString &name, bool recurseParentDirectories) const
{
    // Note: according to what I see in implementations of this function on
    // other platforms, it expects name to be the absolute path (since no
    // member variables of this class are used in there). Thus, assert.
    Q_ASSERT(!::isRelativePath(name) && !::isNotReallyAbsolutePath(name));
    if (::isRelativePath(name) || ::isNotReallyAbsolutePath(name))
        return false;

    QString dirName = QDir::toNativeSeparators(QDir::cleanPath(name));
    if (recurseParentDirectories) {
        int lastSep = -1;
        if (dirName.startsWith(QLatin1String("\\\\"))) {
            // don't attempt to delete a share on the server
            lastSep = dirName.indexOf(QLatin1Char('\\'), 2);
            if (lastSep)
                lastSep = dirName.indexOf(QLatin1Char('\\'), lastSep + 1);
            if (lastSep < 0) // "\\server" or "\\server\share"?
                return false;
        } else if (dirName.at(1) == QLatin1Char(':')) {
            // don't attempt to delete the root dir on the drive
            lastSep = 2;
        }
        int sep = dirName.length();
        while (sep > lastSep) {
            QByteArray chunk = QFile::encodeName(dirName.left(sep));
            QT_STATBUF st;
            if (QT_STAT(chunk, &st) != -1) {
                if ((st.st_mode & S_IFMT) != S_IFDIR)
                    return false;
                if (::rmdir(chunk) != 0)
                    return sep < dirName.length();
            } else {
                return false;
            }
            sep = dirName.lastIndexOf(QLatin1Char('\\'), sep - 1);
        }
        return true;
    }
    return ::rmdir(QFile::encodeName(dirName)) == 0;
}

bool QFSFileEngine::caseSensitive() const
{
    return false;
}

bool QFSFileEngine::setCurrentPath(const QString &path)
{
    int r;
    r = ::chdir(QFile::encodeName(path));
    return r >= 0;
}

QString QFSFileEngine::currentPath(const QString &fileName)
{
    // if filename starts with X: then get the pwd of that drive; otherwise the
    // pwd of the current drive
    char drv = _getdrive();
    if (fileName.length() >= 2 &&
        fileName.at(0).isLetter() && fileName.at(1) == QLatin1Char(':')) {
        drv = fileName.at(0).toUpper().toLatin1();
    }
    char buf[PATH_MAX + 1 + 2 /* X: */];
    buf[0] = drv;
    buf[1] = ':';
    QString ret;
    if (!_getcwd1(buf + 2, drv)) {
        ret = QFile::decodeName(QByteArray(buf));
        ret = QDir::fromNativeSeparators(ret);
    }
#if defined(QT_DEBUG)
    if (ret.isEmpty())
        qWarning("QDir::currentPath: _getcwd1(%s, %d) failed (%d)",
                 fileName.toUtf8().constData(), drv, errno);
#endif
    return ret;
}

QString QFSFileEngine::homePath()
{
    QString home = QFile::decodeName(qgetenv("HOME"));
    if (home.isEmpty()) {
        home = QFile::decodeName(qgetenv("HOMEDRIVE")) +
               QFile::decodeName(qgetenv("HOMEPATH"));
        if (home.isEmpty())
            home = rootPath();
    }
    return home;
}

QString QFSFileEngine::rootPath()
{
    ULONG bootDrive = 0;
    DosQuerySysInfo(QSV_BOOT_DRIVE, QSV_BOOT_DRIVE, (PVOID)&bootDrive,
                    sizeof(bootDrive));
    QString root = QChar(QLatin1Char(bootDrive + 'A' - 1));
    root += QLatin1String(":/");
    return root;
}

QString QFSFileEngine::tempPath()
{
    QString temp = QFile::decodeName(qgetenv("TEMP"));
    if (temp.isEmpty()) {
        temp = QFile::decodeName(qgetenv("TMP"));
        if (temp.isEmpty()) {
            temp = QFile::decodeName(qgetenv("TMPDIR"));
            if (temp.isEmpty())
                temp = rootPath() + QLatin1String("tmp");
        }
    }
    return temp;
}

QFileInfoList QFSFileEngine::drives()
{
    QFileInfoList ret;

    ULONG driveBits, dummy;
    DosQueryCurrentDisk(&dummy, &driveBits);
    driveBits &= 0x3ffffff;

    char driveName[4] = "A:/";

    while (driveBits) {
        if (driveName[0] < 'C') {
            // hide non-existent floppies
            BIOSPARAMETERBLOCK  bpb;
            UCHAR               ioc[2] = { 0, driveName[0] - 'A' };
            if (DosDevIOCtl((HFILE)-1, IOCTL_DISK, DSK_GETDEVICEPARAMS,
                            ioc, 2, NULL, &bpb, sizeof(bpb), NULL) != 0) {
                driveBits &= ~1;
            }
        }
        if (driveBits & 1)
            ret.append(QString::fromLatin1(driveName).toUpper());
        driveName[0]++;
        driveBits = driveBits >> 1;
    }

    return ret;
}

bool QFSFileEnginePrivate::doStat() const
{
    if (tried_stat == 0) {
        if (fh && nativeFilePath.isEmpty()) {
            // ### actually covers two cases: d->fh and when the file is not open
            could_stat = (QT_FSTAT(fileno(fh), &st) == 0);
        } else if (fd == -1) {
            // ### actually covers two cases: d->fh and when the file is not open
            if (isDriveRootPath(filePath)) {
                // it's a root directory of the drive. Check that the drive is
                // ready: an attempt to stat() on a removable drive with no
                // media inserted will cause an unnecessary delay and noise.
                BYTE drv[3] = { filePath.at(0).cell(), ':', '\0' };
                HFILE hdrv;
                ULONG action;
                APIRET arc = DosOpen(drv, &hdrv, &action, 0, FILE_NORMAL, FILE_OPEN,
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
            	if (arc == ERROR_NOT_READY || arc == ERROR_DISK_CHANGE ||
                    arc == ERROR_SECTOR_NOT_FOUND) {
                    // it must be a removable drive with no media in it
                    could_stat = false;
                } else if (arc == ERROR_INVALID_DRIVE) {
                    // just an invalid drive letter
                    could_stat = false;
                } else {
                    could_stat = (QT_STAT(nativeFilePath.constData(), &st) == 0);
                }
            } else {
                could_stat = (QT_STAT(nativeFilePath.constData(), &st) == 0);
            }
        } else {
            could_stat = (QT_FSTAT(fd, &st) == 0);
        }
        tried_stat = 1;
    }
    return could_stat;
}

bool QFSFileEnginePrivate::isSymlink() const
{
    if (need_lstat) {
        QFSFileEnginePrivate *that = const_cast<QFSFileEnginePrivate *>(this);
        that->need_lstat = false;
        if (isDriveRootPath(filePath) || isUncRootPath(filePath)) {
            is_link = false;        // drive/share names are never symlinks
        } else {
            QT_STATBUF st;          // don't clobber our main one
            that->is_link = (QT_LSTAT(nativeFilePath.constData(), &st) == 0) ?
                            S_ISLNK(st.st_mode) : false;
        }
    }
    return is_link;
}

QAbstractFileEngine::FileFlags QFSFileEngine::fileFlags(FileFlags type) const
{
    Q_D(const QFSFileEngine);

    // Force a stat, so that we're guaranteed to get up-to-date results
    if (type & Refresh) {
        d->tried_stat = 0;
        d->need_lstat = 1;
    }

    if ((type & (FlagsMask | TypesMask | PermsMask) & ~Refresh) == 0) {
       // if nothing is requested but Refresh (or even no Refresh),
       // there is nothing to do
       return 0;
    }

    bool isRoot = isDriveRootPath(d->filePath) || isUncRootPath(d->filePath);
    bool exists = d->doStat();

    QAbstractFileEngine::FileFlags ret = 0;

    // flags that don't need doStat()
    if (type & FlagsMask) {
        ret |= LocalDiskFlag;
        if (isRoot)
            ret |= RootFlag;
    }
    if (type & TypesMask) {
        if (isRoot)
            ret |= DirectoryType;
        // note: drive/share names are never symlinks
        else if ((type & LinkType) && d->isSymlink())
            ret |= LinkType;
    }

    if (!exists)
        return ret;

    // flags that need doStat()
    if (type & PermsMask) {
        if (d->st.st_mode & S_IRUSR)
            ret |= ReadOwnerPerm;
        if (d->st.st_mode & S_IWUSR)
            ret |= WriteOwnerPerm;
        if (d->st.st_mode & S_IXUSR)
            ret |= ExeOwnerPerm;
        if (d->st.st_mode & S_IRUSR)
            ret |= ReadUserPerm;
        if (d->st.st_mode & S_IWUSR)
            ret |= WriteUserPerm;
        if (d->st.st_mode & S_IXUSR)
            ret |= ExeUserPerm;
        if (d->st.st_mode & S_IRGRP)
            ret |= ReadGroupPerm;
        if (d->st.st_mode & S_IWGRP)
            ret |= WriteGroupPerm;
        if (d->st.st_mode & S_IXGRP)
            ret |= ExeGroupPerm;
        if (d->st.st_mode & S_IROTH)
            ret |= ReadOtherPerm;
        if (d->st.st_mode & S_IWOTH)
            ret |= WriteOtherPerm;
        if (d->st.st_mode & S_IXOTH)
            ret |= ExeOtherPerm;
    }
    if (type & TypesMask) {
        if ((d->st.st_mode & S_IFMT) == S_IFREG)
            ret |= FileType;
        else if ((d->st.st_mode & S_IFMT) == S_IFDIR)
            ret |= DirectoryType;
    }
    if (type & FlagsMask) {
        ret |= ExistsFlag;
        if (d->st.st_attr & FILE_HIDDEN)
            ret |= HiddenFlag;
    }

    return ret;
}

QString QFSFileEngine::fileName(FileName file) const
{
    Q_D(const QFSFileEngine);
    if(file == BaseName) {
        int slash = d->filePath.lastIndexOf(QLatin1Char('/'));
        if(slash == -1) {
            int colon = d->filePath.lastIndexOf(QLatin1Char(':'));
            if(colon != -1)
                return d->filePath.mid(colon + 1);
            return d->filePath;
        }
        return d->filePath.mid(slash + 1);
    } else if(file == PathName) {
        if(!d->filePath.size())
            return d->filePath;

        int slash = d->filePath.lastIndexOf(QLatin1Char('/'));
        if(slash == -1) {
            if(d->filePath.length() >= 2 && d->filePath.at(1) == QLatin1Char(':'))
                return d->filePath.left(2);
            return QString::fromLatin1(".");
        } else {
            if(!slash)
                return QString::fromLatin1("/");
            if(slash == 2 && d->filePath.length() >= 2 && d->filePath.at(1) == QLatin1Char(':'))
                slash++;
            return d->filePath.left(slash);
        }
    } else if(file == AbsoluteName || file == AbsolutePathName) {
        QString ret;

        if (!::isRelativePath(d->filePath)) {
            if (::isNotReallyAbsolutePath(d->filePath)) {
                QByteArray filePath = QFile::encodeName(d->filePath);
                char buf[PATH_MAX+1];
#ifdef __INNOTEK_LIBC__
                // In kLIBC (0.6.5-), _abspath() doesn't perform path rewriting to expand
                // e.g. /@unixroot/dir to X:/my_unixroot/dir which confuses many apps by
                // translating such paths to invalid ones like X:/@unixroot/dir. Fix this
                // by calling path rewrite explicitly. @todo kLIBC should actually do that.
                if (__libc_PathRewrite(filePath.constData(), buf, sizeof(buf)) <= 0)
#endif
                    strcpy(buf, filePath.constData());
                _abspath(buf, buf, sizeof(buf));
                ret = QFile::decodeName(buf);
            } else {
                ret = d->filePath;
            }
        } else {
            ret = QDir::cleanPath(QDir::currentPath() + QLatin1Char('/') + d->filePath);
        }

        // The path should be absolute at this point.
        // From the docs :
        // Absolute paths begin with the directory separator "/"
        // (optionally preceded by a drive specification under Windows).
        if (ret.at(0) != QLatin1Char('/')) {
            Q_ASSERT(ret.length() >= 2);
            Q_ASSERT(ret.at(0).isLetter());
            Q_ASSERT(ret.at(1) == QLatin1Char(':'));

            // Force uppercase drive letters.
            ret[0] = ret.at(0).toUpper();
        }

        if (file == AbsolutePathName) {
            int slash = ret.lastIndexOf(QLatin1Char('/'));
            if (slash < 0)
                return ret;
            else if (ret.at(0) != QLatin1Char('/') && slash == 2)
                return ret.left(3);      // include the slash
            else
                return ret.left(slash > 0 ? slash : 1);
        }
        return ret;
    } else if(file == CanonicalName || file == CanonicalPathName) {
        if (!(fileFlags(ExistsFlag) & ExistsFlag))
            return QString();

        QString ret = QFSFileEnginePrivate::canonicalized(fileName(AbsoluteName));
        if (!ret.isEmpty() && file == CanonicalPathName) {
            int slash = ret.lastIndexOf(QLatin1Char('/'));
            if (slash == -1)
                ret = QDir::currentPath();
            else if (slash == 0)
                ret = QLatin1String("/");
            ret = ret.left(slash);
        }
        return ret;
    } else if(file == LinkName) {
        if (d->isSymlink()) {
            char buf[PATH_MAX+1];
            int len = readlink(d->nativeFilePath.constData(), buf, PATH_MAX);
            if (len > 0) {
                buf[len] = '\0';
                QString target = QDir::fromNativeSeparators(QFile::decodeName(QByteArray(buf)));
                QString ret;
                if (!::isRelativePath(target)) {
                    if (::isNotReallyAbsolutePath(target)) {
                        QByteArray filePath = QFile::encodeName(target);
                        char buf[PATH_MAX+1];
#ifdef __INNOTEK_LIBC__
                        // In kLIBC (0.6.5-), _abspath() doesn't perform path rewriting to expand
                        // e.g. /@unixroot/dir to X:/my_unixroot/dir which confuses many apps by
                        // translating such paths to invalid ones like X:/@unixroot/dir. Fix this
                        // by calling path rewrite explicitly. @todo kLIBC should actually do that.
                        if (__libc_PathRewrite(filePath.constData(), buf, sizeof(buf)) <= 0)
#endif
                            strcpy(buf, filePath.constData());
                        _abspath(buf, buf, sizeof(buf));
                        ret = QFile::decodeName(buf);
                    } else {
                        ret = target;
                    }
                } else {
                    if (S_ISDIR(d->st.st_mode)) {
                        QDir parent(d->filePath);
                        parent.cdUp();
                        ret = parent.path();
                        if (!ret.isEmpty() && !ret.endsWith(QLatin1Char('/')))
                            ret += QLatin1Char('/');
                    }
                    ret += target;

                    if (::isRelativePath(ret)) {
                        if (!isRelativePath()) {
                            ret.prepend(d->filePath.left(d->filePath.lastIndexOf(QLatin1Char('/')))
                                        + QLatin1Char('/'));
                        } else {
                            ret.prepend(QDir::currentPath() + QLatin1Char('/'));
                        }
                    }
                    ret = QDir::cleanPath(ret);
                    if (ret.size() > 1 && ret.endsWith(QLatin1Char('/')))
                        ret.chop(1);
                }

                // The path should be absolute at this point.
                // From the docs :
                // Absolute paths begin with the directory separator "/"
                // (optionally preceded by a drive specification under Windows).
                if (ret.at(0) != QLatin1Char('/')) {
                    Q_ASSERT(ret.length() >= 2);
                    Q_ASSERT(ret.at(0).isLetter());
                    Q_ASSERT(ret.at(1) == QLatin1Char(':'));

                    // Force uppercase drive letters.
                    ret[0] = ret.at(0).toUpper();
                }

                return ret;
            }
        }
        return QString();
    } else if(file == BundleName) {
        return QString();
    }
    return d->filePath;
}

bool QFSFileEngine::isRelativePath() const
{
    Q_D(const QFSFileEngine);
    return ::isRelativePath(d->filePath);
}

uint QFSFileEngine::ownerId(FileOwner own) const
{
    Q_D(const QFSFileEngine);
    static const uint nobodyID = (uint) -2;
    if (d->doStat()) {
        if (own == OwnerUser)
            return d->st.st_uid;
        else
            return d->st.st_gid;
    }
    return nobodyID;
}

QString QFSFileEngine::owner(FileOwner own) const
{
#if !defined(QT_NO_THREAD) && defined(_POSIX_THREAD_SAFE_FUNCTIONS)
    int size_max = sysconf(_SC_GETPW_R_SIZE_MAX);
    if (size_max == -1)
        size_max = 1024;
    QVarLengthArray<char, 1024> buf(size_max);
#endif

    if (own == OwnerUser) {
        struct passwd *pw = 0;
#if !defined(QT_NO_THREAD) && defined(_POSIX_THREAD_SAFE_FUNCTIONS)
        struct passwd entry;
        getpwuid_r(ownerId(own), &entry, buf.data(), buf.size(), &pw);
#else
        pw = getpwuid(ownerId(own));
#endif
        if (pw)
            return QFile::decodeName(QByteArray(pw->pw_name));
    } else if (own == OwnerGroup) {
        struct group *gr = 0;
#if !defined(QT_NO_THREAD) && defined(_POSIX_THREAD_SAFE_FUNCTIONS)
        size_max = sysconf(_SC_GETGR_R_SIZE_MAX);
        if (size_max == -1)
            size_max = 1024;
        buf.resize(size_max);
        struct group entry;
        // Some large systems have more members than the POSIX max size
        // Loop over by doubling the buffer size (upper limit 250k)
        for (unsigned size = size_max; size < 256000; size += size)
        {
            buf.resize(size);
            // ERANGE indicates that the buffer was too small
            if (!getgrgid_r(ownerId(own), &entry, buf.data(), buf.size(), &gr)
                || errno != ERANGE)
                break;
        }

#else
        gr = getgrgid(ownerId(own));
#endif
        if (gr)
            return QFile::decodeName(QByteArray(gr->gr_name));
    }
    return QString();
}

bool QFSFileEngine::setPermissions(uint perms)
{
    Q_D(QFSFileEngine);
    mode_t mode = 0;
    if (perms & ReadOwnerPerm)
        mode |= S_IRUSR;
    if (perms & WriteOwnerPerm)
        mode |= S_IWUSR;
    if (perms & ExeOwnerPerm)
        mode |= S_IXUSR;
    if (perms & ReadUserPerm)
        mode |= S_IRUSR;
    if (perms & WriteUserPerm)
        mode |= S_IWUSR;
    if (perms & ExeUserPerm)
        mode |= S_IXUSR;
    if (perms & ReadGroupPerm)
        mode |= S_IRGRP;
    if (perms & WriteGroupPerm)
        mode |= S_IWGRP;
    if (perms & ExeGroupPerm)
        mode |= S_IXGRP;
    if (perms & ReadOtherPerm)
        mode |= S_IROTH;
    if (perms & WriteOtherPerm)
        mode |= S_IWOTH;
    if (perms & ExeOtherPerm)
        mode |= S_IXOTH;
    if (d->fd != -1)
        return !fchmod(d->fd, mode);
    return !::chmod(d->nativeFilePath.constData(), mode);
}

bool QFSFileEngine::setSize(qint64 size)
{
    Q_D(QFSFileEngine);
    if (d->fd != -1)
        return !QT_FTRUNCATE(d->fd, size);
    return !QT_TRUNCATE(d->nativeFilePath.constData(), size);
}

QDateTime QFSFileEngine::fileTime(FileTime time) const
{
    Q_D(const QFSFileEngine);
    QDateTime ret;
    if (d->doStat()) {
        if (time == CreationTime)
            ret.setTime_t(d->st.st_ctime ? d->st.st_ctime : d->st.st_mtime);
        else if (time == ModificationTime)
            ret.setTime_t(d->st.st_mtime);
        else if (time == AccessTime)
            ret.setTime_t(d->st.st_atime);
    }
    return ret;
}

uchar *QFSFileEnginePrivate::map(qint64 offset, qint64 size, QFile::MemoryMapFlags flags)
{
#if 1
    // @todo mmap() and friends isn't implemented in kLIBC yet...
    return 0;
#else
    Q_Q(QFSFileEngine);
    Q_UNUSED(flags);
    if (offset < 0) {
        q->setError(QFile::UnspecifiedError, qt_error_string(int(EINVAL)));
        return 0;
    }
    if (openMode == QIODevice::NotOpen) {
        q->setError(QFile::PermissionsError, qt_error_string(int(EACCES)));
        return 0;
    }
    int access = 0;
    if (openMode & QIODevice::ReadOnly) access |= PROT_READ;
    if (openMode & QIODevice::WriteOnly) access |= PROT_WRITE;

    int pagesSize = getpagesize();
    int realOffset = offset / pagesSize;
    int extra = offset % pagesSize;

    void *mapAddress = mmap((void*)0, (size_t)size + extra,
                   access, MAP_SHARED, nativeHandle(), realOffset * pagesSize);
    if (MAP_FAILED != mapAddress) {
        uchar *address = extra + static_cast<uchar*>(mapAddress);
        maps[address] = QPair<int,int>(extra, size);
        return address;
    }

    switch(errno) {
    case EBADF:
        q->setError(QFile::PermissionsError, qt_error_string(int(EACCES)));
        break;
    case ENFILE:
    case ENOMEM:
        q->setError(QFile::ResourceError, qt_error_string(int(errno)));
        break;
    case EINVAL:
        // size are out of bounds
    default:
        q->setError(QFile::UnspecifiedError, qt_error_string(int(errno)));
        break;
    }
    return 0;
#endif
}

bool QFSFileEnginePrivate::unmap(uchar *ptr)
{
#if 1
    // @todo mmap() and friends isn't implemented in kLIBC yet...
    return false;
#else
    Q_Q(QFSFileEngine);
    if (!maps.contains(ptr)) {
        q->setError(QFile::PermissionsError, qt_error_string(EACCES));
        return false;
    }

    uchar *start = ptr - maps[ptr].first;
    int len = maps[ptr].second;
    if (-1 == munmap(start, len)) {
        q->setError(QFile::UnspecifiedError, qt_error_string(errno));
        return false;
    }
    maps.remove(ptr);
    return true;
#endif
}

QT_END_NAMESPACE

#endif // QT_NO_FSFILEENGINE
