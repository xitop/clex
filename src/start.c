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

#include <sys/stat.h>		/* umask() */
#include <stdarg.h>			/* log.h */
#include <stdio.h>			/* puts() */
#include <stdlib.h>			/* setenv() */
#include <string.h>			/* strcmp() */
#include <unistd.h>			/* getpid() */
#include "curses.h"			/* NCURSES_MOUSE_VERSION */

#include "bookmarks.h"		/* bm_initialize() */
#include "cfg.h"			/* cfg_initialize() */
#include "completion.h"		/* compl_initialize() */
#include "control.h"		/* control_loop() */
#include "directory.h"		/* dir_initialize() */
#include "exec.h"			/* exec_initialize() */
#include "filepanel.h"		/* files_initialize() */
#include "help.h"			/* help_initialize() */
#include "history.h"		/* hist_initialize() */
#include "inout.h"			/* curses_initialize() */
#include "inschar.h"		/* inschar_initialize() */
#include "lang.h"			/* locale_initialize() */
#include "list.h"			/* list_initialize() */
#include "mouse.h"			/* mouse_initialize() */
#include "opt.h"			/* opt_initialize() */
#include "log.h"			/* logfile_open() */
#include "signals.h"		/* signal_initialize() */
#include "tty.h"			/* tty_initialize() */
#include "userdata.h"		/* userdata_initialize() */
#include "util.h"			/* base_name() */
#include "xterm_title.h"	/* xterm_title_initialize() */

/*
 * NOTE: ANSI/ISO C requires that all global and static variables
 * are initialized to zero. CLEX relies on it.
 */
static INPUTLINE log_filt = { L"",0,0,0 }, help_filt = { L"",0,0,0 }, shared_filt = { L"",0,0,0 };

static EXTRA_LINE el_exit[1] = {
	{ 0,0,MODE_SPECIAL_RETURN,0 }
};
static EXTRA_LINE el_bm[2] = {
	{ 0,L"Changes will be saved",0,cx_bm_save },
	{ L"Cancel",L"Changes will be discarded",0,cx_bm_revert }
};
static EXTRA_LINE el_cfg[3] = {
	{ L"Cancel",L"Changes will be discarded",MODE_SPECIAL_RETURN,0 },
	{ L"Apply",L"Use the new configuration in this session",
	  MODE_SPECIAL_RETURN,cx_cfg_apply },
	{ L"Apply+Save",L"Save the configuration to disk",
	  MODE_SPECIAL_RETURN,cx_cfg_apply_save }
};
static EXTRA_LINE el_dir[2] = {
	{ 0,0,MODE_SPECIAL_RETURN,cx_dir_enter },
	{ L"Bookmarks",0,MODE_BM,cx_dir_enter },
};
static EXTRA_LINE el_dir_split[1] = {
	{ 0,0,MODE_SPECIAL_RETURN,cx_dir_enter }
};
static EXTRA_LINE el_group[2] = {
	{ 0,0,MODE_SPECIAL_RETURN,0 },
	{ L"Switch to user data (alt-U)",0,MODE_USER,0 }
};
static EXTRA_LINE el_sort[1] = {
	{ L"Cancel",L"Changes will be discarded",MODE_SPECIAL_RETURN,0 }
};
static EXTRA_LINE el_user[2] = {
	{ 0,0,MODE_SPECIAL_RETURN,0 },
	{ L"Switch to group data (alt-G)",0,MODE_GROUP,0 }
};
#define EL_EXIT (-1 * ARRAY_SIZE(el_exit))

/*
 * PANEL_DESC initialization:
 *   cnt, top, curs,  min, type, norev, extra, filter, drawfn
 *   implicit null: help, filtering
 */
static PANEL_DESC pd_bm = { 0,0,0,
  -1 * ARRAY_SIZE(el_bm),PANEL_TYPE_BM,0,el_bm,&shared_filt,draw_line_bm };
static PANEL_DESC pd_bm_edit = { 2,0,0,
  EL_EXIT,PANEL_TYPE_BM,0,el_exit,0,draw_line_bm_edit };
static PANEL_DESC pd_cfg = { CFG_TOTAL_,0,0,
  -1 * ARRAY_SIZE(el_cfg),PANEL_TYPE_CFG,0,el_cfg,0,draw_line_cfg };
static PANEL_DESC pd_cfg_menu = { 0,0,0,
  0,PANEL_TYPE_CFG_MENU,0,0,0,draw_line_cfg_menu };
static PANEL_DESC pd_cmp = { 1 + CMP_TOTAL_,0,0,
  EL_EXIT,PANEL_TYPE_CMP,0,el_exit,0,draw_line_cmp };
static PANEL_DESC pd_cmp_sum = {  0,0,0,
  EL_EXIT,PANEL_TYPE_CMP_SUM,0,el_exit,0,draw_line_cmp_sum };
static PANEL_DESC pd_compl = { 0,0,0,
  EL_EXIT,PANEL_TYPE_COMPL,0,el_exit,&shared_filt,draw_line_compl };
static PANEL_DESC pd_dir = { 0,0,0,
  -1 * ARRAY_SIZE(el_dir),PANEL_TYPE_DIR,0,el_dir,&shared_filt,draw_line_dir };
static PANEL_DESC pd_dir_split = { 0,0,0,
  -1 * ARRAY_SIZE(el_dir_split),PANEL_TYPE_DIR_SPLIT,0,el_dir_split,0,draw_line_dir_split };
static PANEL_DESC pd_fopt = { FOPT_TOTAL_,0,0,
  EL_EXIT,PANEL_TYPE_FOPT,0,el_exit,0,draw_line_fopt };
static PANEL_DESC pd_grp = { 0,0,0,
  -1 * ARRAY_SIZE(el_group),PANEL_TYPE_GROUP,0,el_group,&shared_filt,draw_line_group };
static PANEL_DESC pd_help = { 0,0,0,
  0,PANEL_TYPE_HELP,1,0,&help_filt,draw_line_help };
static PANEL_DESC pd_hist = { 0,0,0,
  EL_EXIT,PANEL_TYPE_HIST,0,el_exit,&shared_filt,draw_line_hist };
static PANEL_DESC pd_log = { 0,0,0,
  EL_EXIT,PANEL_TYPE_LOG,0,el_exit,&log_filt,draw_line_log };
static PANEL_DESC pd_mainmenu = /* 22 items in this menu */ { 22,EL_EXIT,EL_EXIT,
  EL_EXIT,PANEL_TYPE_MAINMENU,0,el_exit,0,draw_line_mainmenu };
static PANEL_DESC pd_notif = { NOTIF_TOTAL_,0,0,	
  EL_EXIT,PANEL_TYPE_NOTIF,0,el_exit,0,draw_line_notif };
static PANEL_DESC pd_paste = /* 15 items in this menu */ { 15,EL_EXIT,EL_EXIT,
  EL_EXIT,PANEL_TYPE_PASTE,0,el_exit,0,draw_line_paste };
static PANEL_DESC pd_preview = /* 15 items in this menu */ { 0,0,0,
  0,PANEL_TYPE_PREVIEW,0,0,0,draw_line_preview };
static PANEL_DESC pd_sort = /* 18 items in this menu */ { 18,0,0,
  EL_EXIT,PANEL_TYPE_SORT,0,el_sort,0,draw_line_sort };
static PANEL_DESC pd_usr = { 0,0,0,
  -1 * ARRAY_SIZE(el_user),PANEL_TYPE_USER,0,el_user,&shared_filt,draw_line_user };
PANEL_DESC *panel = 0;
PANEL_BM panel_bm = { &pd_bm };
PANEL_BM_EDIT panel_bm_edit = { &pd_bm_edit };
PANEL_COMPL panel_compl = { &pd_compl };
PANEL_CFG panel_cfg = { &pd_cfg };
PANEL_CFG_MENU panel_cfg_menu = { &pd_cfg_menu };
PANEL_CMP panel_cmp = { &pd_cmp };
PANEL_CMP_SUM panel_cmp_sum = { &pd_cmp_sum };
PANEL_DIR panel_dir = { &pd_dir };
PANEL_DIR_SPLIT panel_dir_split = { &pd_dir_split };
PANEL_FOPT panel_fopt = { &pd_fopt };
PANEL_GROUP panel_group = { &pd_grp };
PANEL_HELP panel_help = { &pd_help };
PANEL_HIST panel_hist = { &pd_hist };
PANEL_LOG panel_log = { &pd_log };
PANEL_MENU panel_mainmenu = { &pd_mainmenu };
PANEL_NOTIF panel_notif = { &pd_notif };
PANEL_PASTE panel_paste = { &pd_paste };
PANEL_PREVIEW panel_preview = { &pd_preview };
PANEL_SORT panel_sort =  { &pd_sort, GROUP_DSP, SORT_NAME_NUM, HIDE_NEVER };
PANEL_USER panel_user = { &pd_usr };
PANEL_FILE *ppanel_file;

DISP_DATA disp_data;
LANG_DATA lang_data;
USER_DATA user_data;
CLEX_DATA clex_data;

MOUSE_INPUT minp;
KBD_INPUT kinp;

TEXTLINE *textline = 0, line_cmd, line_tmp, line_dir, line_inschar;
CODE next_mode;
volatile FLAG ctrlc_flag;
const void *pcfg[CFG_TOTAL_];

int
main(int argc, char *argv[])
{
	FLAG help = 0, version = 0;
	int i;
	const char *arg;

	locale_initialize();	/* before writing the first text message */

	/* check command line arguments */
	for (i = 1; i < argc; i++) {
		arg = argv[i];
		if (strcmp(arg,"--help") == 0)
			help = 1;
		else if (strcmp(arg,"--version") == 0)
			version = 1;
		else if (strcmp(arg,"--log") == 0) {
			if (++i == argc)
				err_exit("--log option requires an argument (filename)");
			logfile_open(argv[i]);
		}
		else {
			msgout(MSG_W,"Unrecognized option '%s'",arg);
			msgout(MSG_i,"Try '%s --help' for more information",base_name(argv[0]));
			err_exit("Incorrect usage");
		}
	}
	if (version)
		puts(
		  "\nCLEX File Manager " VERSION "\n"
		  "  compiled with POSIX job control: "
#ifdef _POSIX_JOB_CONTROL
  "yes\n"
#else
  "no\n"
#endif
		  "  mouse interface: "
#if NCURSES_MOUSE_VERSION >= 2
  "ncurses\n"
#else
  "xterm\n"
#endif
		  "\nCopyright (C) 2001-2022 Vlado Potisk"
		  "\n\n"
		  "This is free software distributed without any warranty.\n"
		  "See the GNU General Public License for more details.\n"
		  "\n"
		  "Project homepage is https://github.com/xitop/clex");
	if (help)
		printf(
		  "\nUsage: %s [OPTIONS]\n\n"
		  "      --version      display program version and exit\n"
		  "      --help         display this help and exit\n"
		  "      --log logfile  append log information to logfile\n",
		  base_name(argv[0]));
	if (help || version)
		exit(EXIT_SUCCESS);	/* no cleanup necessary at this stage */

	/* real start */
	puts("\n\n\nStarting CLEX " VERSION "\n");

	/* initialize program data */
	clex_data.umask = umask(0777);
	umask(clex_data.umask);
	clex_data.pid = getpid();
	sprintf(clex_data.pidstr,"%d",(int)clex_data.pid);
#ifdef HAVE_SETENV
	setenv("CLEX",clex_data.pidstr,1);	/* let's have $CLEX (just so) */
#endif

	/* low-level stuff except jc_initialize() */
	tty_initialize();
	signal_initialize();

	/* read the configuration and options asap */
	userdata_initialize();	/* required by cfg_initialize */
	cfg_initialize();
	opt_initialize();
	bm_initialize();

	/* initialize the rest, the order is not important (except when noted) */
	compl_initialize();
	dir_initialize();
	files_initialize();
	exec_initialize();			/* after files_initialize (if PROMPT contains $w) */
	help_initialize();
	hist_initialize();
	inschar_initialize();
	list_initialize();

	/* user interface */
	curses_initialize();
	xterm_title_initialize();	/* after curses_initialize */
	mouse_initialize();			/* after curses_initialize */
	cx_version();

	/* job control initialization is done last in order to provide enough time
	  for the parent process to finish its job control tasks */
	jc_initialize();
	control_loop(MODE_FILE);

	/* NOTREACHED */
	return 0;
}
