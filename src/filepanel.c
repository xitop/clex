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

#include <errno.h>			/* errno */
#include <stdarg.h>			/* log.h */
#include <stdlib.h>			/* getenv() */
#include <string.h>			/* strcmp() */
#include <unistd.h>			/* chdir() */

#include "filepanel.h"

#include "bookmarks.h"		/* get_bookmark() */
#include "cfg.h"			/* cfg_str() */
#include "completion.h"		/* compl_text() */
#include "control.h"		/* err_exit() */
#include "directory.h"		/* filepos_save() */
#include "edit.h"			/* edit_macro() */
#include "exec.h"			/* execute_cmd() */
#include "history.h"		/* cx_hist_prev() */
#include "inout.h"			/* win_panel() */
#include "log.h"			/* msgout() */
#include "list.h"			/* list_directory() */
#include "mbwstring.h"		/* convert2mb() */
#include "panel.h"			/* pan_adjust() */
#include "undo.h"			/* undo_reset() */
#include "userdata.h"		/* userdata_expire() */
#include "ustringutil.h"	/* us_getcwd() */
#include "util.h"			/* base_name() */

static void
try_all_parents(const char *directory)
{
	FLAG root;
	char *dir, *p;

	dir = estrdup(directory);
	for (root = 0; chdir(dir) < 0 || us_getcwd(&ppanel_file->dir) < 0; ) {
		if (root)
			err_exit("Access to the root directory was denied !");
		if ((p = strrchr(dir + 1,'/')) != 0)
			*p = '\0';
		else {
			/* last resort */
			strcpy(dir,"/");
			root = 1;
		}
	}
	free(dir);
}

void
files_initialize(void)
{
	const BOOKMARK *bm;

	static INPUTLINE filter1, filter2;
	static PANEL_DESC panel_desc_1 = { 0,0,0,0,PANEL_TYPE_FILE,0,0,&filter1,draw_line_file };
	static PANEL_DESC panel_desc_2 = { 0,0,0,0,PANEL_TYPE_FILE,0,0,&filter2,draw_line_file };
	static PANEL_FILE panel_f1 = { &panel_desc_1 };
	static PANEL_FILE panel_f2 = { &panel_desc_2 };

	panel_f1.other = &panel_f2;
	ppanel_file = panel_f2.other = &panel_f1;

	if ( (bm = get_bookmark(L"DIR1")) && chdir(USTR(bm->dir)) < 0)
		msgout(MSG_w,"Bookmark DIR1: Cannot change directory: %s",strerror(errno));
	if (us_getcwd(&panel_f1.dir) < 0) {
		msgout(MSG_w,"Cannot get the name of the working directory (%s), it will be changed",
		  strerror(errno));
		try_all_parents(checkabs(getenv("PWD")) ? getenv("PWD") : user_data.homedir);
	}
	usw_convert2w(USTR(panel_f1.dir),&panel_f1.dirw);
	/* do not call convert_dir() here, because
	   it could attempt to change the prompt which is not initialized yet */

	if ( (bm = get_bookmark(L"DIR2")) ) {
		usw_copy(&panel_f2.dirw,USTR(bm->dirw));
		us_copy(&panel_f2.dir,USTR(bm->dir));
	}
	else {
		usw_copy(&panel_f2.dirw,user_data.homedirw);	/* tested by checkabs() already */
		us_convert2mb(user_data.homedirw,&panel_f2.dir);
	}

	panel_f1.order = panel_f2.order = panel_sort.order;
	panel_f1.group = panel_f2.group = panel_sort.group;
	panel_f1.hide = panel_f2.hide = panel_sort.hide;
}

int
files_main_prepare(void)
{
	static FLAG prepared = 0;

	/*
	 * allow only one initial run of files_main_prepare(), successive calls
	 * are merely an indirect result of panel exchange commands
	 */
	if (prepared)
		return 0;

	panel = ppanel_file->pd;
	list_directory();
	ppanel_file->other->expired = 1;

	textline = &line_cmd;
	edit_nu_kill();
	if (user_data.noconfig)
		edit_nu_putstr(L"cfg-clex");

	prepared = 1;
	return 0;
}

/* to be called after each cwd change */
void
convert_dir(void)
{
	usw_convert2w(USTR(ppanel_file->dir),&ppanel_file->dirw);
	update_shellprompt();
}

int
file_find(const char *name)
{
	int i;

	for (i = 0; i < ppanel_file->pd->cnt; i++)
		if (strcmp(SDSTR(ppanel_file->files[i]->file),name) == 0)
			return i;
	
	return -1;
}

/*
 * this is an error recovery procedure used when the current working
 * directory and its name stored in the file panel are not in sync
 */
static void
find_valid_cwd(void)
{
	/* panel contents is invalid in different directory */
	filepanel_reset();
	win_title();
	win_panel();
	msgout(MSG_w,"CHANGE DIR: panel's directory is not accessible, it will be changed");

	try_all_parents(USTR(ppanel_file->dir));
	convert_dir();
}

/*
 * changes working directory to 'dir' and updates the absolute
 * pathname in the primary file panel accordingly
 *
 * changedir() returns 0 on success, -1 when the directory could
 * not be changed. In very rare case when multiple errors occur,
 * changedir() might change to other directory as requested. Note
 * that in such case it returns 0 (cwd changed).
 */
int
changedir(const char *dir)
{
	int line;
	FLAG parent = 0;
	static USTRING savedir_buff = UNULL;
	const char *savedir;

	if (chdir(dir) < 0) {
		msgout(MSG_w,"CHANGE DIR: %s",strerror(errno));
		return -1;
	}

	filepos_save();		/* save the last position in the directory we are leaving now */
	savedir = us_copy(&savedir_buff,USTR(ppanel_file->dir));
	if (us_getcwd(&ppanel_file->dir) < 0) {
		/* not sure where we are -> must leave this dir */
		us_copy(&ppanel_file->dir,savedir);
		if (chdir(savedir) == 0) {
			/* we are back in old cwd */
			msgout(MSG_w,"CHANGE DIR: Cannot change directory");
			return -1;
		}
		find_valid_cwd();
	}
	else {
		if (strcmp(dir,"..") == 0)
			parent = 1;
		if (strcmp(savedir,USTR(ppanel_file->dir))) {
			/* panel contents is invalid in different directory */
			filepanel_reset();
			convert_dir();
		}
	}

	list_directory();
	/* if 'dir' argument was a pointer to a filepanel entry
		list_directory() has just invalidated it */

	/* special case: set cursor to the directory we have just left
		because users prefer it this way */
	if (parent) {
		line = file_find(base_name(savedir));
		if (line >= 0) {
			ppanel_file->pd->curs = line;
			pan_adjust(ppanel_file->pd);
		}
	}

	return 0;
}

/* change working directory */
void
cx_files_cd(void)
{
	FILE_ENTRY *pfe;

	pfe = ppanel_file->files[ppanel_file->pd->curs];
	if (IS_FT_DIR(pfe->file_type)) {
		if (changedir(SDSTR(pfe->file)) == 0) {
			win_title();
			win_panel();
		}
	}
	else
		msgout(MSG_i,"not a directory");
}

/* change working directory and switch panels */
void
cx_files_cd_xchg(void)
{
	FILE_ENTRY *pfe;

	pfe = ppanel_file->files[ppanel_file->pd->curs];
	if (IS_FT_DIR(pfe->file_type)) {
		panel = (ppanel_file = ppanel_file->other)->pd;
		if (changedir(SDSTR(pfe->file)) == 0) {
			win_title();
			win_panel();
			/* allow control_loop() to detect the 'panel' change */
			next_mode = MODE_FILE;
			return;
		}
		panel = (ppanel_file = ppanel_file->other)->pd;
	}
	else
		msgout(MSG_i,"not a directory");
}

void
cx_files_cd_root(void)
{
	changedir("/");
	win_title();
	win_panel();
}

void
cx_files_cd_parent(void)
{
	changedir("..");
	win_title();
	win_panel();
}

void
cx_files_cd_home(void)
{
	changedir(user_data.homedir);
	win_title();
	win_panel();
}

void
cx_files_reread(void)
{
	list_directory();
	win_panel();
}

/* reread also user account information (users/groups) */
void
cx_files_reread_ug(void)
{
	userdata_expire();
	list_directory();
	win_panel();
}

/* exchange panels */
void
cx_files_xchg(void)
{
	int exp;

	panel = (ppanel_file = ppanel_file->other)->pd;

	if (chdir(USTR(ppanel_file->dir)) == -1) {
		find_valid_cwd();
		list_directory();
	}
	else {
		update_shellprompt();	/* convert_dir() not necessary */
		exp = ppanel_file->expired ? 0 : PANEL_EXPTIME;
		if (list_directory_cond(exp) < 0)
			/* filepanel_read() which invokes filepos_save() was not called */
			filepos_save();		/* put the new cwd to the top of the list */
	}

	/* allow control_loop() to detect the 'panel' change */
	next_mode = MODE_FILE;
}

/* pressed <ENTER> - several functions: exec, chdir and insert */
void
cx_files_enter(void)
{
	FILE_ENTRY *pfe;

	if (textline->size && (kinp.fkey != 2 || MI_AREA(LINE))) {
		if (execute_cmd(USTR(textline->line))) {
			cx_edit_kill();
			undo_reset();
		}
	}
	else if (ppanel_file->pd->cnt && (kinp.fkey != 2 || MI_AREA(PANEL))) {
		pfe = ppanel_file->files[ppanel_file->pd->curs];
		if (IS_FT_DIR(pfe->file_type)) {
			/* now doing cx_files_cd(); */
			if (changedir(SDSTR(pfe->file)) == 0) {
				win_title();
				win_panel();
			}
		}
		else if (kinp.fkey == 2 && MI_AREA(PANEL))
			control_loop(MODE_PREVIEW);
		else if (IS_FT_EXEC(pfe->file_type))
			edit_macro(L"./$F ");
	}
}

/* pressed <TAB> - also multiple functions: complete and insert */
void
cx_files_tab(void)
{
	int compl, file_type;

	if (panel->filtering == 1) {
		/* cursor is in the filter expression -> no completion in the command line */
		compl = compl_text(COMPL_TYPE_DRYRUN);	/* this will fail */
		if (compl == -1 && ppanel_file->pd->cnt
		  && IS_FT_EXEC(ppanel_file->files[ppanel_file->pd->curs]->file_type))
			edit_macro(L"./$F ");
		else if (compl == -2)
			edit_macro(L"$F ");
		else
			msgout(MSG_i,"cannot complete a filter expression");
		return;
	}

	/* try completion first, it returns 0 on success */
	compl = compl_text(COMPL_TYPE_AUTO);
	if (compl == -1) {
		file_type = ppanel_file->pd->cnt ?
		  ppanel_file->files[ppanel_file->pd->curs]->file_type : FT_NA;
		/* -1: nothing to complete, this will be the first word */
		if (IS_FT_EXEC(file_type))
			edit_macro(L"./$F ");
		else if (IS_FT_DIR(file_type))
			edit_macro(L"$F/");
		else
			/* absolutely clueless ! */
			msgout(MSG_i,"COMPLETION: please type at least the first character");
	} else if (compl == -2)
		/* -2: nothing to complete, but not in the first word */
		edit_macro(L"$F ");
}

void
cx_files_mouse(void)
{
	int compl;

	switch (minp.area) {
	case AREA_TITLE:
		if (MI_DC(1)) {
			if (minp.x <= disp_data.dir1end)
				control_loop(MODE_DIR);
			else if (minp.x >= disp_data.dir2start)
				cx_files_xchg();
		}
		break;
	case AREA_PANEL:
		if (MI_PASTE) {
			compl = compl_text(COMPL_TYPE_DRYRUN);
			if (compl == -1 && IS_FT_EXEC(ppanel_file->files[ppanel_file->pd->curs]->file_type))
				edit_macro(L"./$F ");
			else if (compl == -2)
				edit_macro(L"$F ");
			else
				edit_macro(L" $F ");
		}
		break;
	case AREA_BAR:
		if (MI_DC(1) && minp.cursor == 1) {
			control_loop(MODE_MAINMENU);
			minp.area = AREA_NONE;		/* disable further mouse event processing */
		}
		break;
	case AREA_PROMPT:
		if (MI_DC(1))
			control_loop(MODE_HIST);
		else if (MI_WHEEL)
			MI_B(4) ? cx_hist_prev() : cx_hist_next();
	}
}
