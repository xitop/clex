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

#include <sys/stat.h>	/* stat() */
#include <fcntl.h>		/* open() */
#include <stdlib.h>		/* qsort() */
#include <string.h>		/* strcmp() */
#include <unistd.h>		/* close() */

#include "select.h"

#include "control.h"	/* get_current_mode() */
#include "edit.h"		/* edit_nu_putstr() */
#include "inout.h"		/* win_panel() */
#include "list.h"		/* list_directory_cond() */
#include "match.h"		/* match_pattern() */

static SDSTRINGW savepat[2] = { SDNULL(L"*"), SDNULL(L"*") };
static FLAG mode_sel;		/* 1 = select, 0 = deselect */

int
select_prepare(void)
{
	if (list_directory_cond(PANEL_EXPTIME) == 0)
		win_panel();

	mode_sel = get_current_mode() == MODE_SELECT;	/* 0 or 1 */

	panel = ppanel_file->pd;
	if (panel->filtering == 1)
		panel->filtering = 2;

	edit_setprompt(&line_tmp,mode_sel ? L"SELECT files: " : L"DESELECT files: ");
	textline = &line_tmp;
	edit_nu_putstr(SDSTR(savepat[mode_sel]));

	return 0;
}

void
cx_select_toggle(void)
{
	ppanel_file->selected +=
	  TOGGLE(ppanel_file->files[ppanel_file->pd->curs]->select) ? +1 : -1;

	/* cursor down */
	if (ppanel_file->pd->curs < ppanel_file->pd->cnt - 1) {
		ppanel_file->pd->curs++;
		LIMIT_MIN(ppanel_file->pd->top,
		  ppanel_file->pd->curs - disp_data.panlines + 1);
	}
	win_panel();
}

void
cx_select_range(void)
{
	int i;
	FLAG mode;	/* same meaning as mode_sel */

	for (mode = !ppanel_file->files[i = ppanel_file->pd->curs]->select;
	  i >= 0 && ppanel_file->files[i]->select != mode; i--)
		ppanel_file->files[i]->select = mode;
	ppanel_file->selected += mode
	  ? ppanel_file->pd->curs - i : i - ppanel_file->pd->curs;
	win_panel();
}

void
cx_select_invert(void)
{
	int i;

	for (i = 0; i < ppanel_file->pd->cnt; i++)
		TOGGLE(ppanel_file->files[i]->select);
	ppanel_file->selected = ppanel_file->pd->cnt - ppanel_file->selected;
	win_panel();
}

void
cx_select_files(void)
{
	int i, cnt;
	const wchar_t *sre;
	FILE_ENTRY *pfe;

	next_mode = MODE_SPECIAL_RETURN;
	if (line_tmp.size == 0)
		return;

	sre = USTR(textline->line);
	match_pattern_set(sre);

	/* save the pattern */
	sdw_copy(savepat + mode_sel,sre);

	for (i = cnt = 0; i < ppanel_file->pd->cnt; i++) {
		pfe = ppanel_file->files[i];
		if (pfe->select != mode_sel && match_pattern(SDSTR(pfe->file))) {
			pfe->select = mode_sel;
			cnt++;
		}
	}
	ppanel_file->selected += mode_sel ? +cnt : -cnt;
	win_panel();
}
