/* REXX */
/*
 * Works around stupid xcopy interactivity.
 *
 * (c) too simple to be copyrighted
 */

'@echo off'

parse arg aArgs

aArgs = strip(aArgs)
if (aArgs == '') then do
    say "Usage: cpdir <source> <target>"
    exit 255
end

if (right(aArgs, 1) \== '\') then aArgs = aArgs'\'
'xcopy /s /e /o' aArgs

exit rc
