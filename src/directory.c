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
#include <stdlib.h>			/* qsort() */
#include <string.h>			/* strcmp() */

#include "directory.h"

#include "cfg.h"			/* cfg_num() */
#include "completion.h"		/* compl_text() */
#include "control.h"		/* get_current_mode() */
#include "edit.h"			/* edit_setprompt() */
#include "filepanel.h"		/* changedir() */
#include "filter.h"			/* cx_filter() */
#include "log.h"			/* msgout() */
#include "match.h"			/* match_substr() */
#include "mbwstring.h"		/* convert2w() */
#include "panel.h"			/* pan_adjust() */
#include "userdata.h"		/* dir_tilde() */
#include "util.h"			/* emalloc() */

/*
 * The directory list 'dirlist' maintained here contains:
 *   a) the names of recently visited directories, it is
 *      a source for the directory panel
 *   b) the last cursor position in all those directories, allowing
 *      to restore the cursor position when the user returns to
 *      a directory previously visited
 */

/* additional entries to be allocated when the 'dirlist' table is full */
#define SAVEDIR_ALLOC_UNIT	32
#define SAVEDIR_ALLOC_MAX	384	/* 'dirlist' size limit */

typedef struct {
	USTRING dirname;		/* directory name */
	USTRINGW dirnamew;		/* wide char version (converted on demand) */
	SDSTRING savefile;		/* file panel's current file */
	int savetop, savecurs;	/* top line, cursor line */
} SAVEDIR;

static SAVEDIR **dirlist;	/* list of visited directories */
static int dir_alloc = 0;	/* number of existing entries in 'dirlist' */
static int dir_cnt = 0;		/* number of used entries in 'dirlist' */

/* directory panel's data is built from 'dirlist' */
#define DP_LIST (panel_dir.dir)
static int dp_alloc = 0;	/* number of existing entries in the dir panel */
static int dp_max;			/* max number of entries to be used */

void
dir_initialize(void)
{
	edit_setprompt(&line_dir,L"Change directory: ");
	dir_reconfig();
}

void
dir_reconfig(void)
{
	dp_max = cfg_num(CFG_D_SIZE);
	if (dp_max == 0)	/* D_PANEL_SIZE = AUTO */
		dp_max = 100;	/* should be enough for every screen */

	if (dp_max > dp_alloc) {
		efree(DP_LIST);
		dp_alloc = dp_max;
		DP_LIST = emalloc(dp_alloc * sizeof(DIR_ENTRY));
	}
}

/*
 * length of the common part of two directory names (FQDNs)
 * (this is not the same as common part of two strings)
 */
static size_t
common_part(const char *dir1, const char *dir2)
{
	char ch1, ch2;
	size_t i, slash;

	for (i = slash = 0; /* until return */; i++) {
		ch1 = dir1[i];
		ch2 = dir2[i];
		if (ch1 == '\0')
			return ch2 == '/' || ch2 == '\0' ? i : slash;
		if (ch2 == '\0')
			return ch1 == '/' ? i : slash;
		if (ch1 != ch2)
			return slash;
		if (ch1 == '/')
			slash = i;
	}
}

static size_t
common_part_w(const wchar_t *dir1, const wchar_t *dir2)
{
	wchar_t ch1, ch2;
	size_t i, slash;

	for (i = slash = 0; /* until return */; i++) {
		ch1 = dir1[i];
		ch2 = dir2[i];
		if (ch1 == L'\0')
			return ch2 == L'/' || ch2 == L'\0' ? i : slash;
		if (ch2 == L'\0')
			return ch1 == L'/' ? i : slash;
		if (ch1 != ch2)
			return slash;
		if (ch1 == L'/')
			slash = i;
	}
}

/*
 * check the relationship of two directories (FQDNs)
 *   return -1: dir2 is subdir of dir1 (/dir2 = /dir1 + /sub)
 *   return +1: dir1 is subdir of dir2 (/dir1 = /dir2 + /sub)
 *   return  0: otherwise
 */
static int
check_subdir(const char *dir1, const char *dir2)
{
	size_t slash;

	slash = common_part(dir1,dir2);
	return (dir2[slash] == '\0') - (dir1[slash] == '\0');
}

int
dir_cmp(const char *dir1, const char *dir2)
{
	static USTRING cut1 = UNULL, cut2 = UNULL;
	size_t slash, len1, len2;

	/* skip the common part */
	slash = common_part(dir1,dir2);
	if (dir1[slash] == '\0' && dir2[slash] == '\0')
		return 0;
	if (dir1[slash] == '\0')
		return -1;
	if (dir2[slash] == '\0')
		return 1;

	/* compare one directory component */
	for (dir1 += slash + 1, len1 = 0; dir1[len1] != '/' && dir1[len1] != '\0'; len1++)
		;
	for (dir2 += slash + 1, len2 = 0; dir2[len2] != '/' && dir2[len2] != '\0'; len2++)
		;
	return strcoll(us_copyn(&cut1,dir1,len1),us_copyn(&cut2,dir2,len2));
}

/*
 * comparison function for qsort() - comparing directories
 * (this is not the same as comparing strings)
 */
static int
qcmp(const void *e1, const void *e2)
{
	return dir_cmp(((DIR_ENTRY *)e1)->name,((DIR_ENTRY *)e2)->name);
}

/* build the directory panel from the 'dirlist' */
#define NO_COMPACT	5	/* preserve top 5 directory names */
void
dir_panel_data(void)
{
	int i, j, cnt, sub;
	FLAG store;
	const char *dirname;
	const wchar_t *dirnamew;

	/* D_PANEL_SIZE = AUTO */
	if (cfg_num(CFG_D_SIZE) == 0) {
		/*
		 * substract extra lines and leave the bottom
		 * line empty to show there is no need to scroll
		 */
		dp_max = disp_data.panlines + panel_dir.pd->min - 1;
		LIMIT_MAX(dp_max,dp_alloc);
	}

	if (panel_dir.pd->filtering)
		match_substr_set(panel_dir.pd->filter->line);

	for (i = cnt = 0; i < dir_cnt; i++) {
		if (cnt == dp_max)
			break;
		dirname  = USTR(dirlist[i]->dirname);
		dirnamew = USTR(dirlist[i]->dirnamew);
		if (dirnamew == 0)
			dirnamew = usw_convert2w(dirname,&dirlist[i]->dirnamew);
		if (panel_dir.pd->filtering && !match_substr(dirnamew))
			continue;
		/* compacting */
		store = 1;
		if (i >= NO_COMPACT)
			for (j = 0; j < cnt; j++) {
				sub = check_subdir(dirname,DP_LIST[j].name);
				if (sub == -1) {
					store = 0;
					break;
				}
				if (sub == 1 && j >= NO_COMPACT) {
					DP_LIST[j].name  = dirname;
					DP_LIST[j].namew = dirnamew;
					store = 0;
					break;
				}
		}
		if (store) {
			DP_LIST[cnt].name  = dirname;
			DP_LIST[cnt].namew = dirnamew;
			cnt++;
		}
	}

	qsort(DP_LIST,cnt,sizeof(DIR_ENTRY),qcmp);

	/*
	 * Two lines like these:
	 *     /aaa/bbb/111
	 *     /aaa/bbb/2222
	 * are displayed as:
	 *     /aaa/bbb/111
	 *           __/2222
	 * and for that purpose length 'shlen' is computed
	 *     |<---->|
	 */
	DP_LIST[0].shlen = 0;
	for (i = 1; i < cnt; i++)
		DP_LIST[i].shlen = common_part_w(DP_LIST[i].namew,DP_LIST[i - 1].namew);

	panel_dir.pd->cnt = cnt;
}

int
dir_main_prepare(void)
{
	int i;
	const char *prevdir;

	panel_dir.pd->filtering = 0;
	dir_panel_data();
	panel_dir.pd->norev = 0;
	panel_dir.pd->top = panel_dir.pd->min;

	/* set cursor to previously used directory */
	panel_dir.pd->curs = 0;
	/* note: dirlist is never empty,
		the current dir is always on the top (dirlist[0]);
		prevdir (if exists) is next (dirlist[1]) */
	prevdir = USTR(dirlist[(dir_cnt > 1) /* [1] or [0] */ ]->dirname);
	for (i = 0; i < panel_dir.pd->cnt; i++)
		if (DP_LIST[i].name == prevdir) {
			panel_dir.pd->curs = i;
			break;
		}
	panel = panel_dir.pd;
	textline = &line_dir;
	edit_nu_kill();
	return 0;
}

const char *
dir_split_dir(int pos)
{
	int level;
	char *p;
	static USTRING copy = UNULL;

	if (pos <= 0)
		return panel_dir_split.name;
	level = panel_dir_split.pd->cnt - pos - 1;
	if (level <= 0)
		return "/";
	for (p = us_copy(&copy,panel_dir_split.name); /* until return */ ;)
		if (*++p == '/' && --level == 0) {
			*p = '\0';
			return USTR(copy);
		}
}

int
dir_split_prepare(void)
{
	int i, cnt;
	const char *dirname;

	dirname = panel_dir_split.name  = DP_LIST[panel_dir.pd->curs].name;
	for (cnt = i = 0; dirname[i]; i++)
		if (dirname[i] == '/')
			cnt++;
	if (i > 1)
		cnt++;	/* difference between "/" and "/dir" */
	panel_dir_split.pd->cnt = cnt;
	panel_dir_split.pd->top = panel_dir_split.pd->min;
	panel_dir_split.pd->curs = 0;
	panel_dir_split.pd->norev = 0;

	panel = panel_dir_split.pd;
	/* textline inherited */
	return 0;
}

/*
 * save the current directory name and the current cursor position
 * in the file panel to 'dirlist'
 */
void
filepos_save(void)
{
	int i;
	FLAG new = 1;
	SAVEDIR *storage, *x, *top;
	const char *dir;

	if (dir_cnt == dir_alloc && dir_alloc < SAVEDIR_ALLOC_MAX) {
		dir_alloc += SAVEDIR_ALLOC_UNIT;
		dirlist = erealloc(dirlist,dir_alloc * sizeof(SAVEDIR *));
		storage = emalloc(SAVEDIR_ALLOC_UNIT * sizeof(SAVEDIR));
		for (i = 0; i < SAVEDIR_ALLOC_UNIT; i++) {
			US_INIT(storage[i].dirname);
			US_INIT(storage[i].dirnamew);
			SD_INIT(storage[i].savefile);
			dirlist[dir_cnt + i] = storage + i;
		}
	}

	dir = USTR(ppanel_file->dir);
	for (top = dirlist[0], i = 0; i < dir_alloc; i++) {
		x = dirlist[i];
		dirlist[i] = top;
		top = x;
		if (i == dir_cnt) {
			dir_cnt++;
			break;
		}
		if (strcmp(USTR(top->dirname),dir) == 0) {
			/* avoid duplicates */
			new = 0;
			break;
		}
	}
	if (new) {
		us_copy(&top->dirname,dir);
		usw_reset(&top->dirnamew);
	}

	if (ppanel_file->pd->cnt) {
		sd_copy(&top->savefile,SDSTR(ppanel_file->files[ppanel_file->pd->curs]->file));
		top->savecurs = ppanel_file->pd->curs;
		top->savetop = ppanel_file->pd->top;
	}
	else if (new) {
		sd_copy(&top->savefile,"..");
		top->savecurs = 0;
		top->savetop = 0;
	}
	dirlist[0] = top;
}

/* set the file panel cursor according to data stored in 'dirlist' */
void
filepos_set(void)
{
	char *dir;
	int i, line;
	SAVEDIR *pe;

	if (ppanel_file->pd->cnt == 0)
		return;

	dir = USTR(ppanel_file->dir);
	for (i = 0; i < dir_cnt; i++) {
		pe = dirlist[i];
		if (strcmp(dir,USTR(pe->dirname)) == 0) {
			/* found */
			line = file_find(SDSTR(pe->savefile));
			ppanel_file->pd->curs = line >= 0 ? line : pe->savecurs;
			ppanel_file->pd->top = pe->savetop;
			pan_adjust(ppanel_file->pd);
			return;
		}
	}

	/* not found */
	line = file_find("..");
	ppanel_file->pd->curs = line >= 0 ? line : 0;
	ppanel_file->pd->top = ppanel_file->pd->min;
	pan_adjust(ppanel_file->pd);
}

/* following cx_dir_xxx functions are used in both MODE_DIR_XXX modes */

static void
dir_paste(void)
{
	const wchar_t *dir;

	dir =
	  get_current_mode() == MODE_DIR_SPLIT
	  ? convert2w(dir_split_dir(panel_dir_split.pd->curs))
	  : DP_LIST[panel_dir.pd->curs].namew;
	edit_nu_insertstr(dir,QUOT_NONE);
	if (dir[1])
		edit_nu_insertchar(L'/');
	edit_update();
	if (panel->filtering == 1)
		cx_filter();
}

void
cx_dir_tab(void)
{
	if (textline->size)
		compl_text(COMPL_TYPE_DIRPANEL);
	else if (panel->curs >= 0)
		dir_paste();
}

void
cx_dir_mouse(void)
{
	if (textline->size == 0 && MI_PASTE)
		dir_paste();
}

void
cx_dir_enter(void)
{
	const char *dir;

	if (panel->norev) {
		/* focus on the input line */
		dir = convert2mb(dir_tilde(USTR(textline->line)));
		if (changedir(dir) == 0)
			next_mode = MODE_SPECIAL_RETURN;
		else if (dir[0] == ' ' || dir[strlen(dir) - 1] == ' ')
			msgout(MSG_i,"check the spaces before/after the directory name");
		return;
	}

	/* focus on the panel (i.e. ignore the input line) */
	if (kinp.fkey == 2 && !MI_AREA(PANEL))
		return;
	if (panel->curs < 0)
		return; /* next_mode is set by the EXTRA_LINE table */

	if (textline->size)
		cx_edit_kill();
	if (get_current_mode() == MODE_DIR)
		next_mode = MODE_DIR_SPLIT;
	else if (changedir(dir_split_dir(panel_dir_split.pd->curs)) == 0)
		next_mode = MODE_SPECIAL_RETURN;
}
