/* REXX */
/*
 * Configures to build the Qt library
 * Copyright (C) 2009 netlabs.org. OS/2 parts.
 *
 * This file is provided AS IS with NO WARRANTY OF ANY KIND, INCLUDING THE
 * WARRANTY OF DESIGN, MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */

signal on syntax
signal on halt
signal on novalue
trace off
numeric digits 12
'@echo off'


/*------------------------------------------------------------------------------
 globals
------------------------------------------------------------------------------*/

G.TAB           = '09'x
G.EOL           = '0D0A'x

G.ScreenWidth   = -1
G.ScreenHeight  = -1

G.Verbose       = 1
G.LogFile       = ''

/* initialize global variables */
G.QMAKE_SWITCHES    = ""
G.QMAKE_VARS        = ""
G.QMAKE_CONFIG      = ""
G.QTCONFIG_CONFIG   = ""
G.QT_CONFIG         = ""
G.SUPPORTED         = ""

G.QMAKE_OUTDIR      = ""

G.CFG_DEV           = "no"

G.QT_DEFAULT_BUILD_PARTS = "libs tools examples demos docs translations"

/* all globals to be exposed in procedures */
Globals = 'G. Opt. Static.'


/*------------------------------------------------------------------------------
 startup + main + termination
------------------------------------------------------------------------------*/

/* init system REXX library */
if (RxFuncQuery('SysLoadFuncs')) then do
    call RxFuncAdd 'SysLoadFuncs', 'RexxUtil', 'SysLoadFuncs'
    call SysLoadFuncs
end

/* detect script file and directory */
parse source . . G.ScriptFile
G.ScriptDir = FixDir(filespec('D', G.ScriptFile) || filespec('P', G.ScriptFile))

G.LogFile = FixDirNoSlash(directory())'\configure.log'
call DeleteFile G.LogFile

/* check text screen resolution */
parse value SysTextScreenSize() with G.ScreenHeight G.ScreenWidth
if (G.ScreenHeight < 25 | G.ScreenWidth < 80) then do
    address 'cmd' 'mode co80,25'
    parse value SysTextScreenSize() with G.ScreenHeight G.ScreenWidth
    if (G.ScreenHeight < 25 | G.ScreenWidth < 80) then do
        call SayErr 'WARNING: Cannot set screen size to 80 x 25!'
        call SayErr 'Some messages can be unreadable.'
        say
        call WaitForAnyKey
    end
end

call MagicCmdHook arg(1)
call MagicLogHook arg(1)

call Main arg(1)

call Done 0


/*------------------------------------------------------------------------------
 functions
------------------------------------------------------------------------------*/

/**
 *  Just do the job.
 *
 *  @param aArgs    Comand line arguments.
 */
Main: procedure expose (Globals)

    parse arg aArgs

    /* the directory of this script is the "source tree */
    G.RelPath = G.ScriptDir
    /* the current directory is the "build tree" or "object tree" */
    G.OutPath = directory()

    /* QTDIR may be set and point to an old or system-wide Qt installation */
    call UnsetEnv "QTDIR"

    /*--------------------------------------------------------------------------
     Qt version detection
    --------------------------------------------------------------------------*/

    G.QT_VERSION = ""
    G.QT_MAJOR_VERSION = ""
    G.QT_MINOR_VERSION = 0
    G.QT_PATCH_VERSION = 0
    G.QT_BUILD_VERSION = ""

    G.QT_PACKAGEDATE = ""

    qglobal_h = G.RelPath"\src\corelib\global\qglobal.h"

    i = 2 /* number of lines to match */
    do while lines(qglobal_h)
        str = linein(qglobal_h)
        parse var str w1 w2 w3
        if (w1 == '#define' & w2 == 'QT_VERSION_STR') then do
            parse var w3 '"'G.QT_VERSION'"'
            i = i -1
        end
        else if (w1 == '#define' & w2 == 'QT_PACKAGEDATE_STR') then do
            parse var w3 '"'G.QT_PACKAGEDATE'"'
            i = i -1
        end
        if (i == 0) then leave
    end
    /* close file to unlock it */
    call lineout qglobal_h

    if (G.QT_VERSION \== "") then do
        parse var G.QT_VERSION maj'.'minor'.'patch
        if (patch == "" | verify(patch, '0123456789') = 0) then do
            if (patch \== "") then
                G.QT_PATCH_VERSION = patch
            if (minor == "" | verify(minor, '0123456789') = 0) then do
                if (minor \== "") then
                    G.QT_MINOR_VERSION = minor
                if (maj \== "" & verify(maj, '0123456789') = 0) then
                    G.QT_MAJOR_VERSION = maj
            end
        end
    end
    if (G.QT_MAJOR_VERSION == "") then do
       call SayErr "Cannot process version from qglobal.h: """G.QT_VERSION""""
       call SayErr "Cannot proceed."
       call Done 1
    end

    if (G.QT_PACKAGEDATE == "") then do
       call SayErr "Unable to determine package date from qglobal.h: """G.QT_PACKAGEDATE""""
       call SayErr "Cannot proceed."
       call Done 1
    end

    /* grab the official build option if any
     * (@todo move this to the command line parsing code when it's done) */
    G.OfficialBuild = 0
    build = word(aArgs, 1)
    if (build == "--official-build" |,
        build == "--official-build-quiet") then do
        quiet = (build == "--official-build-quiet")
        build = word(aArgs, 2)
        if (build \= '') then do
            G.OfficialBuild = 1
            G.QT_BUILD_VERSION = build
            G.QT_CONFIG = "official_build"
            G.QT_DEFAULT_BUILD_PARTS = "libs tools examples demos docs translations"
            if (\quiet) then do
                call SaySay,
G.EOL||,
'WARNING: You are going to configure an official build of Qt4 for OS/2!'G.EOL||,
'Full version number is: 'G.QT_VERSION'.'G.QT_BUILD_VERSION||G.EOL||,
G.EOL||,
'If you understand what it means and want to continue,'G.EOL||,
'then type YES below. Otherwise, press Enter or Ctrl-C.'G.EOL
                call SayPrompt "Continue? ", 1
                str = linein()
                if (str \== "YES") then
                    call Done 255
            end
            aArgs = subword(aArgs, 3)
        end
    end

    /*--------------------------------------------------------------------------
     check the license
    --------------------------------------------------------------------------*/

    /* Qt for OS/2 is always the open source edition */
    G.Licensee      = "Open Source"
    G.Edition       = "OpenSource"
    G.EditionString = "Open Source"
    G.QT_EDITION    = "QT_EDITION_OPENSOURCE"

    /*--------------------------------------------------------------------------
     initalize variables
    --------------------------------------------------------------------------*/

    call QMakeVar "add", "styles", "cde mac motif plastique cleanlooks windows"
    call QMakeVar "add", "decorations", "default windows styled"

    /* @todo cleanup the option list below when it's clear which options are
     * inappropriate for OS/2 */

    /* initalize internal variables */
    G.Compiler = ""
    G.CompilerVersion = 0
    G.CompilerVersionString = ""

    G.CFG_CONFIGURE_EXIT_ON_ERROR = "yes"
    G.CFG_PROFILE = "no"
    G.CFG_EXCEPTIONS = "unspecified"
    G.CFG_CONCURRENT = "auto"
    G.CFG_GUI = "auto"
    G.CFG_SCRIPT = "auto"
    G.CFG_SCRIPTTOOLS = "auto"
    G.CFG_CONCURRENT = "auto"
    G.CFG_XMLPATTERNS = "auto"
    G.CFG_INCREMENTAL = "auto"
    G.CFG_QCONFIG = "full"
    G.CFG_DEBUG = "auto"
    G.CFG_DEBUG_RELEASE = "auto"
    G.CFG_SHARED = "yes"
    G.CFG_SM = "auto"
    G.CFG_ZLIB = "auto"
    G.CFG_SQLITE = "qt"
    G.CFG_GIF = "auto"
    G.CFG_TIFF = "auto"
    G.CFG_LIBTIFF = "auto"
    G.CFG_PNG = "yes"
    G.CFG_LIBPNG = "auto"
    G.CFG_JPEG = "auto"
    G.CFG_LIBJPEG = "auto"
    G.CFG_MNG = "auto"
    G.CFG_LIBMNG = "auto"
    G.CFG_OPENGL = "auto"
    G.CFG_SSE = "auto"
    G.CFG_FONTCONFIG = "auto"
    G.CFG_LIBFREETYPE = "auto"
    G.CFG_SQL_AVAILABLE = ""
    G.CFG_BUILD_PARTS = ""
    G.CFG_NOBUILD_PARTS = ""
    G.CFG_RELEASE_QMAKE = "no"
    G.CFG_PHONON = "auto"
    G.CFG_PHONON_BACKEND = "yes"
    G.CFG_MULTIMEDIA = "auto"
    G.CFG_AUDIO_BACKEND = "auto"
    G.CFG_SVG = "auto"
    G.CFG_DECLARATIVE = "auto"
    G.CFG_DECLARATIVE_DEBUG = "yes"
    G.CFG_WEBKIT = "auto"
    G.CFG_JAVASCRIPTCORE_JIT = "auto"

    G.CFG_GFX_AVAILABLE = ""
    G.CFG_GFX_ON = ""
    G.CFG_GFX_PLUGIN_AVAILABLE = ""
    G.CFG_GFX_PLUGIN = ""
    G.CFG_GFX_OFF = ""
    G.CFG_ARCH = ""
    G.CFG_HOST_ARCH = ""
    G.CFG_KBD_PLUGIN_AVAILABLE = ""
    G.CFG_KBD_PLUGIN = ""
    G.CFG_KBD_OFF = ""
    G.CFG_MOUSE_PLUGIN_AVAILABLE = ""
    G.CFG_MOUSE_PLUGIN = ""
    G.CFG_MOUSE_OFF = ""
    G.CFG_USE_GNUMAKE = "no"
    G.CFG_DECORATION_AVAILABLE = "styled windows default"
    G.CFG_DECORATION_ON = G.CFG_DECORATION_AVAILABLE /* all on by default */
    G.CFG_DECORATION_PLUGIN_AVAILABLE = ""
    G.CFG_DECORATION_PLUGIN = ""
    G.CFG_CUPS = "auto"
    G.CFG_NIS = "auto"
/* @todo do we really need this?
    G.CFG_ICONV = "auto"
    G.CFG_GLIB = "auto"
    G.CFG_GSTREAMER = "auto"
    G.CFG_NAS = "no"
*/
    G.CFG_QGTKSTYLE = "auto"
    G.CFG_OPENSSL = "auto"
    G.CFG_DBUS = "auto"
    G.CFG_LARGEFILE = "auto"
    G.CFG_PTMALLOC = "no"
    G.CFG_STL = "auto"
    G.CFG_PRECOMPILE = "auto"
    G.CFG_SEPARATE_DEBUG_INFO = "auto"
    G.CFG_IPV6 = "auto"
    G.CFG_MMX = "auto"
    G.CFG_3DNOW = "auto"
    G.CFG_SSE = "auto"
    G.CFG_SSE2 = "auto"
/* @todo do we really need this?
    G.CFG_CLOCK_GETTIME = "auto"
    G.CFG_CLOCK_MONOTONIC = "auto"
    G.CFG_MREMAP = "auto"
    G.CFG_GETADDRINFO = "auto"
    G.CFG_IPV6IFNAME = "auto"
    G.CFG_GETIFADDRS = "auto"
    G.CFG_INOTIFY = "auto"
*/
    G.CFG_USER_BUILD_KEY = ""
    G.CFG_ACCESSIBILITY = "auto"
    G.CFG_QT3SUPPORT = "no"
    G.CFG_ENDIAN = "auto"
    G.CFG_HOST_ENDIAN = "auto"
/* @todo do we really need this?
    G.CFG_FRAMEWORK = "auto"
*/
    G.CFG_PREFIX_INSTALL = "yes"
    G.CFG_SDK = ""
    G.D_FLAGS = ""
    G.I_FLAGS = ""
    G.L_FLAGS = ""
    G.l_FLAGS = ""
    G.QCONFIG_FLAGS = ""
    G.XPLATFORM = "" /* This seems to be the QMAKESPEC, like "linux-g++" */
    G.PLATFORM = GetEnv("QMAKESPEC")
    G.QT_CROSS_COMPILE = "no"

    if (G.OfficialBuild) then G.OPT_CONFIRM_LICENSE = "yes"
    else G.OPT_CONFIRM_LICENSE = "no"

    G.OPT_SHADOW = "maybe"
    G.OPT_FAST = "auto"
    G.OPT_HELP = ""
    G.CFG_SILENT = "no"
    G.CFG_GRAPHICS_SYSTEM = "default"

    /* variables used for locating installed Qt components */
    G.QT_INSTALL_PREFIX = ""
    G.QT_INSTALL_DOCS = ""
    G.QT_INSTALL_HEADERS = ""
    G.QT_INSTALL_LIBS = ""
    G.QT_INSTALL_BINS = ""
    G.QT_INSTALL_PLUGINS = ""
    G.QT_INSTALL_IMPORTS = ""
    G.QT_INSTALL_DATA = ""
    G.QT_INSTALL_TRANSLATIONS = ""
    G.QT_INSTALL_SETTINGS = ""
    G.QT_INSTALL_EXAMPLES = ""
    G.QT_INSTALL_DEMOS = ""

    G.QT_LIBINFIX = ""
    G.QT_NAMESPACE = ""

    /* variables that override ones in qmake.conf */
    G.QMAKE_MAPSYM = ""
    G.QMAKE_EXEPACK = ""
    G.QMAKE_EXEPACK_FLAGS = ""
    G.QMAKE_EXEPACK_POST_FLAGS = ""

    /* check SQL drivers, mouse drivers and decorations available in
     * this package */

    /* opensource version removes some drivers, so force them to be off */
    G.CFG_SQL_tds = "no"
    G.CFG_SQL_oci = "no"
    G.CFG_SQL_db2 = "no"

    G.CFG_SQL_AVAILABLE = ""
    if (DirExists(G.RelPath"\src\plugins\sqldrivers")) then do
        call SysFileTree G.RelPath"\src\plugins\sqldrivers\*", "found", "DO"
        do i = 1 to found.0
            base = filespec('N', found.i)
            if (left(base, 1) == '.')  then iterate
            G.CFG_SQL_AVAILABLE = Join(G.CFG_SQL_AVAILABLE, base)
            call value "G.CFG_SQL_"base, "auto"
        end
    end

    G.CFG_DECORATION_PLUGIN_AVAILABLE = ""
    if (DirExists(G.RelPath"\src\plugins\decorations")) then do
        call SysFileTree G.RelPath"\src\plugins\decorations\*", "found", "DO"
        do i = 1 to found.0
            base = filespec('N', found.i)
            if (left(base, 1) == '.')  then iterate
            G.CFG_DECORATION_PLUGIN_AVAILABLE = Join(G.CFG_DECORATION_PLUGIN_AVAILABLE, base)
        end
    end

    G.CFG_KBD_PLUGIN_AVAILABLE = ""
    if (DirExists(G.RelPath"\src\plugins\kbddrivers")) then do
        call SysFileTree G.RelPath"\src\plugins\kbddrivers\*", "found", "DO"
        do i = 1 to found.0
            base = filespec('N', found.i)
            if (left(base, 1) == '.')  then iterate
            G.CFG_KBD_PLUGIN_AVAILABLE = Join(G.CFG_KBD_PLUGIN_AVAILABLE, base)
        end
    end

    G.CFG_MOUSE_PLUGIN_AVAILABLE = ""
    if (DirExists(G.RelPath"\src\plugins\mousedrivers")) then do
        call SysFileTree G.RelPath"\src\plugins\mousedrivers\*", "found", "DO"
        do i = 1 to found.0
            base = filespec('N', found.i)
            if (left(base, 1) == '.')  then iterate
            G.CFG_MOUSE_PLUGIN_AVAILABLE = Join(G.CFG_MOUSE_PLUGIN_AVAILABLE, base)
        end
    end

    G.CFG_GFX_PLUGIN_AVAILABLE = ""
    if (DirExists(G.RelPath"\src\plugins\gfxdrivers")) then do
        call SysFileTree G.RelPath"\src\plugins\gfxdrivers\*", "found", "DO"
        do i = 1 to found.0
            base = filespec('N', found.i)
            if (left(base, 1) == '.')  then iterate
            G.CFG_GFX_PLUGIN_AVAILABLE = Join(G.CFG_GFX_PLUGIN_AVAILABLE, base)
        end
        G.CFG_GFX_OFF = G.CFG_GFX_AVAILABLE /* assume all off */
    end

    /*--------------------------------------------------------------------------
     parse command line arguments
    --------------------------------------------------------------------------*/

    /* a no-op to keep the help message close to the parsing code;
     * it will be displayed via "signal ShowHelp" when needed */
    if (0) then do
    ShowHelp:
        call SaySay,
G.EOL||,
"Usage: " filespec('N', G.ScriptFile) "[-h] [-release] [-debug] [-debug-and-release]"G.EOL||,
"        [-developer-build] [-make <part>] [-nomake <part>]"G.EOL||,
G.EOL||G.EOL||,
"Configure options:"G.EOL||,
G.EOL||,
" The defaults (*) are usually acceptable. A plus (+) denotes a default value"G.EOL||,
" that needs to be evaluated. If the evaluation succeeds, the feature is"G.EOL||,
" included. Here is a short explanation of each option:"G.EOL||,
G.EOL||,
" *  -release ........... Compile and link Qt with debugging turned off."G.EOL||,
"    -debug ............. Compile and link Qt with debugging turned on."G.EOL||,
"    -debug-and-release . Compile and link two versions of Qt, with and without"G.EOL||,
"                         debugging turned on (default in non-shadow builds)."G.EOL||,
G.EOL||,
"    -developer-build ... Compile and link Qt with Qt developer options"G.EOL||,
"                         (including auto-tests exporting), implies -debug."G.EOL||,
G.EOL||,
"Additional options:"G.EOL||,
G.EOL||,
"    -make <part> ....... Add part to the list of parts to be built at make time."G.EOL||,
"                         (default: "G.QT_DEFAULT_BUILD_PARTS")"G.EOL||,
"    -nomake <part> ..... Exclude part from the list of parts to be built."G.EOL||,
""
        call Done 1
    end

    if (aArgs \= "") then do
        call TokenizeString aArgs, 'G.Args'
        i = 1
        do while i <= G.Args.0
            a = G.Args.i
            if (StartsWith(a, '-')) then do
                opt = ''
                typ = ''
                select
                    when (a == "-h" | a == "-help" | a == "--help") then
                        opt = 'F G.OPT_HELP'

                    when (a == "-release") then
                        opt = 'F~ G.CFG_DEBUG'
                    when (a == "-debug") then
                        opt = 'F G.CFG_DEBUG'
                    when (a == "-debug-and-release") then
                        opt = 'F G.CFG_DEBUG_RELEASE'
                    when (a == "-developer-build") then
                        opt = 'F G.CFG_DEV'

                    when (a == "-prefix") then
                        opt = 'P G.QT_INSTALL_PREFIX'
                    when (a == "-bindir") then
                        opt = 'P G.QT_INSTALL_BINS'
                    when (a == "-libdir") then
                        opt = 'P G.QT_INSTALL_LIBS'
                    when (a == "-docdir") then
                        opt = 'P G.QT_INSTALL_DOCS'
                    when (a == "-headerdir") then
                        opt = 'P G.QT_INSTALL_HEADERS'
                    when (a == "-plugindir") then
                        opt = 'P G.QT_INSTALL_PLUGINS'
                    when (a == "-importdir") then
                        opt = 'P G.QT_INSTALL_IMPORTS'
                    when (a == "-datadir") then
                        opt = 'P G.QT_INSTALL_DATA'
                    when (a == "-translationdir") then
                        opt = 'P G.QT_INSTALL_TRANSLATIONS'
                    when (a == "-sysconfdir" | a == "settingsdir") then
                        opt = 'P G.QT_INSTALL_SETTINGS'
                    when (a == "-examplesdir") then
                        opt = 'P G.QT_INSTALL_EXAMPLES'
                    when (a == "-demosdir") then
                        opt = 'P G.QT_INSTALL_DEMOS'

                    when (a == "-nomake") then
                        opt = 'S+ G.CFG_NOBUILD_PARTS'
                    when (a == "-make") then
                        opt = 'S+ G.CFG_BUILD_PARTS'

                    otherwise nop
                end
                parse var opt typ opt
                if (opt == '' | typ == '') then do
                    call SayErr "ERROR: Invalid option '"G.Args.i"'."
                    call Done 1
                end
                if (StartsWith(typ, 'F')) then do
                    /* option is a flag */
                    if (EndsWith(typ, '~')) then call value opt, 'no'
                    else call value opt, 'yes'
                end
                else if (StartsWith(typ, 'S')) then do
                    /* argument is a string */
                    if (i == G.Args.0) then do
                        call SayErr "ERROR: Option '"G.Args.i"' needs a string."
                        call Done 1
                    end
                    i = i + 1
                    if (EndsWith(typ, '+')) then
                        call value opt, Join(value(opt), G.Args.i)
                    else
                        call value opt, G.Args.i
                end
                else if (typ == 'P') then do
                    /* argument is a path */
                    if (i == G.Args.0) then do
                        call SayErr "ERROR: Option '"G.Args.i"' needs a path."
                        call Done 1
                    end
                    i = i + 1
                    call value opt, translate(G.Args.i, '\', '/')
                end
            end
            else do
                call SayErr "ERROR: Invalid argument '"G.Args.i"'."
                call Done 1
            end
            i = i + 1
        end
    end

    if (G.CFG_QCONFIG \== "full" & G.CFG_QT3SUPPORT == "yes") then do
        call SayLog "Warning: '-qconfig "G.CFG_QCONFIG"' will disable the qt3support library."
        G.CFG_QT3SUPPORT = "no"
    end

    if (G.CFG_GUI == "no") then do
        call SayLog "Warning: -no-gui will disable the qt3support library."
        G.CFG_QT3SUPPORT = "no"
    end

    /* update QT_CONFIG to show our current predefined configuration */
    cfgs = "minimal small medium large full"
    w = wordpos(G.CFG_QCONFIG, cfgs)
    if (w > 0) then do
        /* these are a sequence of increasing functionality */
        do i = 1 to w
            G.QT_CONFIG = Join(G.QT_CONFIG, word(cfgs, i)"-config")
        end
    end
    else do
        /* not known to be sufficient for anything */
        fn = G.RelPath"\src\corelib\global\qconfig-"G.CFG_QCONFIG".h"
        if (\FileExists(fn)) then do
            call SayErr "Error: configuration file not found:"
            call SayErr "  "fn
            G.OPT_HELP = "yes"
        end
    end

    /*--------------------------------------------------------------------------
     build tree initialization
    --------------------------------------------------------------------------*/

    /* find the perl command */
    G.PERL = SysSearchPath('PATH', 'perl.exe')
    if (G.PERL == "") then
        G.PERL = SysSearchPath('PATH', 'perl.cmd')

    /* is this a shadow build? */
    if (G.OPT_SHADOW == "maybe") then do
        G.OPT_SHADOW = "no"
        if (translate(G.RelPath) \== translate(G.OutPath) &,
            \FileExists(G.OutPath"\configure.cmd")) then do
            G.OPT_SHADOW = "yes"
        end
    end

    if (G.OPT_SHADOW == "yes") then do
        if (FileExists(G.RelPath"\.qmake.cache") |,
            FileExists(G.RelPath"\src\corelib\global\qconfig.h") |,
            FileExists(G.RelPath"\src\corelib\global\qconfig.cpp")) then do
            call SayErr "You cannot make a shadow build from a source tree",
                        "containing a previous build."
            call SayErr "Cannot proceed."
            call Done 1
        end
        call SayVerbose "Performing shadow build..."
    end

    /* skip this if the user just needs help... */
    if (G.OPT_HELP \== "yes") then do

        /* detect build style */
        if (G.CFG_DEBUG == "auto") then do
            if (G.CFG_DEBUG_RELEASE \== "auto") then do
                G.CFG_DEBUG = "no"
            end
            else do
                if (G.OfficialBuild) then do
                    G.CFG_DEBUG_RELEASE = "no"
                    G.CFG_DEBUG = "no"
                end
                else if (G.CFG_DEV == "yes") then do
                    G.CFG_DEBUG_RELEASE = "no"
                    G.CFG_DEBUG = "yes"
                end
                else if (G.OPT_SHADOW == "yes") then do
                    G.CFG_DEBUG_RELEASE = "no"
                    G.CFG_DEBUG = "no"
                end
                else do
                    G.CFG_DEBUG_RELEASE = "yes"
                    G.CFG_DEBUG = "yes"
                end
            end
        end
        else do
            if (G.CFG_DEBUG_RELEASE == "auto") then do
                G.CFG_DEBUG_RELEASE = "no"
            end
        end

        if (G.CFG_DEBUG_RELEASE == "yes") then
            G.QMAKE_CONFIG = Join(G.QMAKE_CONFIG, "build_all")

        if (G.CFG_SILENT == "yes") then
            G.QMAKE_CONFIG = Join(G.QMAKE_CONFIG, "silent")

        /* if the source tree is different from the build tree,
         * symlink or copy part of the sources */
        if (G.OPT_SHADOW == "yes") then do
            call SayVerbose "Preparing build tree..."

            if (G.PERL == "") then do
                call SayErr "You need perl in your PATH to make a shadow build."
                call SayErr "Cannot proceed."
                call Done 1
            end

            if (\DirExists(G.OutPath"\bin")) then
                call MakeDir G.OutPath"\bin"

            /* make a syncqt script that can be used in the shadow */
            call DeleteFile G.OutPath"\bin\syncqt.cmd"
            call MakeDir G.OutPath"\bin"
            call charout G.OutPath"\bin\syncqt.cmd",,
                "/**/"G.EOL||,
                "parse arg aArgs"G.EOL||,
                "'@set QTDIR="G.RelPath"'"G.EOL||,
                "'@call perl -S """G.RelPath"""\bin\syncqt",
                    "-outdir """G.OutPath"""' aArgs"G.EOL||,
                "exit rc"G.EOL
            call charout G.OutPath"\bin\syncqt.cmd"

            /* create a dummy qconfig.h file so that syncqt sets a sane date for
             * the forwarder instead of 01-01-2098 (qconfig.h is created later).
             * This looks like a utime() bug in Perl for OS/2 (the null timestamp
             * should set the date to 01-01-1980, not too sane but would work).*/
            if (\FileExists(G.OutPath"\src\corelib\global\qconfig.h")) then do
                call MakeDir G.OutPath"\src\corelib\global"
                call charout G.OutPath"\src\corelib\global\qconfig.h", "/*dummy*/"
                call charout G.OutPath"\src\corelib\global\qconfig.h"
            end

            /* copy includes */
            cmd = ""
            G.SYNCQT_OPTS = ""
            if (G.CFG_DEV == "yes") then
                G.SYNCQT_OPTS = Join(G.SYNCQT_OPTS, "-check-includes")
            if (G.OPT_SHADOW == "yes") then
                cmd = 'call' G.OutPath'\bin\syncqt.cmd' G.SYNCQT_OPTS
            else if (G.CFG_DEV == "yes" | \DirExists(G.RelPath"\include") |,
                     DirExists(G.RelPath"\.git")) then
                cmd = 'call' G.RelPath'\bin\syncqt.cmd' G.SYNCQT_OPTS
            if (cmd \== '') then do
                cmd
                if (rc \= 0) then do
                    call SayErr "Executing """cmd""" failed with exit code "rc"."
                    call SayErr "Cannot proceed."
                    call Done 1
                end
            end

            /* copy parts of the mkspecs directory */
            call DeleteDir G.OutPath"\mkspecs"
            call SysFileTree G.RelPath"\mkspecs\os2-*", 'found', 'DO'
            do i = 1 to found.0
                call CopyDir found.i, G.OutPath"\mkspecs\"filespec('N', found.i)
            end
            call CopyDir G.RelPath"\mkspecs\features", G.OutPath"\mkspecs\features"
            call CopyDir G.RelPath"\mkspecs\modules", G.OutPath"\mkspecs\modules"
            call DeleteDir G.OutPath"\mkspecs\default"
        end

        /* sometimes syncqt is not called but we still need qconfig.h in the
         * include dir */
        list.1 = G.OutPath"\include\QtCore\qconfig.h"
        list.2 = G.OutPath"\include\Qt\qconfig.h"
        list.0 = 2
        do i = 1 to list.0
            if (\FileExists(list.i)) then do
                call charout list.i, '#include "../../src/corelib/global/qconfig.h"'G.EOL
                call charout list.i
            end
        end

        if (G.OPT_FAST == "auto") then do
           if (G.CFG_DEV == "yes") then G.OPT_FAST = "yes"
           else G.OPT_FAST = "no"
        end

        /* find the make command */
        G.MAKE = GetEnv("MAKE")
        if (G.MAKE == "") then do
            G.MAKE = SysSearchPath('PATH', 'make.exe')
            if (G.MAKE == "") then do
                call SayErr "You don't seem to have 'make.exe' in your PATH."
                call SayErr "Cannot proceed."
                call Done 1
            end
        end

        G.MAKE_JOBS = GetEnv("MAKE_JOBS")
        if (G.MAKE_JOBS \= "") then do
            G.MAKE_JOBS = "-j"G.MAKE_JOBS
        end
    end

    /* # auto-detect all that hasn't been specified in the arguments
    --------------------------------------------------------------------------*/

    /* so far, we default to GCC on OS/2 */
    if (G.PLATFORM == "") then G.PLATFORM = "os2-g++"
    if (DirExists(G.PLATFORM)) then G.QMAKESPEC = G.PLATFORM
    else G.QMAKESPEC = G.RelPath"\mkspecs\"G.PLATFORM

    call CheckCompiler

    /* Cross-builds are not supported */
    G.XPLATFORM = G.PLATFORM
    G.XQMAKESPEC = G.QMAKESPEC
    G.QT_CROSS_COMPILE = "no"

    /* check specified platforms are supported */
    if (\DirExists(G.XQMAKESPEC)) then do
        call SayErr
        call SayErr "   The specified system/compiler is not supported:"
        call SayErr
        call SayErr "      "G.XQMAKESPEC
        call SayErr
        call SayErr "   Please see the README file for a complete list."
        call SayErr
        call Done 2
    end
    if (\FileExists(G.XQMAKESPEC"\qplatformdefs.h")) then do
        call SayErr
        call SayErr "   The specified system/compiler port is not complete:"
        call SayErr
        call SayErr "      "G.XQMAKESPEC"\qplatformdefs.h"
        call SayErr
        call SayErr "   Please see the README file for a complete list."
        call SayErr
        call Done 2
    end

    call SayVerbose "Determining system architecture..."

    if (G.CFG_HOST_ARCH == "") then G.CFG_HOST_ARCH = "os2"

    /* Cross-builds are not supported */
    G.CFG_ARCH = G.CFG_HOST_ARCH

    if (DirExists(G.RelPath"\src\corelib\arch\"G.CFG_ARCH)) then
        call SayVerbose "    '"G.CFG_ARCH"' is supported"
    else do
        call SayVerbose "    '"G.CFG_ARCH"' is unsupported, using 'generic'"
        G.CFG_ARCH = "generic"
    end

    call SayVerbose "System architecture: '"G.CFG_ARCH"'"G.EOL

    /*--------------------------------------------------------------------------
     tests that don't need qmake (must be run before displaying help)
    --------------------------------------------------------------------------*/

    /* setup the build parts */
    if (G.CFG_BUILD_PARTS == "") then
        G.CFG_BUILD_PARTS = G.QT_DEFAULT_BUILD_PARTS
    do i = 1 to words(G.CFG_NOBUILD_PARTS)
        j = wordpos(word(G.CFG_NOBUILD_PARTS, i), G.CFG_BUILD_PARTS)
        if (j > 0) then
            G.CFG_BUILD_PARTS = delword(G.CFG_BUILD_PARTS, j, 1)
    end
    /* libs is a required part of the build */
    if (wordpos("libs", G.CFG_BUILD_PARTS) == 0) then
        G.CFG_BUILD_PARTS = Join(G.CFG_BUILD_PARTS, "libs")

    /* auto-detect precompiled header support */
    if (G.CFG_PRECOMPILE == "auto") then do
        /* @todo enable the below code once PCH actually work in GCC 4.4.2 */
/*
        if (G.Compiler == "g++") then do
            if (G.CompilerVersion >= 030400) then
                G.CFG_PRECOMPILE = "yes"
            else
                G.CFG_PRECOMPILE = "no"
        end
*/
    end

    /* detect the mapsym program */
    if (G.QMAKE_MAPSYM == "") then
        G.QMAKE_MAPSYM = SysSearchPath('PATH', 'mapsym.cmd')
    if (G.QMAKE_MAPSYM == "") then
        G.QMAKE_MAPSYM = SysSearchPath('PATH', 'mapsym.exe')
    if (G.QMAKE_MAPSYM \== "") then
        G.QMAKE_MAPSYM = "mapsym" /* try to be smart and rely on PATH */

    /* detect the exepack program */
    if (G.QMAKE_EXEPACK == "") then
        G.QMAKE_EXEPACK = SysSearchPath('PATH', 'lxlite.cmd')
    if (G.QMAKE_EXEPACK == "") then
        G.QMAKE_EXEPACK = SysSearchPath('PATH', 'lxlite.exe')
    if (G.QMAKE_EXEPACK \== "") then do
        G.QMAKE_EXEPACK = "lxlite" /* try to be smart and rely on PATH */
        G.QMAKE_EXEPACK_FLAGS = "/B- /L- /CS"
        G.QMAKE_EXEPACK_POST_FLAGS = ""
    end

    /* detect the CUPS support */
    G.CUPS_INCLUDEPATH = GetEnv('CUPS_INCLUDEPATH')
    G.CUPS_LIBS = GetEnv('CUPS_LIBS')
    if (G.CUPS_INCLUDEPATH \== '') then do
        call SayVerbose 'CUPS include path : 'G.CUPS_INCLUDEPATH
        call SayVerbose 'CUPS libraries    : 'G.CUPS_LIBS
        call SayVerbose
        G.CFG_CUPS = 'yes'
    end
    else do
        call SayVerbose 'WARNING: CUPS libraries are not specified, CUPS support is disabled.'
        call SayVerbose 'Use CUPS_INCLUDEPATH and CUPS_LIBS environment variables to specify'
        call SayVerbose 'the location of the libraries.'
        call SayVerbose
        G.CFG_CUPS = 'no'
    end
    /* so far, CUPS is the only printing system we support so disable
     * the printer classses at all when CUPS is not available */
    if (G.CFG_CUPS == 'no') then do
        call SayLog 'WARNING: Printing support is completely disabled due to',
                    'disabled CUPS support.'
        call SayLog
        G.QCONFIG_FLAGS = Join(G.QCONFIG_FLAGS, "QT_NO_PRINTER")
    end

    /* detect OpenSSL libraries */
    G.OPENSSL_INCLUDEPATH = GetEnv('OPENSSL_INCLUDEPATH')
    if (G.OPENSSL_INCLUDEPATH == '')  then do
        if (DirExists(GetEnv('UNIXROOT')'\usr\include\openssl')) then
            G.OPENSSL_INCLUDEPATH = GetEnv('UNIXROOT')'\usr\include'
    end
    G.OPENSSL_LIBS = GetEnv('OPENSSL_LIBS')
    if (G.OPENSSL_INCLUDEPATH \== '')  then do
        call SayVerbose 'OpenSSL include path : 'G.OPENSSL_INCLUDEPATH
        call SayVerbose 'OpenSSL libraries    : 'G.OPENSSL_LIBS
        call SayVerbose
        G.CFG_OPENSSL = 'yes'
    end
    else do
        call SayVerbose 'WARNING: OpenSSL libraries are not specified, OpenSSL support is disabled.'
        call SayVerbose 'Use OPENSSL_INCLUDEPATH and OPENSSL_LIBS environment variables to specify'
        call SayVerbose 'the location of the libraries.'
        call SayVerbose
        G.CFG_OPENSSL = 'no'
    end

    /*--------------------------------------------------------------------------
     apply OS/2-specific limitations to the configuration
    --------------------------------------------------------------------------*/

    /* This is the place to force the current limitations of Qt for OS/2
     * (i.e. features that are unconditionally unavailable at the moment) */

    G.QCONFIG_FLAGS = Join(G.QCONFIG_FLAGS,,
        "QT_NO_TABLET QT_NO_SYSTEMSEMAPHORE QT_NO_SHAREDMEMORY",
        "QT_NO_IM QT_NO_LPR")

    G.CFG_PHONON = "no"
    G.CFG_MULTIMEDIA = "no"
    G.CFG_IPV6 = "no"
    G.CFG_NIS = "no"
    G.CFG_DBUS = "no"
    G.CFG_QGTKSTYLE = "no"
    G.CFG_LARGEFILE = "no"
    G.CFG_PRECOMPILE = "no" /* GCC 4.4.2 crashes with PCH */

    /*--------------------------------------------------------------------------
     post process QT_INSTALL_* variables
    --------------------------------------------------------------------------*/

    /* On OS/2 the hard-coded prefix path is given relative to the directory
     * where the module containing QLibraryInfo resides. This will be either a
     * directory of QtCore4.dll (normally) or a directory of the .EXE file (if
     * Qt is a static library). Therefore, setting QT_INSTALL_PREFIX to ".."
     * will make all other paths relative to the parent of that directory which
     * in development builds contains "bin", "include", "plugins", etc. In the
     * official release builds, these paths are hard-coded to the actually
     * used system directories. In portable builds these hard-coded paths are
     * overriden with qt.conf */

    /* prefix */
    if (G.QT_INSTALL_PREFIX == "") then
        G.QT_INSTALL_PREFIX = ".."
    /* docs */
    if (G.QT_INSTALL_DOCS == "") then
        G.QT_INSTALL_DOCS = "doc"
    /* headers */
    if (G.QT_INSTALL_HEADERS == "") then
        G.QT_INSTALL_HEADERS = "include"
    /* libs */
    if (G.QT_INSTALL_LIBS == "") then
        G.QT_INSTALL_LIBS = "lib"
    /* bins */
    if (G.QT_INSTALL_BINS == "") then
        G.QT_INSTALL_BINS = "bin"
    /* plugins */
    if (G.QT_INSTALL_PLUGINS == "") then
        G.QT_INSTALL_PLUGINS = "plugins"
    /* imports */
    if (G.QT_INSTALL_IMPORTS == "") then
        G.QT_INSTALL_IMPORTS = "imports"
    /* data */
    if (G.QT_INSTALL_DATA == "") then
        G.QT_INSTALL_DATA = "."
    /* translations */
    if (G.QT_INSTALL_TRANSLATIONS == "") then
        G.QT_INSTALL_TRANSLATIONS = "translations"
    /* settings */
    if (G.QT_INSTALL_SETTINGS == "") then
        G.QT_INSTALL_SETTINGS = "$(ETC)\xdg"
    /* examples */
    if (G.QT_INSTALL_EXAMPLES == "") then
        G.QT_INSTALL_EXAMPLES = "examples"
    /* demos */
    if (G.QT_INSTALL_DEMOS == "") then
        G.QT_INSTALL_DEMOS = "demos"
    /* sysconffile is left empty */

    /*--------------------------------------------------------------------------
     help - interactive parts of the script _after_ this section please
    --------------------------------------------------------------------------*/

    /* next, emit a usage message if requested. */
    if (G.OPT_HELP == "yes") then do
        signal ShowHelp
    end

    /* -------------------------------------------------------------------------
     LICENSING, INTERACTIVE PART
    --------------------------------------------------------------------------*/

    call SayLog
    call SayLog "This is the Qt for OS/2 "G.EditionString" Edition."
    call SayLog

    if (G.Edition == "OpenSource") then
        do while 1
            call SaySay "You are licensed to use this software under the terms of"
            call SaySay "the GNU General Public License (GPL) versions 3."
            call SaySay "You are also licensed to use this software under the terms of"
            call SaySay "the Lesser GNU General Public License (LGPL) versions 2.1."
            call SaySay
            if (G.OPT_CONFIRM_LICENSE == "yes") then do
    	        call SaySay "You have already accepted the terms of the license."
                acceptance = "yes"
            end
            else do
        	    call SaySay "Type '3' to view the GNU General Public License version 3."
                call SaySay "Type 'L' to view the Lesser GNU General Public License version 2.1."
                call SaySay "Type 'yes' to accept this license offer."
                call SaySay "Type 'no' to decline this license offer."
                call SaySay
                call SaySay "Do you accept the terms of either license?"
                acceptance = InputLine()
            end
            if (acceptance == "yes") then
                leave
            else if (acceptance == "no") then do
                call SaySay "You are not licensed to use this software."
                call Done 1
            end
            else if (acceptance == "3") then
                address "cmd" "type" G.RelPath"\LICENSE.GPL3 | more"
            else if (acceptance == "L") then
                address "cmd" "type" G.RelPath"\LICENSE.LGPL | more"
            call SaySay
        end
    else
        signal Nonsense

    /*--------------------------------------------------------------------------
     generate qconfig.cpp
    --------------------------------------------------------------------------*/

    call MakeDir G.OutPath"\src\corelib\global"

    qconfig_cpp = G.OutPath"\src\corelib\global\qconfig.cpp"

    config_cpp_str =,
'/* License Info */'G.EOL||,
'#define QT_CONFIGURE_LICENSEE "'CPPPath(MaxLen(G.Licensee,259))'"'G.EOL||,
'#define QT_CONFIGURE_LICENSED_PRODUCTS "'CPPPath(MaxLen(G.Edition,259))'"'G.EOL||,
'/* Installation Info */'G.EOL

    config_cpp_str = config_cpp_str||,
'#define QT_CONFIGURE_PREFIX_PATH "'CPPPath(G.QT_INSTALL_PREFIX)'"'G.EOL||,
'#define QT_CONFIGURE_DOCUMENTATION_PATH "'CPPPath(G.QT_INSTALL_DOCS)'"'G.EOL||,
'#define QT_CONFIGURE_HEADERS_PATH "'CPPPath(G.QT_INSTALL_HEADERS)'"'G.EOL||,
'#define QT_CONFIGURE_LIBRARIES_PATH "'CPPPath(G.QT_INSTALL_LIBS)'"'G.EOL||,
'#define QT_CONFIGURE_BINARIES_PATH "'CPPPath(G.QT_INSTALL_BINS)'"'G.EOL||,
'#define QT_CONFIGURE_PLUGINS_PATH "'CPPPath(G.QT_INSTALL_PLUGINS)'"'G.EOL||,
'#define QT_CONFIGURE_IMPORTS_PATH "'CPPPath(G.QT_INSTALL_IMPORTS)'"'G.EOL||,
'#define QT_CONFIGURE_DATA_PATH "'CPPPath(G.QT_INSTALL_DATA)'"'G.EOL||,
'#define QT_CONFIGURE_TRANSLATIONS_PATH "'CPPPath(G.QT_INSTALL_TRANSLATIONS)'"'G.EOL||,
'#define QT_CONFIGURE_SETTINGS_PATH "'CPPPath(G.QT_INSTALL_SETTINGS)'"'G.EOL||,
'#define QT_CONFIGURE_EXAMPLES_PATH "'CPPPath(G.QT_INSTALL_EXAMPLES)'"'G.EOL||,
'#define QT_CONFIGURE_DEMOS_PATH "'CPPPath(G.QT_INSTALL_DEMOS)'"'G.EOL

    today = date('S')
    today = insert('-', today, 4)
    today = insert('-', today, 7)
    config_cpp_str = config_cpp_str||,
'/* Installation date */'G.EOL||,
'static const char qt_configure_installation [12+11] = "qt_instdate='today'";'G.EOL||,
G.EOL

    /* avoid unecessary rebuilds by copying only if qconfig.cpp has changed */
    create = \FileExists(qconfig_cpp)
    if (\create) then create = CompareFileToVar(qconfig_cpp, config_cpp_str) \= 0
    if (create) then do
        call DeleteFile qconfig_cpp
        call charout qconfig_cpp, config_cpp_str
        call charout qconfig_cpp
    end

    /*--------------------------------------------------------------------------
     build qmake
    --------------------------------------------------------------------------*/

    call SayLog "Creating qmake. Please wait..."

    call MakeDir G.OutPath"\qmake"

    /* take the correct Makefile and fix it */
    MakefilePlatform = G.RelPath"\qmake\Makefile."G.PLATFORM
    Makefile = G.OutPath"\qmake\Makefile"

    Makefile_str = charin(MakefilePlatform, 1, chars(MakefilePlatform))
    if (Makefile_str == "") then do
        call SayErr "'"MakefilePlatform"' not found."
        call Done 1
    end

    Makefile_header =,
        '# AutoGenerated by configure.cmd'G.EOL||,
        'BUILD_PATH = 'QuotePath(G.OutPath)||G.EOL||,
        'SOURCE_PATH = 'QuotePath(G.RelPath)||G.EOL||,
        'QMAKESPEC = 'G.PLATFORM||G.EOL||,
        'QMAKE_OPENSOURCE_EDITION = yes'G.EOL||G.EOL

    if (G.QMAKE_MAPSYM \== "") then do
        Makefile_header = Makefile_header||,
            'QMAKE_MAPSYM = 'QuotePath(G.QMAKE_MAPSYM)||G.EOL
    end
    if (G.QMAKE_EXEPACK \== "") then do
        Makefile_header = Makefile_header||,
            'QMAKE_EXEPACK = 'QuotePath(G.QMAKE_EXEPACK)||G.EOL||,
            'QMAKE_EXEPACK_FLAGS = 'G.QMAKE_EXEPACK_FLAGS||G.EOL||,
            'QMAKE_EXEPACK_POST_FLAGS = 'G.QMAKE_EXEPACK_POST_FLAGS||G.EOL||G.EOL
    end

    Makefile_str = Makefile_header||Makefile_str

    /* avoid unecessary rebuilds by copying only if Makefile has changed */
    create = \FileExists(Makefile)
    if (\create) then create = CompareFileToVar(Makefile, Makefile_str) \= 0
    if (create) then do
        call DeleteFile Makefile
        call charout Makefile, Makefile_str
        call charout Makefile
    end

    drop Makefile_header Makefile_str

    /* mkspecs/default is used as a (gasp!) default mkspec so QMAKESPEC needn't
     * be set once configured */
    call DeleteDir G.OutPath"\mkspecs\default"
    call MakeDir G.OutPath"\mkspecs\default"
    /* create a dummy qmake.spec file that will include the original one and
     * set QMAKESPEC_ORIGINAL so that qmake & make know where to find stuff */
    call charout G.OutPath"\mkspecs\default\qmake.conf",,
        "include("""||CPPPath("..\")||G.PLATFORM"\\qmake.conf"")"||G.EOL||G.EOL||,
        "QMAKESPEC_ORIGINAL="||CPPPath("..\")||G.PLATFORM||G.EOL
    call charout G.OutPath"\mkspecs\default\qmake.conf"

    /* create temporary qconfig.h for compiling qmake, if it doesn't exist
     * when building qmake, we use #defines for the install paths,
     * however they are real functions in the library */

    /* use an "at exit" slot to undo temporary file rename operations since we
     * can fail in the middle */
    exs = AddAtExitSlot()

    qconfig_h = G.OutPath"\src\corelib\global\qconfig.h"
    qmake_qconfig_h = qconfig_h".qmake"
    if (FileExists(qconfig_h)) then do
        call MoveFile qconfig_h, qconfig_h".old"
        /* record undo Move operation */
        call PutToAtExitSlot exs, 'call MoveFile "'qconfig_h'.old", "'qconfig_h'"'
    end

    if (\FileExists(qmake_qconfig_h)) then do
        call charout qmake_qconfig_h, '/* All features enabled while building qmake */'G.EOL
        call charout qmake_qconfig_h
    end

    call MoveFile qmake_qconfig_h, qconfig_h
    /* record undo Move operation */
    call PutToAtExitSlot exs, 'call MoveFile "'qconfig_h'", "'qmake_qconfig_h'"'

    curdir = directory()
    call directory G.OutPath"\qmake"

    address "cmd" G.MAKE G.MAKE_JOBS
    make_rc = rc

    call directory curdir

    /* put back original qconfig.h */
    call RunAtExitSlot exs

    /* exit on failure */
    if (make_rc \= 0)  then
        call Done 2

    call SayLog

    /*--------------------------------------------------------------------------
     tests that need qmake
    --------------------------------------------------------------------------*/

    /* the current "tests" don't actually need qmake, we just keep the
     * structure similar to ./configure */

    /* For many things we simply apply "yes" to "auto" options so far */

    if (G.CFG_GUI == "auto")    then G.CFG_GUI = "yes"

    if (G.CFG_ZLIB == "auto")   then G.CFG_ZLIB = "yes"

    if (G.CFG_GRAPHICS_SYSTEM == "default") then G.CFG_GRAPHICS_SYSTEM = "raster"

    if (G.CFG_MMX == "auto")    then G.CFG_MMX = "yes"
    if (G.CFG_3DNOW == "auto")  then G.CFG_3DNOW = "yes"
    if (G.CFG_SSE == "auto")    then G.CFG_SSE = "yes"
    if (G.CFG_SSE2 == "auto")   then G.CFG_SSE2 = "yes"

    if (G.CFG_ACCESSIBILITY == "auto")  then G.CFG_ACCESSIBILITY = "yes"
    if (G.CFG_SM == "auto")             then G.CFG_SM = "yes"
    if (G.CFG_STL == "auto")            then G.CFG_STL = "yes"
    if (G.CFG_CONCURRENT == "auto")     then G.CFG_CONCURRENT = "yes"
    if (G.CFG_PHONON == "auto")         then G.CFG_PHONON = "yes"
    if (G.CFG_MULTIMEDIA == "auto")     then G.CFG_MULTIMEDIA = "yes"
    if (G.CFG_QGTKSTYLE == "auto")      then G.CFG_QGTKSTYLE = "yes"
    if (G.CFG_OPENSSL == "auto")        then G.CFG_OPENSSL = "yes"
    if (G.CFG_DBUS == "auto")           then G.CFG_DBUS = "yes"

    /* detect how jpeg should be built */
    if (G.CFG_JPEG == "auto") then do
        if (G.CFG_SHARED == "yes") then
            G.CFG_JPEG = "plugin"
        else
            G.CFG_JPEG = "yes"
    end

    /* detect how gif should be built */
    if (G.CFG_GIF == "auto") then do
        if (G.CFG_SHARED == "yes") then
            G.CFG_GIF = "plugin"
        else
            G.CFG_GIF = "yes"
    end

    /* detect how tiff should be built */
    if (G.CFG_TIFF == "auto") then do
        if (G.CFG_SHARED == "yes") then
            G.CFG_TIFF = "plugin"
        else
            G.CFG_TIFF = "yes"
    end

    /* detect how mng should be built */
    if (G.CFG_MNG == "auto") then do
        if (G.CFG_SHARED == "yes") then
            G.CFG_MNG = "plugin"
        else
            G.CFG_MNG = "yes"
    end

    /* auto-detect SQL-modules support */
    do i = 1 to words(G.CFG_SQL_AVAILABLE)
        drv = word(G.CFG_SQL_AVAILABLE, i)
        select
            when (drv == "mysql") then do
                if (G.CFG_SQL_mysql \== "no") then do
                    /* detect MySQL libraries */
                    G.MYSQL_INCLUDEPATH = GetEnv('MYSQL_INCLUDEPATH')
                    G.MYSQL_LIBS = GetEnv('MYSQL_LIBS')
                    if (G.MYSQL_INCLUDEPATH \== '' & G.MYSQL_LIBS \== '')  then do
                        call SayVerbose 'MySQL include path : 'G.MYSQL_INCLUDEPATH
                        call SayVerbose 'MySQL libraries    : 'G.MYSQL_LIBS
                        call SayVerbose
                        /* qt sources for some reason include mysql headers directly, so
                         * add a prefix here to fix that */
                        G.MYSQL_INCLUDEPATH = G.MYSQL_INCLUDEPATH'/mysql'
                        if (G.CFG_SQL_mysql == "auto") then
                            G.CFG_SQL_mysql = "plugin"
                    end
                    else do
                        call SayVerbose,
'Warning: MySQL libraries are not specified, MySQL plugin is disabled.'G.EOL||,
'Use MYSQL_INCLUDEPATH and MYSQL_LIBS environment variables to specify'G.EOL||,
'the location of the libraries.'G.EOL||G.EOL
                        G.CFG_SQL_mysql = 'no'
                    end
                end
            end
            when (drv == "psql") then do
                if (G.CFG_SQL_psql \== "no") then do
                    /* detect PostgresSQL libraries */
                    G.PSQL_INCLUDEPATH = GetEnv('PSQL_INCLUDEPATH')
                    G.PSQL_LIBS = GetEnv('PSQL_LIBS')
                    if (G.PSQL_INCLUDEPATH \== '' & G.PSQL_LIBS \== '')  then do
                        call SayVerbose 'PostgresSQL include path : 'G.PSQL_INCLUDEPATH
                        call SayVerbose 'PostgresSQL libraries    : 'G.PSQL_LIBS
                        call SayVerbose
                        /* qt sources for some reason include mysql headers directly, so
                         * add a prefix here to fix that */
                        if (G.CFG_SQL_psql == "auto") then
                            G.CFG_SQL_psql = "plugin"
                    end
                    else do
                        call SayVerbose,
'Warning: PostgresSQL libraries are not specified, PostgresSQL plugin is disabled.'G.EOL||,
'Use PSQL_INCLUDEPATH and PSQL_LIBS environment variables to specify'G.EOL||,
'the location of the libraries.'G.EOL||G.EOL
                        G.CFG_SQL_psql = 'no'
                    end
                end
            end
            when (drv == "sqlite") then do
                if (G.CFG_SQL_sqlite \== "no") then do
                    if (G.CFG_SQLITE == "system") then do
                        /* @todo external sqlite3 library isn't yet supported */
                        call SayVerbose,
"Warning: Linking against the external sqlite3 library isn't yet supported,"G.EOL||,
"the sqlite SQL driver is disabled"G.EOL||G.EOL
                        G.CFG_SQL_sqlite = "no"
                    end
                    else do
                        if (FileExists(G.RelPath"\src\3rdparty\sqlite\sqlite3.h")) then do
                            if (G.CFG_SQL_sqlite == "auto") then
                                G.CFG_SQL_sqlite = "plugin"
                        end
                        else do
                            call SayVerbose,
"Warning: 3rdparty sources for the sqlite3 library are not found,"G.EOL||,
"the sqlite SQL driver is disabled"G.EOL||G.EOL
                            G.CFG_SQL_sqlite = "no"
                        end
                    end
                end
            end
            otherwise do
                /* @todo all other databases aren't yet supported */
                call value "G.CFG_SQL_"drv, "no"
            end
        end
    end

    if (G.CFG_MULTIMEDIA == "auto") then
        CFG_MULTIMEDIA = G.CFG_GUI

    if (G.CFG_MULTIMEDIA == "yes" & G.CFG_GUI == "no") then do
        call SayErr "ERROR: QtMultimedia was requested, but it can't be",
                    "built without QtGui."
        call Done 1
    end

    if (G.CFG_SVG == "auto") then
        G.CFG_SVG = G.CFG_GUI

    if (G.CFG_SVG == "yes" & G.CFG_GUI == "no") then do
        call SayErr "ERROR: QtSvg was requested, but it can't be",
                    "built without QtGui."
        call Done 1
    end

    if (G.CFG_SCRIPT == "auto") then
        G.CFG_SCRIPT = "yes"

    if (G.CFG_SCRIPTTOOLS == "yes" & G.CFG_SCRIPT == "no") then do
        call SayErr "ERROR: QtScriptTools was requested, but it can't be",
                    "built due to QtScript being disabled."
        call Done 1
    end

    if (G.CFG_SCRIPTTOOLS == "auto" & G.CFG_SCRIPT \== "no") then
        G.CFG_SCRIPTTOOLS = "yes"
    else if (G.CFG_SCRIPT == "no") then
        G.CFG_SCRIPTTOOLS = "no"

    if (G.CFG_DECLARATIVE == "yes") then do
        if (G.CFG_SCRIPT == "no" | G.CFG_GUI == "no") then do
            call SayErr "ERROR: QtDeclarative was requested, but it can't be",
                        "built due to QtScript or QtGui being disabled."
            call Done 1
        end
    end

    if (G.CFG_DECLARATIVE == "auto") then do
        if (G.CFG_SCRIPT == "no" | G.CFG_GUI == "no") then
            G.CFG_DECLARATIVE = "no"
        else
            G.CFG_DECLARATIVE = "yes"
    end

    /*--------------------------------------------------------------------------
     ask for all that hasn't been auto-detected or specified in the arguments
    --------------------------------------------------------------------------*/

    if (G.CFG_WEBKIT == "auto") then
        G.CFG_WEBKIT = G.CFG_GUI

    /*--------------------------------------------------------------------------
     process supplied options
    --------------------------------------------------------------------------*/

    if (G.CFG_QT3SUPPORT == "yes") then
        G.QT_CONFIG = Join(G.QT_CONFIG, "qt3support")

    if (G.CFG_PHONON == "yes") then do
        G.QT_CONFIG = Join(G.QT_CONFIG, "phonon")
        if (G.CFG_PHONON_BACKEND == "yes") then
            G.QT_CONFIG = Join(G.QT_CONFIG, "phonon-backend")
    end
    else
        G.QCONFIG_FLAGS = Join(G.QCONFIG_FLAGS, "QT_NO_PHONON")

    if (G.CFG_ACCESSIBILITY == "no") then
        G.QCONFIG_FLAGS = Join(G.QCONFIG_FLAGS, "QT_NO_ACCESSIBILITY")
    else
        G.QT_CONFIG = Join(G.QT_CONFIG, "accessibility")

    if (G.CFG_OPENGL == "yes") then
        G.QT_CONFIG = Join(G.QT_CONFIG, "opengl")
    else
        G.QCONFIG_FLAGS = Join(G.QCONFIG_FLAGS, "QT_NO_OPENGL")

    /* build up the variables for output */
    if (G.CFG_DEBUG == "yes") then do
        G.QMAKE_OUTDIR = G.QMAKE_OUTDIR"debug"
        G.QMAKE_CONFIG = Join(G.QMAKE_CONFIG, "debug")
    end
    else if (G.CFG_DEBUG == "no") then do
        G.QMAKE_OUTDIR = G.QMAKE_OUTDIR"release"
        G.QMAKE_CONFIG = Join(G.QMAKE_CONFIG, "release")
    end
    if (G.CFG_SHARED == "yes") then do
        G.QMAKE_OUTDIR = G.QMAKE_OUTDIR"-shared"
        G.QMAKE_CONFIG = Join(G.QMAKE_CONFIG, "shared dll")
    end
    else if (G.CFG_SHARED == "no") then do
        G.QMAKE_OUTDIR = G.QMAKE_OUTDIR"-static"
        G.QMAKE_CONFIG = Join(G.QMAKE_CONFIG, "static")
    end

    call QMakeVar "set", "PRECOMPILED_DIR", CPPPath("tmp\pch\"G.QMAKE_OUTDIR)
    call QMakeVar "set", "OBJECTS_DIR", CPPPath("tmp\obj\"G.QMAKE_OUTDIR)
    call QMakeVar "set", "MOC_DIR", CPPPath("tmp\moc\"G.QMAKE_OUTDIR)
    call QMakeVar "set", "RCC_DIR", CPPPath("tmp\rcc\"G.QMAKE_OUTDIR)
    call QMakeVar "set", "UI_DIR", CPPPath("tmp\uic\"G.QMAKE_OUTDIR)

    if (G.CFG_LARGEFILE == "yes") then
        G.QMAKE_CONFIG = Join(G.QMAKE_CONFIG, "largefile")

    if (G.CFG_STL == "no") then
        G.QCONFIG_FLAGS = Join(G.QCONFIG_FLAGS, "QT_NO_STL")
    else do
        G.QMAKE_CONFIG = Join(G.QMAKE_CONFIG, "stl")
        G.QTCONFIG_CONFIG = Join(G.QTCONFIG_CONFIG, "stl")
    end

    if (G.CFG_USE_GNUMAKE == "yes") then
        G.QMAKE_CONFIG = Join(G.QMAKE_CONFIG, "GNUmake")

    if (G.CFG_PRECOMPILE == "yes") then
        G.QTCONFIG_CONFIG = Join(G.QTCONFIG_CONFIG, "precompile_header")

    if (G.CFG_SEPARATE_DEBUG_INFO == "yes") then do
        call QMakeVar "add", "QMAKE_CFLAGS", "$$QMAKE_CFLAGS_DEBUG"
        call QMakeVar "add", "QMAKE_CXXFLAGS", "$$QMAKE_CFLAGS_DEBUG"
        G.QMAKE_CONFIG = Join(G.QMAKE_CONFIG, "separate_debug_info")
    end

    if (G.CFG_IPV6 == "yes") then
        G.QT_CONFIG = Join(G.QT_CONFIG, "ipv6")
    else
        G.CFG_IPV6 = "no"

    if (G.CFG_MMX == "yes") then
        G.QTCONFIG_CONFIG = Join(G.QTCONFIG_CONFIG, "mmx")
    if (G.CFG_3DNOW == "yes") then
        G.QTCONFIG_CONFIG = Join(G.QTCONFIG_CONFIG, "3dnow")
    if (G.CFG_SSE == "yes") then
        G.QTCONFIG_CONFIG = Join(G.QTCONFIG_CONFIG, "sse")
    if (G.CFG_SSE2 == "yes") then
        G.QTCONFIG_CONFIG = Join(G.QTCONFIG_CONFIG, "sse2")
/* @todo do we really need this?
    if (G.CFG_CLOCK_GETTIME == "yes") then
        G.QT_CONFIG = Join(G.QT_CONFIG, "clock-gettime")
    if (G.CFG_CLOCK_MONOTONIC == "yes") then
        G.QT_CONFIG = Join(G.QT_CONFIG, "clock-monotonic")
    if (G.CFG_MREMAP == "yes") then
        G.QT_CONFIG = Join(G.QT_CONFIG, "mremap")
    if (G.CFG_GETADDRINFO == "yes") then
        G.QT_CONFIG = Join(G.QT_CONFIG, "getaddrinfo")
    if (G.CFG_IPV6IFNAME == "yes") then
        G.QT_CONFIG = Join(G.QT_CONFIG, "ipv6ifname")
    if (G.CFG_GETIFADDRS == "yes") then
        G.QT_CONFIG = Join(G.QT_CONFIG, "getifaddrs")
    if (G.CFG_INOTIFY == "yes") then
        G.QT_CONFIG = Join(G.QT_CONFIG, "inotify")
*/

    if (G.CFG_LIBJPEG == "system") then
        G.QT_CONFIG = Join(G.QT_CONFIG, "system-jpeg")
    if (G.CFG_JPEG == "no") then
        G.QT_CONFIG = Join(G.QT_CONFIG, "no-jpeg")
    else if (G.CFG_JPEG == "yes") then
        G.QT_CONFIG = Join(G.QT_CONFIG, "jpeg")

    if (G.CFG_LIBMNG == "system") then
        G.QT_CONFIG = Join(G.QT_CONFIG, "system-mng")
    if (G.CFG_MNG == "no") then
        G.QT_CONFIG = Join(G.QT_CONFIG, "no-mng")
    else if (G.CFG_MNG == "yes") then
        G.QT_CONFIG = Join(G.QT_CONFIG, "mng")

    if (G.CFG_LIBPNG == "no") then
        CFG_PNG="no"
    if (G.CFG_LIBPNG == "system") then
        G.QT_CONFIG = Join(G.QT_CONFIG, "system-png")
    if (G.CFG_PNG == "no") then
        G.QT_CONFIG = Join(G.QT_CONFIG, "no-png")
    else if (G.CFG_PNG == "yes") then
        G.QT_CONFIG = Join(G.QT_CONFIG, "png")

    if (G.CFG_GIF == "no") then
        G.QT_CONFIG = Join(G.QT_CONFIG, "no-gif")
    else if (G.CFG_GIF == "yes") then
        G.QT_CONFIG = Join(G.QT_CONFIG, "gif")

    if (G.CFG_LIBTIFF == "system") then
        G.QT_CONFIG = Join(G.QT_CONFIG, "system-tiff")
    if (G.CFG_TIFF == "no") then
        G.QT_CONFIG = Join(G.QT_CONFIG, "no-tiff")
    else if (G.CFG_TIFF == "yes") then
        G.QT_CONFIG = Join(G.QT_CONFIG, "tiff")

    if (G.CFG_FONTCONFIG == "yes") then
        G.QT_CONFIG = Join(G.QT_CONFIG, "fontconfig")

    if (G.CFG_LIBFREETYPE == "no") then
        G.QT_CONFIG = Join(G.QT_CONFIG, "no-freetype")
    else if (G.CFG_LIBFREETYPE == "system") then
        G.QT_CONFIG = Join(G.QT_CONFIG, "system-freetype")
    else
        G.QT_CONFIG = Join(G.QT_CONFIG, "freetype")

    if (G.CFG_GUI == "no") then do
        QT_CONFIG = Join(G.QT_CONFIG, "no-gui")
        G.QCONFIG_FLAGS = Join(G.QCONFIG_FLAGS, "QT_NO_GUI")
    end

    if (G.CFG_ZLIB == "yes") then
        G.QT_CONFIG = Join(G.QT_CONFIG, "zlib")
    else if (G.CFG_ZLIB == "system") then
        G.QT_CONFIG = Join(G.QT_CONFIG, "system-zlib")

    if (G.CFG_CUPS == "yes") then
        G.QT_CONFIG = Join(G.QT_CONFIG, "cups")
    if (G.CFG_NIS == "yes") then
        G.QT_CONFIG = Join(G.QT_CONFIG, "nis")

/* @todo do we really need this?
    if (G.CFG_ICONV == "yes") then
        G.QT_CONFIG = Join(G.QT_CONFIG, "iconv")
    if (G.CFG_ICONV == "gnu") then
        G.QT_CONFIG = Join(G.QT_CONFIG, "gnu-libiconv")
    if (G.CFG_GLIB == "yes") then
        G.QT_CONFIG = Join(G.QT_CONFIG, "glib")
    if (G.CFG_GSTREAMER == "yes") then
        G.QT_CONFIG = Join(G.QT_CONFIG, "gstreamer")
    if (G.CFG_NAS == "system") then
        G.QT_CONFIG = Join(G.QT_CONFIG, "nas")
*/
    if (G.CFG_OPENSSL == "yes") then
        G.QT_CONFIG = Join(G.QT_CONFIG, "openssl")
    if (G.CFG_OPENSSL == "linked") then
        G.QT_CONFIG = Join(G.QT_CONFIG, "openssl-linked")
    if (G.CFG_DBUS == "yes") then
        G.QT_CONFIG = Join(G.QT_CONFIG, "dbus")
    if (G.CFG_DBUS == "linked") then
        G.QT_CONFIG = Join(G.QT_CONFIG, "dbus dbus-linked")

    if (G.D_FLAGS \== "") then
        call QMakeVar "add", "DEFINES", G.D_FLAGS
    if (G.L_FLAGS \== "") then
        call QMakeVar "add", "QMAKE_LIBDIR_FLAGS", G.L_FLAGS
    if (G.l_FLAGS \== "") then
        call QMakeVar "add", "LIBS", G.l_FLAGS
    if (G.I_FLAGS \== "") then do
        call QMakeVar "add", "QMAKE_CFLAGS", G.I_FLAGS
        call QMakeVar "add", "QMAKE_CXXFLAGS", G.I_FLAGS
    end

    if (G.CFG_EXCEPTIONS \== "no") then
        G.QTCONFIG_CONFIG = Join(G.QTCONFIG_CONFIG, "exceptions")
    else
        G.CFG_EXCEPTIONS = "no"

    if (G.CFG_CONCURRENT == "no") then
        G.QCONFIG_FLAGS = Join(G.QCONFIG_FLAGS, "QT_NO_CONCURRENT")

    if (G.CFG_XMLPATTERNS == "yes" & G.CFG_EXCEPTIONS == "no") then do
        call SayErr "QtXmlPatterns was requested, but it can't",
                    "be built due to exceptions being disabled."
        call Done 1
    end
    if (G.CFG_XMLPATTERNS == "auto" & G.CFG_EXCEPTIONS \== "no") then
        G.CFG_XMLPATTERNS = "yes"
    else if (G.CFG_EXCEPTIONS == "no") then
        G.CFG_XMLPATTERNS = "no"
    if (G.CFG_XMLPATTERNS == "yes") then
        G.QT_CONFIG = Join(G.QT_CONFIG, "xmlpatterns")
    else
        G.QCONFIG_FLAGS = Join(G.QCONFIG_FLAGS, "QT_NO_XMLPATTERNS")

    if (G.CFG_MULTIMEDIA == "no") then
        G.QCONFIG_FLAGS = Join(G.QCONFIG_FLAGS, "QT_NO_MULTIMEDIA")
    else
        G.QT_CONFIG = Join(G.QT_CONFIG, "multimedia")

    if (G.CFG_AUDIO_BACKEND == "yes") then
        G.QT_CONFIG = Join(G.QT_CONFIG, "audio-backend")

    if (G.CFG_SVG == "yes") then
        G.QT_CONFIG = Join(G.QT_CONFIG, "svg")
    else
        G.QCONFIG_FLAGS = Join(G.QCONFIG_FLAGS, "QT_NO_SVG")

    if (G.CFG_WEBKIT == "yes") then do
        /* This include takes care of adding "webkit" to QT_CONFIG. */
        call CopyFile G.RelPath"\src\3rdparty\webkit\WebKit\qt\qt_webkit_version.pri",,
                      G.OutPath"\mkspecs\modules\qt_webkit_version.pri"
        if (G.CFG_WEBKIT == "debug") then
            G.QMAKE_CONFIG = Join(G.QMAKE_CONFIG, "webkit-debug")
        if (G.CFG_JAVASCRIPTCORE_JIT == "yes") then
            call QMakeVar "set", "JAVASCRIPTCORE_JIT", "yes"
        else if (G.CFG_JAVASCRIPTCORE_JIT == "no") then
            call QMakeVar "set", "JAVASCRIPTCORE_JIT", "no"
    end
    else do
        call DeleteFile G.OutPath"\mkspecs\modules\qt_webkit_version.pri"
        G.QCONFIG_FLAGS = Join(G.QCONFIG_FLAGS, "QT_NO_WEBKIT")
    end

    if (G.CFG_SCRIPT == "yes") then
        G.QT_CONFIG = Join(G.QT_CONFIG, "script")
    else
        G.QCONFIG_FLAGS = Join(G.QCONFIG_FLAGS, "QT_NO_SCRIPT")

    if (G.CFG_SCRIPTTOOLS == "yes") then
        G.QT_CONFIG = Join(G.QT_CONFIG, "scripttools")
    else
        G.QCONFIG_FLAGS = Join(G.QCONFIG_FLAGS, "QT_NO_SCRIPTTOOLS")

    if (G.CFG_DECLARATIVE == "yes") then do
        G.QT_CONFIG = Join(G.QT_CONFIG, "declarative")
        if (G.CFG_DECLARATIVE_DEBUG == "no") then do
            G.QCONFIG_FLAGS = Join(G.QCONFIG_FLAGS,,
                                   "QDECLARATIVE_NO_DEBUG_PROTOCOL")
        end
    end
    else
        G.QCONFIG_FLAGS = Join(G.QCONFIG_FLAGS, "QT_NO_DECLARATIVE")

    if (G.CFG_EXCEPTIONS == "no") then do
    	call QMakeVar "add", "QMAKE_CFLAGS", "$$QMAKE_CFLAGS_EXCEPTIONS_OFF"
    	call QMakeVar "add", "QMAKE_CXXFLAGS", "$$QMAKE_CXXFLAGS_EXCEPTIONS_OFF"
    	call QMakeVar "add", "QMAKE_LFLAGS", "$$QMAKE_LFLAGS_EXCEPTIONS_OFF"
        G.QMAKE_CONFIG = Join(G.QMAKE_CONFIG, "exceptions_off")
    end

    /*--------------------------------------------------------------------------
     generate QT_BUILD_KEY
    --------------------------------------------------------------------------*/

    /*
     * QT_CONFIG can contain the following:
     *
     * Things that affect the Qt API/ABI:
     *
     *   Options:
     *     minimal-config small-config medium-config large-config full-config
     *
     *   Different edition modules:
     *     network canvas table xml opengl sql
     *
     * Things that do not affect the Qt API/ABI:
     *     stl
     *     system-jpeg no-jpeg jpeg
     *     system-mng no-mng mng
     *     system-png no-png png
     *     system-zlib no-zlib zlib
     *     system-libtiff no-libtiff
     *     no-gif gif
     *     debug release
     *     dll staticlib
     *
     *     nocrosscompiler
     *     GNUmake
     *     largefile
     *     nis
     *     nas
     *     tablet
     *     ipv6
     *
     *     X11     : x11sm xinerama xcursor xfixes xrandr xrender mitshm fontconfig xkb
     *     Embedded: embedded freetype
     */

    ALL_OPTIONS = ""
    BUILD_CONFIG = ""
    BUILD_OPTIONS = ""

    /* determine the build options */
    opts = G.QMAKE_CONFIG G.QT_CONFIG
    do i = 1 to words(opts)
        opt = word(opts, i)
        skip = "yes"
        if (EndsWith(opt, "-config")) then do
            /* take the last *-config setting.  this is the highest config being used,
             * and is the one that we will use for tagging plugins */
            BUILD_CONFIG = opt
        end
        else if (opt == "debug" | opt == "release") then do
            /* the debug build is binary incompatible with the release build,
             * but a separate build key is maintained for each, so skip it now */
             nop
        end
        /* skip all other options since they don't affect the Qt API/ABI. */

        if (skip == "no") then do
            /* only leave those options in BUILD_OPTIONS that are missing from
             * ALL_OPTIONS and add the "no-" prefix to them */
            if (wordpos(opt, ALL_OPTIONS) == 0) then do
                opt = "no-"opt
                /* also, maintan sort order and unicity */
                if (BUILD_OPTIONS == "") then
                    BUILD_OPTIONS = opt
                else
                do j = 1 to words(BUILD_OPTIONS)
                    w = word(BUILD_OPTIONS, j)
                    c = compare(opt, w)
                    if (c == 0) then leave
                    if (substr(opt, c, 1) < substr(w, c, 1)) then do
                        BUILD_OPTIONS = insert(opt" ", BUILD_OPTIONS, wordindex(BUILD_OPTIONS, j) - 1)
                        leave
                    end
                    if (j == words(BUILD_OPTIONS)) then
                        BUILD_OPTIONS = Join(BUILD_OPTIONS, opt)
                end
            end
        end
    end

    BUILD_OPTIONS = BUILD_CONFIG BUILD_OPTIONS

    /* some compilers generate binary incompatible code between different versions,
     * so we need to generate a build key that is different between these compilers */
    COMPILER = G.Compiler
    if (COMPILER == 'g++') then do
        if (StartsWith(G.CompilerVersionString, "3.")) then
            COMPILER = COMPILER'-3'
        else if (StartsWith(G.CompilerVersionString, "4.")) then
            COMPILER = COMPILER'-4'
        else
            COMPILER = COMPILER'-'G.CompilerVersionString
    end
    else do
        COMPILER = COMPILER'-'G.CompilerVersionString
    end

    QT_BUILD_KEY_PRE = G.CFG_USER_BUILD_KEY G.CFG_ARCH COMPILER
    QT_BUILD_KEY_POST = BUILD_OPTIONS
    if (G.QT_NAMESPACE \== "") then
        QT_BUILD_KEY_POST = Join(QT_BUILD_KEY_POST, G.QT_NAMESPACE)

    /*--------------------------------------------------------------------------
     part of configuration information goes into qconfig.h
    --------------------------------------------------------------------------*/

    qconfig_h = G.OutPath"\src\corelib\global\qconfig.h"
    qconfig_h_new = qconfig_h".new"

    call DeleteFile qconfig_h_new

    if (G.CFG_QCONFIG == "full") then do
        call lineout qconfig_h_new, "/* Everything */"G.EOL
    end
    else do
        config_h_config = G.OutPath"\src\corelib\global\qconfig-"G.CFG_QCONFIG".h"
        config_h_str = charin(config_h_config, 1, chars(config_h_config))
        call charout qconfig_h_new, config_h_str||G.EOL
    end

    call charout qconfig_h_new,,
'#ifndef QT_DLL'G.EOL||,
'#  define QT_DLL'G.EOL||,
'#endif'G.EOL||,
''G.EOL||,
'/* License information */'G.EOL||,
'#define QT_PRODUCT_LICENSEE "'G.Licensee'"'G.EOL||,
'#define QT_PRODUCT_LICENSE "'G.Edition'"'G.EOL||,
''G.EOL||,
'/* Qt Edition */'G.EOL||,
'#ifndef QT_EDITION'G.EOL||,
'#  define QT_EDITION' G.QT_EDITION||G.EOL||,
'#endif'G.EOL||,
''G.EOL||,
'/* Machine byte-order */'G.EOL||,
'#define Q_BIG_ENDIAN 4321'G.EOL||,
'#define Q_LITTLE_ENDIAN 1234'G.EOL

    /* little endian on OS/2 by default */
    if (G.CFG_ENDIAN == "auto") then
        call charout qconfig_h_new, '#define Q_BYTE_ORDER Q_LITTLE_ENDIAN'G.EOL
    else
        call charout qconfig_h_new, '#define Q_BYTE_ORDER 'G.CFG_ENDIAN||G.EOL
    call charout qconfig_h_new, G.EOL

    call charout qconfig_h_new,,
'#define QT_ARCH_'translate(G.CFG_ARCH)||G.EOL,
''G.EOL||,
'/* Compile time features */'||G.EOL||G.EOL

    if (G.CFG_LARGEFILE == "yes") then
        call charout qconfig_h_new, '#define QT_LARGEFILE_SUPPORT 64'G.EOL||G.EOL

    if (G.CFG_DEV == "yes") then
        call charout qconfig_h_new, '#define QT_BUILD_INTERNAL'G.EOL||G.EOL

    /* Add turned on SQL drivers */
    SQL_DRIVERS = ""
    SQL_PLUGINS = ""
    do i = 1 to words(G.CFG_SQL_AVAILABLE)
        drv = word(G.CFG_SQL_AVAILABLE, i)
        val = value("G.CFG_SQL_"drv)
        if (val == "qt") then do
            G.QCONFIG_FLAGS = Join(G.QCONFIG_FLAGS, "QT_SQL_"translate(drv))
            SQL_DRIVERS = Join(SQL_DRIVERS, drv)
        end
        else if (val == "plugin") then
            SQL_PLUGINS = Join(SQL_PLUGINS, drv)
    end

    if (SQL_DRIVERS \== "") then
        call QMakeVar "set", "sql-drivers", SQL_DRIVERS
    if (SQL_PLUGINS \== "") then
        call QMakeVar "set", "sql-plugins", SQL_PLUGINS

    /* Add other configuration options to the qconfig.h file */
    if (G.CFG_GIF == "yes") then
        G.QCONFIG_FLAGS = Join(G.QCONFIG_FLAGS, "QT_BUILTIN_GIF_READER=1")
    if (G.CFG_TIFF \== "yes") then
        G.QCONFIG_FLAGS = Join(G.QCONFIG_FLAGS, "QT_NO_IMAGEFORMAT_TIFF")
    if (G.CFG_PNG \== "yes") then
        G.QCONFIG_FLAGS = Join(G.QCONFIG_FLAGS, "QT_NO_IMAGEFORMAT_PNG")
    if (G.CFG_JPEG \== "yes") then
        G.QCONFIG_FLAGS = Join(G.QCONFIG_FLAGS, "QT_NO_IMAGEFORMAT_JPEG")
    if (G.CFG_MNG \== "yes") then
        G.QCONFIG_FLAGS = Join(G.QCONFIG_FLAGS, "QT_NO_IMAGEFORMAT_MNG")
    if (G.CFG_ZLIB \== "yes") then
        G.QCONFIG_FLAGS = Join(G.QCONFIG_FLAGS, "QT_NO_ZLIB")
    if (G.CFG_EXCEPTIONS == "no") then
        G.QCONFIG_FLAGS = Join(G.QCONFIG_FLAGS, "QT_NO_EXCEPTIONS")
    if (G.CFG_IPV6 \== "yes") then
        G.QCONFIG_FLAGS = Join(G.QCONFIG_FLAGS, "QT_NO_IPV6")
    if (G.CFG_QGTKSTYLE \== "yes") then
        G.QCONFIG_FLAGS = Join(G.QCONFIG_FLAGS, "QT_NO_STYLE_GTK")
    if (G.CFG_OPENSSL \== "yes") then
        G.QCONFIG_FLAGS = Join(G.QCONFIG_FLAGS, "QT_NO_OPENSSL")
    if (G.CFG_OPENSSL == "linked") then
        G.QCONFIG_FLAGS = Join(G.QCONFIG_FLAGS, "QT_LINKED_OPENSSL")
    if (G.CFG_DBUS \== "yes") then
        G.QCONFIG_FLAGS = Join(G.QCONFIG_FLAGS, "QT_NO_DBUS")

    if (G.CFG_GRAPHICS_SYSTEM == "raster") then
        G.QCONFIG_FLAGS = Join(G.QCONFIG_FLAGS, "QT_GRAPHICSSYSTEM_RASTER")
    if (G.CFG_GRAPHICS_SYSTEM == "opengl") then
        G.QCONFIG_FLAGS = Join(G.QCONFIG_FLAGS, "QT_GRAPHICSSYSTEM_OPENGL")

    if (G.CFG_CUPS == "no") then
        G.QCONFIG_FLAGS = Join(G.QCONFIG_FLAGS, "QT_NO_CUPS")
    if (G.CFG_NIS == "no") then
        G.QCONFIG_FLAGS = Join(G.QCONFIG_FLAGS, "QT_NO_NIS")

    /* X11/Unix/Mac only configs */
/* @todo detect what's actually relevant
    if (G.CFG_ICONV == "no") then
        G.QCONFIG_FLAGS = Join(G.QCONFIG_FLAGS, "QT_NO_ICONV")
    if (G.CFG_GLIB \== "yes") then
        G.QCONFIG_FLAGS = Join(G.QCONFIG_FLAGS, "QT_NO_GLIB")
    if (G.CFG_GSTREAMER \== "yes") then
        G.QCONFIG_FLAGS = Join(G.QCONFIG_FLAGS, "QT_NO_GSTREAMER")
    if (G.CFG_CLOCK_MONOTONIC == "no") then
        G.QCONFIG_FLAGS = Join(G.QCONFIG_FLAGS, "QT_NO_CLOCK_MONOTONIC")
    if (G.CFG_MREMAP == "no") then
        G.QCONFIG_FLAGS = Join(G.QCONFIG_FLAGS, "QT_NO_MREMAP")
    if (G.CFG_GETADDRINFO == "no") then
        G.QCONFIG_FLAGS = Join(G.QCONFIG_FLAGS, "QT_NO_GETADDRINFO")
    if (G.CFG_IPV6IFNAME == "no") then
        G.QCONFIG_FLAGS = Join(G.QCONFIG_FLAGS, "QT_NO_IPV6IFNAME")
    if (G.CFG_GETIFADDRS == "no") then
        G.QCONFIG_FLAGS = Join(G.QCONFIG_FLAGS, "QT_NO_GETIFADDRS")
    if (G.CFG_INOTIFY == "no") then
        G.QCONFIG_FLAGS = Join(G.QCONFIG_FLAGS, "QT_NO_INOTIFY")
    if (G.CFG_NAS == "no") then
        G.QCONFIG_FLAGS = Join(G.QCONFIG_FLAGS, "QT_NO_NAS")
*/

    if (G.CFG_SM == "no") then
        G.QCONFIG_FLAGS = Join(G.QCONFIG_FLAGS, "QT_NO_SESSIONMANAGER")

    /* do primitive sorting (the list is not so big so it should be fine) */
    sorted = ""
    do i = 1 to words(G.QCONFIG_FLAGS)
        w = word(G.QCONFIG_FLAGS, i)
        if (sorted == "") then
            sorted = w
        else
        do j = 1 to words(sorted)
            w2 = word(sorted, j)
            c = compare(w, w2)
            if (c == 0) then leave
            if (substr(w, c, 1) < substr(w2, c, 1)) then do
                sorted = insert(w" ", sorted, wordindex(sorted, j) - 1)
                leave
            end
            if (j == words(sorted)) then
                sorted = sorted" "w
        end
    end
    G.QCONFIG_FLAGS = sorted

    if (G.QCONFIG_FLAGS \== "") then do
        do i = 1 to words(G.QCONFIG_FLAGS)
            cfg = word(G.QCONFIG_FLAGS, i)
            parse value cfg with cfgd'='cfgv /* trim pushed 'Foo=Bar' defines */
            cfg = strip(cfgd' 'cfgv)         /* turn first '=' into a space */
            /* figure out define logic, so we can output the correct
             * ifdefs to override the global defines in a project */
            cfgdNeg = ""
            if (StartsWith(cfgd, "QT_NO_")) then
                /* QT_NO_option can be forcefully turned on by QT_option */
                cfgdNeg = "QT_"substr(cfgd, 7)
            else if (StartsWith(cfgd, "QT_")) then
                /* QT_option can be forcefully turned off by QT_NO_option */
                cfgdNeg = "QT_NO_"substr(cfgd, 4)
            if (cfgdNeg == "") then
                call charout qconfig_h_new,,
'#ifndef 'cfgd||G.EOL||,
'# define 'cfg||G.EOL||,
'#endif'G.EOL||G.EOL
            else
                call charout qconfig_h_new,,
'#if defined('cfgd') && defined('cfgdNeg')'G.EOL||,
'# undef 'cfgd||G.EOL||,
'#elif !defined('cfgd') && !defined('cfgdNeg')'G.EOL||,
'# define 'cfg||G.EOL||,
'#endif'G.EOL||G.EOL
        end
    end

    /* Add QT_NO* defines to the build key */
    DEFS = ""
    ALL_FLAGS = G.D_FLAGS G.QCONFIG_FLAGS
    do i = 1 to words(ALL_FLAGS)
        opt = strip(word(ALL_FLAGS, i))
        if (StartsWith(opt, "QT_NO_")) then do
            /* maintan sort order and unicity (helps ensure that changes in this
             * configure script don't affect the QT_BUILD_KEY generation) */
            if (DEFS == "") then
                DEFS = opt
            else
            do j = 1 to words(DEFS)
                w = word(DEFS, j)
                c = compare(opt, w)
                if (c == 0) then leave
                if (substr(opt, c, 1) < substr(w, c, 1)) then do
                    DEFS = insert(opt" ", DEFS, wordindex(DEFS, j) - 1)
                    leave
                end
                if (j == words(DEFS)) then
                    DEFS = DEFS" "opt
            end
        end
        /* skip all other compiler defines */
    end
    QT_BUILD_KEY_POST = QT_BUILD_KEY_POST DEFS

    /* write out QT_BUILD_KEY */

    /* we maintain two separate build keys (mainly necessary for the
     * debug-and-release build) differentiated by the DEBUG compier define */
    QT_BUILD_KEY_RELEASE = Normalize(QT_BUILD_KEY_PRE "release" QT_BUILD_KEY_POST)
    QT_BUILD_KEY_DEBUG = Normalize(QT_BUILD_KEY_PRE "debug" QT_BUILD_KEY_POST)

    call charout qconfig_h_new,,
'/* Qt Build Key */'G.EOL||,
'#ifndef QT_NO_DEBUG'G.EOL||,
'# define QT_BUILD_KEY "'QT_BUILD_KEY_DEBUG'"'G.EOL||,
'#else'G.EOL||,
'# define QT_BUILD_KEY "'QT_BUILD_KEY_RELEASE'"'G.EOL||,
'#endif'G.EOL||,
''G.EOL

    /* close the file */
    call charout qconfig_h_new

    /* avoid unecessary rebuilds by copying only if qconfig.h has changed */
    changed = \FileExists(qconfig_h)
    if (\changed) then do
        qconfig_h_str = charin(qconfig_h, 1, chars(qconfig_h))
        call charout qconfig_h
        changed = CompareFileToVar(qconfig_h_new, qconfig_h_str) \= 0
    end
    if (changed) then
        call MoveFile qconfig_h_new, qconfig_h
    else
        call DeleteFile qconfig_h_new

    /*--------------------------------------------------------------------------
     save configuration into qconfig.pri
    --------------------------------------------------------------------------*/

    qconfig_pri = G.OutPath"\mkspecs\qconfig.pri"
    qconfig_pri_new = qconfig_pri".new"

    call DeleteFile qconfig_pri_new

    G.QTCONFIG_CONFIG = Join(G.QTCONFIG_CONFIG, "no_mocdepend")

    if (G.CFG_DEBUG == "yes") then do
        G.QTCONFIG_CONFIG = Join(G.QTCONFIG_CONFIG, "debug")
        if (G.CFG_DEBUG_RELEASE == "yes") then do
            G.QTCONFIG_CONFIG = Join(G.QTCONFIG_CONFIG, "debug_and_release")
            G.QTCONFIG_CONFIG = Join(G.QTCONFIG_CONFIG, "debug_and_release_target")
            G.QT_CONFIG = Join(G.QT_CONFIG, "release")
        end
        G.QT_CONFIG = Join(G.QT_CONFIG, "debug")
    end
    else if (G.CFG_DEBUG == "no") then do
        G.QTCONFIG_CONFIG = Join(G.QTCONFIG_CONFIG, "release")
        if (G.CFG_DEBUG_RELEASE == "yes") then do
            G.QTCONFIG_CONFIG = Join(G.QTCONFIG_CONFIG, "debug_and_release")
            G.QTCONFIG_CONFIG = Join(G.QTCONFIG_CONFIG, "debug_and_release_target")
            G.QT_CONFIG = Join(G.QT_CONFIG, "debug")
        end
        G.QT_CONFIG = Join(G.QT_CONFIG, "release")
    end

/* @todo do we really need this?
    if (G.CFG_FRAMEWORK == "no") then
        G.QTCONFIG_CONFIG = Join(G.QTCONFIG_CONFIG, "qt_no_framework")
    else do
        G.QT_CONFIG = Join(G.QT_CONFIG, "qt_framework")
        G.QTCONFIG_CONFIG = Join(G.QTCONFIG_CONFIG, "qt_framework")
    end
*/

    call charout qconfig_pri_new,,
'# configuration'G.EOL||,
'CONFIG += 'G.QTCONFIG_CONFIG||G.EOL||,
'QT_ARCH = 'G.CFG_ARCH||G.EOL||,
'QT_EDITION = 'G.Edition||G.EOL||,
'QT_CONFIG += 'G.QT_CONFIG||G.EOL||,
''G.EOL||,
'# versioning'G.EOL||,
'QT_VERSION = 'G.QT_VERSION||G.EOL||,
'QT_MAJOR_VERSION = 'G.QT_MAJOR_VERSION||G.EOL||,
'QT_MINOR_VERSION = 'G.QT_MINOR_VERSION||G.EOL||,
'QT_PATCH_VERSION = 'G.QT_PATCH_VERSION||G.EOL||,
'QT_BUILD_VERSION = 'G.QT_BUILD_VERSION||G.EOL||,
''G.EOL||,
'# namespaces'G.EOL||,
'QT_LIBINFIX = 'G.QT_LIBINFIX||G.EOL||,
'QT_NAMESPACE = 'G.QT_NAMESPACE||G.EOL||,
''G.EOL||,
'# qmake.conf overrides'G.EOL

    if (G.QMAKE_MAPSYM \== '') then do
        call charout qconfig_pri_new,,
'QMAKE_MAPSYM = 'G.QMAKE_MAPSYM||G.EOL
    end

    if (G.QMAKE_EXEPACK \== '') then do
        call charout qconfig_pri_new,,
'QMAKE_EXEPACK = 'G.QMAKE_EXEPACK||G.EOL||,
'QMAKE_EXEPACK_FLAGS = 'G.QMAKE_EXEPACK_FLAGS||G.EOL||,
'QMAKE_EXEPACK_POST_FLAGS = 'G.QMAKE_EXEPACK_POST_FLAGS||G.EOL
    end

    call charout qconfig_pri_new, G.EOL

    /* close the file */
    call charout qconfig_pri_new

    /* replace qconfig.pri if it differs from the newly created temp file */
    changed = \FileExists(qconfig_pri)
    if (\changed) then do
        qconfig_pri_str = charin(qconfig_pri, 1, chars(qconfig_pri))
        call charout qconfig_pri
        changed = CompareFileToVar(qconfig_pri_new, qconfig_pri_str) \= 0
    end
    if (changed) then
        call MoveFile qconfig_pri_new, qconfig_pri
    else
        call DeleteFile qconfig_pri_new

    /*--------------------------------------------------------------------------
     save configuration into .qmake.cache
    --------------------------------------------------------------------------*/

    qmake_cache = G.OutPath"\.qmake.cache"
    qmake_cache_new = qmake_cache".new"

    call DeleteFile qmake_cache_new

    call charout qmake_cache_new,,
'CONFIG         += 'G.QMAKE_CONFIG' create_prl link_prl depend_includepath fix_output_dirs QTDIR_build'G.EOL||,
'QT_SOURCE_TREE  = 'QuotePath(CPPPath(G.RelPath))||G.EOL||,
'QT_BUILD_TREE   = 'QuotePath(CPPPath(G.OutPath))||G.EOL||,
'QT_BUILD_PARTS  = 'G.CFG_BUILD_PARTS||G.EOL||,
'QMAKE_MOC_SRC   = $$QT_BUILD_TREE\\src\\moc'G.EOL||,
''G.EOL||,
'#local paths that cannot be queried from the QT_INSTALL_* properties while building QTDIR'G.EOL||,
'QMAKE_MOC       = $$QT_BUILD_TREE\\bin\\moc.exe'G.EOL||,
'QMAKE_UIC       = $$QT_BUILD_TREE\\bin\\uic.exe'G.EOL||,
'QMAKE_UIC3      = $$QT_BUILD_TREE\\bin\\uic3.exe'G.EOL||,
'QMAKE_RCC       = $$QT_BUILD_TREE\\bin\\rcc.exe'G.EOL||,
'QMAKE_INCDIR_QT = $$QT_BUILD_TREE\\include'G.EOL||,
'QMAKE_LIBDIR_QT = $$QT_BUILD_TREE\\lib'G.EOL||G.EOL

    if (G.MYSQL_INCLUDEPATH \== "") then
        call lineout qmake_cache_new, 'MYSQL_INCLUDEPATH = 'CPPPath(G.MYSQL_INCLUDEPATH)
    if (G.MYSQL_LIBS \== "") then
        call lineout qmake_cache_new, 'MYSQL_LIBS = 'CPPPath(G.MYSQL_LIBS)

    if (G.PSQL_INCLUDEPATH \== "") then
        call lineout qmake_cache_new, 'PSQL_INCLUDEPATH = 'CPPPath(G.PSQL_INCLUDEPATH)
    if (G.PSQL_LIBS \== "") then
        call lineout qmake_cache_new, 'PSQL_LIBS = 'CPPPath(G.PSQL_LIBS)

    if (G.QT_EDITION \== "QT_EDITION_OPENSOURCE") then
        call charout qmake_cache_new, 'DEFINES *= QT_EDITION=QT_EDITION_DESKTOP'

    /* dump CUPS_INCLUDEPATH and friends */
    if (G.CFG_CUPS == "yes") then do
        if (G.CUPS_INCLUDEPATH \== "") then
            call lineout qmake_cache_new, 'CUPS_INCLUDEPATH = 'CPPPath(G.CUPS_INCLUDEPATH)
        if (G.CUPS_LIBS \== "") then
            call lineout qmake_cache_new, 'CUPS_LIBS = 'CPPPath(G.CUPS_LIBS)
    end

    /* dump in the OPENSSL_LIBS info */
    if (G.CFG_OPENSSL == "yes") then do
        if (G.OPENSSL_INCLUDEPATH \== "") then
            call lineout qmake_cache_new, 'OPENSSL_INCLUDEPATH = 'CPPPath(G.OPENSSL_INCLUDEPATH)
        if (G.OPENSSL_LIBS \== "") then
            call lineout qmake_cache_new, 'OPENSSL_LIBS = 'CPPPath(G.OPENSSL_LIBS)
    end

    /* dump the qmake spec */
    call lineout qmake_cache_new, ""
    if (DirExists(G.OutPath"\mkspecs\"G.XPLATFORM)) then
       call lineout qmake_cache_new, 'QMAKESPEC = $$QT_BUILD_TREE\\mkspecs\\'G.XPLATFORM
    else
       call lineout qmake_cache_new, 'QMAKESPEC = 'G.XPLATFORM

    /* cmdline args */
    call charout qmake_cache_new, G.EOL||G.QMAKE_VARS

    /* @todo process CFG_INCREMENTAL? */

    /* close the file */
    call lineout qmake_cache_new

    /* replace .qmake.cache if it differs from the newly created temp file */
    changed = \FileExists(qmake_cache)
    if (\changed) then do
        qmake_cache_str = charin(qmake_cache, 1, chars(qmake_cache))
        call charout qmake_cache
        changed = CompareFileToVar(qmake_cache_new, qmake_cache_str) \= 0
    end
    if (changed) then
        call MoveFile qmake_cache_new, qmake_cache
    else
        call DeleteFile qmake_cache_new

    /*--------------------------------------------------------------------------
     give feedback on configuration
    --------------------------------------------------------------------------*/

    call SayLog

    /* G++ is the only supported compiler on OS/2 so far */
    /* @todo enable this code when we start processing command line options
    if (G.CFG_EXCEPTIONS \== "no") then
        call SayLog,
'        This target is using the GNU C++ compiler ('G.PLATFORM').'G.EOL||,
''G.EOL||,
'        Recent versions of this compiler automatically include code for'G.EOL||,
'        exceptions, which increase both the size of the Qt libraries and'G.EOL||,
'        the amount of memory taken by your applications.'G.EOL||,
''G.EOL||,
'        You may choose to re-run `'filespec('N', G.ScriptFile)'` with the -no-exceptions'G.EOL||,
'        option to compile Qt without exceptions. This is completely binary'G.EOL||,
'        compatible, and existing applications will continue to work.'G.EOL||G.EOL
    */

    if (G.XPLATFORM == G.PLATFORM) then
        call SayLog "Build type:    "G.PLATFORM
    else do
        call SayLog "Building on:   "G.PLATFORM
        call SayLog "Building for:  "G.XPLATFORM
    end

    call SayLog "Architecture:  "G.CFG_ARCH
    call SayLog

    /* @todo process CFG_INCREMENTAL? */

    call SayLog "Build .................. "G.CFG_BUILD_PARTS
    call SayLog "Configuration .......... "G.QMAKE_CONFIG G.QT_CONFIG
    if (G.CFG_DEBUG_RELEASE == "yes") then do
       call SayLog "Debug .................. yes (combined)"
       if (G.CFG_DEBUG == "yes") then
           call SayLog "Default Link ........... debug"
       else
           call SayLog "Default Link ........... release"
    end
    else
       call SayLog "Debug .................. "G.CFG_DEBUG

    call SayLog "Qt 3 compatibility ..... "G.CFG_QT3SUPPORT
    if (G.CFG_DBUS == "no") then
        call SayLog "QtDBus module .......... no"
    if (G.CFG_DBUS == "yes") then
        call SayLog "QtDBus module .......... yes (run-time)"
    if (G.CFG_DBUS == "linked") then
        call SayLog "QtDBus module .......... yes (linked)"
    call SayLog "QtConcurrent code....... "G.CFG_CONCURRENT
    call SayLog "QtGui module ........... "G.CFG_GUI
    call SayLog "QtScript module ........ "G.CFG_SCRIPT
    call SayLog "QtScriptTools module ... "G.CFG_SCRIPTTOOLS
    call SayLog "QtXmlPatterns module ... "G.CFG_XMLPATTERNS
    call SayLog "Phonon module .......... "G.CFG_PHONON
    call SayLog "Multimedia module ...... "G.CFG_MULTIMEDIA
    call SayLog "SVG module ............. "G.CFG_SVG
    call SayLog "WebKit module .......... "G.CFG_WEBKIT
    if (G.CFG_WEBKIT == "yes") then do
        if (G.CFG_JAVASCRIPTCORE_JIT == "auto") then
            call SayLog "JavaScriptCore JIT ..... To be decided by JavaScriptCore"
        else
            call SayLog "JavaScriptCore JIT ..... "G.CFG_JAVASCRIPTCORE_JIT
    end
    call SayLog "Declarative module ..... "G.CFG_DECLARATIVE
    call SayLog "Declarative debugging .. "G.CFG_DECLARATIVE_DEBUG
    call SayLog "STL support ............ "G.CFG_STL
    call SayLog "PCH support ............ "G.CFG_PRECOMPILE
    call SayLog "MMX/3DNOW/SSE/SSE2 ..... "G.CFG_MMX"/"G.CFG_3DNOW"/"G.CFG_SSE"/"G.CFG_SSE2
    call SayLog "Graphics System ........ "G.CFG_GRAPHICS_SYSTEM
    call SayLog "IPv6 support ........... "G.CFG_IPV6
    call SayLog "Accessibility .......... "G.CFG_ACCESSIBILITY
    call SayLog "NIS support ............ "G.CFG_NIS
    call SayLog "CUPS support ........... "G.CFG_CUPS
/* @todo do we really need this?
    call SayLog "IPv6 ifname support .... "G.CFG_IPV6IFNAME
    call SayLog "getifaddrs support ..... "G.CFG_GETIFADDRS
    call SayLog "Iconv support .......... "G.CFG_ICONV
    call SayLog "Glib support ........... "G.CFG_GLIB
    call SayLog "GStreamer support ...... "G.CFG_GSTREAMER
*/
    call SayLog "Large File support ..... "G.CFG_LARGEFILE
    call SayLog "GIF support ............ "G.CFG_GIF
    if (G.CFG_TIFF == "no") then
        call SayLog "TIFF support ........... "G.CFG_TIFF
    else
        call SayLog "TIFF support ........... "G.CFG_TIFF" ("G.CFG_LIBTIFF")"
    if (G.CFG_JPEG == "no") then
        call SayLog "JPEG support ........... "G.CFG_JPEG
    else
        call SayLog "JPEG support ........... "G.CFG_JPEG" ("G.CFG_LIBJPEG")"
    if (G.CFG_PNG == "no") then
        call SayLog "PNG support ............ "G.CFG_PNG
    else
        call SayLog "PNG support ............ "G.CFG_PNG" ("G.CFG_LIBPNG")"
    if (G.CFG_MNG == "no") then
        call SayLog "MNG support ............ "G.CFG_MNG
    else
        call SayLog "MNG support ............ "G.CFG_MNG" ("G.CFG_LIBMNG")"
    call SayLog "zlib support ........... "G.CFG_ZLIB
    call SayLog "Session management ..... "G.CFG_SM

    if (G.CFG_OPENGL == "desktop") then
        call SayLog "OpenGL support ......... yes (Desktop OpenGL)"
    else if (G.CFG_OPENGL == "es1") then
        call SayLog "OpenGL support ......... yes (OpenGL ES 1.x Common profile)"
    else if (G.CFG_OPENGL == "es1cl") then
        call SayLog "OpenGL support ......... yes (OpenGL ES 1.x Common Lite profile)"
    else if (G.CFG_OPENGL == "es2") then
        call SayLog "OpenGL support ......... yes (OpenGL ES 2.x)"
    else
        call SayLog "OpenGL support ......... no"

    if (G.CFG_SQL_mysql \== "no") then
        call SayLog "MySQL support .......... "G.CFG_SQL_mysql
    if (G.CFG_SQL_psql \== "no") then
        call SayLog "PostgreSQL support ..... "G.CFG_SQL_psql
    if (G.CFG_SQL_odbc \== "no") then
        call SayLog "ODBC support ........... "G.CFG_SQL_odbc
    if (G.CFG_SQL_oci \== "no") then
        call SayLog "OCI support ............ "G.CFG_SQL_oci
    if (G.CFG_SQL_tds \== "no") then
        call SayLog "TDS support ............ "G.CFG_SQL_tds
    if (G.CFG_SQL_db2 \== "no") then
        call SayLog "DB2 support ............ "G.CFG_SQL_db2
    if (G.CFG_SQL_ibase \== "no") then
        call SayLog "InterBase support ...... "G.CFG_SQL_ibase
    if (G.CFG_SQL_sqlite2 \== "no") then
        call SayLog "SQLite 2 support ....... "G.CFG_SQL_sqlite2
    if (G.CFG_SQL_sqlite \== "no") then
        call SayLog "SQLite support ......... "G.CFG_SQL_sqlite" ("G.CFG_SQLITE")"

    OPENSSL_LINKAGE = ""
    if (G.CFG_OPENSSL == "yes") then
        OPENSSL_LINKAGE = "(run-time)"
    else if (G.CFG_OPENSSL == "linked") then
        OPENSSL_LINKAGE = "(linked)"
    call SayLog "OpenSSL support ........ "G.CFG_OPENSSL OPENSSL_LINKAGE

    if (G.CFG_PTMALLOC \== "no") then
        call SayLog "Use ptmalloc ........... "G.CFG_PTMALLOC

    if (G.QMAKE_MAPSYM \== "") then
        call SayLog "Symbol file support .... yes ("G.QMAKE_MAPSYM")"
    else
        call SayLog "Symbol file support .... no"

    if (G.QMAKE_EXEPACK \== "") then
        call SayLog "Exepack support ........ yes ("G.QMAKE_EXEPACK")"
    else
        call SayLog "Exepack support ........ no"

    /* complain about not being able to use dynamic plugins if we are using a static build */
    if (G.CFG_SHARED == "no") then
        call SayLog,,
G.EOL||,
"WARNING: Using static linking will disable the use of dynamically"G.EOL||,
"loaded plugins. Make sure to import all needed static plugins,"G.EOL||,
"or compile needed modules into the library."G.EOL||,
G.EOL

    if (G.CFG_OPENSSL == "linked" & G.OPENSSL_LIBS == "") then
        call SayLog,,
G.EOL||,
"WARNING: You are linking against OpenSSL but didn't specify the OpenSSL library"G.EOL||,
"names, so the build will most likely fail. You may fix this by setting the"G.EOL||,
"OPENSSL_LIBS environment variable."G.EOL||,
G.EOL

    call SayLog

    /* we don't want to print this, it's in .qmake.cache anyway */
    /*
    call SayVerbose "qmake vars ............. "G.EOL'  'replace(G.QMAKE_VARS,G.EOL,G.EOL'  ')
    call SayVerbose "qmake switches ......... "G.QMAKE_SWITCHES
    call SayVerbose
    */

    /*--------------------------------------------------------------------------
     build makefiles based on the configuration
    --------------------------------------------------------------------------*/

    call SayLog "Finding project files. Please wait..."

    /* @todo walk through the demos, examples, doc, translations trees
     * (depending on G.CFG_BUILD_PARTS) and call qmake on .pro files */

    call SayLog "Creating makefiles. Please wait..."

    if (pos("qmake", G.CFG_BUILD_PARTS) > 0) then
        /* we need to delete the bootstrapped Makefile so that a proper one will
         * be generated from qmake.pro when it's time for it */
        call DeleteFile G.OutPath"\qmake\Makefile"

    /* generate the root makefile */
    str = G.OutPath"\bin\qmake.exe" G.RelPath"\projects.pro -o" G.OutPath"\Makefile"
    say str
    address "cmd" str
    make_rc = rc
    /* exit on failure */
    if (make_rc \= 0)  then
        call Done 2

    /* generate the immediate child makefiles, otherwise 'make release|debug'
     * from the root directory will not work (because there are no real
     * Makefile targets and make will complain Makefiles are missing which
     * I consider as a qmake bug) */
    if (G.CFG_DEBUG_RELEASE == "yes") then do
        address "cmd" G.MAKE "-f" G.OutPath"\Makefile" "qmake"
        make_rc = rc
        /* exit on failure */
        if (make_rc \= 0)  then
            call Done 2
    end

    /*--------------------------------------------------------------------------
     check for platforms that we don't yet know about
    --------------------------------------------------------------------------*/
    if (G.CFG_ARCH == "generic") then
        call SayLog,,
G.EOL||,
'            NOTICE: Atomic operations are not yet supported for this'G.EOL||,
'            architecture.'G.EOL||,
G.EOL||,
'            Qt will use the 'generic' architecture instead, which uses a'G.EOL||,
'            single pthread_mutex_t to protect all atomic operations. This'G.EOL||,
'            implementation is the slow (but safe) fallback implementation'G.EOL||,
'            for architectures Qt does not yet support.'G.EOL||,
G.EOL

    /*--------------------------------------------------------------------------
     finally save the executed command to another script
    --------------------------------------------------------------------------*/

    /* @todo save a call to configure.cmd with all options to a script called
     * config.staus.cmd */

    G.MAKE = filespec('N', G.MAKE)
    call SayLog
    call SayLog "Qt is now configured for building. Just run '"G.MAKE"'."
    call SayLog
    call SayLog "To reconfigure, run '"G.MAKE" confclean' and 'configure.cmd'."
    call SayLog

    return

/**
 * Gets the information about the compiler from QMAKESPEC and from the
 * compiler's executable and sets the following variables:
 *
 *  G.Compiler              - compiler name from the QMAKESPEC value (e.g. after '-')
 *  G.CompilerVersion       - compiler version as a single number, for comparison
 *  G.CompilerVersionString - compiler version as a string, for printing
 */
CheckCompiler: procedure expose (Globals)

    parse value filespec('N', G.QMAKESPEC) with "-" G.Compiler
    select
        when (G.Compiler == "g++") then do
            address "cmd" "g++ -dumpversion | rxqueue.exe"
            do while queued() <> 0
                parse pull str
                if (str \= "") then do
                    G.CompilerVersionString = strip(str)
                    parse var G.CompilerVersionString x "." y "." z
                    if (x == "") then x = 0
                    if (y == "") then y = 0
                    if (z == "") then z = 0
                    G.CompilerVersion = x * 10000 + y * 100 + z
                    leave
                end
            end
        end
        otherwise
            nop
    end

    if (G.CompilerVersion == '' | G.CompilerVersionString == '') then do
        call SayErr 'FATAL: Could not determine compiler version for '''G.Compiler'''!'
        call Done 1
    end

    return

/**
 * Adds a new qmake variable to the cache.
 *
 * @param  aMode        One of: set, add, del.
 * @param  aVarname     Variable name.
 * @param  aContents    Variable value.
 */
QMakeVar: procedure expose (Globals)

    parse arg aMode, aVarname, aContents

    select
        when aMode == "set" then eq = "="
        when aMode == "add" then eq = "+="
        when aMode == "del" then eq = "-="
        otherwise signal Nonsense
    end

    G.QMAKE_VARS = G.QMAKE_VARS||aVarname eq aContents||G.EOL
    return

/*------------------------------------------------------------------------------
 utility functions
------------------------------------------------------------------------------*/

GetPkgVersionAndPath: procedure expose (Globals)
    parse arg aPkgId, aStem
    ver = ''
    pth = ''
    WarpInDir = strip(SysIni('USER', 'WarpIN', 'Path'), 'T', '0'x)
    if (WarpInDir \== '') then do
        rc = SysFileTree(WarpInDir'\DATBAS_?.INI', 'inis', 'FO')
        if (rc == 0) then do
            do i = 1 to inis.0
                rc = SysIni(inis.i, 'ALL:', 'apps')
                if (rc == '') then do
                    do j = 1 to apps.0
                        apps.j = strip(apps.j, 'T', '0'x)
                        if (left(apps.j, length(aPkgId)) == aPkgId) then do
                            /* found the app */
                            ver = right(apps.j, length(apps.j) - length(aPkgId) - 1)
                            ver = translate(ver, '.', '\')
                            pth = strip(SysIni(inis.i, apps.j, 'TargetPath'), 'T', '0'x)
                            leave
                        end
                    end
                end
            end
        end
    end
    call value aStem'.version', ver
    call value aStem'.path', pth
    return

MaxLen: procedure expose (Globals)
    parse arg aStr, aMaxLen
    if (length(aStr) > aMaxLen) then return left(aStr,aMaxLen)
    return aStr

CompareFileToVar: procedure expose (Globals)
    parse arg aFile, aVar
    rc = stream(aFile, 'C', 'OPEN READ')
    if (rc \= 'READY:') then do
        call SayErr 'FATAL: Could not open '''aFile'''!'
        call SayErr 'stream returned 'rc
        call Done 1
    end
    contents = charin(aFile, 1, chars(aFile))
    call stream aFile, 'C', 'CLOSE'
    if (contents < aVar) then return -1
    if (contents > aVar) then return 1
    return 0

CopyFile: procedure expose (Globals)
    parse arg fileFrom, fileTo
    address 'cmd' 'copy' fileFrom fileTo '1>nul 2>nul'
    if (rc \== 0) then do
        call SayErr 'FATAL: Could not copy '''fileFrom''' to '''fileTo'''!'
        call SayErr 'copy returned 'rc
        call Done 1
    end
    return

DeleteFile: procedure expose (Globals)
    parse arg file
    rc = SysFileDelete(file)
    if (rc \= 0 & rc \= 2) then do
        call SayErr 'FATAL: Could not delete file '''file'''!'
        call SayErr 'SysFileDelete returned 'rc
        call Done 1
    end
    return

MoveFile: procedure expose (Globals)
    parse arg fileFrom, fileTo
    if (filespec('D', fileFrom) == filespec('D', fileTo)) then
        fileTo = filespec('P', fileTo)||filespec('N', fileTo)
    else do
        call SayErr 'FATAL: Could not move '''fileFrom''' to '''fileTo'''!'
        call SayErr 'Source and target are on different drives'
        call Done 1
    end
    call DeleteFile fileTo
    address 'cmd' 'move' fileFrom fileTo '1>nul 2>nul'
    if (rc \== 0) then do
        call SayErr 'FATAL: Could not move '''fileFrom''' to '''fileTo'''!'
        call SayErr 'move returned 'rc
        call Done 1
    end
    return

MakeDir: procedure expose (Globals)
    parse arg aDir
    aDir = FixDirNoSlash(aDir)
    curdir = directory()
    base = aDir
    todo.0 = 0
    do while 1
        d = directory(base)
        if (d \== '') then
            leave
        i = todo.0 + 1
        todo.i = filespec('N', base)
        todo.0 = i
        drv = filespec('D', base)
        path = filespec('P', base)
        if (path == '\' | path == '') then do
            base = drv||path
            leave
        end
        base = drv||strip(path, 'T', '\')
    end
    call directory curdir
    do i = todo.0 to 1 by -1
        if (i < todo.0 | (base \== '' & right(base,1) \== '\' &,
                                        right(base,1) \== ':')) then
            base = base'\'
        base = base||todo.i
        rc = SysMkDir(base)
        if (rc \= 0) then do
            call SayErr 'FATAL: Could not create directory '''base'''!'
            call SayErr 'SysMkDir returned 'rc
            call Done 1
        end
    end
    return

CopyDir: procedure expose (Globals)
    parse arg aDirFrom, aDirTo
    address 'cmd' 'xcopy',
        FixDirNoSlash(aDirFrom) FixDirNoSlash(aDirTo)'\ /s /e 1>nul 2>nul'
    if (rc \== 0) then do
        call SayErr 'FATAL: Could not copy '''aDirFrom''' to '''aDirTo'''!'
        call SayErr 'xcopy returned 'rc
        call Done 1
    end
    return

/**
 * Removes the given directory @aDir and all its contents, recursively. If @a
 * aMask is not empty, then only files matching this mask are recursively
 * deleted in the given directory tree, but not the directories themselves.
 *
 * @param aDir    Directory to remove.
 * @param aMask   Mask of files to remove.
 */
DeleteDir: procedure expose (Globals)
    parse arg aDir, aMask
    call SysFileTree aDir'\*', 'dirs.', 'DO'
    call SysFileTree aDir'\'aMask, 'files.', 'FO'
    do i = 1 to files.0
        rc = SysFileDelete(files.i)
        if (rc \= 0) then do
            call SayErr 'FATAL: Could not remove file '''files.i'''!'
            call SayErr 'SysFileDelete returned 'rc
            call Done 1
        end
    end
    do i = 1 to dirs.0
        call DeleteDir dirs.i, aMask
    end
    if (aMask == '') then do
        rc = SysRmDir(aDir)
        if (rc \= 0 & rc \= 3) then do
            call SayErr 'FATAL: Could not remove directory '''aDir'''!'
            call SayErr 'SysRmDir returned 'rc
            call Done 1
        end
    end
    return

SaySay: procedure expose (Globals)
    parse arg str, noeol
    noeol = (noeol == 1)
    if (noeol) then call charout, str
    else say str
    return

SayPrompt: procedure expose (Globals)
    parse arg str, noeol
    noeol = (noeol == 1)
    if (noeol) then call charout, '>>> 'str
    else say '>>> 'str
    return

SayLog: procedure expose (Globals)
    parse arg str, noeol
    noeol = (noeol == 1)
    call SaySay str, noeol
    if (noeol) then call charout G.LogFile, str
    else call lineout G.LogFile, str
    return

SayErr: procedure expose (Globals)
    parse arg str, noeol
    call SayLog str, noeol
    return

SayVerbose: procedure expose (Globals)
    parse arg str, noeol
    if (G.Verbose & G.OPT_HELP \== "yes") then call SayLog str, noeol
    return

/**
 *  Waits for any key.
 */
WaitForAnyKey: procedure expose (Globals)
    call SayPrompt 'Press any key to continue...'
    call SysGetKey 'NOECHO'
    say
    return

SayNoMove: procedure expose (Globals)
    parse arg str
    parse value SysCurPos() with row col
    call SysCurState 'OFF'
    call charout, str
    call SysCurPos row, col
    call SysCurState 'ON'
    return

MovePosBy: procedure expose (Globals)
    parse arg delta
    parse value SysCurPos() with row col
    col = col + delta
    row = row + (col % G.ScreenWidth)
    if (col < 0) then
        row = row - 1
    col = col // G.ScreenWidth
    if (col < 0) then
        col = col + G.ScreenWidth
    call SysCurPos row, col
    return

/**
 * Joins the supplied argulents with one space character. Empty arguments are
 * skipped.
 *
 * @param  ...          String arguments to join.
 * @return              Resulting string.
 */
Join: procedure expose (Globals)

    result = ""
    do i = 1 to arg()
        if (length(arg(i)) > 0) then do
            if (length(result) > 0) then
                result = result' 'arg(i)
            else
                result = arg(i)
        end
    end

    return result

/**
 * Normalizes the given string by removing leading, trailing and extra spaces
 * in the middle so that words in the returned string are separated with exactly
 * one space.
 *
 * @param  aStr         String to normalize.
 * @return              Resulting string.
 */
Normalize: procedure expose (Globals)
    parse arg aStr
    result = ""
    do i = 1 to words(aStr)
        if (result == "") then result = word(aStr, i)
        else result = result" "word(aStr, i)
    end
    return result

/**
 * Returns 1 if the given string @a aStr1 starts with the string @a aStr2 and
 * 0 otherwise. If @a aStr2 is null or empty, 0 is returned.
 *
 * @param  aStr1        String to search in.
 * @param  aStr2        String to search for.
 * @return              1 or 0.
 */
StartsWith: procedure expose (Globals)
    parse arg aStr1, aStr2
    len = length(aStr2)
    if (len == 0) then return 0
    if (length(aStr1) < len) then return 0
    return (left(aStr1, len) == aStr2)

/**
 * Returns 1 if the given string @a aStr1 ends with the string @a aStr2 and
 * 0 otherwise. If @a aStr2 is null or empty, 0 is returned.
 *
 * @param  aStr1        String to search in.
 * @param  aStr2        String to search for.
 * @return              1 or 0.
 */
EndsWith: procedure expose (Globals)
    parse arg aStr1, aStr2
    len = length(aStr2)
    if (len == 0) then return 0
    if (length(aStr1) < len) then return 0
    return (right(aStr1, len) == aStr2)

/**
 *  Displays a prompt to input a text line and returns the line entered by the
 *  user. Letters in the mode argument have the following meanings:
 *
 *      N -- empty lines are not allowed (e.g. '' can never be returned)
 *      C -- ESC can be pressed to cancel input ('1B'x is returned)
 *
 *  @param  prompt  prompt to display
 *  @param  default initial line contents
 *  @param  mode    input mode string consisting of letters as described above
 *  @return         entered line
 */
InputLine: procedure expose (Globals)

    parse arg prompt, default, mode

    if (length(prompt) > 0) then
        call SaySay prompt
    say

    mode = translate(mode)
    allow_empty = pos('N', mode) == 0
    allow_cancel = pos('C', mode) > 0

    line = default
    len = length(line)
    n = len

    call SayPrompt line, 1
    extended = 0

    do forever

        key = SysGetKey('NOECHO')
        if (key == 'E0'x) then do
            extended = 1
            iterate
        end

        select
            when (\extended & key == '08'x) then do
                /* Backspace */
                if (n > 0) then do
                    call MovePosBy -1
                    line = delstr(line, n, 1)
                    call SayNoMove substr(line, n)||' '
                    len = len - 1
                    n = n - 1
                end
                iterate
            end
            when (key == '0D'x) then do
                /* Enter */
                if (line == '' & \allow_empty) then iterate
                say
                leave
            end
            when (key == '1B'x) then do
                /* ESC */
                line = key
                leave
            end
            when (extended) then do
                select
                    when (key == '53'x) then do
                        /* Delete */
                        if (n < len) then do
                            line = delstr(line, n + 1, 1)
                            call SayNoMove substr(line, n + 1)||' '
                        end
                        len = len - 1
                    end
                    when (key == '4B'x) then do
                        /* Left arrow */
                        if (n > 0) then do
                            call MovePosBy -1
                            n = n - 1
                        end
                    end
                    when (key == '4D'x) then do
                        /* Right arrow */
                        if (n < len) then do
                            call MovePosBy 1
                            n = n + 1
                        end
                    end
                    when (key == '47'x) then do
                        /* Home */
                        if (n > 0) then do
                            call MovePosBy -n
                            n = 0
                        end
                    end
                    when (key == '4F'x) then do
                        /* End */
                        if (n < len) then do
                            call MovePosBy len - n
                            n = len
                        end
                    end
                    otherwise nop
                end
                extended = 0
                iterate
            end
            otherwise do
                call charout, key
                if (n == len) then do
                    line = line||key
                end
                else do
                    line = insert(key, line, n)
                    call SayNoMove substr(line, n + 2)
                end
                len = len + 1
                n = n + 1
            end
        end
    end

    say
    return line

/**
 *  Shows the prompt to input a directory path and returns the path entered by
 *  the user. The procedure does not return until a valid existing path is
 *  entered or until ESC is pressed in which case it returns ''.
 *
 *  @param  prompt  prompt to show
 *  @param  default initial directory
 *  @return         selected directory
 */
InputDir: procedure expose (Globals)
    parse arg prompt, default
    dir = default
    do forever
        dir = InputLine(prompt, dir, 'NC')
        if (dir == '1B'x) then do
            say
            return '' /* canceled */
        end
        if (DirExists(dir)) then leave
        call SayErr 'The entered directory does not exist.'
        say
    end
    return dir

/**
 *  Shows a Yes/No choice.
 *
 *  @param  prompt  prompt to show (specify '' to suppress the prompt)
 *  @param  default default choice:
 *      ''          - no default choice
 *      'Y' or 1    - default is yes
 *      other       - default is no
 *  @return
 *      1 if Yes is selected, otherwise 0
 */
GetYesNo: procedure expose (Globals)
    parse arg prompt, default
    default = translate(default)
    if (default == 1) then default = 'Y'
    else if (default \== '' & default \== 'Y') then default = 'N'
    if (prompt \= '') then call SaySay prompt
    say
    call SayPrompt '[YN] ', 1
    yn = ReadChoice('YN',, default, 'I')
    say
    say
    return (yn == 'Y')

/**
 *  Reads a one-key choice from the keyboard.
 *  user. Letters in the mode argument have the following meanings:
 *
 *      E -- allow to press Enter w/o a choice (will return '')
 *      C -- ESC can be pressed to cancel selection (will return '1B'x)
 *      I -- ignore case of pressed letters
 *
 *  @param  choices     string of allowed one-key choices
 *  @param  extChoices  string of allowed one-extended-key choices
 *  @param  default     default choice (can be a key from choices)
 *  @param  mode        input mode string consisting of letters as described above
 *  @return
 *      entered key (prefixed with 'E0'x if from extChoices)
 */
ReadChoice: procedure expose (Globals)
    parse arg choices, extChoices, default, mode
    mode = translate(mode)
    ignoreCase = pos('I', mode) > 0
    allowEnter = pos('E', mode) > 0
    allowCancel = pos('C', mode) > 0
    choice = default
    call charout, choice
    if (ignoreCase) then choice = translate(choice)
    extended = 0
    do forever
        key = SysGetKey('NOECHO')
        if (key == 'E0'x) then do
            extended = 1
            iterate
        end
        if (\extended & ignoreCase) then key = translate(key)
        select
            when (allowCancel & \extended & key == '1B'x) then do
                choice = key
                leave
            end
            when (choice == '' & \extended & verify(key, choices) == 0) then do
                choice = key
            end
            when (extended & verify(key, extChoices) == 0) then do
                choice = '0E'x||key
                leave
            end
            when (\extended & key == '08'x & choice \== '') then do
                /* backspace pressed */
                call charout, key' '
                choice = ''
            end
            when (\extended & key == '0D'x & (choice \== '' | allowEnter)) then do
                leave
            end
            otherwise do
                extended = 0
                iterate
            end
        end
        call charout, key
        extended = 0
    end
    return choice

/**
 *  Encloses the given path with quotes if it contains
 *  space characters, otherwise returns it w/o changes.
 */
QuotePath: procedure expose (Globals)
    parse arg path
    if (verify(path, ' +', 'M') > 0) then path = '"'path'"'
    return path

/**
 *  Doubles all back slash characters to make the path ready to be put
 *  to a C/C++ source.
 */
CPPPath: procedure expose (Globals)
    parse arg path
    return Replace(path, '\', '\\')

/**
 *  Fixes the directory path by a) converting all slashes to back
 *  slashes and b) ensuring that the trailing slash is present if
 *  the directory is the root directory, and absent otherwise.
 *
 *  @param dir      the directory path
 *  @param noslash
 *      optional argument. If 1, the path returned will not have a
 *      trailing slash anyway. Useful for concatenating it with a
 *      file name.
 */
FixDir: procedure expose (Globals)
    parse arg dir, noslash
    noslash = (noslash = 1)
    dir = translate(dir, '\', '/')
    if (right(dir, 1) == '\' &,
        (noslash | \(length(dir) == 3 & (substr(dir, 2, 1) == ':')))) then
        dir = substr(dir, 1, length(dir) - 1)
    return dir

/**
 *  Shortcut to FixDir(dir, 1)
 */
FixDirNoSlash: procedure expose (Globals)
    parse arg dir
    return FixDir(dir, 1)

/**
 *  Returns 1 if the specified dir exists and 0 otherwise.
 */
DirExists: procedure expose (Globals)
    parse arg dir
    return (GetAbsDirPath(dir) \== '')

/**
 *  Returns the absolute path to the given directory
 *  or an empty string if no directory exists.
 */
GetAbsDirPath: procedure expose (Globals)
    parse arg dir
    if (dir \== '') then do
        curdir = directory()
        dir = directory(FixDir(dir))
        call directory curdir
    end
    return dir

/**
 *  Returns 1 if the specified file exists and 0 otherwise.
 */
FileExists: procedure expose (Globals)
    parse arg file
    return (GetAbsFilePath(file) \= '')

/**
 *  Returns the absolute path to the given file (including the filename)
 *  or an empty string if no file exists.
 */
GetAbsFilePath: procedure expose (Globals)
    parse arg file
    if (file \= '') then do
        file = stream(FixDir(file), 'C', 'QUERY EXISTS')
    end
    return file

/**
 *  Returns the absolute path to the temporary directory.
 *  The returned value doesn't end with a slash.
 */
GetTempDir: procedure expose (Globals)
    dir = value('TEMP',,'OS2ENVIRONMENT')
    if (dir == '') then dir = value('TMP',,'OS2ENVIRONMENT')
    if (dir == '') then dir = SysBootDrive()
    return dir

/**
 *  Magic cmd handler.
 *  Executes the commad passed as --magic-cmd <cmd> and writes the result code
 *  to the standard output in the form of 'RC:<rc>:CR'
 *  (to be used by MagicLogHook).
 */
MagicCmdHook: procedure
    parse arg magic' 'cmd
    if (magic \== '--magic-cmd') then return
    cmd = strip(cmd)
    signal on halt name MagicCmdHalt
    address 'cmd' cmd
    say 'RC:'rc':CR'
    exit 0

MagicCmdHalt:
    say 'RC:255:CR'
    exit 255

/**
 *  Magic log handler.
 */
MagicLogHook: procedure
    parse arg magic' 'file
    if (magic \== '--magic-log') then return
    file = strip(file)
    signal on halt name MagicLogHalt
    rc = 0
    do while (stream('STDIN', 'S') == 'READY')
        line = linein()
        if (left(line, 3) == 'RC:') then do
            if (right(line,3) == ':CR') then do
                rc = substr(line, 4, length(line) - 6)
                iterate
            end
        end
        call lineout, line
        call lineout file, line
    end
    exit rc

MagicLogHalt:
    exit 255

/**
 * Gets the OS/2 environment variable.
 *
 * @param aName     Variable name.
 * @return          Variable value.
 */
GetEnv: procedure expose (Globals)

    parse arg aName

    extLibPath = (translate(aName) == 'BEGINLIBPATH' | translate(aName) == 'ENDLIBPATH')
    if (extLibPath) then val = SysQueryExtLibPath(left(aName, 1))
    else val = value(aName,, 'OS2ENVIRONMENT')

    return val

/**
 * Sets the OS/2 environment variable.
 *
 * @param aName     Variable name.
 * @param aValue    Variable Value.
 */
SetEnv: procedure expose (Globals)

    parse arg aName, aValue

    extLibPath = (translate(aName) == 'BEGINLIBPATH' | translate(aName) == 'ENDLIBPATH')
    if (extLibPath) then call SysSetExtLibPath aValue, left(aName, 1)
    else call value aName, aValue, 'OS2ENVIRONMENT'

    return

/**
 * Unsets the OS/2 environment variable.
 *
 * @param aName     Variable name.
 * @param aValue    Variable Value.
 */
UnsetEnv: procedure expose (Globals)

    parse arg aName

    extLibPath = (translate(aName) == 'BEGINLIBPATH' | translate(aName) == 'ENDLIBPATH')
    if (extLibPath) then call SysSetExtLibPath "", left(aName, 1)
    else call value aName, "", 'OS2ENVIRONMENT'

    return

/**
 *  Adds the given path to the contents of the given variable
 *  containing a list of paths separated by semicolons.
 *  This function guarantees that if the given path is already contained in the
 *  variable, the number of occurences will not increase (but the order may be
 *  rearranged depending on the mode argument).
 *
 *  @param  name        variable name
 *  @param  path        path to add
 *  @param  mode        'P' (prepend): remove the old occurence of path (if any)
 *                          and put it to the beginning of the variable's contents
 *                      'A' (append): remove the old occurence of path (if any)
 *                          and put it to the end of the variable's contents
 *                      otherwise: append path to the variable only if it's
 *                          not already contained there
 *  @param  environment either '' to act on REXX variables or 'OS2ENVIRONMENT'
 *                      to act on OS/2 environment variables
 *
 *  @version 1.1
 */
AddPathVar: procedure expose (Globals)
    parse arg name, path, mode, environment
    if (path == '') then return
    if (verify(path, ' +', 'M') > 0) then path = '"'path'"'
    mode = translate(mode)
    prepend = (mode == 'P') /* strictly prepend */
    append = (mode == 'A') /* strictly append */
    os2Env = (translate(environment) == 'OS2ENVIRONMENT')
    if (os2Env) then do
        extLibPath = (translate(name) == 'BEGINLIBPATH' | translate(name) == 'ENDLIBPATH')
        if (extLibPath) then cur = SysQueryExtLibPath(left(name, 1))
        else cur = value(name,, environment)
    end
    else cur = value(name)
    /* locate the previous occurence of path */
    l = length(path)
    found = 0; p = 1
    pathUpper = translate(path)
    curUpper = translate(cur)
    do while (\found)
        p = pos(pathUpper, curUpper, p)
        if (p == 0) then leave
        cb = ''; ca = ''
        if (p > 1) then cb = substr(cur, p - 1, 1)
        if (p + l <= length(cur)) then ca = substr(cur, p + l, 1)
        found = (cb == '' | cb == ';') & (ca == '' | ca == ';')
        if (\found) then p = p + 1
    end
    if (found) then do
        /* remove the old occurence when in strict P or A mode */
        if (prepend | append) then cur = delstr(cur, p, l)
    end
    /* add path when necessary */
    if (prepend) then cur = path';'cur
    else if (append | \found) then cur = cur';'path
    /* remove excessive semicolons */
    cur = strip(cur, 'B', ';')
    p = 1
    do forever
        p = pos(';;', cur, p)
        if (p == 0) then leave
        cur = delstr(cur, p, 1)
    end
    if (os2Env) then do
        if (extLibPath) then call SysSetExtLibPath cur, left(name, 1)
        else call value name, cur, environment
    end
    else call value name, cur
    return

/**
 *  Shortcut to AddPathVar(name, path, prepend, 'OS2ENVIRONMENT')
 */
AddPathEnv: procedure expose (Globals)
    parse arg name, path, prepend
    call AddPathVar name, path, prepend, 'OS2ENVIRONMENT'
    return

/**
 *  Replaces all occurences of a given substring in a string with another
 *  substring.
 *
 *  @param  str the string where to replace
 *  @param  s1  the substring which to replace
 *  @param  s2  the substring to replace with
 *  @return     the processed string
 *
 *  @version 1.1
 */
Replace: procedure expose (Globals)
    parse arg str, s1, s2
    l1  = length(s1)
    l2  = length(s2)
    i   = 1
    do while (i > 0)
        i = pos(s1, str, i)
        if (i > 0) then do
            str = delstr(str, i, l1)
            str = insert(s2, str, i-1)
            i = i + l2
        end
    end
    return str

/**
 *  Returns a list of all words from the string as a stem.
 *  Delimiters are spaces, tabs and new line characters.
 *  Words containg spaces must be enclosed with double
 *  quotes. Double quote symbols that need to be a part
 *  of the word, must be doubled.
 *
 *  @param string   the string to tokenize
 *  @param stem
 *      the name of the stem. The stem must be global
 *      (i.e. its name must start with 'G.!'), for example,
 *      'G.!wordlist'.
 *  @param leave_ws
 *      1 means whitespace chars are considered as a part of words they follow.
 *      Leading whitespace (if any) is always a part of the first word (if any).
 *
 *  @version 1.1
 */
TokenizeString: procedure expose (Globals)

    parse arg string, stem, leave_ws
    leave_ws = (leave_ws == 1)

    delims  = '20090D0A'x
    quote   = '22'x /* " */

    num = 0
    token = ''

    len = length(string)
    last_state = '' /* D - in delim, Q - in quotes, W - in word */
    seen_QW = 0

    do i = 1 to len
        c = substr(string, i, 1)
        /* determine a new state */
        if (c == quote) then do
            if (last_state == 'Q') then do
                /* detect two double quotes in a row */
                if (substr(string, i + 1, 1) == quote) then i = i + 1
                else state = 'W'
            end
            else state = 'Q'
        end
        else if (verify(c, delims) == 0 & last_state \== 'Q') then do
            state = 'D'
        end
        else do
            if (last_state == 'Q') then state = 'Q'
            else state = 'W'
        end
        /* process state transitions */
        if ((last_state == 'Q' | state == 'Q') & state \== last_state) then c = ''
        else if (state == 'D' & \leave_ws) then c = ''
        if (last_state == 'D' & state \== 'D' & seen_QW) then do
            /* flush the token */
            num = num + 1
            call value stem'.'num, token
            token = ''
        end
        token = token||c
        last_state = state
        seen_QW = (seen_QW | state \== 'D')
    end

    /* flush the last token if any */
    if (token \== '' | seen_QW) then do
        num = num + 1
        call value stem'.'num, token
    end

    call value stem'.0', num

    return

/**
 * Initializes a new "at exit" slot and returns its ID.
 */
AddAtExitSlot: procedure expose (Globals)
    id = 1
    if (symbol('G.AtExit.0') == 'VAR') then
        id = G.AtExit.0 + 1
    G.AtExit.0 = id
    G.AtExit.id = ""
    return id

/**
 * Puts the REXX code passed in @a aCommand to the "at exit" slot with @a aID.
 * The entire set of commands will be executed upon program termination
 * unless the slot is purged earlier with RunAtExitSlot.
 *
 * Note that the commands will be executed in the order reverse to the order of
 * PutToAtExitSlot calls (i.e. the last put command will be executed first).
 *
 * @param aID       Slot ID to add the command to.
 * @param aCommand  Command to add.
 */
PutToAtExitSlot: procedure expose (Globals)
    parse arg aID, aCommand
    if (G.AtExit.aID == "") then
        G.AtExit.aID = aCommand
    else
        G.AtExit.aID = aCommand"; "G.AtExit.aID
    return

/**
 * Executes the "at exit" slot with @a aID and purges (removes) it from the
 * collection of slots so that it won't be executed upon program termination.
 *
 * Note that the commands will be executed in the order reverse to the order of
 * PutToAtExitSlot calls (i.e. the last put command will be executed first).
 *
 * @param aID       Slot ID to run.
 */
RunAtExitSlot: procedure expose (Globals)
    parse arg aID
    cmds = G.AtExit.aID
    drop G.AtExit.aID
    /* shorten the list if we are the last (unless called from Done) */
    if (value('G.Done_done') \= 1) then
        if (G.AtExit.0 == aID) then
            G.AtExit.0 = aID - 1
    /* execute commands */
    interpret cmds
    return

/**
 *  NoValue signal handler.
 */
NoValue:
    errl    = sigl
    say
    say
    say 'EXPRESSION HAS NO VALUE at line #'errl'!'
    say
    say 'This is usually a result of a misnamed variable.'
    say 'Please contact the author.'
    call Done 252

/**
 *  Nonsense handler.
 */
Nonsense:
    errl    = sigl
    say
    say
    say 'NONSENSE at line #'errl'!'
    say
    say 'The author decided that this point of code should'
    say 'have never been reached, but it accidentally had.'
    say 'Please contact the author.'
    call Done 253

/**
 *  Syntax signal handler.
 */
Syntax:
    errn    = rc
    errl    = sigl
    say
    say
    say 'UNEXPECTED PROGRAM TERMINATION!'
    say
    say 'REX'right(errn , 4, '0')': 'ErrorText(rc)' at line #'errl
    say
    say 'Possible reasons:'
    say
    say '  1. Some of REXX libraries are not found but required.'
    say '  2. You have changed this script and made a syntax error.'
    say '  3. Author made a mistake.'
    say '  4. Something else...'
    call Done 254

/**
 *  Halt signal handler.
 */
Halt:
    say
    say 'CTRL-BREAK pressed, exiting.'
    call Done 255

/**
 *  Always called at the end. Should not be called directly.
 *  @param the exit code
 */
Done: procedure expose (Globals)
    parse arg code
    /* protect against recursive calls */
    if (value('G.Done_done') == 1) then exit code
    call value 'G.Done_done', 1
    /* cleanup stuff goes there */
    if (symbol('G.AtExit.0') == 'VAR') then do
        /* run all AtExit slots */
        do i = 1 to G.AtExit.0
            if (symbol('G.AtExit.'i) == 'VAR') then
                call RunAtExitSlot i
        end
    end
    drop G.AtExit.
    /* finally, exit */
    exit code


