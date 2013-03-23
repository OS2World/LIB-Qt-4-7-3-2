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

#include <qsettings.h>
#include <qdir.h>
#include <qurl.h>
#include <qstringlist.h>
#include <qprocess.h>
#include <qtemporaryfile.h>
#include <qcoreapplication.h>

#include <qt_os2.h>

#ifndef QT_NO_DESKTOPSERVICES

QT_BEGIN_NAMESPACE

static bool openDocument(const QUrl &file)
{
    if (!file.isValid())
        return false;

    QString path = file.toLocalFile();
    if (path.isEmpty())
        return false;

    path = QDir::toNativeSeparators(QDir::cleanPath(QDir::current()
                                                    .absoluteFilePath(path)));

    HOBJECT hobj = WinQueryObject(QFile::encodeName(path));
    if (hobj != NULLHANDLE)
        return WinOpenObject(hobj, 0 /* OPEN_DEFAULT */, FALSE);

    return false;
}

static QString openUrlParam(const char *key)
{
    QByteArray val;

    ULONG len = 0;
    if (PrfQueryProfileSize(HINI_USERPROFILE, "WPURLDEFAULTSETTINGS",
                            key, &len) && len) {
        val.resize(len);
        len = PrfQueryProfileString(HINI_USERPROFILE, "WPURLDEFAULTSETTINGS",
                                    key, 0, val.data(), len);
        --len; // excude terminating NULL
        val.truncate(len);
    }

    return QString::fromLocal8Bit(val);
}

static bool launchWebBrowser(const QUrl &url)
{
    if (!url.isValid())
        return false;

    struct
    {
        const char *proto;
        const char *exe;
        const char *params;
        const char *workDir;
    }
    Apps[] = {
        { "mailto", "DefaultMailExe", "DefaultMailParameters", "DefaultMailWorkingDir" },
        { "news", "DefaultNewsExe", "DefaultNewsParameters", "DefaultNewsWorkingDir" },
        { "ftp", "DefaultFTPExe", "DefaultFTPParameters", "DefaultFTPWorkingDir" },
        { "irc", "DefaultIRCExe", "DefaultIRCParameters", "DefaultIRCWorkingDir" },
        // default: proto must be 0 and it must always come last
        { 0, "DefaultBrowserExe", "DefaultParameters", "DefaultWorkingDir" },
    };

    for (size_t i = 0; i < sizeof(Apps)/sizeof(Apps[0]); ++i) {
        if (!Apps[i].proto || url.scheme() == QLatin1String(Apps[i].proto)) {
            QString exe = openUrlParam(Apps[i].exe);
            if (!exe.isEmpty()) {
                QString params = openUrlParam(Apps[i].params);
                QString workDir = openUrlParam(Apps[i].workDir);
                QStringList args;
                args << params << url.toString();
                QProcess process;
                return process.startDetached(exe, args, workDir);
            }
        }
    }

    return false;
}

QString QDesktopServices::storageLocation(StandardLocation type)
{
    if (type == QDesktopServices::HomeLocation)
        return QDir::homePath();
    if (type == QDesktopServices::TempLocation)
        return QDir::tempPath();

    // http://standards.freedesktop.org/basedir-spec/basedir-spec-0.6.html
    if (type == QDesktopServices::CacheLocation) {
        QString xdgCacheHome = QFile::decodeName(qgetenv("XDG_CACHE_HOME"));
        if (xdgCacheHome.isEmpty())
            xdgCacheHome = QDir::homePath() + QLatin1String("/.cache");
        xdgCacheHome += QLatin1Char('/') + QCoreApplication::organizationName()
                    + QLatin1Char('/') + QCoreApplication::applicationName();
        return QDir::cleanPath(xdgCacheHome);
    }

    if (type == QDesktopServices::DataLocation) {
        QString xdgDataHome = QFile::decodeName(qgetenv("XDG_DATA_HOME"));
        if (xdgDataHome.isEmpty())
            xdgDataHome = QDir::homePath() + QLatin1String("/.local/share");
        xdgDataHome += QLatin1String("/data/")
                    + QCoreApplication::organizationName() + QLatin1Char('/')
                    + QCoreApplication::applicationName();
        return QDir::cleanPath(xdgDataHome);
    }

    // http://www.freedesktop.org/wiki/Software/xdg-user-dirs
    QString xdgConfigHome = QFile::decodeName(qgetenv("XDG_CONFIG_HOME"));
    if (xdgConfigHome.isEmpty())
        xdgConfigHome = QDir::homePath() + QLatin1String("/.config");
    QFile file(xdgConfigHome + QLatin1String("/user-dirs.dirs"));
    if (file.exists() && file.open(QIODevice::ReadOnly)) {
        QHash<QString, QString> lines;
        QTextStream stream(&file);
        // Only look for lines like: XDG_DESKTOP_DIR="$HOME/Desktop"
        QRegExp exp(QLatin1String("^XDG_(.*)_DIR=(.*)$"));
        while (!stream.atEnd()) {
            QString line = stream.readLine();
            if (exp.indexIn(line) != -1) {
                QStringList lst = exp.capturedTexts();
                QString key = lst.at(1);
                QString value = lst.at(2);
                if (value.length() > 2
                    && value.startsWith(QLatin1String("\""))
                    && value.endsWith(QLatin1String("\"")))
                    value = value.mid(1, value.length() - 2);
                // Store the key and value: "DESKTOP", "$HOME/Desktop"
                lines[key] = value;
            }
        }

        QString key;
        switch (type) {
        case DesktopLocation: key = QLatin1String("DESKTOP"); break;
        case DocumentsLocation: key = QLatin1String("DOCUMENTS"); break;
        case PicturesLocation: key = QLatin1String("PICTURES"); break;
        case MusicLocation: key = QLatin1String("MUSIC"); break;
        case MoviesLocation: key = QLatin1String("VIDEOS"); break;
        default: break;
        }
        if (!key.isEmpty() && lines.contains(key)) {
            QString value = lines[key];
            // value can start with $HOME or %HOME%
            if (value.startsWith(QLatin1String("$HOME")))
                value = QDir::homePath() + value.mid(5);
            else if (value.startsWith(QLatin1String("%HOME%")))
                value = QDir::homePath() + value.mid(6);
            return QDir::cleanPath(value);
        }
    }

    QString path;
    switch (type) {
    case DesktopLocation: {
        QByteArray buf(CCHMAXPATH, '\0');
        WinQueryActiveDesktopPathname(buf.data(), buf.size());
        buf.truncate(qstrlen(buf));
        if (!buf.isEmpty())
            path = QDir::cleanPath(QFile::decodeName(buf));
        else
            path = QDir::rootPath() + QLatin1String("Desktop");
        break;
    }
    case DocumentsLocation:
        path = QDir::homePath() + QLatin1String("/Documents");
       break;
    case PicturesLocation:
        path = QDir::homePath() + QLatin1String("/Pictures");
        break;
    case MusicLocation:
        path = QDir::homePath() + QLatin1String("/Music");
        break;
    case MoviesLocation:
        path = QDir::homePath() + QLatin1String("/Videos");
        break;

    case ApplicationsLocation:
        path = QDir::cleanPath(QFile::decodeName(qgetenv("PROGRAMS")));
        if (path.isEmpty())
            path = QDir::rootPath() + QLatin1String("PROGRAMS");
        break;

    case FontsLocation:
        path = QDir::cleanPath(QFile::decodeName(qgetenv("FONTS")));
        if (path.isEmpty())
            path = QDir::rootPath() + QLatin1String("PSFONTS");
        break;

    default:
        break;
    }

    return path;
}

QString QDesktopServices::displayName(StandardLocation type)
{
    Q_UNUSED(type);
    return QString();
}

QT_END_NAMESPACE

#endif // QT_NO_DESKTOPSERVICES
