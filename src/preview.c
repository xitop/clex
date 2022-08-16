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

#include <stdarg.h>
#include <stdio.h>

#include "preview.h"

#include "filerw.h"
#include "log.h"
#include "mbwstring.h"

static const char *
expand_tabs(const char *str)
{
	char ch, *out;
	int len, cnt;
	static USTRING buff = UNULL;

	for (cnt = len = 0; (ch = str[len++]); )
		if (ch == '\t')
			cnt++;
	if (cnt == 0)
		return str;

	us_setsize(&buff, len + 7 * cnt);
	out = USTR(buff);
	for (len = 0; (ch = *str++); ) {
		if (ch == '\t')
			do {
				out[len++] = ' ';
			} while (len % 8);
		else
			out[len++] = ch;
	}
	out[len] = '\0';

	return out;
}

int
preview_prepare(void)
{
	int i, tfd;
	const char *line;
	FILE_ENTRY *pfe;

	pfe = ppanel_file->files[ppanel_file->pd->curs];

	if (!IS_FT_PLAIN(pfe->file_type)) {
		msgout(MSG_i,"PREVIEW: not a regular file");
		return -1;
	}

	tfd = fr_open_preview(SDSTR(pfe->file), PREVIEW_BYTES);
	if (tfd < 0) {
		msgout(MSG_i,"PREVIEW: unable to read the file, details in log");
		return -1;
	}
	if (!fr_is_text(tfd)) {
		msgout(MSG_i, "PREVIEW: not a text file");
		fr_close(tfd);
		return -1;
	}
	fr_split_preview(tfd,PREVIEW_LINES);
	for (i = 0; (line = fr_line(tfd,i)); i++)
		usw_convert2w(expand_tabs(line), &panel_preview.line[i]);
	panel_preview.pd->cnt = panel_preview.realcnt = i;
	if (fr_is_truncated(tfd))
		panel_preview.pd->cnt++;
	fr_close(tfd);

	panel_preview.pd->top = panel_preview.pd->curs = 0;
	panel_preview.title = SDSTR(pfe->filew);

	panel = panel_preview.pd;
	textline = 0;
	return 0;
}

void
cx_preview_mouse(void)
{
	if (MI_AREA(PANEL) && MI_DC(1)) {
		next_mode = MODE_SPECIAL_RETURN;
		minp.area = AREA_NONE;
	}
}
