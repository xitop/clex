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

#include <stdlib.h>		/* free() */
#include <string.h>		/* strlen() */

#include "util.h"		/* estrdup() */

/*
 * The SDSTRING structure (defined in sdstring.h) was designed for
 * storing/retrieving of object names (e.g. file names or user
 * names).
 *
 * Up to 95% of such names are short (less than 16 chars), these
 * names are stored in static memory, long names are stored in
 * dynamically allocated memory.
 *
 * - to initialize before first use:
 *     static SDSTRING sds = SDNULL("short_string");
 *     static SDSTRINGW sds = SDNULL(L"short_string");
 *       or
 *     SD_INIT(sdstring);
 * - to re-initialize, e.g. before deallocating dynamic SDSTRING:
 *     sd_reset();
 * - to store a name (NULL ptr cannot be stored):
 *     sd_copy()
 *       or
 *     sd_copyn();
 * - to retrieve a name:
 *     SDSTR(sds)
 *       or
 *     PSDSTR(psds)
 *
 * WARNING: SD_INIT, SNULL, SDSTR, and PSDSTR are macros.
 */

void
sd_reset(SDSTRING *psd)
{
	if (psd->SDname) {
		free(psd->SDname);
		psd->SDname = 0;
	}
	psd->SDmem[0] = '\0';
}

void
sd_copy(SDSTRING *psd, const char *src)
{
	if (psd->SDname)
		free(psd->SDname);

	if (strlen(src) <= SDSTRING_LEN) {
		psd->SDname = 0;
		strcpy(psd->SDmem,src);
	}
	else {
		psd->SDname = estrdup(src);
		psd->SDmem[0] = '\0';
	}
}

/* note: sd_copyn() adds terminating null byte */
void
sd_copyn(SDSTRING *psd, const char *src, size_t len)
{
	char *dst;

	if (psd->SDname)
		free(psd->SDname);

	if (len <= SDSTRING_LEN) {
		psd->SDname = 0;
		dst = psd->SDmem;
	}
	else {
		dst = psd->SDname = emalloc(len + 1);
		psd->SDmem[0] = '\0';
	}

	dst[len] = '\0';
	while (len-- > 0)
		dst[len] = src[len];
}

void
sdw_reset(SDSTRINGW *psd)
{
	if (psd->SDname) {
		free(psd->SDname);
		psd->SDname = 0;
	}
	psd->SDmem[0] = L'\0';
}

#if 0
void
sdw_xchg(SDSTRINGW *psd1, SDSTRINGW *psd2)
{
	wchar_t *name, mem[SDSTRING_LEN + 1];

	name = psd2->SDname;
	psd2->SDname = psd1->SDname;
	psd1->SDname = name;

	wcscpy(mem,psd2->SDmem);
	wcscpy(psd2->SDmem,psd1->SDmem);
	wcscpy(psd1->SDmem,mem);
}
#endif

void
sdw_copy(SDSTRINGW *psd, const wchar_t *src)
{
	if (psd->SDname)
		free(psd->SDname);

	if (wcslen(src) <= SDSTRING_LEN) {
		psd->SDname = 0;
		wcscpy(psd->SDmem,src);
	}
	else {
		psd->SDname = ewcsdup(src);
		psd->SDmem[0] = L'\0';
	}
}

/* note: sdw_copyn() adds terminating null byte */
void
sdw_copyn(SDSTRINGW *psd, const wchar_t *src, size_t len)
{
	wchar_t *dst;

	if (psd->SDname)
		free(psd->SDname);

	if (len <= SDSTRING_LEN) {
		psd->SDname = 0;
		dst = psd->SDmem;
	}
	else {
		dst = psd->SDname = emalloc((len + 1) * sizeof(wchar_t));
		psd->SDmem[0] = L'\0';
	}

	dst[len] = L'\0';
	while (len-- > 0)
		dst[len] = src[len];
}
