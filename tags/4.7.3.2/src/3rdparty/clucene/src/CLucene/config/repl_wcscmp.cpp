/*------------------------------------------------------------------------------
* Copyright (C) 2003-2006 Ben van Klinken and the CLucene Team
* 
* Distributable under the terms of either the Apache License (Version 2.0) or 
* the GNU Lesser General Public License, as specified in the COPYING file.
------------------------------------------------------------------------------*/
#include "CLucene/StdHeader.h"

#if defined(_OS2) && defined(__INNOTEK_LIBC__)

// wcscmp() and wcsncmp() are broken in at least kLIBC <= 0.6.3 so provide a
// replacement. Note that defining _ASCII instead of _UCS2 will actually break
// fulltext search in CLucenee (looks like the _ASCII code path is no longer
// maintanied) so it's not an option.
extern "C" int wcscmp(const wchar_t *s1, const wchar_t *s2)
{
    while (*s1 == *s2++)
       if (*s1++ == 0)
           return (0);
    return ((int)*s1 - (int)*--s2);
}

extern "C" int wcsncmp(const wchar_t *s1, const wchar_t *s2, size_t n)
{
    if (n == 0)
        return (0);
    do {
        if (*s1 != *s2++) {
            return ((int)*s1 - (int)*--s2);
        }
        if (*s1++ == 0)
            break;
    } while (--n != 0);
    return (0);
}

#endif
