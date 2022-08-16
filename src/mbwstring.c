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

/* multibyte and wide string functions */

#include "clexheaders.h"

#include <stdlib.h>		/* mbstowcs() */
#include <string.h>		/* strlen() */
#include <wctype.h>		/* iswprint() in WCW macro */

#include "mbwstring.h"

/* wc_cols() returns width (in display columns) of a substring */
int
wc_cols(const wchar_t *str, int from, int to /* negative = till end */)
{
	int i, cols;
	wchar_t ch;

	for (cols = 0, i = from; to < 0 || i < to; i++) {
		if ((ch = str[i]) == L'\0')
			break;
		cols += WCW(ch);
	}
	return cols;
}

/*
 * multibyte to wide string conversion with error recovery,
 * the result is returned as exit value and also stored in
 * the USTRINGW structure 'dst'
*/
const wchar_t *
usw_convert2w(const char *str, USTRINGW *dst)
{
	int len, max, i, conv;
	const char *src;
	mbstate_t mbstate;

	/* try the easy way first */
	len = mbstowcs(0,str,0);
	if (len >= 0) {
		usw_setsize(dst,len + 1);
		mbstowcs(PUSTR(dst),str,len + 1);
		return PUSTR(dst);
	}

	/* there was an error, make a char-by-char conversion with error recovery */
	src = str;
	max = usw_setsize(dst,strlen(src) + 1);
	memset(&mbstate,0,sizeof(mbstate));
	for (i = 0; /* until return */; i++) {
		if (i == max)
			max = usw_resize(dst,max + ALLOC_UNIT);
		conv = mbsrtowcs(PUSTR(dst) + i,&src,1,&mbstate);
		if (conv == -1) {
			/* invalid sequence */
			src++;
			PUSTR(dst)[i] = lang_data.repl;
			memset(&mbstate,0,sizeof(mbstate));
		}
		else if (src == 0)
			return PUSTR(dst);		/* conversion completed */
	}

	/* NOTREACHED */
	return 0;
}

/* NOTE: the result is overwritten with each successive function call */
const wchar_t *
convert2w(const char *str)
{
	static USTRINGW local = UNULL;

	return usw_convert2w(str,&local);
}

/* wide to multibyte string conversion with error recovery */
const char *
us_convert2mb(const wchar_t *str,USTRING *dst)
{
	int len, max, i, conv;
	const wchar_t *src;
	mbstate_t mbstate;

	/* try the easy way first */
	len = wcstombs(0,str,0);
	if (len >= 0) {
		us_setsize(dst,len + 1);
		wcstombs(PUSTR(dst),str,len + 1);
		return PUSTR(dst);
	}

	/* there was an error, make a char-by-char conversion with error recovery */
	src = str;
	max = us_setsize(dst,wcslen(src) + 1);
	memset(&mbstate,0,sizeof(mbstate));
	for (i = 0; /* until return */; i++) {
		if (i == max)
			max = us_resize(dst,max + ALLOC_UNIT);
		conv = wcsrtombs(PUSTR(dst) + i,&src,1,&mbstate);
		if (conv == -1) {
			/* invalid sequence */
			src++;
			PUSTR(dst)[i] = '?';
			memset(&mbstate,0,sizeof(mbstate));
		}
		else if (src == 0)
			return PUSTR(dst);		/* conversion completed */
	}

	/* NOTREACHED */
	return 0;
}

/* NOTE: the result is overwritten with each successive function call */
const char *
convert2mb(const wchar_t *str)
{
	static USTRING local = UNULL;

	return us_convert2mb(str,&local);
}

/*
 * CREDITS: the utf_iscomposing() code including the intable() function
 * was taken with small modifications from the VIM text editor
 * written by Bram Moolenaar (www.vim.org)
 */

typedef struct {
    unsigned int first, last;
} INTERVAL;

/* return 1 if 'c' is in 'table' */
static int
intable (INTERVAL *table, size_t size, int c)
{
    int mid, bot, top;

    /* first quick check */
    if (c < table[0].first)
		return 0;

    /* binary search in table */
    bot = 0;
    top = size - 1;
    while (top >= bot) {
		mid = (bot + top) / 2;
		if (table[mid].last < c)
			bot = mid + 1;
		else if (table[mid].first > c)
			top = mid - 1;
		else
			return 1;
    }
    return 0;
}

/*
 * Return 1 if "ch" is a composing UTF-8 character. This means it will be
 * drawn on top of the preceding character.
 * Based on code from Markus Kuhn.
 */
int
utf_iscomposing(wchar_t ch)
{
    /* sorted list of non-overlapping intervals */
    static INTERVAL combining[] =
    {
	{0x0300, 0x034f}, {0x0360, 0x036f}, {0x0483, 0x0486}, {0x0488, 0x0489},
	{0x0591, 0x05a1}, {0x05a3, 0x05b9}, {0x05bb, 0x05bd}, {0x05bf, 0x05bf},
	{0x05c1, 0x05c2}, {0x05c4, 0x05c4}, {0x0610, 0x0615}, {0x064b, 0x0658},
	{0x0670, 0x0670}, {0x06d6, 0x06dc}, {0x06de, 0x06e4}, {0x06e7, 0x06e8},
	{0x06ea, 0x06ed}, {0x0711, 0x0711}, {0x0730, 0x074a}, {0x07a6, 0x07b0},
	{0x0901, 0x0903}, {0x093c, 0x093c}, {0x093e, 0x094d}, {0x0951, 0x0954},
	{0x0962, 0x0963}, {0x0981, 0x0983}, {0x09bc, 0x09bc}, {0x09be, 0x09c4},
	{0x09c7, 0x09c8}, {0x09cb, 0x09cd}, {0x09d7, 0x09d7}, {0x09e2, 0x09e3},
	{0x0a01, 0x0a03}, {0x0a3c, 0x0a3c}, {0x0a3e, 0x0a42}, {0x0a47, 0x0a48},
	{0x0a4b, 0x0a4d}, {0x0a70, 0x0a71}, {0x0a81, 0x0a83}, {0x0abc, 0x0abc},
	{0x0abe, 0x0ac5}, {0x0ac7, 0x0ac9}, {0x0acb, 0x0acd}, {0x0ae2, 0x0ae3},
	{0x0b01, 0x0b03}, {0x0b3c, 0x0b3c}, {0x0b3e, 0x0b43}, {0x0b47, 0x0b48},
	{0x0b4b, 0x0b4d}, {0x0b56, 0x0b57}, {0x0b82, 0x0b82}, {0x0bbe, 0x0bc2},
	{0x0bc6, 0x0bc8}, {0x0bca, 0x0bcd}, {0x0bd7, 0x0bd7}, {0x0c01, 0x0c03},
	{0x0c3e, 0x0c44}, {0x0c46, 0x0c48}, {0x0c4a, 0x0c4d}, {0x0c55, 0x0c56},
	{0x0c82, 0x0c83}, {0x0cbc, 0x0cbc}, {0x0cbe, 0x0cc4}, {0x0cc6, 0x0cc8},
	{0x0cca, 0x0ccd}, {0x0cd5, 0x0cd6}, {0x0d02, 0x0d03}, {0x0d3e, 0x0d43},
	{0x0d46, 0x0d48}, {0x0d4a, 0x0d4d}, {0x0d57, 0x0d57}, {0x0d82, 0x0d83},
	{0x0dca, 0x0dca}, {0x0dcf, 0x0dd4}, {0x0dd6, 0x0dd6}, {0x0dd8, 0x0ddf},
	{0x0df2, 0x0df3}, {0x0e31, 0x0e31}, {0x0e34, 0x0e3a}, {0x0e47, 0x0e4e},
	{0x0eb1, 0x0eb1}, {0x0eb4, 0x0eb9}, {0x0ebb, 0x0ebc}, {0x0ec8, 0x0ecd},
	{0x0f18, 0x0f19}, {0x0f35, 0x0f35}, {0x0f37, 0x0f37}, {0x0f39, 0x0f39},
	{0x0f3e, 0x0f3f}, {0x0f71, 0x0f84}, {0x0f86, 0x0f87}, {0x0f90, 0x0f97},
	{0x0f99, 0x0fbc}, {0x0fc6, 0x0fc6}, {0x102c, 0x1032}, {0x1036, 0x1039},
	{0x1056, 0x1059}, {0x1712, 0x1714}, {0x1732, 0x1734}, {0x1752, 0x1753},
	{0x1772, 0x1773}, {0x17b6, 0x17d3}, {0x17dd, 0x17dd}, {0x180b, 0x180d},
	{0x18a9, 0x18a9}, {0x1920, 0x192b}, {0x1930, 0x193b}, {0x20d0, 0x20ea},
	{0x302a, 0x302f}, {0x3099, 0x309a}, {0xfb1e, 0xfb1e}, {0xfe00, 0xfe0f},
	{0xfe20, 0xfe23},
    };

    return lang_data.utf8 && intable(combining,ARRAY_SIZE(combining),(int)ch);
}
