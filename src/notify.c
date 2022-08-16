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

#include "notify.h"

#include "inout.h"		/* win_panel_opt() */
#include "opt.h"		/* opt_changed() */

int
notif_prepare(void)
{
	panel_notif.pd->top = panel_notif.pd->curs = panel_notif.pd->min;
	panel = panel_notif.pd;
	textline = 0;
	return 0;
}

/* write options to a string */
const char *
notif_saveopt(void)
{
	int i, j;
	static char buff[NOTIF_TOTAL_ + 1];

	for (i = j = 0; i < NOTIF_TOTAL_; i++)
		if (NOPT(i))
			buff[j++] = 'A' + i;
	buff[j] = '\0';

	return buff;
}

/* read options from a string */
int
notif_restoreopt(const char *opt)
{
	int i;
	unsigned char ch;

	for (i = 0; i < NOTIF_TOTAL_; i++)
		NOPT(i) = 0;

	while ( (ch = *opt++) ) {
		if (ch < 'A' || ch >= 'A' + NOTIF_TOTAL_)
			return -1;
		NOPT(ch - 'A') = 1;
	}

	return 0;
}

void
cx_notif(void)
{
	TOGGLE(NOPT(panel_notif.pd->curs));
	opt_changed();
	win_panel_opt();
}
