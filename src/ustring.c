/*
 *
 * CLEX File Manager
 *
 * Copyright (C) 2001-2022 Vlado Potisk
 *
 * CLEX is free software without warranty of any kind; see the
 * GNU General Public License as set out in the "COPYING" document
 * which accompanies the CLEX File Manager package.
 *
 * CLEX can be downloaded from https://github.com/xitop/clex
 *
 */

#include "clexheaders.h"

#include <stdarg.h>				/* va_list */
#include <stdlib.h>				/* free() */
#include <string.h>				/* strlen() */

#include "util.h"				/* emalloc() */

/*
 * The USTRING structure (defined in ustring.h) can store a string
 * of unlimited length. The memory is allocated dynamically.
 *
 * - to initialize (to NULL ptr value) before first use:
 *   - static and global variables are initialized by default,
 *     but if you prefer explicit initialization:
 *       static USTRING us = UNULL;
 *         which is equivalent to
 *       static USTRING us = { 0, 0 };
 *   - dynamic variables are initialized this way:
 *     US_INIT(ustring);
 * - to re-initialize, e.g. before deallocating dynamic USTRING:
 *     us_reset();
 * - to store a string: (us_copy accepts also a null ptr)
 *     us_copy();
 *       or
 *     us_copyn();
 * - to retrieve a string:
 *     USTR(us)
 *       or
 *     PUSTR(pus)
 * - to edit stored name:
 *     a) allocate enough memory with us_setsize() or us_resize()
 *     b) edit the string starting at USTR() location
 *
 * WARNING: US_INIT, USTR, and PUSTR are macros
 */

/* these are tunable parameters */
/* ALLOC_UNIT in ustring.h */
#define MINIMUM_FREE		(4 * ALLOC_UNIT)

/*
 * SHOULD_CHANGE_ALLOC() is true if
 *  1) we need more memory, or
 *  2) we can free considerable amount of memory (MINIMUM_FREE)
 */
#define SHOULD_CHANGE_ALLOC(RQ)	\
	(pustr->USalloc < RQ || pustr->USalloc >= RQ + MINIMUM_FREE)

/* memory is allocated in chunks to prevent excessive resizing */
#define ROUND_ALLOC(RQ)	\
	((1 + (RQ - 1) / ALLOC_UNIT) * ALLOC_UNIT)
/* note that ROUND_ALLOC(0) is ALLOC_UNIT and not 0 */

/* clear the data and free the memory */
void
us_reset(USTRING *pustr)
{
	if (pustr->USalloc) {
		free(pustr->USstr);
		pustr->USalloc = 0;
	}
	pustr->USstr = 0;
}

/* wchar version */
void
usw_reset(USTRINGW *pustr)
{
	if (pustr->USalloc) {
		free(pustr->USstr);
		pustr->USalloc = 0;
	}
	pustr->USstr = 0;
}

/* us_setsize() makes room for at least 'req' characters */
size_t
us_setsize(USTRING *pustr, size_t req)
{
	if (SHOULD_CHANGE_ALLOC(req)) {
		if (pustr->USalloc)
			free(pustr->USstr);
		pustr->USalloc = ROUND_ALLOC(req);
		pustr->USstr = emalloc(pustr->USalloc);
	}

	return pustr->USalloc;	/* real buffer size is returned */
}

/* wchar version; note that 'req' is in characters, not bytes */
size_t
usw_setsize(USTRINGW *pustr, size_t req)
{
	if (SHOULD_CHANGE_ALLOC(req)) {
		if (pustr->USalloc)
			free(pustr->USstr);
		pustr->USalloc = ROUND_ALLOC(req);
		pustr->USstr = emalloc(sizeof(wchar_t) * pustr->USalloc);
	}

	return pustr->USalloc;
}

/* like us_setsize(), but preserving contents */
size_t
us_resize(USTRING *pustr, size_t req)
{
	if (SHOULD_CHANGE_ALLOC(req)) {
		pustr->USalloc = ROUND_ALLOC(req);
		pustr->USstr = erealloc(pustr->USstr,pustr->USalloc);
	}

	return pustr->USalloc;
}


/* wchar version */
size_t
usw_resize(USTRINGW *pustr, size_t req)
{
	if (SHOULD_CHANGE_ALLOC(req)) {
		pustr->USalloc = ROUND_ALLOC(req);
		pustr->USstr = erealloc(pustr->USstr,sizeof(wchar_t) * pustr->USalloc);
	}

	return pustr->USalloc;
}

/* quick alternative to copy */
void
us_xchg(USTRING *s1, USTRING *s2)
{
	char *xstr;
	size_t xalloc;

	xstr      = s1->USstr;
	s1->USstr = s2->USstr;
	s2->USstr = xstr;

	xalloc      = s1->USalloc;
	s1->USalloc = s2->USalloc;
	s2->USalloc = xalloc;
}

/* wchar version */
void
usw_xchg(USTRINGW *s1, USTRINGW *s2)
{
	wchar_t *xstr;
	size_t xalloc;

	xstr      = s1->USstr;
	s1->USstr = s2->USstr;
	s2->USstr = xstr;

	xalloc      = s1->USalloc;
	s1->USalloc = s2->USalloc;
	s2->USalloc = xalloc;
}

char *
us_copy(USTRING *pustr, const char *src)
{
	if (src == 0) {
		us_reset(pustr);
		return 0;
	}
	us_setsize(pustr,strlen(src) + 1);
	strcpy(pustr->USstr,src);
	return pustr->USstr;
}

/* wchar version */
wchar_t *
usw_copy(USTRINGW *pustr, const wchar_t *src)
{
	if (src == 0) {
		usw_reset(pustr);
		return 0;
	}
	usw_setsize(pustr,wcslen(src) + 1);
	wcscpy(pustr->USstr,src);
	return pustr->USstr;
}

/* note: us_copyn() adds terminating null byte */
char *
us_copyn(USTRING *pustr, const char *src, size_t len)
{
	char *dst;

	us_setsize(pustr,len + 1);
	dst = pustr->USstr;
	dst[len] = '\0';
	while (len-- > 0)
		dst[len] = src[len];
	return dst;
}

/* wchar version */
wchar_t *
usw_copyn(USTRINGW *pustr, const wchar_t *src, size_t len)
{
	wchar_t *dst;

	usw_setsize(pustr,len + 1);
	dst = pustr->USstr;
	dst[len] = L'\0';
	while (len-- > 0)
		dst[len] = src[len];
	return dst;
}

/* concatenation: us_cat(&ustring, str1, str2, ..., strN, (char *)0); */
void
us_cat(USTRING *pustr, ...)
{
	size_t len;
	char *str;
	va_list argptr;

	va_start(argptr,pustr);
	for (len = 1; (str = va_arg(argptr, char *)); )
		len += strlen(str);
	va_end(argptr);
	us_setsize(pustr,len);

	va_start(argptr,pustr);
	for (len = 0; (str = va_arg(argptr, char *)); ) {
		strcpy(pustr->USstr + len,str);
		len += strlen(str);
	}
	va_end(argptr);
}

/* wchar version */
void
usw_cat(USTRINGW *pustr, ...)
{
	size_t len;
	wchar_t *str;
	va_list argptr;

	va_start(argptr,pustr);
	for (len = 1; (str = va_arg(argptr, wchar_t *)); )
		len += wcslen(str);
	va_end(argptr);
	usw_setsize(pustr,len);

	va_start(argptr,pustr);
	for (len = 0; (str = va_arg(argptr, wchar_t *)); ) {
		wcscpy(pustr->USstr + len,str);
		len += wcslen(str);
	}
	va_end(argptr);
}
