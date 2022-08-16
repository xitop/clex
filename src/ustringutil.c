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

/* USTRING enhanced versions of fixed buffer functions */

#include "clexheaders.h"

#include <errno.h>			/* errno */
#include <stdarg.h>			/* va_list */
#include <stdio.h>			/* vsnprintf() */
#include <string.h>			/* strlen() */
#include <unistd.h>			/* readlink() */

#include "ustringutil.h"

#ifndef va_copy
#  ifdef __va_copy
#    define va_copy __va_copy
#  else
#    define va_copy(dst,src) memcpy(&dst,&src,sizeof(va_list))
#  endif
#endif

/* USTRING version of getcwd() */
int
us_getcwd(USTRING *pustr)
{
	us_setsize(pustr,ALLOC_UNIT);
	for (;/* until return*/;) {
		if (getcwd(pustr->USstr,pustr->USalloc))
			return 0;
		if (errno != ERANGE)
			return -1;
		/* increase buffer */
		us_setsize(pustr,pustr->USalloc + ALLOC_UNIT);
	}
}

/* USTRING version of readlink() */
int
us_readlink(USTRING *pustr, const char *path)
{
	int len;

	us_setsize(pustr,ALLOC_UNIT);
	for (;/* until return*/;) {
		len = readlink(path,pustr->USstr,pustr->USalloc);
		if (len == -1)
			return -1;
		if (len < pustr->USalloc) {
			pustr->USstr[len] = '\0';
			return 0;
		}
		/* increase buffer */
		us_setsize(pustr,pustr->USalloc + ALLOC_UNIT);
	}
}

#define VPRINTF_MAX 512		/* max output length if vsnpritnf() does not behave correctly */
/* USTRING version of vprintf() */
void
us_vprintf(USTRING *pustr, const char *format, va_list argptr)
{
	int i, len, cnt;
	va_list ap;
	static FLAG conformsC99 = 0;

	/* roughly estimate the output size */
	len = strlen(format);
	for (cnt = i = 0; i < len - 1; i++)
		if (format[i] == '%')
			cnt++;
	us_setsize(pustr,len + cnt * ALLOC_UNIT);

	for (;/* until return */;) {
		va_copy(ap,argptr);
		len = vsnprintf(pustr->USstr,pustr->USalloc,format,ap);
		va_end(ap);
		if (len == -1) {
			if (conformsC99 || pustr->USalloc > VPRINTF_MAX) {
				us_cat(pustr,"INTERNAL ERROR: failed format string: \"",format,"\"",(char *)0);
				return;
			}
			us_setsize(pustr,pustr->USalloc + ALLOC_UNIT);
		}
		else if (len >= pustr->USalloc) {
			conformsC99 = 1;
			us_setsize(pustr,len + 1);
		}
		else
			return;
	}
}
