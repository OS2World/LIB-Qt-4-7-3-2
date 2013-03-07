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

#include "qfontengine_pm_p.h"

#include "qabstractfileengine.h"
#include "qsettings.h"
#include "qfileinfo.h"
#include "qdatetime.h"
#include "qhash.h"
#include "qtextcodec.h"

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_TYPES_H
#include FT_TRUETYPE_TABLES_H
#include FT_LCD_FILTER_H
#include FT_SFNT_NAMES_H
#include FT_TRUETYPE_IDS_H

QT_BEGIN_NAMESPACE

extern FT_Library qt_getFreetype(); // qfontengine_ft.cpp

struct FaceData
{
    QByteArray file;
    int index;
    QString familyName;
    QtFontStyle::Key styleKey;
    QList<QFontDatabase::WritingSystem> systems;
    bool fixedPitch;
    bool smoothScalable;
    QList<unsigned short> pixelSizes;
};

static QDataStream &operator<<(QDataStream &data, const QFontDatabase::WritingSystem &ws)
{
    data << (int)ws;
    return data;
}

static QDataStream &operator>>(QDataStream &data, QFontDatabase::WritingSystem &ws)
{
    data >> (int&)ws;
    return data;
}

static QDataStream &operator<<(QDataStream &data, const FaceData &cached)
{
    data << cached.familyName;
    data << cached.styleKey.style << cached.styleKey.weight
         << cached.styleKey.stretch;
    data << cached.systems;
    data << cached.fixedPitch << cached.smoothScalable;
    data << cached.pixelSizes;
    return data;
}

static QDataStream &operator>>(QDataStream &data, FaceData &cached)
{
    data >> cached.familyName;
    uint style;
    int weight, stretch;
    data >> style; cached.styleKey.style = style;
    data >> weight; cached.styleKey.weight = weight;
    data >> stretch; cached.styleKey.stretch = stretch;
    data >> cached.systems;
    data >> cached.fixedPitch >> cached.smoothScalable;
    data >> cached.pixelSizes;
    return data;
}

struct FileData
{
    FileData() : seen(false) {}
    FileData(const QFileInfo &fi, bool s) : fileInfo(fi), seen(s) {}

    QFileInfo fileInfo;
    bool seen;
};

typedef QHash<QString, FileData> FontFileHash;
static FontFileHash knownFontFiles;

static bool lookupFamilyName(FT_Face ftface, QString &familyName)
{
    FT_UInt     nNameCount;
    FT_UInt     i;
    FT_SfntName sfntName;
    FT_UInt     found, best;

    nNameCount = FT_Get_Sfnt_Name_Count(ftface);

    if (nNameCount == 0)
        return false;

#ifndef QT_NO_TEXTCODEC
    QTextCodec *codec = QTextCodec::codecForName("UTF-16BE");

    // find a unicode name at first
    if (codec) {
        found = (FT_UInt)-1;
        best  = (FT_UInt)-1;

        // try to find the unicode name matching to the locale
        for (i = 0; found == (FT_UInt)-1 && i < nNameCount; i++) {
            FT_Get_Sfnt_Name(ftface, i, &sfntName);

            if (sfntName.name_id     == TT_NAME_ID_FONT_FAMILY &&
                sfntName.platform_id == TT_PLATFORM_MICROSOFT &&
                sfntName.encoding_id == TT_MS_ID_UNICODE_CS) {
                if (best == (FT_UInt)-1 || sfntName.language_id == TT_MS_LANGID_ENGLISH_UNITED_STATES)
                    best = i;

                QLocale sysLocale = QLocale::system();
                switch (sfntName.language_id) {
                    case TT_MS_LANGID_KOREAN_EXTENDED_WANSUNG_KOREA :
                        if (sysLocale.language() == QLocale::Korean)
                            found = i;
                        break;

                    case TT_MS_LANGID_JAPANESE_JAPAN :
                        if (sysLocale.language() == QLocale::Japanese)
                            found = i;
                        break;

                    case TT_MS_LANGID_CHINESE_PRC :
                        if (sysLocale.country() == QLocale::China &&
                            sysLocale.language() == QLocale::Chinese)
                            found = i;
                        break;

                    case TT_MS_LANGID_CHINESE_TAIWAN :
                        if (sysLocale.country() == QLocale::Taiwan &&
                            sysLocale.language() == QLocale::Chinese)
                            found = i;
                        break;
                }
            }
        }

        if (found == (FT_UInt)-1)
            found = best;

        if (found != (FT_UInt)-1) {
            FT_Get_Sfnt_Name(ftface, found, &sfntName);

            familyName = codec->toUnicode((const char *)sfntName.string, sfntName.string_len);

            return true;
        }
    }
#endif

    found = (FT_UInt)-1;
    best  = (FT_UInt)-1;

    // unicode name is not available, try the NLS encoded name
    for (i = 0; found == (FT_UInt)-1 && i < nNameCount; i++) {
        FT_Get_Sfnt_Name(ftface, i, &sfntName);

        if (sfntName.name_id     == TT_NAME_ID_FONT_FAMILY &&
            sfntName.platform_id == TT_PLATFORM_MICROSOFT) {
            if (best == (FT_UInt)-1)
                best = i;

            QLocale sysLocale = QLocale::system();
            switch (sfntName.encoding_id) {
                case TT_MS_ID_WANSUNG :
                    if (sysLocale.language() == QLocale::Korean)
                        found = i;
                    break;

                case TT_MS_ID_SJIS :
                    if (sysLocale.language() == QLocale::Japanese)
                        found = i;
                    break;

                case TT_MS_ID_BIG_5 :
                    if (sysLocale.country() == QLocale::Taiwan &&
                        sysLocale.language() == QLocale::Chinese)
                        found = i;
                    break;

                case TT_MS_ID_GB2312 :
                    if (sysLocale.country() == QLocale::China &&
                        sysLocale.language() == QLocale::Chinese)
                        found = i;
                    break;

                case TT_MS_ID_SYMBOL_CS :
                    found = i;
                    break;
            }
        }
    }

    if (found == (FT_UInt)-1)
        found = best;

    if (found != (FT_UInt)-1)
    {
        char   *name;
        FT_UInt name_len = 0;

        FT_Get_Sfnt_Name(ftface, found, &sfntName);

        name = (char *)alloca(sfntName.string_len);

        for (FT_UInt j = 0; j < sfntName.string_len; j++)
            if (sfntName.string[j])
                name[name_len++] = sfntName.string[j];

#ifndef QT_NO_TEXTCODEC
        switch (sfntName.encoding_id) {
            case TT_MS_ID_WANSUNG :
                codec = QTextCodec::codecForName("cp949");
                break;

            case TT_MS_ID_SJIS :
                codec = QTextCodec::codecForName("SJIS");
                break;

            case TT_MS_ID_BIG_5 :
                codec = QTextCodec::codecForName("Big5");
                break;

            case TT_MS_ID_GB2312 :
                codec = QTextCodec::codecForName("GB2312");
                break;

            case TT_MS_ID_SYMBOL_CS :
            default :
                codec = NULL;
                break;
        }

        if (codec)
            familyName = codec->toUnicode(name, name_len);
        else
#endif
            familyName = QString::fromLocal8Bit(name, name_len);

        return true;
    }

    return false;
}

static QList<FaceData> readFreeTypeFont(FT_Library lib, const QByteArray &file,
                                        const QByteArray *data = 0)
{
    QList<FaceData> faces;

    FT_Long numFaces = 0;
    FT_Face face;

    bool isMemFont = file.startsWith(":qmemoryfonts/");
    Q_ASSERT(!isMemFont || data);

    FT_Error rc = isMemFont ?
        FT_New_Memory_Face(lib, (const FT_Byte *)data->constData(),
                                data->size(), -1, &face) :
        FT_New_Face(lib, file, -1, &face);

    if (rc == 0) {
        numFaces = face->num_faces;
        FT_Done_Face(face);
    } else {
        // note: for invalid/unsupported font file, numFaces is left 0 so that
        // this function will return an empty list
    }

    FD_DEBUG("readFreeTypeFont: Font file %s: FT error %d, has %ld faces",
             file.constData(), (int) rc, numFaces);

    // go throuhg each face
    for (FT_Long idx = 0; idx < numFaces; ++idx) {
        rc = isMemFont ?
            FT_New_Memory_Face(lib, (const FT_Byte *)data->constData(),
                                    data->size(), idx, &face) :
            FT_New_Face(lib, file, idx, &face);
        if (rc != 0)
            continue;

        FaceData faceData;

        faceData.file = file;
        faceData.index = idx;

        if (!lookupFamilyName(face, faceData.familyName))
            faceData.familyName = QString::fromLocal8Bit(face->family_name);

        // familyName may contain extra spaces (at least this is true for
        // TNR.PFB that is reported as "Times New Roman ". Trim them.
        faceData.familyName = faceData.familyName.trimmed();

        faceData.styleKey.style = face->style_flags & FT_STYLE_FLAG_ITALIC ?
            QFont::StyleItalic : QFont::StyleNormal;

        TT_OS2 *os2_table = 0;
        if (face->face_flags & FT_FACE_FLAG_SFNT) {
            os2_table = (TT_OS2 *)FT_Get_Sfnt_Table(face, ft_sfnt_os2);
        }
        if (os2_table) {
            // map weight and width values
            if (os2_table->usWeightClass < 400)
                faceData.styleKey.weight = QFont::Light;
            else if (os2_table->usWeightClass < 600)
                faceData.styleKey.weight = QFont::Normal;
            else if (os2_table->usWeightClass < 700)
                faceData.styleKey.weight = QFont::DemiBold;
            else if (os2_table->usWeightClass < 800)
                faceData.styleKey.weight = QFont::Bold;
            else
                faceData.styleKey.weight = QFont::Black;

            switch (os2_table->usWidthClass) {
                case 1: faceData.styleKey.stretch = QFont::UltraCondensed; break;
                case 2: faceData.styleKey.stretch = QFont::ExtraCondensed; break;
                case 3: faceData.styleKey.stretch = QFont::Condensed; break;
                case 4: faceData.styleKey.stretch = QFont::SemiCondensed; break;
                case 5: faceData.styleKey.stretch = QFont::Unstretched; break;
                case 6: faceData.styleKey.stretch = QFont::SemiExpanded; break;
                case 7: faceData.styleKey.stretch = QFont::Expanded; break;
                case 8: faceData.styleKey.stretch = QFont::ExtraExpanded; break;
                case 9: faceData.styleKey.stretch = QFont::UltraExpanded; break;
                default: faceData.styleKey.stretch = QFont::Unstretched; break;
            }

            quint32 unicodeRange[4] = {
                os2_table->ulUnicodeRange1, os2_table->ulUnicodeRange2,
                os2_table->ulUnicodeRange3, os2_table->ulUnicodeRange4
            };
            quint32 codePageRange[2] = {
                os2_table->ulCodePageRange1, os2_table->ulCodePageRange2
            };
            faceData.systems =
                determineWritingSystemsFromTrueTypeBits(unicodeRange, codePageRange);
        } else {
            // we've only got simple weight information and no stretch
            faceData.styleKey.weight = face->style_flags & FT_STYLE_FLAG_BOLD ?
                QFont::Bold : QFont::Normal;
            faceData.styleKey.stretch = QFont::Unstretched;
        }

        faceData.fixedPitch = face->face_flags & FT_FACE_FLAG_FIXED_WIDTH;

        faceData.smoothScalable = face->face_flags & FT_FACE_FLAG_SCALABLE;

        // the font may both be scalable and contain fixed size bitmaps
        if (face->face_flags & FT_FACE_FLAG_FIXED_SIZES) {
            for (FT_Int i = 0; i < face->num_fixed_sizes; ++i) {
                faceData.pixelSizes << face->available_sizes[i].height;
            }
        }

        faces << faceData;

        FT_Done_Face(face);
    }

    return faces;
}

static void populateDatabase(const QString& fam)
{
    QFontDatabasePrivate *db = privateDb();
    if (!db)
        return;

    QtFontFamily *family = 0;
    if(!fam.isEmpty()) {
        family = db->family(fam);
        if(family)
            return;
    } else if (db->count) {
        return;
    }

    // we don't recognize foundries on OS/2, use an empty one
    const QString foundryName;

#ifdef QFONTDATABASE_DEBUG
    QTime timer;
    timer.start();
#endif

    QSettings fontCache(QSettings::UserScope, QLatin1String("Trolltech"));
    fontCache.beginGroup(QLatin1String("Qt/Fonts/Cache 1.0"));

    if (!db->valid) {
        // Obtain the initial list of known font files from the font cache in
        // the registry. This list is used as an in-process cache which speeds
        // speed up populating by eliminating the need to access the registry
        // each time an unknown family is requested. This list is also necessary
        // to detect deleted font files.
        FD_DEBUG("populateDatabase: INVALID, getting font list from the cache");
        db->valid = true;
        knownFontFiles.clear();
        QStringList files = fontCache.childGroups();
        foreach(QString file, files) {
            file.replace(QLatin1Char('|'), QLatin1Char('/'));
            knownFontFiles.insert(file, FileData());
            // note that QFileInfo is empty so the file will be considered as
            // NEW which is necessary for the font to get into the database
        }
    } else {
        // reset the 'seen' flag
        for (FontFileHash::iterator it = knownFontFiles.begin();
             it != knownFontFiles.end(); ++it)
            it.value().seen = false;
    }

    QList<QFileInfo> fontFiles;

    // take the font files from HINI_USERPROFILE\PM_Fonts
    ULONG bufSize = 0;
    BOOL ok = PrfQueryProfileSize(HINI_USERPROFILE, "PM_Fonts", 0, &bufSize);
    Q_ASSERT(ok);
    if (ok) {
        char *buffer = new char[bufSize + 1 /*terminating NULL*/];
        Q_ASSERT(buffer);
        if (buffer) {
            ULONG bufLen = PrfQueryProfileString(HINI_USERPROFILE, "PM_Fonts", 0, 0,
                                                 buffer, bufSize);
            if (bufLen) {
                char *key = buffer;
                while (*key) {
                    ULONG keySize = 0;
                    ok = PrfQueryProfileSize(HINI_USERPROFILE, "PM_Fonts", key,
                                             &keySize);
                    if (ok) {
                        QByteArray file(keySize, 0);
                        ULONG keyLen =
                            PrfQueryProfileString(HINI_USERPROFILE, "PM_Fonts", key, 0,
                                                  file.data(), file.size());
                        file.truncate(keyLen - 1 /*terminating NULL*/);
                        if (!file.isEmpty()) {
                            // FreeType doesn't understand .OFM but understands .PFB
                            if (file.toUpper().endsWith(".OFM")) {
                                file.chop(4);
                                file.append(".PFB");
                            }
                            fontFiles << QFileInfo(QFile::decodeName(file));
                        }
                    }
                    key += strlen(key) + 1;
                }
            }
            delete buffer;
        }
    }

    // add the application-defined fonts (only file-based)
    foreach(const QFontDatabasePrivate::ApplicationFont &font, db->applicationFonts) {
        if (!font.fileName.startsWith(QLatin1String(":qmemoryfonts/")))
            fontFiles << QFileInfo(font.fileName);
    }

    // go through each font file and check if we have a valid cahce for it
    for (QList<QFileInfo>::iterator it = fontFiles.begin(); it != fontFiles.end();) {
        QFileInfo fileInfo = *it;
        QString fileName = fileInfo.canonicalFilePath().toLower();
        if (!fileName.isEmpty()) { // file may have been deleted
            fileInfo.setFile(fileName);
            // check the in-process file name cache
            FileData &cached = knownFontFiles[fileName];
            if (cached.fileInfo.filePath().isEmpty() ||
                cached.fileInfo.lastModified() != fileInfo.lastModified() ||
                cached.fileInfo.size() != fileInfo.size()) {
                // no cache entry or outdated
                FD_DEBUG("populateDatabase: NEW/UPDATED font file %s",
                         qPrintable(fileName));
                cached.fileInfo = fileInfo;
                cached.seen = true;
                // keep it in the list for further inspection
                ++it;
                continue;
            } else {
                // just set the 'seen' flag and skip this font
                // (it's already in the database)
                FD_DEBUG("populateDatabase: UNCHANGED font file %s",
                         qPrintable(fileName));
                cached.seen = true;
            }
        }
        // remove from the list, nothing to do with it
        it = fontFiles.erase(it);
    }

    FT_Library lib = qt_getFreetype();

    QList<FaceData> foundFaces;

    // go through each new/outdated font file and get available faces
    foreach(const QFileInfo &fileInfo, fontFiles) {
        QString fileKey = fileInfo.canonicalFilePath().toLower();
        QByteArray file = QFile::encodeName(fileKey);

        // QSettings uses / for splitting into groups, suppress it
        fileKey.replace(QLatin1Char('/'), QLatin1Char('|'));

        QList<FaceData> cachedFaces;

        // first, look up the cached data
        fontCache.beginGroup(fileKey);

        if (fontCache.value(QLatin1String("DateTime")).toDateTime() != fileInfo.lastModified() ||
            fontCache.value(QLatin1String("Size")).toUInt() != fileInfo.size()) {
            // the cache is outdated or doesn't exist, query the font file
            cachedFaces = readFreeTypeFont(lib, file);

            // store the data into the cache
            fontCache.setValue(QLatin1String("DateTime"), fileInfo.lastModified());
            fontCache.setValue(QLatin1String("Size"), fileInfo.size());

            // note: for an invalid/unsupported font file, cachedFaces is empty,
            // so only DateTime and Size will be cached indicating hat this
            // file is not recognized
            foreach(FaceData cached, cachedFaces) {
                QByteArray rawData;
                QDataStream data(&rawData, QIODevice::WriteOnly);
                data << cached;

                QString face = QString::number(cached.index);
                fontCache.beginGroup(face);
                fontCache.setValue(QLatin1String("Info"), rawData);
                fontCache.endGroup();
            }
        } else {
            // take the face data from the cache
            QStringList faces = fontCache.childGroups();

            FD_DEBUG("populateDatabase: Font file %s: IN CACHE, has %d faces",
                     file.constData(), faces.count());

            foreach(QString face, faces) {
                bool ok = false;
                FaceData cached;
                cached.file = file;
                cached.index = face.toInt(&ok);
                if (!ok || cached.index < 0) // not a valid index
                    continue;

                fontCache.beginGroup(face);
                QByteArray rawData =
                    fontCache.value(QLatin1String("Info")).toByteArray();
                QDataStream data(rawData);
                data >> cached;
                fontCache.endGroup();

                cachedFaces << cached;
            }
        }

        foundFaces << cachedFaces;

        fontCache.endGroup();
    }

    // get available faces of the application-defined fonts (memory-based)
    foreach(const QFontDatabasePrivate::ApplicationFont &font, db->applicationFonts) {
        if (font.fileName.startsWith(QLatin1String(":qmemoryfonts/"))) {
            QList<FaceData> faces = readFreeTypeFont(lib, font.fileName.toLatin1(),
                                                     &font.data);
            foundFaces << faces;
        }
    }

    // go throuhg each found face and add it to the database
    foreach(const FaceData &face, foundFaces) {

        QtFontFamily *family = privateDb()->family(face.familyName, true);

        // @todo is it possible that the same family is both fixed and not?
        Q_ASSERT(!family->fixedPitch || face.fixedPitch);
        family->fixedPitch = face.fixedPitch;

        if (face.systems.isEmpty()) {
            // it was hard or impossible to determine the actual writing system
            // of the font (as in case of OS/2 bitmap and PFB fonts for which it is
            // usually simply reported that they support standard/system codepages).
            // Pretend that we support all writing systems to not miss the one.
            //
            // @todo find a proper way to detect actual supported scripts to make
            // sure these fonts are not matched for scripts they don't support.
            for (int ws = 0; ws < QFontDatabase::WritingSystemsCount; ++ws)
                family->writingSystems[ws] = QtFontFamily::Supported;
        } else {
            for (int i = 0; i < face.systems.count(); ++i)
                family->writingSystems[face.systems.at(i)] = QtFontFamily::Supported;
        }

        QtFontFoundry *foundry = family->foundry(foundryName, true);
        QtFontStyle *style = foundry->style(face.styleKey, true);

        // so far, all recognized fonts are antialiased
        style->antialiased = true;

        if (face.smoothScalable && !style->smoothScalable) {
            // add new scalable style only if it hasn't been already added --
            // the first one of two duplicate (in Qt terms) non-bitmap font
            // styles wins.
            style->smoothScalable = true;
            QtFontSize *size =
                style->pixelSize(SMOOTH_SCALABLE, true);
            size->fileName = face.file;
            size->fileIndex = face.index;
            size->systems = face.systems;
        }

        foreach(unsigned short pixelSize, face.pixelSizes) {
            QtFontSize *size = style->pixelSize(pixelSize, true);
            // the first bitmap style with a given pixel and point size wins
            if (!size->fileName.isEmpty())
                continue;
            size->fileName = face.file;
            size->fileIndex = face.index;
            size->systems = face.systems;
        }
    }

    // go through the known file list to detect what files have been removed
    for (FontFileHash::iterator it = knownFontFiles.begin();
         it != knownFontFiles.end();) {
        if (!it.value().seen) {
            FD_DEBUG("populateDatabase: DELETED font file %s",
                     qPrintable(it.key()));
            // remove from the both caches
            QString fileKey = it.key();
            fileKey.replace(QLatin1Char('/'), QLatin1Char('|'));
            fontCache.remove(fileKey);
            it = knownFontFiles.erase(it);
            // @todo should we remove all references to this file from the
            // font database? My concern is that this font may be in use by Qt
            // and its glyphs may be still cached when file deletion happens
        } else {
            ++it;
        }
    }

#ifdef QFONTDATABASE_DEBUG
    FD_DEBUG("populateDatabase: took %d ms", timer.elapsed());
#endif
}

static void initializeDb()
{
    QFontDatabasePrivate *db = privateDb();
    if (!db || db->count)
        return;

    populateDatabase(QString());

#ifdef QFONTDATABASE_DEBUG
    // print the database
    qDebug("initializeDb:");
    for (int f = 0; f < db->count; f++) {
        QtFontFamily *family = db->families[f];
        qDebug("    %s: %p", qPrintable(family->name), family);
        populateDatabase(family->name);
#if 1
        qDebug("        writing systems supported:");
        QStringList systems;
        for (int ws = 0; ws < QFontDatabase::WritingSystemsCount; ++ws)
            if (family->writingSystems[ws] & QtFontFamily::Supported)
                systems << QFontDatabase::writingSystemName((QFontDatabase::WritingSystem)ws);
        qDebug() << "            " << systems;
        for (int fd = 0; fd < family->count; fd++) {
            QtFontFoundry *foundry = family->foundries[fd];
            qDebug("        %s", foundry->name.isEmpty() ? "(empty foundry)" :
                   qPrintable(foundry->name));
            for (int s = 0; s < foundry->count; s++) {
                QtFontStyle *style = foundry->styles[s];
                qDebug("            style: style=%d weight=%d smooth=%d",  style->key.style,
                       style->key.weight, style->smoothScalable);
                for(int i = 0; i < style->count; ++i) {
                    if (style->pixelSizes[i].pixelSize == SMOOTH_SCALABLE)
                        qDebug("                smooth %s:%d",
                               style->pixelSizes[i].fileName.constData(),
                               style->pixelSizes[i].fileIndex);
                    else
                        qDebug("                %d px %s:%d", style->pixelSizes[i].pixelSize,
                               style->pixelSizes[i].fileName.constData(),
                               style->pixelSizes[i].fileIndex);
                }
            }
        }
#endif
    }
#endif // QFONTDATABASE_DEBUG
}

static inline void load(const QString &family = QString(), int = -1)
{
    populateDatabase(family);
}

static void registerFont(QFontDatabasePrivate::ApplicationFont *fnt)
{
    Q_ASSERT(fnt);
    if (!fnt)
        return;

    QByteArray file =
        QFile::encodeName(QDir::current().absoluteFilePath(fnt->fileName));

    FT_Library lib = qt_getFreetype();
    QList<FaceData> faces = readFreeTypeFont(lib, file, &fnt->data);

    QStringList families;

    foreach(const FaceData &face, faces)
        if (!families.contains(face.familyName))
            families << face.familyName;

    fnt->families = families;
}

static QFontDef fontDescToFontDef(const QFontDef &req, const QtFontDesc &desc)
{
    static LONG dpi = -1;
    if (dpi == -1) {
        // PM cannot change resolutions on the fly so cache it
        int hps = qt_display_ps();
        DevQueryCaps(GpiQueryDevice(hps), CAPS_HORIZONTAL_FONT_RES, 1, &dpi);
    }

    QFontDef fontDef;

    fontDef.family = desc.family->name;

    if (desc.size->pixelSize == SMOOTH_SCALABLE) {
        // scalable font matched, calculate the missing size (points or pixels)
        fontDef.pointSize = req.pointSize;
        fontDef.pixelSize = req.pixelSize;
        if (req.pointSize < 0) {
            fontDef.pointSize = req.pixelSize * 72. / dpi;
        } else if (req.pixelSize == -1) {
            fontDef.pixelSize = qRound(req.pointSize * dpi / 72.);
        }
    } else {
        // non-scalable font matched, calculate both point and pixel size
        fontDef.pixelSize = desc.size->pixelSize;
        fontDef.pointSize = desc.size->pixelSize * 72. / dpi;
    }

    fontDef.styleStrategy = req.styleStrategy;
    fontDef.styleHint = req.styleHint;

    fontDef.weight = desc.style->key.weight;
    fontDef.fixedPitch = desc.family->fixedPitch;
    fontDef.style = desc.style->key.style;
    fontDef.stretch = desc.style->key.stretch;

    return fontDef;
}

static QFontEngine *loadEngine(const QFontDef &req, const QtFontDesc &desc)
{
    // @todo all these fixed so far; make configurable through the Registry
    // on per-family basis
    QFontEnginePMFT::HintStyle hintStyle = QFontEnginePMFT::HintFull;
    bool autoHint = true;
    QFontEngineFT::SubpixelAntialiasingType subPixel = QFontEngineFT::Subpixel_None;
    int lcdFilter = FT_LCD_FILTER_DEFAULT;
    bool useEmbeddedBitmap = true;

    QFontEngine::FaceId faceId;
    faceId.filename = desc.size->fileName;
    faceId.index = desc.size->fileIndex;

    QFontEngineFT *fe = new QFontEnginePMFT(fontDescToFontDef(req, desc), faceId,
                                            desc.style->antialiased, hintStyle,
                                            autoHint, subPixel, lcdFilter,
                                            useEmbeddedBitmap);
    Q_ASSERT(fe);
    if (fe && fe->invalid()) {
        FM_DEBUG("   --> invalid!\n");
        delete fe;
        fe = 0;
    }
    return fe;
}

static QString getAssociateFont()
{
    static char szFontName[FACESIZE + 1] = {0,};

    // already queried ?
    if (szFontName[0] == '\0') {
        // query the associated font
        if (PrfQueryProfileString(HINI_USERPROFILE,
                                  "PM_SystemFonts", "PM_AssociateFont", 0,
                                  szFontName, sizeof(szFontName))) {
            szFontName[FACESIZE] = '\0';

            // the format of the associated font is face_name;point_size
            // so remove ';' and later
            for (PSZ pch = szFontName; *pch; pch++)
                if (*pch == ';') {
                    *pch = '\0';
                    break;
                }
        }

        if (szFontName[0] == '\0')
            szFontName[0] = ';';
    }

    if (szFontName[0] != ';')
        return QString::fromLocal8Bit(szFontName);

    return QString::null;
}

static QFontEngine *loadPM(const QFontPrivate *d, int script, const QFontDef &req)
{
    // list of families to try
    QStringList families = familyList(req);

    const char *styleHint = qt_fontFamilyFromStyleHint(d->request);
    if (styleHint)
        families << QLatin1String(styleHint);

    // add the default family
    QString defaultFamily = QApplication::font().family();
    if (!families.contains(defaultFamily))
        families << defaultFamily;

    // add QFont::defaultFamily() to the list, for compatibility with
    // previous versions
    families << QApplication::font().defaultFamily();

    // add PM_AssociateFont to the list (used on DBCS systems to take the
    // missing glyphs from)
    QString associateFont = getAssociateFont();
    if (!associateFont.isEmpty() && !families.contains(associateFont))
        families << associateFont;

    // null family means find the first font matching the specified script
    families << QString();

    QtFontDesc desc;
    QFontEngine *fe = 0;
    QList<int> blacklistedFamilies;

    while (!fe) {
        for (int i = 0; i < families.size(); ++i) {
            QString family, foundry;
            parseFontName(families.at(i), foundry, family);
            FM_DEBUG("loadPM: >>>>>>>>>>>>>> trying to match '%s'", qPrintable(family));
            QT_PREPEND_NAMESPACE(match)(script, req, family, foundry, -1, &desc, blacklistedFamilies);
            if (desc.family)
                break;
        }
        if (!desc.family)
            break;
        FM_DEBUG("loadPM: ============== matched '%s'", qPrintable(desc.family->name));
        fe = loadEngine(req, desc);
        if (!fe)
            blacklistedFamilies.append(desc.familyIndex);
    }
    return fe;
}

void QFontDatabase::load(const QFontPrivate *d, int script)
{
    Q_ASSERT(script >= 0 && script < QUnicodeTables::ScriptCount);

    // normalize the request to get better caching
    QFontDef req = d->request;
    if (req.pixelSize <= 0)
        req.pixelSize = qMax(1, qRound(req.pointSize * d->dpi / 72.));
    req.pointSize = 0;
    if (req.weight == 0)
        req.weight = QFont::Normal;
    if (req.stretch == 0)
        req.stretch = 100;

    // @todo a hack to substitute "WarpSans" with "Workplace Sans". Remove this
    // when we start supporting OS/2 bitmap fonts.
    if (req.family == QLatin1String("WarpSans"))
        req.family = QLatin1String("Workplace Sans");

    QFontCache::Key key(req, d->rawMode ? QUnicodeTables::Common : script, d->screen);
    if (!d->engineData)
        getEngineData(d, key);

    // the cached engineData could have already loaded the engine we want
    if (d->engineData->engines[script])
        return;

    // set it to the actual pointsize, so QFontInfo will do the right thing
    req.pointSize = req.pixelSize * 72. / d->dpi;

    QFontEngine *fe = QFontCache::instance()->findEngine(key);

    if (!fe) {
        if (qt_enable_test_font && req.family == QLatin1String("__Qt__Box__Engine__")) {
            fe = new QTestFontEngine(req.pixelSize);
            fe->fontDef = req;
        } else {
            QMutexLocker locker(fontDatabaseMutex());
            if (!privateDb()->count)
                initializeDb();
            fe = loadPM(d, script, req);
        }
        if (!fe) {
            fe = new QFontEngineBox(req.pixelSize);
            fe->fontDef = QFontDef();
        }
    }
    if (fe->symbol || (d->request.styleStrategy & QFont::NoFontMerging)) {
        for (int i = 0; i < QUnicodeTables::ScriptCount; ++i) {
            if (!d->engineData->engines[i]) {
                d->engineData->engines[i] = fe;
                fe->ref.ref();
            }
        }
    } else {
        d->engineData->engines[script] = fe;
        fe->ref.ref();
    }
    QFontCache::instance()->insertEngine(key, fe);
}

bool QFontDatabase::removeApplicationFont(int handle)
{
    // nothing special to do here; just empty the given ApplicationFont entry

    QMutexLocker locker(fontDatabaseMutex());

    QFontDatabasePrivate *db = privateDb();
    if (handle < 0 || handle >= db->applicationFonts.count())
        return false;

    db->applicationFonts[handle] = QFontDatabasePrivate::ApplicationFont();

    db->invalidate();
    return true;
}

bool QFontDatabase::removeAllApplicationFonts()
{
    // nothing special to do here; just empty all ApplicationFont entries

    QMutexLocker locker(fontDatabaseMutex());

    QFontDatabasePrivate *db = privateDb();
    if (db->applicationFonts.isEmpty())
        return false;

    db->applicationFonts.clear();
    db->invalidate();
    return true;
}

bool QFontDatabase::supportsThreadedFontRendering()
{
    // qt_getFreetype() returns a global static FT_Library object but
    // FreeType2 docs say that each thread should use its own FT_Library
    // object. For this reason, we return false here to prevent apps from
    // rendering fonts on threads other than the main GUI thread.
    return false;
}

QT_END_NAMESPACE
