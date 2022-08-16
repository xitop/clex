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

#include <sys/stat.h>		/* stat() */
#include <stdarg.h>			/* log.h */
#include <stdio.h>			/* fprintf() */
#include <stdlib.h>			/* free() */
#include <string.h>			/* strerror() */

#include "bookmarks.h"

#include "cfg.h"			/* cfg_num() */
#include "completion.h"		/* compl_text() */
#include "control.h"		/* control_loop() */
#include "edit.h"			/* edit_setprompt() */
#include "filepanel.h"		/* changedir() */
#include "filerw.h"			/* fr_open() */
#include "inout.h"			/* win_panel() */
#include "log.h"			/* msgout() */
#include "match.h"			/* match_substr_set() */
#include "mbwstring.h"		/* convert2w() */
#include "panel.h"			/* pan_adjust() */
#include "util.h"			/* emalloc() */

#define BM_SIZE		100					/* size of bookmark table */
#define BM_MAXMEM	(200 * BM_SIZE)		/* memory limit for the bookmark file */

static BOOKMARK *bmlist[BM_SIZE];	/* list of bookmarks */
static BOOKMARK *filtbm[BM_SIZE];	/* filtered list of bookmarks */
static int bm_cnt = 0;				/* number of bookmarks */
static time_t bm_file_mod = 0;		/* last modification of the file */
/* there is a small risk of data incoherence if the file is modified by
   two running CLEX programs within the same second. Users normally
   don't do such things */
static FLAG changed = 0;			/* nonzero if unsaved changes exist */
static const char *append = 0;		/* add this directory when entering the panel */

static time_t
mod_time(const char *file)
{
	struct stat stbuf;

	return stat(file,&stbuf) < 0 ? 0 : stbuf.st_mtime;
}

const BOOKMARK *
get_bookmark(const wchar_t *name)
{
	int i;

	for (i = 0; i < bm_cnt; i++)
		if (wcscmp(SDSTR(bmlist[i]->name),name) == 0) {
			if (*(USTR(bmlist[i]->dir)) != '/') {
				msgout(MSG_NOTICE,"Ignoring the %ls bookmark, because it is not "
				  "an absolute pathname starting with /", name);
				return 0;
			}
			return bmlist[i];
		}
	return 0;
}

static int
bm_save_main(void)
{
	int i;
	wchar_t *name;
	FILE *fp;

	if ( (fp = fw_open(user_data.file_bm)) ) {
		fprintf(fp,"#\n# CLEX bookmarks file\n#\n");
		for (i = 0; i < bm_cnt; i++) {
			name = SDSTR(bmlist[i]->name);
			if (*name != L'\0')
				fprintf(fp,"*%s\n",convert2mb(name));
			fprintf(fp,"%s\n",USTR(bmlist[i]->dir));
		}
	}
	if (fw_close(fp))
		return -1;

	changed = 0;
	bm_file_mod = mod_time(user_data.file_bm);
	return 0;
}

static void
bm_save(void)
{
	if (!changed)
		return;

	if (user_data.nowrite)
		msgout(MSG_W,"BOOKMARKS: Saving data to disk is prohibited");
	else if (bm_save_main() < 0)
		msgout(MSG_W,"BOOKMARKS: Could not save data, details in log");
	else
		msgout(MSG_I,"BOOKMARKS: Data saved");
}

void
cx_bm_save(void)
{
	bm_save();
	next_mode = MODE_SPECIAL_RETURN;
}

static int
is_full(void)
{
	if (bm_cnt >= BM_SIZE) {
		msgout(MSG_W,"Bookmark list is full");
		return 1;
	}
	return 0;
}

static void
bm_reset(int i)
{
	sdw_reset(&bmlist[i]->name);
	us_reset( &bmlist[i]->dir);
	usw_reset(&bmlist[i]->dirw);
}

static void
bm_reset_all(void)
{
	int i;

	bm_cnt = 0;
	for (i = 0; i < BM_SIZE; i++)
		bm_reset(i);
}

/*
 * return value:
 *   0 = bookmarks loaded without problems
 *  -1 = failure, no or incomplete bookmarks loaded
 */
static int
bm_read_main(void)
{
	int i, tfd, split;
	const char *line;
	FLAG corrupted, name;

	tfd = fr_open(user_data.file_bm,BM_MAXMEM);
	if (tfd == FR_NOFILE) {
		bm_reset_all();
		changed = 0;
		return 0;	/* missing optional file is OK */
	}
	if (tfd < 0)
		return -1;
	msgout(MSG_DEBUG,"BOOKMARKS: Processing bookmark file \"%s\"",user_data.file_bm);

	split = fr_split(tfd,BM_SIZE * 2 /* name + dir = 2 lines */ );
	if (split < 0 && split != FR_LINELIMIT) {
		fr_close(tfd);
		return -1;
	}

	bm_reset_all();
	for (corrupted = name = 0, i = 0; (line = fr_line(tfd,i)); i++) {
		if (*line == '/') {
			if (is_full()) {
				split = FR_LINELIMIT;
				break;
			}
			us_copy(&bmlist[bm_cnt]->dir,line);
			usw_convert2w(line,&bmlist[bm_cnt]->dirw);
			name = 0;
			bm_cnt++;
		}
		else if (*line == '*') {
			if (TSET(name))
				corrupted = 1;
			sdw_copy(&bmlist[bm_cnt]->name,convert2w(line + 1));
		}
		else
			corrupted = 1;
	}
	fr_close(tfd);

	if (corrupted)
		msgout(MSG_NOTICE,"Invalid contents, file is corrupted");
	changed = 0;
	return (split < 0 || corrupted) ? -1 : 0;
}

/*
 * bm_read() reads the bookmark file if it was modified or the 'force' flag is set
 * return value: 0 file not read, -1 = read error, 1 = read success
 */
static int
bm_read(int force)
{
	time_t mod;

	mod = mod_time(user_data.file_bm);
	if (mod == bm_file_mod && !force)
		/* file unchanged */
		return 0;
	
	if (bm_read_main() == 0) {
		bm_file_mod = mod;
		return 1;
	}

	if (!user_data.nowrite) {
		/* automatic recovery */
		msgout(MSG_NOTICE,"Attempting to overwrite the invalid bookmark file");
		msgout(MSG_NOTICE,bm_save_main() < 0 ? "Attempt failed" : "Attempt succeeded");
	}

	msgout(MSG_W,"BOOKMARKS: An error occurred while reading data, details in log");
	return -1;
}

void
bm_panel_data(void)
{
	int i, j;
	BOOKMARK *curs;

	curs = VALID_CURSOR(panel_bm.pd) ? panel_bm.bm[panel_bm.pd->curs] : 0;

	if (panel_bm.pd->filtering) {
		match_substr_set(panel_bm.pd->filter->line);
		for (i = j = 0; i < bm_cnt; i++) {
			if (bmlist[i] == curs)
				panel_bm.pd->curs = j;
			if (!match_substr(USTR(bmlist[i]->dirw)) && !match_substr_ic(SDSTR(bmlist[i]->name)))
				continue;
			filtbm[j++] = bmlist[i];
		}
		panel_bm.bm = filtbm;
		panel_bm.pd->cnt = j;
	}
	else {
		if (curs)
			for (i = 0; i < bm_cnt; i++)
				if (bmlist[i] == curs) {
					panel_bm.pd->curs = i;
					break;
				}
		panel_bm.bm = bmlist;
		panel_bm.pd->cnt = bm_cnt;
	}
}

void
cx_bm_revert(void)
{
	if (changed && bm_read(1) > 0)
		msgout(MSG_i,"original bookmarks restored");

	next_mode = MODE_SPECIAL_RETURN;
}

void
bm_initialize(void)
{
	static BOOKMARK storage[BM_SIZE];
	int i;

	for (i = 0; i < BM_SIZE; i++)
		bmlist[i] = storage + i;
	panel_bm.bm = bmlist;

	bm_read(1);
}

static void
set_field_width(void)
{
	int i, len, cw;

	cw = 0;
	for (i = 0; i < bm_cnt; i++) {
		len = wc_cols(SDSTR(bmlist[i]->name),0,-1);
		if (len > cw) {
			if (len > disp_data.pancols / 3) {
				cw = disp_data.pancols / 3;
				break;
			}
			cw = len;
		}
	}

	panel_bm.cw_name = cw;
}

int
bm_prepare(void)
{
	int i;
	const char *dir;

	if (bm_read(0) > 0) {
		msgout(MSG_i,"New version of the bookmarks was loaded");
		panel_bm.pd->curs = panel_bm.pd->min;
	}
	set_field_width();

	if (append) {
		dir = append;
		append = 0;

		for (i = 0; i < bm_cnt; i++)
			if (strcmp(USTR(bmlist[i]->dir),dir) == 0) {
				msgout(MSG_i,"Already bookmarked");
				return -1;
			}

		if (is_full())
			return -1;

		sdw_reset(&bmlist[bm_cnt]->name);
		us_copy(&bmlist[bm_cnt]->dir,dir);
		usw_convert2w(dir,&bmlist[bm_cnt]->dirw);
		panel_bm.pd->curs = bm_cnt++;
		changed = 1;
	}

	if (panel_bm.pd->curs < 0)
		panel_bm.pd->curs = panel_bm.pd->min;
	panel_bm.pd->cnt = bm_cnt;

	panel_bm.pd->filtering = 0;
	panel_bm.bm = bmlist;

	panel = panel_bm.pd;
	textline = 0;
	return 0;
}

void
cx_bm_chdir(void)
{
	bm_save();
	if (changedir(USTR(bmlist[panel_bm.pd->curs]->dir)) == 0)
		next_mode = MODE_SPECIAL_RETURN;
}

static void
bm_rotate(int from, int to)
{
	BOOKMARK *b;
	int i;

	if (from < to) {
		b = bmlist[from];
		for (i = from; i < to; i++)
			bmlist[i] = bmlist[i + 1];
		bmlist[i] = b;
	}
	else if (from > to) {
		b = bmlist[from];
		for (i = from; i > to; i--)
			bmlist[i] = bmlist[i - 1];
		bmlist[i] = b;
	}
	changed = 1;
}

void
cx_bm_up(void)
{
	int pos;

	if ((pos = panel_bm.pd->curs - 1) < 0)
		return;

	bm_rotate(pos + 1, pos);

	panel_bm.pd->curs = pos;	/* curs-- */
	LIMIT_MAX(panel_bm.pd->top,pos);
	win_panel();
}

void
cx_bm_down(void)
{
	int pos;

	if ((pos = panel_bm.pd->curs + 1) == bm_cnt)
		return;

	bm_rotate(pos - 1,pos);

	panel_bm.pd->curs = pos;	/* curs++ */
	LIMIT_MIN(panel_bm.pd->top,pos - disp_data.panlines + 1);
	win_panel();
}

void
cx_bm_del(void)
{
	bm_reset(panel_bm.pd->curs);
	bm_rotate(panel_bm.pd->curs,bm_cnt--);

	panel_bm.pd->cnt = bm_cnt;
	if (panel_bm.pd->curs == bm_cnt) {
		panel_bm.pd->curs--;
		pan_adjust(panel_bm.pd);
	}
	set_field_width();
	win_panel();
}

/* * * edit modes * * */

int
bm_edit0_prepare(void)
{
	panel_bm_edit.pd->top =  panel_bm_edit.pd->min;
	panel_bm_edit.pd->curs = 0;
	panel = panel_bm_edit.pd;
	textline = 0;
	return 0;
}

int
bm_edit1_prepare(void)
{
	/* panel = panel_bm_edit.pd; */
	textline = &line_tmp;
	edit_setprompt(textline,L"Bookmark name: ");
	edit_nu_putstr(SDSTR(panel_bm_edit.bm->name));
	win_panel_opt();
	return 0;
}

int
bm_edit2_prepare(void)
{
	const wchar_t *dirw;

	/* panel = panel_bm_edit.pd; */
	textline = &line_tmp;
	edit_setprompt(textline,L"Bookmark directory: ");
	dirw = USTR(panel_bm_edit.bm->dirw);
	edit_nu_putstr(dirw ? dirw : L"/");
	win_panel_opt();
	return 0;
}

void
cx_bm_edit(void)
{
	panel_bm_edit.bm = bmlist[panel_bm.pd->curs];	/* current bookmark */
	control_loop(MODE_BM_EDIT0);
	set_field_width();
	win_panel();
}

void
cx_bm_new(void)
{
	if (is_full())
		return;

	panel_bm_edit.bm = bmlist[bm_cnt];
	bm_reset(bm_cnt);
	control_loop(MODE_BM_EDIT0);

	if (USTR(panel_bm_edit.bm->dir) == 0)
		/* dir is still null -> operation was canceled with ctrl-C */
		return;

	LIMIT_MIN(panel_bm.pd->curs,-1);
	bm_rotate(bm_cnt,++panel_bm.pd->curs);
	panel_bm.pd->cnt = ++bm_cnt;
	set_field_width();

	pan_adjust(panel_bm.pd);
	win_panel();
}

/* note: called from the file panel or the main menu */
void
cx_bm_addcwd(void)
{
	append = USTR(ppanel_file->dir);
	control_loop(MODE_BM);
}

void
cx_bm_edit0_enter(void)
{
	control_loop(panel_bm_edit.pd->curs ? MODE_BM_EDIT2 : MODE_BM_EDIT1);
}

void
cx_bm_edit1_enter(void)
{
	sdw_copy(&panel_bm_edit.bm->name,USTR(textline->line));
	changed = 1;
	win_panel_opt();
	next_mode = MODE_SPECIAL_RETURN;
}

void
cx_bm_edit2_enter(void)
{
	const wchar_t *dirw;

	dirw = USTR(textline->line);
	if (*dirw != L'/') {
		msgout(MSG_w,"Directory name must start with a slash /");
		return;
	}
	usw_copy(&panel_bm_edit.bm->dirw,dirw);
	us_convert2mb(dirw,&panel_bm_edit.bm->dir);
	changed = 1;
	win_panel_opt();
	next_mode = MODE_SPECIAL_RETURN;
}

void
cx_bm_edit2_compl(void)
{
	if (compl_text(COMPL_TYPE_DIRPANEL) < 0)
		msgout(MSG_i,"COMPLETION: please type at least the first character");
}
