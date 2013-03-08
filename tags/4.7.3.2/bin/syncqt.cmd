/*--------------------------------------------------------------------
 *
 * Synchronizes Qt header files - internal development tool.
 *
 * Copyright (C) 2011 Nokia Corporation and/or its subsidiary(-ies).
 * Contact: Nokia Corporation (qt-info@nokia.com)
 *
 *------------------------------------------------------------------*/
parse source . . Script
ScriptDir = strip(filespec('D', Script) || filespec('P', Script), 'T', '\')
parse arg aArgs
'@set QTDIR='ScriptDir'\..'
'@call perl -S syncqt' aArgs
exit rc
