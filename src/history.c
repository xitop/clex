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

#include <stdarg.h>		/* log.h */
#include <stdlib.h>		/* free() */
#include <string.h>		/* strlen() */

#include "history.h"

#include "cfg.h"		/* cfg_num() */
#include "edit.h"		/* edit_putstr() */
#include "filter.h"		/* cx_filter() */
#include "inout.h"		/* win_panel() */
#include "lex.h"		/* cmd2lex() */
#include "log.h"		/* msgout() */
#include "match.h"		/* match_substr() */
#include "panel.h"		/* pan_adjust() */
#include "util.h"		/* emalloc() */

static HIST_ENTRY **history;/* command line history */
static int hs_alloc = 0;	/* number of allocated entries */
static int hs_cnt;			/* entries in use */
static int pn_index;		/* index for previous/next cmd */
static USTRINGW save_line = UNULL;
						/* for temporary saving of the command line */

void
hist_reconfig(void)
{
	int i;
	static HIST_ENTRY *storage;

	if (hs_alloc > 0) {
		for (i = 0; i < hs_alloc; i++)
			usw_reset(&history[i]->cmd);
		free(storage);
		free(history);
		free(panel_hist.hist);
	}

	hs_alloc = cfg_num(CFG_H_SIZE);
	storage = emalloc(hs_alloc * sizeof(HIST_ENTRY));
	history = emalloc(hs_alloc * sizeof(HIST_ENTRY *));
	panel_hist.hist = emalloc(hs_alloc * sizeof(HIST_ENTRY *));
	for (i = 0; i < hs_alloc; i++) {
		history[i] = storage + i;
		US_INIT(history[i]->cmd);
	}

	hs_cnt = 0;
	hist_reset_index();
}

void
hist_panel_data(void)
{
	int i, j;
	HIST_ENTRY *curs;

	curs = VALID_CURSOR(panel_hist.pd) ? panel_hist.hist[panel_hist.pd->curs] : 0;
	if (panel_hist.pd->filtering)
		match_substr_set(panel_hist.pd->filter->line);

	for (i = j = 0; i < hs_cnt; i++) {
		if (history[i] == curs)
			panel_hist.pd->curs = j;
		if (panel_hist.pd->filtering && !match_substr(USTR(history[i]->cmd)))
			continue;
		panel_hist.hist[j++] = history[i];
	}
	panel_hist.pd->cnt = j;
}

int
hist_prepare(void)
{
	panel_hist.pd->filtering = 0;
	panel_hist.pd->curs = -1;
	hist_panel_data();
	panel_hist.pd->top = panel_hist.pd->min;
	panel_hist.pd->curs = pn_index > 0 ? pn_index : 0;

	panel = panel_hist.pd;
	textline = &line_cmd;
	return 0;
}

const HIST_ENTRY *
get_history_entry(int i)
{
	return (i >= 0 && i < hs_cnt) ? history[i] : 0;
}

void
hist_reset_index(void)
{
	pn_index = -1;
}

/*
 * hist_save() puts the command 'cmd' on the top
 * of the command history list.
 */
void
hist_save(const wchar_t *cmd, int failed)
{
	int i;
	FLAG new = 1;
	HIST_ENTRY *x, *top;

	hist_reset_index();

	for (top = history[0], i = 0; i < hs_alloc; i++) {
		x = history[i];
		history[i] = top;
		top = x;
		if (i == hs_cnt) {
			hs_cnt++;
			break;
		}
		if (wcscmp(USTR(top->cmd),cmd) == 0) {
			/* avoid duplicates */
			new = 0;
			break;
		}
	}
	if (new)
		usw_copy(&top->cmd,cmd);
	top->failed = failed;
	
	history[0] = top;
}

/* file panel functions */

static void
warn_fail(int i)
{
	if (i >= 0 && i < hs_cnt && history[i]->failed)
		msgout(MSG_i,"this command failed last time");
}

/* copy next (i.e. more recent) command from the history list */
void
cx_hist_next(void)
{
	if (pn_index == -1) {
		msgout(MSG_i,"end of the history list (newest command)");
		return;
	}

	if (pn_index-- == 0)
		edit_putstr(USTR(save_line));
	else {
		edit_putstr(USTR(history[pn_index]->cmd));
		warn_fail(pn_index);
	}
}

/* copy previous (i.e. older) command from the history list */
void
cx_hist_prev(void)
{
	if (pn_index >= hs_cnt - 1) {
		msgout(MSG_i,"end of the history list (oldest command)");
		return;
	}

	if (++pn_index == 0)
		usw_xchg(&save_line,&line_cmd.line);
	edit_putstr(USTR(history[pn_index]->cmd));
	warn_fail(pn_index);
}

/* history panel functions */

void
cx_hist_paste(void)
{
	int i, len;
	const char *lex;

	len = textline->size;
	if (len > 0 && textline->curs == len) {
		/* appending */
		lex = cmd2lex(USTR(textline->line));
		for (i = len - 1; i >= 0 && IS_LEX_SPACE(lex[i]); i--)
			;
		if (i >= 0) {
			if (i == len - 1)
				edit_nu_insertchar(L' ');
			if (lex[i] != LEX_CMDSEP)
				edit_nu_insertstr(L"; ",QUOT_NONE);
		}
		
	}
	edit_insertstr(USTR(panel_hist.hist[panel_hist.pd->curs]->cmd),QUOT_NONE);
	if (panel->filtering == 1)
		/* move focus from the filter to the command line */
		cx_filter();
}

void
cx_hist_mouse(void)
{
	if (MI_PASTE)
		cx_hist_paste();
}

void
cx_hist_enter(void)
{
	if (line_cmd.size == 0)
		cx_hist_paste();

	next_mode = MODE_SPECIAL_RETURN;
}

void
cx_hist_del(void)
{
	int i;
	HIST_ENTRY *del;
	FLAG move;

	del = panel_hist.hist[panel_hist.pd->curs];
	hs_cnt--;
	for (move = 0, i = 0; i < hs_cnt; i++) {
		if (history[i] == del) {
			if (pn_index > i)
				pn_index--;
			else if (pn_index == i)
				hist_reset_index();
			move = 1;
		}
		if (move)
			history[i] = history[i + 1];
	}
	history[hs_cnt] = del;
	hist_panel_data();
	pan_adjust(panel_hist.pd);
	win_panel();
}
