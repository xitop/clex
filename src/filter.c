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

#include <stdarg.h>			/* log.h */

#include "filter.h"

#include "bookmarks.h"		/* bm_panel_data() */
#include "control.h"		/* fopt_change() */
#include "completion.h"		/* compl_panel_data() */
#include "directory.h"		/* dir_panel_data() */
#include "filepanel.h"		/* files_condreread() */
#include "history.h"		/* hist_panel_data() */
#include "inout.h"			/* win_panel() */
#include "list.h"			/* file_panel_data() */
#include "log.h"			/* msgout() */
#include "match.h" 			/* match_substr_set() */
#include "mbwstring.h"		/* utf_iscomposing() */
#include "opt.h" 			/* opt_changed() */
#include "panel.h" 			/* pan_adjust() */
#include "userdata.h"		/* user_panel_data() */

int
fopt_prepare(void)
{
	panel_fopt.pd->top = panel_fopt.pd->curs = panel_fopt.pd->min;
	panel = panel_fopt.pd;
	textline = 0;
	return 0;
}

void
cx_fopt_enter(void)
{
	TOGGLE(FOPT(panel_fopt.pd->curs));
	fopt_change();
	opt_changed();
	win_panel_opt();
}

/* write options to a string */
const char *
fopt_saveopt(void)
{
	int i, j;
	static char buff[FOPT_TOTAL_ + 1];

	for (i = j = 0; i < FOPT_TOTAL_; i++)
		if (FOPT(i))
			buff[j++] = 'A' + i;
	buff[j] = '\0';

	return buff;
}

/* read options from a string */
int
fopt_restoreopt(const char *opt)
{
	int i;
	unsigned char ch;

	for (i = 0; i < FOPT_TOTAL_; i++)
		FOPT(i) = 0;

	while ( (ch = *opt++) ) {
		if (ch < 'A' || ch >= 'A' + FOPT_TOTAL_)
			return -1;
		FOPT(ch - 'A') = 1;
	}

	return 0;
}

void
cx_filteredit_begin(void)
{
	panel->filter->curs = 0;
}

void
cx_filteredit_end(void)
{
	panel->filter->curs = panel->filter->size;
}

void
cx_filteredit_left(void)
{
	FLAG move = 1;

	while (move && panel->filter->curs > 0)
		move = utf_iscomposing(panel->filter->line[--panel->filter->curs]);
}

void
cx_filteredit_right(void)
{
	FLAG move = 1;

	while (move && panel->filter->curs < panel->filter->size)
		move = utf_iscomposing(panel->filter->line[++panel->filter->curs]);
}

void
cx_filteredit_kill(void)
{
	panel->filter->line[panel->filter->curs = panel->filter->size = 0] = L'\0';
	panel->filter->changed = 1;
	win_filter();
}

/* delete 'cnt' chars at cursor position */
static void
delete_chars(int cnt)
{
	int i;

	panel->filter->size -= cnt;
	for (i = panel->filter->curs; i <= panel->filter->size; i++)
		panel->filter->line[i] = panel->filter->line[i + cnt];
	panel->filter->changed = 1;
}

void
cx_filteredit_backsp(void)
{
	int pos;
	FLAG del = 1;

	if (panel->filter->curs == 0)
		return;

	pos = panel->filter->curs;
	/* composed UTF-8 characters: delete also the combining chars */
	while (del && panel->filter->curs > 0)
		del = utf_iscomposing(panel->filter->line[--panel->filter->curs]);
	delete_chars(pos - panel->filter->curs);
	win_filter();
}

void
cx_filteredit_delchar(void)
{
	int pos;
	FLAG del = 1;

	if (panel->filter->curs == panel->filter->size)
		return;

	pos = panel->filter->curs;
	/* composed UTF-8 characters: delete also the combining chars */
	while (del && panel->filter->curs < panel->filter->size)
		del = utf_iscomposing(panel->filter->line[++pos]);
	delete_chars(pos - panel->filter->curs);
	win_filter();
}

/* delete until the end of line */
void
cx_filteredit_delend(void)
{
	panel->filter->line[panel->filter->size = panel->filter->curs] = L'\0';
	panel->filter->changed = 1;
	win_filter();
}

/* make room for 'cnt' chars at cursor position */
static wchar_t *
insert_space(int cnt)
{
	int i;
	wchar_t *ins;

	if (panel->filter->size + cnt >= INPUT_STR)
		return 0;

	ins = panel->filter->line + panel->filter->curs;	/* insert new character(s) here */
	panel->filter->size += cnt;
	panel->filter->curs += cnt;
	for (i = panel->filter->size; i >= panel->filter->curs; i--)
		panel->filter->line[i] = panel->filter->line[i - cnt];

	panel->filter->changed = 1;
	return ins;
}

void
filteredit_nu_insertchar(wchar_t ch)
{
	wchar_t *ins;

	if ( (ins = insert_space(1)) )
		*ins = ch;
}

void
filteredit_insertchar(wchar_t ch)
{
	filteredit_nu_insertchar(ch);
	win_filter();
}

/* * * filter_update functions * * */

/* dir_panel_data() wrapper that preserves the cursor position */
static void
dir_panel_data_wrapper(void)
{
	int i, save_curs_rel;
	const char *save_curs_dir;

	/* save cursor */
	if (VALID_CURSOR(panel_dir.pd)) {
		save_curs_dir = panel_dir.dir[panel_dir.pd->curs].name;
		save_curs_rel = (100 * panel_dir.pd->curs) / panel_dir.pd->cnt;	/* percentage */
	}
	else {
		save_curs_dir = 0;
		save_curs_rel = 0;
	}

	/* apply filter */
	dir_panel_data();

	/* restore cursor */
	if (save_curs_dir)
		for (i = 0; i < panel_dir.pd->cnt; i++)
			if (panel_dir.dir[i].name == save_curs_dir) {
				panel_dir.pd->curs = i;
				return;
			}
	panel_dir.pd->curs = (save_curs_rel * panel_dir.pd->cnt) / 100;
}

/* match the whole help line */
static int
match_help(int ln)
{
	HELP_LINE *ph;
	int i;

	ph = panel_help.line[ln];
	if (match_substr_ic(ph->text))
		return 1;
	for (i = 0; i < ph->links; i++)
		if (match_substr_ic(ph[i * 3 + 2].text) || match_substr_ic(ph[i * 3 + 3].text))
			return 1;

	return 0;
}

static void
filter_update_help(void)
{
	int i;

	match_substr_set(panel_help.pd->filter->line);

	/* for "find" function is the 'changed' flag set; for "find next" is the flag unset */
	if (panel->filter->changed && match_help(panel_help.pd->curs))
		return;
	for (i = panel_help.pd->curs + 1; i < panel_help.pd->cnt; i++)
		if (match_help(i)) {
			panel_help.pd->curs = i;
			return;
		}
	msgout(MSG_i,"end of page reached, searching from the top");
	for (i = 0; i <= panel_help.pd->curs; i++)
		if (match_help(i)) {
			panel_help.pd->curs = i;
			return;
		}
	msgout(MSG_i,"text not found");
}

void
filter_update(void)
{
	switch (panel->type) {
	case PANEL_TYPE_BM:
		bm_panel_data();
		break;
	case PANEL_TYPE_COMPL:
		compl_panel_data();
		break;
	case PANEL_TYPE_DIR:
		dir_panel_data_wrapper();
		break;
	case PANEL_TYPE_FILE:
		file_panel_data();
		break;
	case PANEL_TYPE_GROUP:
		group_panel_data();
		break;
	case PANEL_TYPE_HELP:
		filter_update_help();
		break;
	case PANEL_TYPE_HIST:
		hist_panel_data();
		break;
	case PANEL_TYPE_LOG:
		log_panel_data();
		break;
	case PANEL_TYPE_USER:
		user_panel_data();
		break;
	default:
		;
	}
	panel->filter->changed = 0;
	pan_adjust(panel);
	win_panel();
}

void
filter_off(void)
{
	panel->filtering = 0;
	if (panel->type == PANEL_TYPE_HELP)
		win_panel_opt();
	else
		filter_update();
	win_filter();
	if (panel->curs < 0)
		win_infoline();
}

void
filter_help(void)
{
	if (panel->filtering == 1)
		win_sethelp(HELPMSG_OVERRIDE,panel->type != PANEL_TYPE_HELP ?
		  L"alt-O = filter options" : L"ctrl-F = find next");
	else
		win_sethelp(HELPMSG_OVERRIDE,0);
}

void
cx_filter(void)
{
	if (panel->filter == 0) {
		msgout(MSG_i,"this panel does not support filtering");
		return;
	}

	if (panel->filtering == 0) {
		if (panel->type == PANEL_TYPE_FILE) {
			if (list_directory_cond(PANEL_EXPTIME) == 0)
				win_panel();
			ppanel_file->filtype = 0;
		}
		panel->filtering = 1;
		cx_filteredit_kill();
		panel->filter->changed = 0;
		if (panel->type == PANEL_TYPE_HELP)
			win_panel_opt();
		if (panel->curs < 0)
			win_infoline();
	}
	else if (panel->filter->size == 0)
		filter_off();
	else if (panel->type == PANEL_TYPE_HELP)
		/* find next */
		filter_update();
	else if (textline == 0)
		filter_off();
	else
		/* switch focus: filter line (1) <--> input line (2) */
		panel->filtering = 3 - panel->filtering;

	filter_help();
}

/* filepanel filter controlled from the main menu */
void
cx_filter2(void)
{
	panel = ppanel_file->pd;
	cx_filter();
	panel = panel_mainmenu.pd;
}
