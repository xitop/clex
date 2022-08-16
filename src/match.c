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

#include <fnmatch.h>		/* fnmatch */
#include <wctype.h>			/* towlower() */

#include "match.h"

#include "mbwstring.h"		/* us_convert2mb() */

/* match pattern */

static USTRING pattern = UNULL;

void
match_pattern_set(const wchar_t *expr)
{
	us_convert2mb(expr,&pattern);
}

int
match_pattern(const char *word)
{
	return fnmatch(USTR(pattern),word,FOPT(FOPT_ALL) ? 0 : FNM_PERIOD) == 0;
}

/* match substring */

static USTRINGW substr_orig = UNULL;	/* original */
static USTRINGW substr_lc   = UNULL;	/* lowercase copy */
static FLAG lc;							/* substr_lc is valid */

static void
inplace_tolower(wchar_t *p)
{
	wchar_t ch;

	for (; (ch = *p) != L'\0'; p++)
		if (iswupper(ch))
			*p = towlower(ch);
}

void
match_substr_set(const wchar_t *expr)
{
	usw_copy(&substr_orig,expr);
	lc = 0;
}

int
match_substr(const wchar_t *str)
{
	if (FOPT(FOPT_IC))
		return match_substr_ic(str);

	return wcsstr(str,USTR(substr_orig)) != 0;
}

int
match_substr_ic(const wchar_t *str)
{
	static USTRINGW buff = UNULL;
	wchar_t *str_lc;

	if (!lc) {
		inplace_tolower(usw_copy(&substr_lc,USTR(substr_orig)));
		lc = 1;
	}

	str_lc = usw_copy(&buff,str);
	inplace_tolower(str_lc);
	return wcsstr(str_lc,USTR(substr_lc)) != 0;
}
