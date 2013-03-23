/* REXX */
/*
 */ G.!Id = '$Id: emxexpw.cmd 505 2010-02-03 02:18:16Z dmik $' /*
 *
 *  :mode=netrexx:tabSize=4:indentSize=4:noTabs=true:
 *  :folding=explicit:collapseFolds=1:
 *
 *  ----------------------------------------------------------------------------
 *
 *  EMXEXP Wrapper
 *
 *  Manages exports created by the emxexp tool from the GCC for OS/2 package.
 *
 *  Please run the script without arguments to get some help.
 *
 *  Author: Dmitry A. Kuminov
 *
 *  FREEWARE. NO ANY WARRANTY..., ON YOUR OWN RISC... ETC.
 *
 */

signal on syntax
signal on halt
signal on novalue
trace off
numeric digits 12
'@echo off'

/* globals
 ******************************************************************************/

G.!Title        = 'emxexpw r.${revision} [${revision.date} ${revision.time}] - EMXEXP Wrapper'

G.!tab          = '09'x
G.!eol          = '0D0A'x

G.!DefaultDllTemplate =,
    "LIBRARY ${name} INITINSTANCE TERMINSTANCE"G.!eol||,
    "DESCRIPTION '${bldlevel}'"G.!eol||,
    "DATA MULTIPLE NONSHARED"
    /* <-- EXPORTS + ${exports} go here */

G.!DefaultExeTemplate =,
    "NAME ${name} ${exetype}"G.!eol||,
    "DESCRIPTION '${bldlevel}'"
    /* <-- EXPORTS + ${exports} go here */

G.!StatsTemplate =,
    G.!eol||,
    ';'G.!eol||,
    '; EXPORTS STATISTICS'G.!eol||,
    ';'G.!eol||,
    '; Total exports                        : %1'G.!eol||,
    '; Total weak (marked as weak)          : %2'G.!eol||,
    '; Total new (marked as new)            : %3'G.!eol||,
    '; Total resurrected (marked as renew)  : %4'G.!eol||,
    '; Total outdated (marked as ;@@@)      : %5'G.!eol||,
    ';'

/* options */
Opt.!DefFile         = ''
Opt.!LibName         = ''
Opt.!LibVersion      = '0'
Opt.!LibDesc         = ''
Opt.!LibVendor       = 'unknown'
Opt.!LibBldLevel     = ''
Opt.!DefTemplate     = ''
Opt.!DefMap          = ''
Opt.!ExeType         = ''
Opt.!Quiet           = 0
Opt.!Objects.0       = 0

/* all globals to be exposed in procedures */
Globals = 'G. Opt.'

/* init rexx lib
 ******************************************************************************/

if (RxFuncQuery('SysLoadFuncs')) then do
    call RxFuncAdd 'SysLoadFuncs', 'RexxUtil', 'SysLoadFuncs'
    call SysLoadFuncs
end

/* startup code
 ******************************************************************************/

parse source . . G.!SourceFile

parse value strip(G.!Id,,'$') with,
    'Id: '.' 'G.!Revision' 'G.!RevisionDate' 'G.!RevisionTime' '.
G.!Title = Replace(G.!Title, '${revision}', G.!Revision)
G.!Title = Replace(G.!Title, '${revision.date}', G.!RevisionDate)
G.!Title = Replace(G.!Title, '${revision.time}', G.!RevisionTime)

parse arg args
if (args == '') then call Usage

call ParseCommandLine args

/* check options */
if (Opt.!DefFile == '') then call Error "E_NoDefFile"
if (Opt.!ExeType \== '' & Opt.!DefMap \== '') then
    call Error "E_UnexpectedOption", "-map "Opt.!DefMap
if (Opt.!DefMap \== '') then
    if (Opt.!Objects.0 == 0) then call Error "E_NoObjectFiles"

/* apply default options */
if (Opt.!LibName == '') then do
    parse value filespec('N', Opt.!DefFile) with Opt.!LibName '.' .
end
if (Opt.!ExeType \== '') then
    G.!DefaultTemplate = G.!DefaultExeTemplate
else
    G.!DefaultTemplate = G.!DefaultDllTemplate

/* check emxexp.exe existence */
if (Opt.!DefMap \== '' | Opt.!Objects.0 > 0) then do
    G.emxexp_exe = SysSearchPath('PATH', 'EMXEXP.EXE')
    if (G.emxexp_exe == '') then call Error "E_CantFindEMXEXP"
end

Opt.!ObjectsList = ''
do i = 1 to Opt.!Objects.0
    Opt.!ObjectsList = Opt.!ObjectsList Opt.!Objects.i
    /* command line length limit (1024 chars) */
    if (length(Opt.!ObjectsList) > 900) then do
        Opt.!ObjectsList = ''
        leave
    end
end
if (Opt.!ObjectsList \== '') then drop Opt.!Objects.

call Main

/* termination code
 ******************************************************************************/

call Done 0


/* functions
 ******************************************************************************/

/**
 *  Just do the job.
 */
Main: procedure expose (Globals)

    rc = SysFileDelete(Opt.!DefFile)
    if (rc \== 0 & rc \== 2) then
        call Error "E_CantDeleteFile", Opt.!DefFile, rc

    /* write object files to a responce file for EMXEXP */
    if (Opt.!ObjectsList == '' & Opt.!Objects.0 > 0) then do
        G.!ObjectListFile = SysTempFileName(GetTempDir()'\emxxexp_?????.tmp')
        do i = 1 to Opt.!Objects.0
            call lineout G.!ObjectListFile, Opt.!Objects.i
        end
        call lineout G.!ObjectListFile
    end
    else
        G.!ObjectListFile = ''

    if (Opt.!DefMap == '') then do
        /*
         *  Easy path: no map file. Just use emxexp to generate the EXPORTS
         *  section of the DEF file.
         */

        have_exports_macro = 0
        exports_tail = ''

        /* if there are no object files, jsut leave the EXPORTS section empty */
        if (Opt.!Objects.0 == 0) then
        	have_exports_macro = 0

        /* write stuff before ${exports} */
        if (Opt.!DefTemplate == '') then do
            call lineout Opt.!DefFile, ReplaceMacros(G.!DefaultTemplate)
            if (have_exports_macro) then
                call lineout Opt.!DefFile, 'EXPORTS'
        end
        else do
            parse value WriteTemplatedDEF('B') with have_exports_macro' 'exports_tail
        end

        call lineout Opt.!DefFile

        if (have_exports_macro) then do

            if (Opt.!ObjectsList == '') then do
                say G.emxexp_exe '@'G.!ObjectListFile '>>'Opt.!DefFile
                G.emxexp_exe '@'G.!ObjectListFile '>>'Opt.!DefFile
            end
            else do
                say G.emxexp_exe Opt.!ObjectsList '>>'Opt.!DefFile
                G.emxexp_exe Opt.!ObjectsList '>>'Opt.!DefFile
            end

            call lineout Opt.!DefFile

            if (G.!ObjectListFile \= '') then do
                call SysFileDelete G.!ObjectListFile
                G.!ObjectListFile = ''
            end

            if (rc \== 0) then
                call Error "E_EMXEXPFailed", rc
        end

        /* write remaining stuff after ${exports} */
        if (Opt.!DefTemplate == '') then nop
        else do
            if (have_exports_macro) then call WriteTemplatedDEF 'A', exports_tail
        end

    end
    else do

        /*
         *  Map file is given. Parse it and then combine with the temporary
         *  exports generated by emxexp.
         */

        /* parse the map file */
        call ParseExportsSection Opt.!DefMap, 'P'

        /* call the original emxexp to put EXPORTS to a temporary DEF file */
        G.!TempDefFile = SysTempFileName(GetTempDir()'\emxxexp_?????_def.tmp')
        if (Opt.!ObjectsList == '') then
            G.emxexp_exe '@'G.!ObjectListFile '>'G.!TempDefFile
        else
            G.emxexp_exe Opt.!ObjectsList '>'G.!TempDefFile
        if (rc \== 0) then call Error "E_EMXEXPFailed", rc

        if (G.!ObjectListFile \= '') then do
            call SysFileDelete G.!ObjectListFile
            G.!ObjectListFile = ''
        end

        /* parse the temporary DEF file and combine with G.!Exports */
        call ParseExportsSection G.!TempDefFile, 'C'
        call SysFileDelete G.!TempDefFile

        if (G.!Exports.0 == 0) then
            call Error "E_NoExports"

        /* update the map file */

        rc = SysFileDelete(Opt.!DefMap)
        if (rc \== 0 & rc \== 2) then
            call Error "E_CantDeleteFile", Opt.!DefMap, rc

        stats = AppendExportsToFile(Opt.!DefMap)

        /* returned stats are 'weak new renew coutdated' */
        stats = ReplaceArgs(G.!StatsTemplate,,
            G.!Exports.0,,
            word(stats, 1),,
            word(stats, 2),,
            word(stats, 3),,
            word(stats, 4))

        call lineout Opt.!DefMap, stats
        call lineout Opt.!DefMap

        /* generate a DEF file */

        have_exports_macro = 0
        exports_tail = ''

        /* write stuff before ${exports} */
        if (Opt.!DefTemplate == '') then do
            call lineout Opt.!DefFile, ReplaceMacros(G.!DefaultTemplate)
            call lineout Opt.!DefFile, 'EXPORTS'
            have_exports_macro = 1
        end
        else do
            parse value WriteTemplatedDEF('B') with have_exports_macro' 'exports_tail
        end

        call lineout Opt.!DefFile

        if (have_exports_macro) then do
            address 'cmd' 'copy "'Opt.!DefFile'" /B + "'Opt.!DefMap'" /B '||,
                               '"'Opt.!DefFile'" 1>nul 2>nul'
        end

        /* write remaining stuff after ${exports} */
        if (Opt.!DefTemplate == '') then nop
        else do
            if (have_exports_macro) then call WriteTemplatedDEF 'A', exports_tail
        end

        call lineout Opt.!DefFile

    end

    return

/**
 *  Replaces all known ${} macros in the given string and returns it.
 */
ReplaceMacros: procedure expose (Globals)

    parse arg str

    str = Replace(str, '${name}', Opt.!LibName)
    str = Replace(str, '${version}', Opt.!LibVersion)
    str = Replace(str, '${desc}', Opt.!LibDesc)
    str = Replace(str, '${vendor}', Opt.!LibVendor)
    str = Replace(str, '${bldlevel}', Opt.!LibBldLevel)
    str = Replace(str, '${exetype}', Opt.!ExeType)

    return str

/**
 *  Expands DEF template macros and writes results to a DEF file.
 *
 *  @param mode
 *      'B' to expand and write everything before '${exports}' or
 *      'A' (or any other string) to write what's left after it.
 *  @param exports_tail
 *       When mode is 'A', this is the string that was returned when calling
 *       this procedure in 'B' mode.
 *       When mode is 'B', this argument is not used.
 *  @return
 *      When mode is 'B':
 *          '1 <str>' if '${exports}' has been found (where <str> is the rest of
 *          the line after '${exports}') or
 *          '0' if no '${exports}' have been found (and no need to call this
 *          procedure in 'A' mode).
 *      When mode is 'A':
 *          nothing.
 */
WriteTemplatedDEF: procedure expose (Globals)

    parse arg mode, exports_tail

    if (mode == 'B') then do
        str = ''
        epos = 0
        parse value stream(Opt.!DefTemplate, 'C', 'OPEN READ') with status':'code
        if (status \== 'READY') then
            call Error "E_CantOpenFile", Opt.!DefTemplate, code
        do while lines(Opt.!DefTemplate)
            str = linein(Opt.!DefTemplate)
            epos = pos('${exports}', str)
            if (epos == 0) then do
                str = ReplaceMacros(str)
                call lineout Opt.!DefFile, str
            end
            else do
                call charout Opt.!DefFile, substr(str, 1, epos - 1)
                str = substr(str, epos + length('${exports}'))
                leave
            end
        end
        if (epos > 0) then return 1' 'str
        return 0
    end

    call lineout Opt.!DefFile, exports_tail

    do while lines(Opt.!DefTemplate)
        str = linein(Opt.!DefTemplate)
        str = ReplaceMacros(str)
        call lineout Opt.!DefFile, str
    end

    return

/**
 *  Parses the EXPORTS-like sectoion of given map or DEF file
 *  and accumulate the results in the G.!Exports stem.
 *  Parsing is based on the format of the EXPORTS section, generated
 *  by emxexp.
 *
 *  When mode is 'P'arse, exports are parsed and stored in the stem.
 *  The given file must be a map file. Its format is similar to what
 *  is generated by emxexp but has some incompatible additions.
 *  First, the symbol name must be followed by an ordinal and the NONAME
 *  keyword. Second, magicseg can appear on a separate comment line right
 *  before a group of symbols it should be applied to (this can reduce the size
 *  of the map file because there is no need to put magicseg in the comments
 *  of every line that defines a symbol).
 *
 *  When mode is 'C'ombine, exports being parsed are searched in the stem
 *  and new symbols are added only if missing. Outdated symbols are also
 *  specially marked. The format of the given file must be the same as
 *  generated by emxexp.
 *
 *  @note
 *      The procedure doesn't currently check for duplicate ordinals!
 *      (this will not happen until ordinals are manually hacked)
 *
 *  @param file     file to parse
 *  @param mode     'P' to parse or 'C' (or any other string) to combine
 */
ParseExportsSection: procedure expose (Globals)

    parse arg file, mode

    if (mode \== 'P') then mode = 'C'

    if (mode == 'P') then do
        drop G.!Exports.
        G.!Exports.0 = 0 /* # of exports  */
        G.!Exports.!file = file
        G.!Exports.!maxOrd = 0
    end

    ln = 0

    global_magicseg = ''

    do while lines(file)

        ln = ln + 1
        str = linein(file)
        call TokenizeString str, 'G.!line'

        if (G.!line.0 == 0) then iterate

        w = 1
        outdated = 0

        if (mode == 'P') then do
            if (G.!line.w == ';@@@') then do
                /* skip the magic 'outdated symbol' mark */
                w = w + 1
                outdated = 1
            end
            else if (G.!line.w == ';') then do
                /* test for a global magicseg definition */
                w = w + 1
                if (left(G.!line.w, 10) == "magicseg='" &,
                    right(G.!line.w, 1) == "'") then do
                    global_magicseg = substr(G.!line.w, 11, length(G.!line.w) - 11)
                    w = w + 1
                end
                else
                    global_magicseg = ''
                iterate
            end
        end

        if (G.!line.0 < w | left(G.!line.w, 1) == ';') then iterate

        symbol = G.!line.w
        if (symbol = '_DLL_InitTerm') then iterate

        w = w + 1
        ordinal = ''

        if (mode == 'P') then do
            if (G.!line.0 >= w) then
                parse value(G.!line.w) with '@'ordinal
            if (\datatype(ordinal, 'W') | ordinal <= 0) then
                call Error "E_InvalidOrdinal", symbol, ordinal, file, ln
            w = w + 1
            if (G.!line.0 < w | translate(G.!line.w) \== 'NONAME') then
                call Error "E_NoNoname", symbol, file, ln
            w = w + 1
        end

        magicseg = ''
        weak = 0

        if (G.!line.0 >= w + 1) then do
            if (G.!line.w == ';') then do
                w = w + 1
                if (left(G.!line.w, 10) == "magicseg='" &,
                    right(G.!line.w, 1) == "'") then do
                    magicseg = substr(G.!line.w, 11, length(G.!line.w) - 11)
                    w = w + 1
                end
                do i = w to G.!line.0
                    if (G.!line.i == 'weak') then do
                        weak = 1
                        leave
                    end
                end
            end
        end

        if (magicseg == '') then magicseg = global_magicseg

        select
            when (magicseg == 'BSS32' | magicseg == 'DATA32') then nop
            when (magicseg == 'TEXT32') then nop
            otherwise
                call Error "E_InvalidMagicSeg", symbol, magicseg, file, ln
        end

        if (mode == 'P') then do
            n = G.!Exports.0 + 1
            G.!Exports.0 = n
            G.!Exports.n = symbol
            G.!Exports.n.!seg = magicseg
            G.!Exports.n.!weak = weak
            G.!Exports.n.!ord = ordinal
            if (outdated) then
                G.!Exports.n.!type = 'OO' /* mark as twice outdated */
            else
                G.!Exports.n.!type = 'O' /* mark everything else as outdated */
            signal off novalue
            o = G.!Exports.symbol
            signal on novalue
            if (datatype(o, 'W')) then
                call Error "E_DuplicateSymbol", symbol, file, ln
            G.!Exports.symbol = n /* back reference! */
            if (G.!Exports.!maxOrd < ordinal) then
                G.!Exports.!maxOrd = ordinal
        end
        else do
            signal off novalue
            n = G.!Exports.symbol
            signal on novalue
            if (datatype(n, 'W')) then do
                /* existing symbol */
                if (n > 0 & n <= G.!Exports.0) then do
                    if (G.!Exports.n.!type == 'OO') then
                        G.!Exports.n.!type = 'R' /* mark as resurrected */
                    else
                        G.!Exports.n.!type = '' /* remove outdated mark */
                end
                else signal Nonsense
            end
            else do
                /* new symbol */
                ordinal = G.!Exports.!maxOrd + 1
                G.!Exports.!maxOrd = ordinal
                n = G.!Exports.0 + 1
                G.!Exports.0 = n
                G.!Exports.n = symbol
                G.!Exports.n.!seg = magicseg
                G.!Exports.n.!weak = weak
                G.!Exports.n.!ord = ordinal
                G.!Exports.n.!type = 'N' /* mark all as new */
                /* no need in back reference */
            end
        end

    end

    call lineout file

    return

/**
 *  Appends exports from the G.!Exports stem to the existing file.
 *  The generated contents corresponds to the format of the map file
 *  described in ParseExportsSection.
 *
 *  @param file     file to append to
 */
AppendExportsToFile: procedure expose (Globals)

    parse arg file

    weak = 0
    new = 0
    renew = 0
    outdated = 0

    magicseg = ''

    do n = 1 to G.!Exports.0
        prefix = '  '
        suffix = ''
        if (G.!Exports.n.!weak) then do
            suffix = suffix' weak'
            weak = weak + 1
        end
        if (G.!Exports.n.!type == 'N') then do
            suffix = suffix' new'
            new = new + 1
        end
        else if (G.!Exports.n.!type == 'R') then do
            suffix = suffix' renew'
            renew = renew + 1
        end
        else if (G.!Exports.n.!type == 'O' |,
                 G.!Exports.n.!type == 'OO') then do
            prefix = prefix';@@@ '
            outdated = outdated + 1
        end
        seg = G.!Exports.n.!seg
        if (magicseg \= seg) then do
            magicseg = ''
            /* look at next 2 exports to see what magicseg do they have */
            if (n + 2 <= G.!Exports.0) then do
                if (value('G.!Exports.'||(n + 1)||'.!seg') == seg &,
                    value('G.!Exports.'||(n + 2)||'.!seg') == seg) then do
                    magicseg = seg
                    call lineout file, "  ; magicseg='"magicseg"'"
                end
            end
        end

        if (magicseg \== '') then do
            if (suffix \== '') then suffix = ' ;'suffix
        end
        else suffix = " ; magicseg='"G.!Exports.n.!seg"'"suffix

        call lineout file,,
            prefix||G.!Exports.n '@'||G.!Exports.n.!ord 'NONAME'||,
            suffix
    end

    return weak' 'new' 'renew' 'outdated

/**
 *  Parses the command line and sets up the corresponding global
 *  variables.
 *
 *  @param string  the command line to parse
 */
ParseCommandLine: procedure expose (Globals)

    parse arg options

    printbldlevel = 0

    call TokenizeString options, 'G.!opts'

    /* first pass: read responce files */
    resp_options = ''
    do i = 1 to G.!opts.0
        if (left(G.!opts.i, 1) == '@') then do
            o = substr(G.!opts.i, 2)
            if (stream(o, 'C', 'QUERY EXISTS') == '') then
                call Error "E_FileNotFound", o
            resp_options = resp_options || charin(o,, chars(o))
        end
    end
    if (resp_options \== '') then do
        /* retokenize options */
        call TokenizeString resp_options || options, 'G.!opts'
    end

    /* add an empty word after the last option */
    call value 'G.!opts.'||(G.!opts.0 + 1), ''

    /* second pass: read direct options */
    do i = 1 to G.!opts.0
        if (left(G.!opts.i, 1) == '@') then iterate
        if (left(G.!opts.i, 1) == '-') then do
            o = substr(G.!opts.i, 2)
            select
                when (wordpos(o, 'h H ? help') > 0) then call Usage
                when (o == 'rawhelp') then call Usage 1
                when (o == 'def') then do
                    i = i + 1
                    Opt.!DefFile = G.!opts.i
                end
                when (o == 'name') then do
                    i = i + 1
                    Opt.!LibName = G.!opts.i
                end
                when (o == 'version') then do
                    i = i + 1
                    Opt.!LibVersion = G.!opts.i
                end
                when (o == 'desc') then do
                    i = i + 1
                    Opt.!LibDesc = G.!opts.i
                end
                when (o == 'vendor') then do
                    i = i + 1
                    Opt.!LibVendor = G.!opts.i
                end
                when (o == 'template') then do
                    i = i + 1
                    Opt.!DefTemplate = G.!opts.i
                end
                when (o == 'map') then do
                    i = i + 1
                    Opt.!DefMap = G.!opts.i
                end
                when (o == 'exe') then do
                    i = i + 1
                    Opt.!ExeType = G.!opts.i
                end
                when (o == 'q') then do
                    Opt.!Quiet = 1
                end
                when (o == 'printbldlevel') then do
                    printbldlevel = 1
                end
                otherwise
                    call Error "E_InvalidOption", o
            end
        end
        else do
            n = Opt.!Objects.0 + 1
            Opt.!Objects.n = G.!opts.i
            Opt.!Objects.0 = n
        end
    end

    /* drop the contents of our global stem */
    do i = 1 to G.!opts.0
        drop G.!opts.i
    end
    drop G.!opts.0

    /* compose the BLDLEVEL string using the extended syntax */

    parse var Opt.!LibVersion rev1'.'rev2'.'ver3'.'feature
    if (\datatype(rev1, 'N')) then do
        feature = Opt.!LibVersion
        rev1 = 0
        rev2 = 0
        ver3 = ''
    end
    else if (\datatype(rev2, 'N')) then do
        feature = substr(Opt.!LibVersion, length(rev1) + 2)
        rev2 = 0
        ver3 = ''
    end
    else if (\datatype(ver3, 'N')) then do
        feature = substr(Opt.!LibVersion, length(rev1) + length(rev2) + 3)
        ver3 = ''
    end

    timestamp = right(date('N'), 11, ' ') time('N')

    Opt.!LibBldLevel = '@#'Opt.!LibVendor':'rev1'.'rev2'#@##1## '||,
                       timestamp'    :'feature':::'ver3'::@@'Opt.!LibDesc

    if (printbldlevel) then do
        /* this is a magic option that causes BldLevel to be printed and exits */
        say Opt.!LibBldLevel
        call Done 0
    end

    return

/**
 *  Prints the usage information.
 */
Usage: procedure expose (Globals)

    parse arg just_say

    just_say = (just_say == 1)
    if (\just_say) then do
        address 'cmd' 'call' G.!SourceFile '-rawhelp | more'
        call Done 0
    end

    say G.!Title
    say
    say 'Usage: 'filespec('N', G.!SourceFile)' <options> <input_file>... [@<response_file>...]'
    say
    say 'Options:'
    say
    say '   -def <file>             : Output DEF file [required]'
    say '   -name <string>          : Library name [default: DEF file name w/o ext]'
    say '   -version <string>       : Library version [default: 0]'
    say '   -desc <string>          : Library description [default: none]'
    say '   -vendor <string>        : Library vendor [default: "unknown"]'
    say '   -template <file>        : DEF file template [default: none]'
    say '   -map <file>             : Input/output map file [default: none]'
    say '   -exe <string>           : Executable type [default: none]'
/*     say '   -q                      : Be quiet [default: be loud]' */
    say
    say '<input_file> is one or more object files to process (wildcards'
    say 'are NOT supported). This argument may be omitted in -exe mode (see below)'
    say 'or if it is only necessary to attach the description to the DLL. If'
    say '-map is specified, this argument may not be omitted.'
    say
    say '<response_file> contains options and input files delimited with'
    say 'spaces, tabs or new line charcters.  Options from response files'
    say 'are processed first, so command line options override them.'
    say
    say 'The default export mode is "export by name".  In this mode, every public'
    say 'symbol is exported using its mangled name and ordinals are not explicitly'
    say 'assigned.  This mode is suitable for exporting a relatively small amount'
    say 'of symbols.'
    say
    say 'Specifying a non-empty -map option turns on the "export by ordinal"'
    say 'mode.  In this mode, public symbols are exported by ordinals only,'
    say 'which is very efficient when there are thousands of symbols.  The given'
    say 'map file is used to store ordinals assigned to every symbol in order to'
    say 'make these assignments persistent from build to build.'
    say
    say 'If the -exe option is given, the script generates a .DEF file for the'
    say 'executable rather than for the DLL, which is useful for specifying the'
    say 'executable type (passed as an option value, either WINDOWCOMPAT or WINDOWAPI)'
    say 'and for embedding the application description into the executable. The -map'
    say 'option is not allowed in -exe mode.'
    say
    say 'If the -template option is not empty, the given file is used as a template'
    say 'to generate the resulting DEF file.  The following macros are recognized:'
    say
    say '-  ${name} is replaced with the library name;'
    say '-  ${version} is replaced with the library version string;'
    say '-  ${desc} is replaced with the library description string;'
    say '-  ${vendor} is replaced with the library vendor string;'
    say '-  ${exports} is replaced with the list of exported entries.'
    say
    say 'In -exe mode, the following macro is additionally recognized:'
    say
    say '-  ${exetype} is replaced with the specified EXE type.'
    say
    say 'In addition to the above, there is also the ${bldlevel} macro that is'
    say 'replaced with a string in the format expected by the BLDLEVEL utility'
    say 'composed of the values passed with -version, -desc and -vendor options.'
    say 'This macro is typically used with the DESCRIPTION statement in the .DEF'
    say 'template. Note that for this macro to work correctly, the version must'
    say 'be specified in the following format: "X.Y[.Z[.Extra]]" where X, Y and Z'
    say 'are integer numbers and Extra is an arbitrary string.'
    say

    call Done 0

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
 *  Returns the name of the temporary directory.
 *  The returned value doesn't end with a slash.
 */
GetTempDir: procedure expose (Globals)
    dir = value('TEMP',,'OS2ENVIRONMENT')
    if (dir == '') then dir = value('TMP',,'OS2ENVIRONMENT')
    if (dir == '') then dir = SysBootDrive()
    return dir

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
 *  Replaces argument markers %1, %2 (up to %Z) in the given string
 *  with the values of the remaining arguments.
 *
 *  @param string     string containing argument markers to replace
 *  @param ...        arguments to replace markers with values
 *
 *  @version 1.1
 */
ReplaceArgs: procedure expose (Globals)
    parse arg str
    do i = 2 to arg()
        c = i - 1
        if (c >= 10) then c = d2c(65 + (c - 10))
        str = Replace(str, '%'c, arg(i))
    end
    return str

/**
 *  Prints the error or warning message corresponding to the first
 *  argument (error code). If it starts with 'E_' an error message is
 *  assumed, if it starts with 'W_' -- it's a warning message.
 *
 *  The special error code 'E_OK' prints 'all done' and exits the
 *  script.
 *
 *  @param code     error or warning code (as a string)
 *  @param ...
 *      arbitrary amount of arguments with additional error information.
 *      These arguments are used to substitute %1, %2 (upto %9) markers
 *      in the error message text.
 */
Error: procedure expose (Globals)

    eol = '0D0A'x

    e.E_CantFindEMXEXP   = 'Cannot find EMXEXP.EXE in the PATH.'
    e.E_CantOpenFile     = 'Cannot open file ''%1'' (RC = %2).'
    e.E_CantDeleteFile   = 'Cannot delete file ''%1'' (RC = %2).'
    e.E_InvalidOption    = 'Invalid option: ''%1''.'
    e.E_UnexpectedOption = 'Unexpected option: ''%1''.'
    e.E_FileNotFound     = 'File not found: ''%1''.'
    e.E_NoDefFile        = 'No output DEF file.'
    e.E_NoObjectFiles    = 'No input object files.'
    e.E_EMXEXPFailed     = 'EMXEXP.EXE failed with RC = %1.'
    e.E_NoExports        =,
        'The specified list of objects doesn''t export any symbols.'
    e.E_InvalidMagicSeg  =,
        'Invalid or missing magic segment ''%2'' for symbol "%1" in ''%3'' (line %4).'
    e.E_InvalidOrdinal   =,
        'Invalid or missing ordinal ''%2'' for symbol "%1" in ''%3'' (line %4).'
    e.E_NoNoname         =,
        'No NONAME keyword for symbol "%1" in ''%2'' (line %3).'
    e.E_DuplicateSymbol  =,
        'Duplicate symbol symbol "%1" detected in ''%2'' (line %3).'

    parse upper arg code

    str = value('e.'code)
    do i = 2 to arg()
        str = Replace(str, '%'||(i-1), arg(i))
    end

    if (left(code,2) == 'E_') then str = 'ERROR: 'str
    else if (left(code,2) == 'W_') then str = 'WARNING: 'str
    else str = 'Something is really wrong...'

    if (word(SysCurPos(), 2) > 0) then say

    say str

    if (left(code,2) == 'E_') then call done 1

    return code

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
 *
 *  @param code     exit code
 */
Done: procedure expose (Globals)
    parse arg code
    /* protect against recursive calls */
    if (value('G.!Done_done') == 1) then exit code
    call value 'G.!Done_done', 1
    /* cleanup stuff goes there */
    if (symbol('G.!ObjectListFile') == 'VAR') then
        if (G.!ObjectListFile \== '') then call SysFileDelete G.!ObjectListFile
    /* finally, exit */
    exit code

