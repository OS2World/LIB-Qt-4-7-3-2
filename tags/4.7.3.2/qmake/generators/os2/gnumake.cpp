/****************************************************************************
**
** Copyright (C) 2010 Nokia Corporation and/or its subsidiary(-ies).
** All rights reserved.
** Contact: Nokia Corporation (qt-info@nokia.com)
**
** This file is part of the qmake application of the Qt Toolkit.
**
** Copyright (C) 2010 netlabs.org. OS/2 parts.
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

#include "gnumake.h"
#include "option.h"
#include "meta.h"
#include <qregexp.h>
#include <qdir.h>
#include <stdlib.h>
#include <time.h>

QT_BEGIN_NAMESPACE

GNUMakefileGenerator::GNUMakefileGenerator() : Win32MakefileGenerator(), init_flag(false)
{
    if (isDosLikeShell())
        quote = "\"";
    else
        quote = "'";
}

bool GNUMakefileGenerator::isDosLikeShell() const
{
#ifdef Q_OS_OS2
    return Option::shellPath.isEmpty();
#else
    return Win32MakefileGenerator::isDosLikeShell();
#endif
}

QString GNUMakefileGenerator::escapeFilePath(const QString &path) const
{
    QString ret = path;
    // we assume that make variables contain paths already properly escaped
    if (!ret.isEmpty() && !ret.contains(QRegExp("\\$\\(([^$)]+)\\)"))) {
        ret = unescapeFilePath(ret);
        // fix separators and remove the trailing slahsh (if any) since ancient
        // tools such as CMD.EXE (e.g. its "cd" command) don't like them
        ret = Option::fixPathToTargetOS(ret, false);
        if (!isDosLikeShell()) {
            ret.replace(' ', "\\ ");
        } else {
            if (ret.contains(QRegExp("[ +&;%]")))
                ret = quote + ret + quote;
        }
    }
    return ret;
}

QString GNUMakefileGenerator::escapeDependencyPath(const QString &path) const
{
    // dependency escaping is always done Unix-way since this is a requirement
    // of GNU make which allows " and ' to be part of the file name.
    // Note that if the string is a make variable reference, we don't eascape
    // but instead use the q function defined in writeMakefile() that will do
    // it at runtime.
    QString ret = path;
    if (!ret.isEmpty()) {
        bool matched = false;
        if (path.contains("$("))
            ret = escapeFileVars(path, &matched);
        if (!matched) {
            // didn't find variable references, do plain escaping
            ret = unescapeFilePath(ret);
            ret.replace(' ', "\\ ");
            // and we still want to normalize slashes as this is what we do in
            // other places (e.g. .cpp targets); this function will do it for
            // QMAKE_EXTRA_TARGETS as well (this will also remove the trailing
            // slash, if any, see above)
            ret = Option::fixPathToTargetOS(ret, false);
        }
    }
    return ret;
}

// Searches for the best version of the requested library named \a stem in
// \a dir and append the result to the \a found list. If the best version is
// found, it is appended first in the list, otherwise \a stem is appended as
// is. If a .prl file corresponding to \a stem is found, the library name is
// taken from there. If the .prl refers to other libraries the found one
// depends on, they are also appended to the returned list (as is).
//
// The function returns \c true if the best version for the given \a stem was
// actually found and \c false otherwise (\a found will remain untouched in this
// case).
//
// Note that if a circular reference to the project's target library or to a
// library we already processed is detected, the function returns \c true but
// \a found remains untouched; this indicates that the given \a stem should be
// dropped from the resulting library list at all.
bool
GNUMakefileGenerator::findBestVersion(const QString &dir, const QString &stem,
                                      const QString &ext, QStringList &found)
{
    QString localDir = Option::fixPathToLocalOS(dir, true);
    if(!exists(localDir)) {
        return false;
    }

    QString dllStem = stem + QTDLL_POSTFIX;

    QString versionOverride;
    if(!project->values("QMAKE_" + stem.toUpper() + "_VERSION_OVERRIDE").isEmpty())
        versionOverride = project->values("QMAKE_" + stem.toUpper() + "_VERSION_OVERRIDE").first();

    if (project->isActiveConfig("link_prl")) {
        QMakeMetaInfo libinfo;
        QString prl = QMakeMetaInfo::findLib(localDir + Option::dir_sep + dllStem);
        if (project->values("QMAKE_PRL_INTERNAL_FILES").contains(prl)) {
            // already processed, drop this stem
            return true;
        }
        if (libinfo.readLib(prl)) {
            // take the data from .prl
            project->values("QMAKE_PRL_INTERNAL_FILES") += prl;
            QString target = libinfo.first("QMAKE_PRL_TARGET");
            if (target == project->first("TARGET")) {
                // circular reference to ourselves, drop this stem
                return true;
            }
            if (target.isEmpty())
                target = dllStem;
            if (!versionOverride.isEmpty())
                target += versionOverride;
            else if (!libinfo.values("QMAKE_PRL_CONFIG").contains("staticlib") &&
                     !libinfo.isEmpty("QMAKE_PRL_VERSION"))
                target += libinfo.first("QMAKE_PRL_VERSION").replace(".", "");

            // put the stem replacement as found in .prl
            found << target;
            // put all dependencies as is
            foreach (const QString &lib, libinfo.values("QMAKE_PRL_LIBS"))
                found << lib;

            return true;
        }
    }

    if (!versionOverride.isEmpty()) {
        // unconditionally use verrsion overrride
        found << stem + versionOverride;
        return true;
    }

    int biggest = -1;
    if(!project->isActiveConfig("no_versionlink")) {
        // search for a library with the highest version number
        QDir dir(localDir);
        QStringList entries = dir.entryList();
        QRegExp regx(QString("((lib)?%1([0-9]*))(%2)$").arg(dllStem).arg(ext),
                     Qt::CaseInsensitive);
        foreach (const QString &ent, entries) {
            if(regx.exactMatch(ent)) {
                if (!regx.cap(3).isEmpty()) {
                    bool ok = true;
                    int num = regx.cap(3).toInt(&ok);
                    biggest = qMax(biggest, (!ok ? -1 : num));
                }
            }
        }
    }
    if (biggest != -1) {
        found << stem + QString::number(biggest);
        return true;
    }

    return false;
}

bool GNUMakefileGenerator::findLibraries()
{
    return findLibraries("QMAKE_LIBS") && findLibraries("QMAKE_LIBS_PRIVATE");
}

bool GNUMakefileGenerator::findLibraries(const QString &where)
{
    QStringList &libs = project->values(where);

    QList<QMakeLocalFileName> dirs;
    foreach (const QString &path, project->values("QMAKE_LIBDIR"))
        dirs.append(QMakeLocalFileName(path));

    QStringList seen;

    for (QStringList::Iterator it = libs.begin(); it != libs.end(); /*below*/) {
        QString &lib = *it;

        if (lib.startsWith("-L")) {
            // this is a library path
            dirs.append(QMakeLocalFileName(lib.mid(2)));
            ++it;
            continue;
        }

        // this must be a library file
        if (lib.startsWith("-l"))
            lib = lib.mid(2);

        if (seen.contains(lib, Qt::CaseInsensitive)) {
            // already processed, remove
            it = libs.erase(it);
            continue;
        }

        seen << lib;

        QString suffix;
        if (!project->isEmpty("QMAKE_" + lib.toUpper() + "_SUFFIX"))
            suffix = project->first("QMAKE_" + lib.toUpper() + "_SUFFIX");

        // try every libpath we have by now
        bool found = false;
        QStringList foundLibs;
        foreach (const QMakeLocalFileName &dir, dirs) {
            if ((found = findBestVersion(dir.local(), lib,
                                         QString("%1.lib").arg(suffix), foundLibs)))
                break;
        }
        if (!found) {
            // assume the stem is the exact specification and use as is
            foundLibs << lib;
        }

        if (foundLibs.isEmpty()) {
            // circular reference detected, remove this library from the list
            it = libs.erase(it);
        } else {
            // replace the library with the best found one
            lib = foundLibs.takeFirst();
            // this may introduce a duplicate, check that
            bool remove = false;
            for (QStringList::ConstIterator it2 = libs.begin(); it2 != it; ++it2)
                if ((remove = it2->compare(lib, Qt::CaseInsensitive) == 0))
                    break;
            // append the dependencies
            if (!foundLibs.empty()) {
                int itDiff = it - libs.begin();
                foreach (const QString &foundLib, foundLibs) {
                    if (!libs.contains(foundLib, Qt::CaseInsensitive))
                        libs << foundLib;
                }
                // restore the iterator as appending invalidated it
                it = libs.begin() + itDiff;
            }
            if (remove)
                it = libs.erase(it);
            else
                ++it;
        }
    }

    return true;
}

void GNUMakefileGenerator::writeHeader(QTextStream &t)
{
    MakefileGenerator::writeHeader(t);

    /* function to convert from DOS-like to Unix-like space escaping in file
     * names */
    t << "null :=" << endl << "space := $(null) # end of the line" << endl;
    t << "# function to change DOS-like space escaping to Unix-like" << endl;
    t << "q = $(subst %,\\%,$(subst ;,\\;,$(subst &,\\&,"
         "$(subst +,\\+,$(subst $(space),\\ ,$(subst \",,$(1)))))))" << endl << endl;

    QString spec = specdir();
    if (QFileInfo(spec).fileName() == "default") {
        // use the real specdir; this allows to only have a dumb qmake.conf in
        // the default mkspec dir and keep the rest in the real one (to avoid
        // unnecessary duplication, in the absense of symlinks on OS/2)
        QString origSpec = var("QMAKESPEC_ORIGINAL");
        if (!origSpec.isEmpty()) {
            // Note: Option::mkfile::qmakespec is asbolute, specdir() may be not
            spec = QDir(Option::mkfile::qmakespec).absoluteFilePath(origSpec);
            spec = fileFixify(QDir::cleanPath(spec));
        }
    }
    project->values("QMAKESPECDIR") = QStringList(spec);
    t << "QMAKESPECDIR  = " << escapeFilePath(spec) << endl;

    // MakefileGenerator::writeSubTargets() already puts the MAKEFILE definition
    // for subdirs-like makefiles but we also use it in normal ones
    if (project->first("TEMPLATE") != "subdirs" &&
        (project->isActiveConfig("build_pass") || project->values("BUILDS").isEmpty())) {
        QString ofile = escapeFilePath(Option::output.fileName());
        if(ofile.lastIndexOf(Option::dir_sep) != -1)
            ofile = ofile.right(ofile.length() - ofile.lastIndexOf(Option::dir_sep) -1);
        t << "MAKEFILE      = " << ofile << endl;
    }

    t << endl;
}

bool GNUMakefileGenerator::writeMakefile(QTextStream &t)
{
    writeHeader(t);

    if(!project->values("QMAKE_FAILED_REQUIREMENTS").isEmpty()) {
        t << "all clean:" << "\n\t"
          << "@echo \"Some of the required modules ("
          << var("QMAKE_FAILED_REQUIREMENTS") << ") are not available.\"" << "\n\t"
          << "@echo \"Skipped.\"" << endl << endl;
        writeMakeQmake(t);
        return true;
    }

    if(project->first("TEMPLATE") == "app" ||
       project->first("TEMPLATE") == "lib") {
        if(Option::mkfile::do_stub_makefile) {
            t << "QMAKE    = "        << (project->isEmpty("QMAKE_QMAKE") ? QString("qmake") : var("QMAKE_QMAKE")) << endl;
            QStringList &qut = project->values("QMAKE_EXTRA_TARGETS");
            for(QStringList::ConstIterator it = qut.begin(); it != qut.end(); ++it)
                t << *it << " ";
            t << "first all clean install distclean uninstall: qmake" << endl
              << "qmake_all:" << endl;
            writeMakeQmake(t);
            if(project->isEmpty("QMAKE_NOFORCE"))
                t << "FORCE:" << endl << endl;
            return true;
        }
        writeGNUParts(t);
        return MakefileGenerator::writeMakefile(t);
    }
    else if(project->first("TEMPLATE") == "subdirs") {
        writeSubDirs(t);
        if (project->isActiveConfig("ordered")) {
            // Ordered builds are usually used when one sub-project links to
            // another sub-project's library. Sometimes detecting the proper
            // library name (e.g. the version suffix) requires at least a .prl
            // for that library to exist. The .prl is created by qmake at Makefile
            // generation time and these qmake calls are not actually ordered at
            // dependency level so in case of parallel builds they may get
            // executed simultaneously which breaks the build. It is not really
            // possible to order them using dependencies because there are usually
            // several target's flavors all depending on its Makefile and it's
            // impossible to detect which flavor of the "parent" target this
            // make file should depend on (depending on the "parent"'s Makefile
            // is not enough because the required libraries may be generated by
            // its own sub-projects at a later stage). One case showing this
            // behavior is designer.exe depending on QtDesignerComponents.lib.
            //
            // The only solution to this is to disable parallelism for subdirs
            // Makefiles completely. Which is not a big deal since "ordered"
            // implies sequential building any way.
            t << ".NOTPARALLEL:" << endl << endl;
        }
        return true;
    }
    return false;
 }

void GNUMakefileGenerator::writeGNUParts(QTextStream &t)
{
    writeStandardParts(t);

    if (!preCompHeaderOut.isEmpty()) {
        QString header = project->first("PRECOMPILED_HEADER");
        QString cHeader = preCompHeaderOut + Option::dir_sep + "c";
        t << escapeDependencyPath(cHeader) << ": " << escapeDependencyPath(header) << " "
            << escapeDependencyPaths(findDependencies(header)).join(" \\\n\t\t")
            << "\n\t" << mkdir_p_asstring(preCompHeaderOut)
            << "\n\t" << "$(CC) -x c-header -c $(CFLAGS) $(INCPATH) -o " << cHeader << " " << header
            << endl << endl;
        QString cppHeader = preCompHeaderOut + Option::dir_sep + "c++";
        t << escapeDependencyPath(cppHeader) << ": " << escapeDependencyPath(header) << " "
            << escapeDependencyPaths(findDependencies(header)).join(" \\\n\t\t")
            << "\n\t" << mkdir_p_asstring(preCompHeaderOut)
            << "\n\t" << "$(CXX) -x c++-header -c $(CXXFLAGS) $(INCPATH) -o " << cppHeader << " " << header
            << endl << endl;
    }
}

void GNUMakefileGenerator::init()
{
    if(init_flag)
        return;
    init_flag = true;

    if(project->first("TEMPLATE") == "app") {
        mode = App;
        project->values("QMAKE_APP_FLAG").append("1");
    } else if(project->first("TEMPLATE") == "lib") {
        mode = project->isActiveConfig("staticlib") ? StaticLib : DLL;
        project->values("QMAKE_LIB_FLAG").append("1");
    } else if(project->first("TEMPLATE") == "subdirs") {
        MakefileGenerator::init();
        if(project->isEmpty("QMAKE_COPY_FILE"))
            project->values("QMAKE_COPY_FILE").append("$(COPY)");
        if(project->isEmpty("QMAKE_COPY_DIR"))
            project->values("QMAKE_COPY_DIR").append("$(COPY)");
        if(project->isEmpty("QMAKE_INSTALL_FILE"))
            project->values("QMAKE_INSTALL_FILE").append("$(COPY_FILE)");
        if(project->isEmpty("QMAKE_INSTALL_PROGRAM"))
            project->values("QMAKE_INSTALL_PROGRAM").append("$(COPY_FILE)");
        if(project->isEmpty("QMAKE_INSTALL_DIR"))
            project->values("QMAKE_INSTALL_DIR").append("$(COPY_DIR)");
        if(project->values("MAKEFILE").isEmpty())
            project->values("MAKEFILE").append("Makefile");
        if(project->values("QMAKE_QMAKE").isEmpty())
            project->values("QMAKE_QMAKE").append("qmake");
        return;
    }

    project->values("TARGET_PRL").append(project->first("TARGET"));

    processVars();

    // LIBS defined in Profile comes first for gcc
    project->values("QMAKE_LIBS") += project->values("LIBS");
    project->values("QMAKE_LIBS_PRIVATE") += project->values("LIBS_PRIVATE");

    QStringList &configs = project->values("CONFIG");

    if(project->isActiveConfig("qt_dll"))
        if(configs.indexOf("qt") == -1)
            configs.append("qt");

    if(project->isActiveConfig("dll")) {
        QString destDir = "";
        if(!project->first("DESTDIR").isEmpty())
            destDir = Option::fixPathToTargetOS(project->first("DESTDIR") + Option::dir_sep, false, false);
    }

    MakefileGenerator::init();

    // precomp
    if (!project->first("PRECOMPILED_HEADER").isEmpty()
        && project->isActiveConfig("precompile_header")) {
        QString preCompHeader = var("PRECOMPILED_DIR")
                         + QFileInfo(project->first("PRECOMPILED_HEADER")).fileName();
        preCompHeaderOut = preCompHeader + ".gch";
        project->values("QMAKE_CLEAN").append(preCompHeaderOut + Option::dir_sep + "c");
        project->values("QMAKE_CLEAN").append(preCompHeaderOut + Option::dir_sep + "c++");

        project->values("QMAKE_RUN_CC").clear();
            project->values("QMAKE_RUN_CC").append("$(CC) -c -include " + preCompHeader +
                                               " $(CFLAGS) $(INCPATH) -o $obj $src");
        project->values("QMAKE_RUN_CC_IMP").clear();
        project->values("QMAKE_RUN_CC_IMP").append("$(CC)  -c -include " + preCompHeader +
                                                   " $(CFLAGS) $(INCPATH) -o $@ $<");
        project->values("QMAKE_RUN_CXX").clear();
        project->values("QMAKE_RUN_CXX").append("$(CXX) -c -include " + preCompHeader +
                                                " $(CXXFLAGS) $(INCPATH) -o $obj $src");
        project->values("QMAKE_RUN_CXX_IMP").clear();
        project->values("QMAKE_RUN_CXX_IMP").append("$(CXX) -c -include " + preCompHeader +
                                                    " $(CXXFLAGS) $(INCPATH) -o $@ $<");
    }
}

void GNUMakefileGenerator::fixTargetExt()
{
    if (mode == App)
        project->values("TARGET_EXT").append(".exe");
    else if (mode == DLL)
        project->values("TARGET_EXT").append(project->first("TARGET_VERSION_EXT") + ".dll");
    else
        project->values("TARGET_EXT").append(".lib");
}

void GNUMakefileGenerator::writeIncPart(QTextStream &t)
{
    t << "INCPATH       =";

    QString opt = var("QMAKE_CFLAGS_INCDIR");
    QStringList &incs = project->values("INCLUDEPATH");
    QString incsSemicolon;
    for(QStringList::Iterator incit = incs.begin(); incit != incs.end(); ++incit) {
        QString inc = escapeFilePath(*incit);
        t << " " << opt << inc;
        incsSemicolon += inc + Option::dirlist_sep;
    }
    t << " " << opt << "$(QMAKESPECDIR)" << endl;
    incsSemicolon += "$(QMAKESPECDIR)";
    t << "INCLUDEPATH   = " << incsSemicolon << endl;
    /* createCompilerResponseFiles() will need QMAKESPECDIR expanded) */
    project->values("INCLUDEPATH").append(var("QMAKESPECDIR"));
}

void GNUMakefileGenerator::writeLibsPart(QTextStream &t)
{
    if (mode == StaticLib) {
        t << "LIB           = " << var("QMAKE_LIB") << endl;
    } else {
        t << "LINK          = " << var("QMAKE_LINK") << endl;
        t << "LFLAGS        = " << var("QMAKE_LFLAGS") << endl;
        t << "LIBS          =";
        if(!project->values("QMAKE_LIBDIR").isEmpty())
            writeLibDirPart(t);
        QString opt = var("QMAKE_LFLAGS_LIB");
        QString optL = var("QMAKE_LFLAGS_LIBDIR");
        QStringList libVars;
        libVars << "QMAKE_LIBS" << "QMAKE_LIBS_PRIVATE";
        foreach(const QString &libVar, libVars) {
            QStringList libs = project->values(libVar);
            for(QStringList::Iterator it = libs.begin(); it != libs.end(); ++it) {
                QString &lib = *it;

                /* lib may be prefixed with -l which is commonly used in e.g. PRF
                 * (feature) files on all platforms; remove it before prepending
                 * the compiler-specific option. There is even more weird case
                 * when LIBS contains library paths prefixed with -L; we detect
                 * this as well and replace it with the proper option. */
                if (lib.startsWith("-L")) {
                    lib = lib.mid(2);
                    t << " " << optL << escapeFilePath(lib);
                } else {
                    if (lib.startsWith("-l"))
                        lib = lib.mid(2);
                    t << " " << opt << escapeFilePath(lib);
                }
            }
        }
        t << endl;
    }
}

void GNUMakefileGenerator::writeLibDirPart(QTextStream &t)
{
    QString opt = var("QMAKE_LFLAGS_LIBDIR");
    QStringList libDirs = project->values("QMAKE_LIBDIR");
    for(QStringList::Iterator it = libDirs.begin(); it != libDirs.end(); ++it) {
        QString libDir = escapeFilePath(*it);
        /* libDir may be prefixed with -L which is commonly used in e.g. PRF
         * (feature) files on all platforms; remove it before prepending
         * the compiler-specific option */
        if (libDir.startsWith("-L"))
            libDir = libDir.mid(2);
        t << " " << opt << libDir;
    }
}

void GNUMakefileGenerator::writeObjectsPart(QTextStream &t)
{
    Win32MakefileGenerator::writeObjectsPart(t);
    createLinkerResponseFiles(t);

    /* this function is a nice place to also handle compiler options response
     * files */
    createCompilerResponseFiles(t);
}

void GNUMakefileGenerator::writeBuildRulesPart(QTextStream &t)
{
    t << "first: all" << endl;
    t << "all: " << escapeDependencyPath(fileFixify(Option::output.fileName())) << " "
                 << valGlue(escapeDependencyPaths(project->values("ALL_DEPS"))," "," "," ")
                 << escapeFileVars(" $(DESTDIR_TARGET)") << endl << endl;
    t << escapeFileVars("$(DESTDIR_TARGET): ") << escapeFileVars(var("PRE_TARGETDEPS"))
                                               << escapeFileVars(" $(OBJECTS) ")
                                               << escapeFileVars(var("POST_TARGETDEPS"));
    if (!project->isEmpty("QMAKE_PRE_LINK"))
        t << "\n\t" <<var("QMAKE_PRE_LINK");
    if (mode == StaticLib) {
        /* static library */
        t << "\n\t" << var("QMAKE_RUN_LIB");
    } else {
        /* application or DLL */
        t << "\n\t" << var("QMAKE_RUN_LINK");
        if (!project->isEmpty("RES_FILE") && !project->isEmpty("QMAKE_RUN_RC2EXE")) {
            t << "\n\t" << var("QMAKE_RUN_RC2EXE");
        }
    }
    if(!project->isEmpty("QMAKE_POST_LINK"))
        t << "\n\t" <<var("QMAKE_POST_LINK");
    t << endl;
}

void GNUMakefileGenerator::writeRcAndDefVariables(QTextStream &t)
{
    if (!project->isEmpty("RC_FILE")) {
        t << "RC_FILE       = " << escapeFilePath(fileFixify(var("RC_FILE"))) << endl;
    }
    if (!project->isEmpty("RES_FILE")) {
        t << "RES_FILE      = " << valList(escapeFilePaths(fileFixify(project->values("RES_FILE")))) << endl;
    }

    if (mode != StaticLib) {
        if (project->isEmpty("DEF_FILE")) {
            /* no DEF file supplied, emit handy variable definitions to simplify
             * DEF generation rules defined somewhere in default_post.prf */
            bool haveSomething = false;
            if (!project->isEmpty("DEF_FILE_VERSION")) {
                t << "DEF_FILE_VERSION        = " << var("DEF_FILE_VERSION") << endl;
                haveSomething = true;
            }
            if (!project->isEmpty("DEF_FILE_DESCRIPTION")) {
                t << "DEF_FILE_DESCRIPTION    = " << var("DEF_FILE_DESCRIPTION") << endl;
                haveSomething = true;
            }
            if (!project->isEmpty("DEF_FILE_VENDOR")) {
                t << "DEF_FILE_VENDOR         = " << var("DEF_FILE_VENDOR") << endl;
                haveSomething = true;
            }
            if (!project->isEmpty("DEF_FILE_TEMPLATE")) {
                t << "DEF_FILE_TEMPLATE       = " << escapeFilePath(fileFixify(var("DEF_FILE_TEMPLATE"))) << endl;
                haveSomething = true;
            }
            if (!project->isEmpty("DEF_FILE_MAP")) {
                t << "DEF_FILE_MAP            = " << escapeFilePath(fileFixify(var("DEF_FILE_MAP"))) << endl;
                haveSomething = true;
            }
            if (mode == DLL) {
                // the DLL needs a file in any case
                t << "DEF_FILE      = $(basename $(DESTDIR_TARGET)).def" << endl;
            } else if (haveSomething) {
                // the EXE needs it only if there's a description info
                t << "DEF_FILE_EXETYPE        = " << var("DEF_FILE_EXETYPE") << endl;
                t << "DEF_FILE      = $(OBJECTS_DIR)\\$(TARGET).def" << endl;
            }
        } else {
            if (!project->isEmpty("DEF_FILE_TEMPLATE")) {
                fprintf(stderr, "Both DEF_FILE and DEF_FILE_TEMPLATE are specified.\n");
                fprintf(stderr, "Please specify one of them, not both.");
                exit(1);
            }
            t << "DEF_FILE      = " << escapeFilePath(fileFixify(var("DEF_FILE"))) << endl;
        }
    }

    if (mode == DLL) {
        // Note: $(TARGET) may be shortened here due to TARGET_SHORT, use
        // getLibTarget() instead (shortening does not affect implib names)
        t << "TARGET_IMPLIB = $(DESTDIR)\\" << getLibTarget() << endl;
    }
}

void GNUMakefileGenerator::writeRcAndDefPart(QTextStream &t)
{
    if (!project->isEmpty("RC_FILE") && !project->isEmpty("RES_FILE") &&
        !project->isEmpty("QMAKE_RUN_RC2RES")) {
        if (!project->isEmpty("QMAKE_RUN_RC2RES_ENV"))
            t << escapeFileVars("$(RES_FILE): ")
              << var("QMAKE_RUN_RC2RES_ENV") << endl;
        t << escapeFileVars("$(RES_FILE): $(RC_FILE)\n\t");
        t << var("QMAKE_RUN_RC2RES") << endl << endl;
    }
}

void GNUMakefileGenerator::processRcFileVar()
{
    if (Option::qmake_mode == Option::QMAKE_GENERATE_NOTHING)
        return;

    if (!project->isEmpty("RC_FILE")) {
        if (!project->isEmpty("RES_FILE")) {
            fprintf(stderr, "Both rc and res file specified.\n");
            fprintf(stderr, "Please specify one of them, not both.");
            exit(1);
        }
        project->values("RES_FILE").prepend(escapeFilePath(QString("$(OBJECTS_DIR)") +
                                                           QDir::separator() +
                                                           QFileInfo(var("RC_FILE")).baseName() +
                                                           ".res"));
        project->values("CLEAN_FILES") += "$(RES_FILE)";
    }

    if (!project->isEmpty("RES_FILE"))
        project->values("POST_TARGETDEPS") += escapeFileVars("$(RES_FILE)");
}

void GNUMakefileGenerator::processPrlVariable(const QString &var, const QStringList &l)
{
    // we don't do any processing here; everything we need is done in
    // GNUMakefileGenerator::findLibraries()
    return;
}

void GNUMakefileGenerator::processPrlFiles()
{
    // we don't do any processing here; everything we need is done in
    // GNUMakefileGenerator::findLibraries()
    return;
}

QString GNUMakefileGenerator::getPdbTarget()
{
    // we don't have .pdb files (.map and .sym are processed elsewere)
    return QString::null;
}

QString GNUMakefileGenerator::filePrefixRoot(const QString &root, const QString &path)
{
    QString ret;
    if(path.length() > 2 && path[1] == ':') { //c:\foo
        if (root == "$(INSTALL_ROOT)")
            ret = QString("$(if %1,%1,%2)").arg(root, path.left(2)) + path.mid(2);
        else
            ret = QString(path.left(2) + root + path.mid(2));
    } else {
        ret = root + path;
    }
    while(ret.endsWith("\\"))
        ret = ret.left(ret.length()-1);
    return ret;
}

QStringList &GNUMakefileGenerator::findDependencies(const QString &file)
{
    QStringList &aList = MakefileGenerator::findDependencies(file);
    // Note: The QMAKE_IMAGE_COLLECTION file have all images
    // as dependency, so don't add precompiled header then
    if (file == project->first("QMAKE_IMAGE_COLLECTION")
        || preCompHeaderOut.isEmpty())
        return aList;
    for (QStringList::Iterator it = Option::c_ext.begin(); it != Option::c_ext.end(); ++it) {
        if (file.endsWith(*it)) {
            QString cHeader = preCompHeaderOut + Option::dir_sep + "c";
            if (!aList.contains(cHeader))
                aList += cHeader;
            break;
        }
    }
    for (QStringList::Iterator it = Option::cpp_ext.begin(); it != Option::cpp_ext.end(); ++it) {
        if (file.endsWith(*it)) {
            QString cppHeader = preCompHeaderOut + Option::dir_sep + "c++";
            if (!aList.contains(cppHeader))
                aList += cppHeader;
            break;
        }
    }
    return aList;
}

void GNUMakefileGenerator::writeProjectVarToStream(QTextStream &t, const QString &var)
{
    QStringList &list = project->values(var);
    for(QStringList::Iterator it = list.begin(); it != list.end(); ++it) {
        t << (*it) << endl;
    }
}

QString GNUMakefileGenerator::makeResponseFileName(const QString &base)
{
    QString fileName = base + "." + var("TARGET");
    if (!var("BUILD_NAME").isEmpty()) {
        fileName += "." + var("BUILD_NAME");
    }
    fileName += ".rsp";
    QString filePath = project->first("OBJECTS_DIR");
    if (filePath.isEmpty())
        filePath = fileFixify(Option::output_dir);
    filePath = Option::fixPathToTargetOS(filePath + QDir::separator() + fileName);
    return filePath;
}

static QStringList fixDefines(const QStringList &vals)
{
    // Existing GCC 4.x.x builds for OS/2 can't handle escaping meta symbols
    // (e.g. quotes) with backslashes in response files (probably an OS/2-
    // specific bug). The fix this replacing all "\<char>" occurences with
    // "'<char>'"-like singlequoting which works.
    //
    // This backslash escaping is so frequently used in .pro files to pass
    // string defines to C/C++ (in the form of "DEFINES += VAR=\\\"STRING\\\")
    // that it makes sense to fix it here rather than introduce an OS/2-specific
    // alteration of the DEFINES statement in each .pro file.

    QStringList result;
    foreach(const QString &val, vals) {
        result << QString(val).replace(QRegExp("\\\\(.)"), "'\\1'");
    }
    return result;
}

void GNUMakefileGenerator::createCompilerResponseFiles(QTextStream &t)
{
    static const char *vars[] = { "CFLAGS", "CXXFLAGS", "DEFINES", "INCPATH" };

    /* QMAKE_XXX_RSP_VAR is used as a flag whether it is necessary to
     * generate response files to overcome the 1024 chars CMD.EXE limitation.
     * When this variable is defined, a response file with the relevant
     * information will be generated and its full path will be stored in an
     * environment variable with the given name which can then be referred to in
     * other places of qmake.conf (e.g. rules) */

    for (size_t i = 0; i < sizeof(vars)/sizeof(vars[0]); ++i) {
        QString rspVar =
            project->first(QString().sprintf("QMAKE_%s_RSP_VAR", vars[i]));
        if (!rspVar.isEmpty()) {
            QString fileName = makeResponseFileName(vars[i]);
            t << rspVar.leftJustified(14) << "= " << fileName << endl;
            QFile file(QDir(Option::output_dir).absoluteFilePath(fileName));
            if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
                QTextStream rt(&file);
                if (!qstrcmp(vars[i], "CFLAGS")) {
                    rt << varGlue("QMAKE_CFLAGS", QString::null, "\n", "\n");
                } else if (!qstrcmp(vars[i], "CXXFLAGS")) {
                    rt << varGlue("QMAKE_CXXFLAGS", QString::null, "\n", "\n");
                } else if (!qstrcmp(vars[i], "DEFINES")) {
                    rt << valGlue(fixDefines(project->values("PRL_EXPORT_DEFINES")),
                                  "-D", "\n-D", "\n")
                       << valGlue(fixDefines(project->values("DEFINES")),
                                  "-D", "\n-D", "\n");
                } else if (!qstrcmp(vars[i], "INCPATH")) {
                    QString opt = var("QMAKE_CFLAGS_INCDIR");
                    rt << varGlue("INCLUDEPATH", opt, "\n" + opt, "\n");
                } else {
                    Q_ASSERT(false);
                }
                rt.flush();
                file.close();
            }
            project->values("QMAKE_DISTCLEAN").append("$(" + rspVar + ")");
        }
    }
}

void GNUMakefileGenerator::createLinkerResponseFiles(QTextStream &t)
{
    /* see createCompilerResponseFiles() */
    QString var = project->first("QMAKE_OBJECTS_RSP_VAR");
    if (!var.isEmpty()) {
        QString fileName = makeResponseFileName("OBJECTS");
        t << var.leftJustified(14) << "= " << fileName << endl;
        QFile file(QDir(Option::output_dir).absoluteFilePath(fileName));
        if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream rt(&file);
            rt << varGlue("OBJECTS", QString::null, "\n", "\n");
            rt.flush();
            file.close();
        }
        project->values("QMAKE_DISTCLEAN").append("$(" + var + ")");
    }
}

// static
QString GNUMakefileGenerator::escapeFileVars(const QString &vars, bool *matched)
{
    /* In DOS environment, we escape spaces and other illegal characters in
     * filenames with double quotes. However, this is not appropriate for GNU
     * make rule definitions (targets/dependencies) where Unix escaping is
     * expected. If we'd deal only with immediate strings, we could provide
     * necessary escaping in place, but we often use make variables instead of
     * direct file names so we must perform such escaping on the fly. This is
     * what we do here using the q function that we define in writeMakefile().*/
    QString ret = vars;
    QRegExp rx = QRegExp("(?:\\$\\(call q,\\$\\((.+)\\)\\))|"
                         "(?:\\$\\(([^$)]+)\\))");
    rx.setMinimal(true);
    int pos = 0;
    bool wasMatch = false;
    while ((pos = rx.indexIn(ret, pos)) != -1) {
        wasMatch = true;
        QString var = rx.cap(1);
        if (var.isEmpty())
            var = rx.cap(2);
        // there are some make variables that contain multiple paths which we
        // cannot escape (finding double-quoted spaces is too complex for
        // the q function), so just skip them hoping they contain no spaces...
        if (var != "OBJECTS") {
            var = QString("$(call q,$(%1))").arg(var);
            ret.replace(pos, rx.matchedLength(), var);
            pos += var.length();
        } else {
            pos += rx.matchedLength();
        }
    }
    if (matched)
        *matched = wasMatch;
    return ret;
}

QT_END_NAMESPACE
