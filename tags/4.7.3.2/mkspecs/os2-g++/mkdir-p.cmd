/* REXX */
/*
 * Equivalent of 'mkdir -p' on Linux (creates all missing path components)
 *
 * (c) too simple to be copyrighted
 */

'@echo off'

Globals = 'G.'

parse arg aArgs

/* @todo process quotes in arguments to make it possible to create several
 * directories at once */

aArgs = strip(aArgs)
call TokenizeString aArgs, 'G.Args'
if (G.Args.0 \= 1) then do
    say "Usage: mkdir-p <directory>"
    exit 255
end

exit MakeDir(strip(G.Args.1))

MakeDir: procedure expose (Globals)
    parse arg aDir
    aDir = translate(aDir, '\', '/')
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
        if (todo.i = '') then
            iterate
        if (i < todo.0 | (base \== '' & right(base,1) \== '\' &,
                                        right(base,1) \== ':')) then
            base = base'\'
        base = base||todo.i
        rc = SysMkDir(base)
        if (rc \= 0) then do
            say 'Could not create directory '''base'''!'
            say 'SysMkDir returned 'rc
            return rc
        end
    end
    return 0

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
