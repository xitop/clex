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

#include <langinfo.h>	/* nl_langinfo() */
#include <locale.h>		/* setlocale() */
#include <stdarg.h>		/* log.h */
#include <string.h>		/* strlen() */

#include "lang.h"

#include "log.h"		/* msgout() */
#include "mbwstring.h"	/* convert2w() */
#include "util.h"		/* ewcsdup() */

/* thousands separator */
static wchar_t
sep000(void)
{
	const char *info;

	info = nl_langinfo(THOUSEP);
	if (strlen(info) == 1) {
		if (info[0] == '.')
			return L'.';
		if (info[0] == ',')
			return L',';
	}
	msgout(MSG_DEBUG,"LOCALE: the thousands separator is neither dot nor comma, "
	  "CLEX will use the opposite of the radix character");
	info = nl_langinfo(RADIXCHAR);
	if (strlen(info) == 1) {
		if (info[0] == '.')
			return L',';
		if (info[0] == ',')
			return L'.';
	}
	msgout(MSG_NOTICE,"LOCALE: the radix character is neither dot nor comma");
	return L'.';
}

void
locale_initialize(void)
{
	const char *tf, *df;

	if (setlocale(LC_ALL,"") == 0)
		msgout(MSG_W,"LOCALE: cannot set locale");

	lang_data.utf8 = strcmp(nl_langinfo(CODESET),"UTF-8") == 0;
	lang_data.repl = lang_data.utf8 ? L'\xFFFD' : L'?';
	lang_data.sep000 = sep000();
	tf = nl_langinfo(T_FMT);
	df = nl_langinfo(D_FMT);
	lang_data.time_fmt = ewcsdup(convert2w(tf));
	lang_data.date_fmt = ewcsdup(convert2w(df));
	msgout(MSG_DEBUG,"LOCALE: standard time format: \"%s\", standard date format: \"%s\"",tf,df);
}
