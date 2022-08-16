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

#include "curses.h"
#include <stdarg.h>			/* log.h */
#include <stdio.h>			/* fputs() */
#include <stdlib.h>			/* exit() */
#include <wctype.h>			/* iswcntrl() */

#include "control.h"

#include "bookmarks.h"		/* bm_prepare() */
#include "cmp.h"			/* cmp_prepare() */
#include "cfg.h"			/* cfg_prepare() */
#include "completion.h"		/* compl_prepare() */
#include "directory.h"		/* dir_main_prepare() */
#include "edit.h"			/* cx_edit_xxx() */
#include "filepanel.h"		/* cx_files_xxx() */
#include "filerw.h"			/* fw_cleanup() */
#include "filter.h"			/* fopt_prepare() */
#include "help.h"			/* help_prepare() */
#include "history.h"		/* hist_prepare() */
#include "inout.h"			/* win_panel() */
#include "inschar.h"		/* inschar_prepare() */
#include "log.h"			/* vmsgout() */
#include "mouse.h"			/* cx_common_mouse() */
#include "notify.h"			/* notif_prepare() */
#include "opt.h"			/* opt_save() */
#include "panel.h"			/* cx_pan_xxx() */
#include "preview.h"		/* preview_prepare() */
#include "rename.h"			/* rename_prepare() */
#include "select.h"			/* select_prepare() */
#include "sort.h"			/* sort_prepare() */
#include "string.h"			/* strncmp() */
#include "tty.h"			/* tty_reset() */
#include "undo.h"			/* undo_reset() */
#include "userdata.h"		/* user_prepare() */
#include "xterm_title.h"	/* xterm_title_restore() */

/*
 * PANEL is the main part of the screen, it shows data
 *   depending on the panel type; the user can scroll through it.
 *
 * TEXTLINE is a line of text where the user can enter and edit
 *   his/her input.
 *
 * KEY_BINDING contains a keystroke and a corresponding function
 *   to be called every time that key is pressed. All such handler
 *   function names begin with the cx_ prefix.
 *
 * CLEX operation mode is defined by a PANEL, TEXTLINE, and a set of
 *   KEY_BINDING tables. The PANEL and TEXTLINE are initialized by
 *   a so-called preparation function after each mode change.
 *
 * Operation mode can be changed in one of these two ways:
 *   - straightforward transition from mode A to mode B; this
 *     is achieved by setting the 'next_mode' global variable
 *   - nesting of modes; this is achieved by calling another
 *     instance of 'control_loop()'. To go back the variable
 *     'next_mode' must be set to MODE_SPECIAL_RETURN.
 */

typedef struct {
	CODE fkey;			/* type of the key */
	FLAG escp;			/* press escape key first */
	wint_t key;			/* if this key was pressed ... */
	void (*fn)(void);	/* ... then this function is to be invoked */
	int options;		/* option bits - see OPT_XXX below */
} KEY_BINDING;

#define OPT_CURS	1
#define OPT_NOFILT	2
#define OPT_ALL		4
/*
 * OPT_CURS: call the handler function only if the cursor is on a valid regular line,
 * i.e. not on an extra line and not in an empty panel
 *     extra panel lines:   curs < 0
 *     regular panel lines: curs >= 0
 * note1: in a panel with extra lines is OPT_CURS not necessary for the <enter> key
 *     (i.e. ctrl-M), because when the cursor is not on a regular panel line, the
 *     <enter> is handled by an EXTRA_LINE function
 * note2: see the warning at tab_mainmenu[] !
 *
 * OPT_NOFILT: ignore the binding when filtering is active
 *
 * OPT_ALL: call all handlers for the event, not only the first one
 *     note: do_action() return value is of limited or no use with OPT_ALL
 */

#define CXM(X,M) void cx_mode_ ## X (void) { control_loop(MODE_ ## M); }
static CXM(bm,BM)
static CXM(cfg,CFG)
static CXM(cmp,CMP)
static CXM(deselect,DESELECT)
static CXM(dir,DIR)
static CXM(fopt,FOPT)
static CXM(group,GROUP)
static CXM(help,HELP)
static CXM(history,HIST)
static CXM(inschar,INSCHAR)
static CXM(log,LOG)
static CXM(mainmenu,MAINMENU)
static CXM(notif,NOTIF)
static CXM(paste,PASTE)
static CXM(preview,PREVIEW)
static CXM(rename,RENAME)
static CXM(select,SELECT)
static CXM(sort,SORT)
static CXM(user,USER)

#define CXT(X,M) void cx_trans_ ## X (void) { next_mode = MODE_ ## M; }
static CXT(bm,BM)
static CXT(group,GROUP)
static CXT(user,USER)
static CXT(quit,SPECIAL_QUIT)
static CXT(return,SPECIAL_RETURN)

static void
cx_trans_discard(void) {
	msgout(MSG_i,"Changes discarded");
	next_mode = MODE_SPECIAL_RETURN;
}

static void noop(void) { ; }

/* defined below */
static int menu_prepare(void);
static void cx_menu_pick(void);
static int paste_prepare(void);
static void cx_paste_pick(void);

#define END_TABLE { 0,0,0,0,0 }

static KEY_BINDING tab_bm[] = {
	{ 0, 0,  WCH_CTRL('M'),	cx_bm_chdir,		OPT_NOFILT				},
	{ 0, 0,  WCH_CTRL('C'),	cx_bm_revert,		OPT_NOFILT				},
	{ 0, 0,  L'd',			cx_bm_down,			OPT_NOFILT | OPT_CURS	},
	{ 0, 0,  L'n',			cx_bm_new,			OPT_NOFILT				},
	{ 0, 0,  L'p',			cx_bm_edit,			OPT_NOFILT | OPT_CURS	},
	{ 0, 0,  L'u',			cx_bm_up,			OPT_NOFILT | OPT_CURS	},
	{ 0, 1,  L'k',			cx_bm_save,			0						},
	{ 1, 0,  KEY_DC,		cx_bm_del,			OPT_NOFILT | OPT_CURS	},
	END_TABLE
};

static KEY_BINDING tab_bm_edit0[] = {
	{ 0, 0,  WCH_CTRL('M'),	cx_bm_edit0_enter,	OPT_CURS	},
	END_TABLE
};

static KEY_BINDING tab_bm_edit1[] = {
	{ 0, 0,  WCH_CTRL('M'),	cx_bm_edit1_enter,	0 	},
	END_TABLE
};

static KEY_BINDING tab_bm_edit2[] = {
	{ 0, 0,  WCH_CTRL('I'),	cx_bm_edit2_compl,	0 	},
	{ 0, 0,  WCH_CTRL('M'),	cx_bm_edit2_enter,	0 	},
	END_TABLE
};

static KEY_BINDING tab_cfg[] = {
	{ 0, 0,  WCH_CTRL('M'),	cx_cfg_enter,		0			},
	{ 0, 0,  L's',			cx_cfg_default,		OPT_CURS	},
	{ 0, 0,  L'o',			cx_cfg_original,	OPT_CURS	},
	{ 0, 1,  L'c',			cx_cfg_noexit,		0			},
	{ 0, 0,  WCH_CTRL('C'),	cx_trans_discard,	0			},
	END_TABLE
};

static KEY_BINDING tab_cfg_edit_num[] = {
	{ 0, 0,  WCH_CTRL('M'),	cx_cfg_num_enter,	0	},
	END_TABLE
};

static KEY_BINDING tab_cfg_edit_str[] = {
	{ 0, 0,  WCH_CTRL('M'),	cx_cfg_str_enter,	0	},
	END_TABLE
};

static KEY_BINDING tab_cfg_menu[] = {
	{ 0, 0,  WCH_CTRL('M'),	cx_cfg_menu_enter,	0	},
	END_TABLE
};

static KEY_BINDING tab_common[] = {
	{ 0, 0,  WCH_CTRL('C'),	cx_trans_return,	0		},
	{ 0, 0,  WCH_CTRL('F'),	cx_filter,			0		},
	{ 0, 1,  L'c',			cx_mode_cfg,		0		},
	{ 0, 1,  L'l',			cx_mode_log,		0		},
	{ 0, 1,  L'n',			cx_mode_notif,		0		},
	{ 0, 1,  L'o',			cx_mode_fopt,		0		},
	{ 0, 1,  L'q',			cx_trans_quit,		0		},
	{ 0, 1,  L'v',			cx_version,			0		},
	{ 1, 0,  KEY_F(1),		cx_mode_help,		0		},
#ifdef KEY_HELP
	{ 1, 0,  KEY_HELP,		cx_mode_help,		0		},
#endif
	END_TABLE
};

static KEY_BINDING tab_cmp[] = {
	{ 0, 0,  L' ',			cx_cmp,				OPT_CURS	},
	{ 0, 0,  WCH_CTRL('M'),	cx_cmp,				OPT_CURS	},
	{ 0, 1,  L'=',			cx_trans_return,	0			},
	END_TABLE
};

static KEY_BINDING tab_compl[] = {
	{ 0, 0,  WCH_CTRL('I'),	cx_compl_enter,	OPT_CURS	},
	{ 0, 0,  WCH_CTRL('M'),	cx_compl_enter,	OPT_CURS	},
	END_TABLE
};

static KEY_BINDING tab_compl_sum[] = {
	{ 0, 0,  WCH_CTRL('M'),	cx_pan_home,	0	},
	END_TABLE
};

static KEY_BINDING tab_dir[] = {
	{ 0, 1,  L'k',			cx_trans_bm,		0		},
	{ 0, 0,  WCH_CTRL('I'),	cx_dir_tab,			0		},
	{ 0, 0,  WCH_CTRL('M'),	cx_dir_enter,		0		},
	{ 0, 1,  L'w',			cx_trans_return,	0		},
	{ 2, 0,  0,				cx_dir_mouse,		OPT_ALL	},
	END_TABLE
};

static KEY_BINDING tab_edit[] = {
	{ 0, 1,  L'b',			cx_edit_w_left,		0		},
#ifdef KEY_SLEFT
	{ 1, 0,  KEY_SLEFT,		cx_edit_w_left,		0		},
#endif
	{ 1, 1,  KEY_LEFT,		cx_edit_w_left,		0		},
	{ 0, 1,  L'd',			cx_edit_w_del,		0		},
	{ 0, 1,  L'i',			cx_mode_inschar,	0		},
	{ 0, 1,  L'f',			cx_edit_w_right,	0		},
#ifdef KEY_SRIGHT
	{ 1, 0,  KEY_SRIGHT,	cx_edit_w_right,	0		},
#endif
	{ 1, 1,  KEY_RIGHT,		cx_edit_w_right,	0		},
	{ 0, 1,  L't',			cx_edit_flipcase,	0		},
	{ 1, 0,  KEY_BACKSPACE,	cx_edit_backsp,		0		},
	{ 0, 0,  WCH_CTRL('K'),	cx_edit_delend,		0		},
	{ 0, 0,  WCH_CTRL('U'),	cx_edit_kill,		0		},
	{ 0, 0,  WCH_CTRL('V'),	cx_edit_inschar,	0		},
	{ 0, 0,  WCH_CTRL('Z'),	cx_undo,			0		},
	{ 0, 1,  WCH_CTRL('Y'),	cx_undo,			0		},
	{ 0, 0,  WCH_CTRL('Y'),	cx_redo,			0		},
	{ 0, 1,  WCH_CTRL('Z'),	cx_redo,			0		},
	{ 1, 0,  KEY_DC,		cx_edit_delchar,	0		},
	{ 1, 0,  KEY_LEFT,		cx_edit_left,		0		},
	{ 1, 0,  KEY_RIGHT,		cx_edit_right,		0		},
	{ 1, 0,  KEY_HOME,		cx_edit_begin,		0		},
	{ 1, 0,  KEY_END,		cx_edit_end,		0		},
	{ 1, 1,  KEY_UP,		cx_edit_up,			0		},
	{ 1, 1,  KEY_DOWN,		cx_edit_down,		0		},
	{ 2, 0,  0,				cx_edit_mouse,		OPT_ALL	},
	END_TABLE
};

static KEY_BINDING tab_editcmd[] = {
	{ 0, 0,  WCH_CTRL('A'),	cx_edit_paste_path,	0			},
	{ 0, 0,  WCH_CTRL('E'),	cx_edit_paste_dir2,	0			},
	{ 0, 1,  WCH_CTRL('E'),	cx_edit_paste_dir1,	0			},
	{ 0, 0,  WCH_CTRL('I'),	cx_files_tab,		0			},
	{ 0, 1,  WCH_CTRL('I'),	cx_mode_paste,		0			},
	{ 0, 0,  WCH_CTRL('M'),	cx_files_enter,		0			},
	{ 0, 1,  WCH_CTRL('M'),	cx_files_cd,		OPT_CURS	},
	{ 0, 0,  WCH_CTRL('N'),	cx_hist_next,		0			},
	{ 0, 0,  WCH_CTRL('O'),	cx_edit_paste_link,	OPT_CURS	},
	{ 0, 0,  WCH_CTRL('P'),	cx_hist_prev,		0			},
	{ 0, 1,  WCH_CTRL('R'),	cx_files_reread_ug,	0			},
	{ 0, 0,  WCH_CTRL('T'),	cx_select_toggle,	OPT_CURS	},
	{ 0, 1,  WCH_CTRL('T'),	cx_select_range,	OPT_CURS	},
	{ 0, 0,  WCH_CTRL('X'),	cx_files_xchg,		0			},
	{ 0, 1,  L'e',			cx_mode_preview,	OPT_CURS	},
	{ 0, 1,  L'g',			cx_mode_group,		0			},
	{ 0, 1,  L'm',			cx_mode_mainmenu,	0			},
	{ 0, 1,  L'p',			cx_complete_hist,	0			},
	{ 0, 1,  L'r',			cx_mode_rename,		OPT_CURS	},
	{ 0, 1,  L'x',			cx_files_cd_xchg,	OPT_CURS	},
	{ 1, 0,  KEY_F(16),		cx_mode_mainmenu,	0			},
	{ 1, 0,  KEY_IC,		cx_select_toggle,	OPT_CURS	},
	{ 1, 0,  KEY_IL,		cx_select_toggle,	OPT_CURS	},
	{ 1, 1,  KEY_IC,		cx_select_range,	OPT_CURS	},
	{ 1, 1,  KEY_IL,		cx_select_range,	OPT_CURS	},
#ifdef KEY_SIC
	{ 1, 0,  KEY_SIC,		cx_select_range,	OPT_CURS	},
#endif
	{ 1, 0,  KEY_F(2),		cx_edit_cmd_f2,		0			},
	{ 1, 0,  KEY_F(3),		cx_edit_cmd_f3,		0			},
	{ 1, 0,  KEY_F(4),		cx_edit_cmd_f4,		0			},
	{ 1, 0,  KEY_F(5),		cx_edit_cmd_f5,		0			},
	{ 1, 0,  KEY_F(6),		cx_edit_cmd_f6,		0			},
	{ 1, 0,  KEY_F(7),		cx_edit_cmd_f7,		0			},
	{ 1, 0,  KEY_F(8),		cx_edit_cmd_f8,		0			},
	{ 1, 0,  KEY_F(9),		cx_edit_cmd_f9,		0			},
	{ 1, 0,  KEY_F(10),		cx_edit_cmd_f10,	0			},
	{ 1, 0,  KEY_F(11),		cx_edit_cmd_f11,	0			},
	{ 1, 0,  KEY_F(12),		cx_edit_cmd_f12,	0			},
	{ 2, 0,  0,				cx_files_mouse,		OPT_ALL		},
	END_TABLE
};

static KEY_BINDING tab_filteredit[] = {
	{ 0, 0,  WCH_CTRL('K'),	cx_filteredit_delend,	0		},
	{ 0, 0,  WCH_CTRL('U'),	cx_filteredit_kill,		0		},
	{ 0, 0,  WCH_CTRL('V'),	cx_edit_inschar,		0		},
	{ 0, 1,  L'i',			cx_mode_inschar,		0		},
	{ 1, 0,  KEY_BACKSPACE,	cx_filteredit_backsp,	0		},
	{ 1, 0,  KEY_DC,		cx_filteredit_delchar,	0		},
	{ 1, 0,  KEY_LEFT,		cx_filteredit_left,		0		},
	{ 1, 0,  KEY_RIGHT,		cx_filteredit_right,	0		},
	{ 1, 0,  KEY_HOME,		cx_filteredit_begin,	0		},
	{ 1, 0,  KEY_END,		cx_filteredit_end,		0		},
	{ 2, 0,  0,				cx_edit_mouse,			OPT_ALL	},
	END_TABLE
};

static KEY_BINDING tab_fopt[] = {
	{ 0, 0,  L' ',			cx_fopt_enter,		OPT_CURS	},
	{ 0, 0,  WCH_CTRL('M'),	cx_fopt_enter,		OPT_CURS	},
	{ 0, 1,  L'o',			cx_trans_return,	0			},
	END_TABLE
};

static KEY_BINDING tab_group[] = {
	{ 0, 0,  WCH_CTRL('I'),	cx_group_paste,		OPT_CURS	},
	{ 0, 0,  WCH_CTRL('M'),	cx_pan_home,		0			},
	{ 0, 1,  L'g',			cx_trans_return,	0			},
	{ 0, 1,  L'u',			cx_trans_user,		0			},
	{ 2, 0,  0,				cx_group_mouse,		OPT_ALL		},
	END_TABLE
};

static KEY_BINDING tab_help[] = {
	{ 0, 0,  WCH_CTRL('M'),	cx_help_link,	OPT_NOFILT	},
	{ 0, 1,  WCH_CTRL('M'),	cx_help_link,	0			},
	{ 1, 0,  KEY_LEFT,		cx_help_back,	OPT_NOFILT	},
	{ 1, 1,  KEY_LEFT,		cx_help_back,	0			},
	{ 1, 0,  KEY_RIGHT,		cx_help_link,	OPT_NOFILT	},
	{ 1, 1,  KEY_RIGHT,		cx_help_link,	0			},
	{ 1, 0,  KEY_BACKSPACE,	cx_help_back,	OPT_NOFILT	},
	{ 1, 1,  KEY_BACKSPACE,	cx_help_back,	0			},
	{ 1, 0,  KEY_F(1),		cx_help_main,	0			},
#ifdef KEY_HELP
	{ 1, 0,  KEY_HELP,		cx_help_main,	0			},
#endif
	{ 2, 0,  0,				cx_help_mouse,	OPT_ALL		},
	END_TABLE
};

static KEY_BINDING tab_help_panel[] = {
	{ 1, 0,  KEY_UP,		cx_help_up,		0	},
	{ 1, 0,  KEY_DOWN,		cx_help_down,	0	},
	END_TABLE
};

static KEY_BINDING tab_hist[] = {
	{ 1, 1,  KEY_DC,		cx_hist_del,		OPT_CURS	},
	{ 1, 1,  KEY_BACKSPACE,	cx_hist_del,		OPT_CURS	},
	{ 0, 0,  WCH_CTRL('I'),	cx_hist_paste,		OPT_CURS	},
	{ 0, 0,  WCH_CTRL('M'),	cx_hist_enter,		OPT_CURS	},
	{ 0, 0,  WCH_CTRL('N'),	cx_pan_up,			0			},	/* redefine history next */
	{ 0, 0,  WCH_CTRL('P'),	cx_pan_down,		0			},	/* redefine history prev */
	{ 0, 1,  L'h',			cx_trans_return,	0			},
	{ 2, 0,  0,				cx_hist_mouse,		OPT_ALL		},
	END_TABLE
};

/* pseudo-table returned by do_action() */
static KEY_BINDING tab_insertchar[] = {
	END_TABLE
};

static KEY_BINDING tab_inschar[] = {
	{ 0, 0,  WCH_CTRL('M'),	cx_ins_enter,	0	},
	END_TABLE
};

static KEY_BINDING tab_log[] = {
	{ 0, 0,  WCH_CTRL('M'),	cx_pan_home,		0			},
	{ 1, 0,  KEY_LEFT,		cx_log_left,		OPT_NOFILT	},
	{ 1, 0,  KEY_RIGHT,		cx_log_right,		OPT_NOFILT	},
	{ 1, 0,  KEY_HOME,		cx_log_home,		OPT_NOFILT	},
	{ 0, 0,  L'm',			cx_log_mark,		OPT_NOFILT	},
	{ 0, 1,  L'l',			cx_trans_return,	0			},
	END_TABLE
};

static KEY_BINDING tab_mainmenu[] = {
/*
 * these lines correspond with the main menu panel,
 * if you change the menu, you must update also the
 * initialization in start.c and descriptions in inout.c
 *
 * Warning: the OPT_CURS option would check the panel_mainmenu, this is probably not
 * what you want ! It can be corrected if need be, hint: VALID_CURSOR(filepanel)
 * rather than VALID_CURSOR(menupanel) in callfn())
 */
	/*
	 * no key for cx_mode_help, note the difference:
	 * <F1> in tab_common:    main menu -> help -> main menu
	 * help in tab_mainmenu:  main menu -> help -> file panel
	 */
	{ 0, 0,  0,				cx_mode_help,		0	},
	{ 0, 1,  L'w',			cx_mode_dir,		0	},
	{ 0, 1,  L'/',			cx_files_cd_root,	0	},
	{ 0, 1,  L'.',			cx_files_cd_parent,	0	},
	{ 0, 1,  L'~',			cx_files_cd_home,	0	},
	{ 0, 1,  L'k',			cx_mode_bm,			0	},
	{ 0, 0,  WCH_CTRL('D'),	cx_bm_addcwd,		0	},
	{ 0, 1,  L'h',			cx_mode_history,	0	},
	{ 0, 1,  L's',			cx_mode_sort,		0	},
	{ 0, 0,  WCH_CTRL('R'),	cx_files_reread,	0	},
	{ 0, 1,  L'=',			cx_mode_cmp,		0	},
	{ 0, 0,  0,				cx_filter2,			0	},	/* key in tab_mainmenu2 */
	{ 0, 1,  L'+',			cx_mode_select,		0	},
	{ 0, 1,  L'-',			cx_mode_deselect,	0	},
	{ 0, 1,  L'*',			cx_select_invert,	0	},
	{ 0, 0,  0,				cx_mode_fopt,		0	},	/* key in tab_common */
	{ 0, 1,  L'u',			cx_mode_user,		0	},
	{ 0, 1,  L'l',			cx_mode_log,		0	},
	{ 0, 0,  0,				cx_mode_notif,		0	},	/* key in tab_common */
	{ 0, 0,  0,				cx_mode_cfg,		0	},	/* key in tab_common */
	{ 0, 1,  L'v',			cx_version,			0	},
	{ 0, 0,  0,				cx_trans_quit,		0	},	/* key in tab_common */
/* the main menu ends here, the following entries are hidden */
	{ 0, 1,  L'`',			cx_files_cd_home,	0	},	/* like alt-~ but easier to type */
	END_TABLE
};

/* main menu keys that are not to be used in the file panel */
/* this table must correspond with the panel_mainmenu as well ! */
static KEY_BINDING tab_mainmenu2[] = {
	{ 0, 0,  0,				noop,			0	},
	{ 0, 0,  0,				noop,			0	},
	{ 0, 0,  0,				noop,			0	},
	{ 0, 0,  0,				noop,			0	},
	{ 0, 0,  0,				noop,			0	},
	{ 0, 0,  0,				noop,			0	},
	{ 0, 0,  0,				noop,			0	},
	{ 0, 0,  0,				noop,			0	},
	{ 0, 0,  0,				noop,			0	},
	{ 0, 0,  0,				noop,			0	},
	{ 0, 0,  0,				noop,			0	},
	{ 0, 0,  WCH_CTRL('F'),	cx_filter2,		0	},
	{ 0, 0,  0,				noop,			0	},
	{ 0, 0,  0,				noop,			0	},
	{ 0, 0,  0,				noop,			0	},
	{ 0, 0,  0,				noop,			0	},
	{ 0, 1,  L'g',			cx_mode_group,	0	},
	{ 0, 0,  0,				noop,			0	},
	{ 0, 0,  0,				noop,			0	},
	{ 0, 0,  0,				noop,			0	},
	{ 0, 0,  0,				noop,			0	},
	{ 0, 0,  0,				noop,			0	},
/* the main menu ends here, the following entries are hidden */
	{ 0, 0,  WCH_CTRL('M'),	cx_menu_pick,	0	},
	{ 0, 1,  L'm',			cx_trans_return,0	},
	END_TABLE
};

/* pseudo-table returned for mouse operations with OPT_ALL */
static KEY_BINDING tab_mouse[] = {
	{ 2, 0,  0,	cx_common_mouse,	0	},
	END_TABLE
};

static KEY_BINDING tab_panel[] = {
	{ 1, 0,  KEY_UP,	cx_pan_up,		0		},
#ifdef KEY_SR
	{ 1, 0,  KEY_SR,	cx_pan_up,		0		},
#endif
	{ 1, 0,  KEY_DOWN,	cx_pan_down,	0		},
#ifdef KEY_SF
	{ 1, 0,  KEY_SF,	cx_pan_down,	0		},
#endif
	{ 1, 0,  KEY_PPAGE,	cx_pan_pgup,	0		},
	{ 1, 0,  KEY_NPAGE,	cx_pan_pgdown,	0		},
	{ 1, 1,  KEY_HOME,	cx_pan_home,	0		},
#ifdef KEY_SHOME
	{ 1, 0,  KEY_SHOME,	cx_pan_home,	0		},
#endif
	{ 1, 1,  KEY_END,	cx_pan_end,		0		},
#ifdef KEY_SEND
	{ 1, 0,  KEY_SEND,	cx_pan_end,		0		},
#endif
	{ 0, 1,  L'z',		cx_pan_middle,	0		},
	{ 2, 0,  0,			cx_pan_mouse,	OPT_ALL	},
	END_TABLE
};

static KEY_BINDING tab_notif[] = {
	{ 0, 0,  L' ',			cx_notif,			OPT_CURS	},
	{ 0, 0,  WCH_CTRL('M'),	cx_notif,			OPT_CURS	},
	{ 0, 1,  L'n',			cx_trans_return,	0			},
	END_TABLE
};

static KEY_BINDING tab_pastemenu[] = {
/*
 * these lines correspond with the paste menu panel,
 * if you change this, you must update initialization
 * in start.c and descriptions in inout.c
 */
	{ 0, 0,  0,				cx_compl_wordstart,	0		},
	{ 0, 0,  0,				cx_complete_auto,	0		},
	{ 0, 0,  0,				cx_complete_file,	0		},	/* no key */
	{ 0, 0,  0,				cx_complete_dir,	0		},	/* no key */
	{ 0, 0,  0,				cx_complete_cmd,	0		},	/* no key */
	{ 0, 0,  0,				cx_complete_user,	0		},	/* no key */
	{ 0, 0,  0,				cx_complete_group,	0		},	/* no key */
	{ 0, 0,  0,				cx_complete_env,	0		},	/* no key */
	{ 0, 1, 'p',			cx_complete_hist,	0		},
	{ 1, 0,  KEY_F(2),		cx_edit_paste_currentfile,	0	},
	{ 1, 1,  KEY_F(2),		cx_edit_paste_filenames,	0	},
	{ 0, 0,  WCH_CTRL('A'),	cx_edit_paste_path,	0		},
	{ 0, 0,  WCH_CTRL('E'),	cx_edit_paste_dir2,	0		},
	{ 0, 1,  WCH_CTRL('E'),	cx_edit_paste_dir1,	0		},
	{ 0, 0,  WCH_CTRL('O'),	cx_edit_paste_link,	0		},
/* the menu ends here, the following entries are hidden */
	{ 0, 0,  WCH_CTRL('I'),	cx_paste_pick,		OPT_CURS	},
	{ 0, 0,  WCH_CTRL('M'),	cx_paste_pick,		0			},
	END_TABLE
};

static KEY_BINDING tab_preview[] = {
	{ 0, 0,  WCH_CTRL('M'),	cx_trans_return,	0		},
	{ 2, 0,  0,				cx_preview_mouse,	OPT_ALL	},
	END_TABLE
};

static KEY_BINDING tab_rename[] = {
	{ 0, 0,  WCH_CTRL('M'),	cx_rename,	0	},
	END_TABLE
};

static KEY_BINDING tab_select[] = {
	{ 0, 0,  WCH_CTRL('M'),	cx_select_files,	0	},
	END_TABLE
};

static KEY_BINDING tab_sort[] = {
	{ 0, 0,  L' ',			cx_sort_set,		OPT_CURS	},
	{ 0, 0,  WCH_CTRL('M'),	cx_sort_set,		OPT_CURS	},
	{ 0, 1,  L's',			cx_trans_return,	0			},
	{ 0, 0,  WCH_CTRL('C'),	cx_trans_discard,	0			},
	END_TABLE
};

static KEY_BINDING tab_user[] = {
	{ 0, 0,  WCH_CTRL('I'),	cx_user_paste,		OPT_CURS	},
	{ 0, 0,  WCH_CTRL('M'),	cx_pan_home,		0			},
	{ 0, 1,  L'u',			cx_trans_return,	0			},
	{ 0, 1,  L'g',			cx_trans_group,		0			},
	{ 2, 0,  0,				cx_user_mouse,		OPT_ALL		},
	END_TABLE
};

typedef struct {
	enum MODE_TYPE mode;
	FLAG saveopt;			/* save options when returning from this mode */
	const char *helppages[MAIN_LINKS - 1];	/* corresponding help page(s) */
	const wchar_t *title;	/* panel title, if not variable */
	const wchar_t *help;	/* brief help, 0 if none */
	int (*prepare_fn)(void);
	KEY_BINDING *table[4];	/* up to 3 tables terminated with NULL;
							   order is sometimes important, only
							   the first KEY_BINDING for a given key
							   is followed */
} MODE_DEFINITION;

/* tab_edit and tab_common are appended automatically */
static MODE_DEFINITION mode_definition[] = {
	{ MODE_BM_EDIT0, 0,
		{ "bookmarks_edit" },
		L"DIRECTORY BOOKMARKS > PROPERTIES",
		L"<enter> = edit",
		bm_edit0_prepare, { tab_panel,tab_bm_edit0,0 } },
	{ MODE_BM_EDIT1, 0,
		{ "bookmarks_edit" },
		L"DIRECTORY BOOKMARKS > PROPERTIES > NAME",0,
		bm_edit1_prepare, { tab_bm_edit1,0 } },
	{ MODE_BM_EDIT2, 0,
		{ "bookmarks_edit" },
		L"DIRECTORY BOOKMARKS > PROPERTIES > DIRECTORY", 0,
		bm_edit2_prepare, { tab_bm_edit2,0 } },
	{ MODE_BM, 0,
		{ "bookmarks" },
		L"DIRECTORY BOOKMARKS",
		L"U/D = up/down, N = new, P = properties, <del> = remove",
		bm_prepare, { tab_panel,tab_bm,0 } },
	{ MODE_CFG, 0,
		{ "cfg", "cfg_parameters" },
		L"CONFIGURATION", L"<enter> = change, O = original, S = standard",
		cfg_prepare, { tab_panel,tab_cfg,0 } },
	{ MODE_CFG_EDIT_NUM, 0,
		{ "cfg" },
		L"CONFIGURATION > EDIT", 0,
		cfg_edit_num_prepare, { tab_cfg_edit_num,0 } },
	{ MODE_CFG_EDIT_TXT, 0,
		{ "cfg" },
		L"CONFIGURATION > EDIT", 0,
		cfg_edit_str_prepare, { tab_cfg_edit_str,0 } },
	{ MODE_CFG_MENU, 0,
		{ "cfg" },
		L"CONFIGURATION > SELECT", 0,
		cfg_menu_prepare, { tab_panel, tab_cfg_menu,0 } },
	{ MODE_CMP, 1,
		{ "compare" },
		L"DIRECTORY COMPARE", 0,
		cmp_prepare, { tab_panel,tab_cmp,0 } },
	{ MODE_CMP_SUM, 1,
		{ "summary" },
		L"COMPARISON SUMMARY", 0,
		cmp_summary_prepare, { tab_panel,tab_compl_sum,0 } },
	{ MODE_COMPL, 0,
		{ "completion" },
		0, 0,
		compl_prepare, { tab_panel,tab_compl,0 } },
	{ MODE_DESELECT, 0,
		{ "select" },
		L"DESELECT FILES", L"wildcards: ? * and [..], see help",
		select_prepare, { tab_panel,tab_select,0 } },
	{ MODE_DIR, 0,
		{ "dir" },
		L"CHANGE WORKING DIRECTORY", L"<tab> = insert/complete the directory name",
		dir_main_prepare, { tab_panel,tab_dir,0 } },
	{ MODE_DIR_SPLIT, 0,
		{ "dir" },
		L"CHANGE WORKING DIRECTORY", 0,
		dir_split_prepare, { tab_panel,tab_dir,0 } },
	{ MODE_FILE, 0,
		{ "file1", "file2", "file3" },
		0, 0,
		files_main_prepare, { tab_panel,tab_editcmd,tab_mainmenu,0 } },
	{ MODE_FOPT, 1,
		{ "filter_opt" },
		L"FILTERING AND PATTERN MATCHING OPTIONS", 0,
		fopt_prepare, { tab_panel,tab_fopt,0 } },
	{ MODE_GROUP, 0,
		{ "user" },
		L"GROUP INFORMATION", L"<tab> = insert the group name",
		group_prepare, { tab_panel,tab_group,0 } },
	{ MODE_HELP, 0,
		{ "help" },
		0, L"Please report any errors at https://github.com/xitop/clex/issues",
		help_prepare, { tab_help_panel,tab_panel,tab_help,0 } },
	{ MODE_HIST, 0,
		{ "history" },
		L"COMMAND HISTORY", L"<tab> = insert, <esc> <del> = delete",
		hist_prepare, { tab_panel,tab_hist,0 } },
	{ MODE_INSCHAR, 0,
		{ "insert" },
		L"EDIT > INSERT SPECIAL CHARACTERS",
		L"^X (^ and X) = ctrl-X, DDD = decimal code, \\xHHH or 0xHHH or U+HHH = hex code",
		inschar_prepare, { tab_panel,tab_inschar,0 } },
	{ MODE_LOG, 0,
		{ "log" },
		L"PROGRAM LOG", L"<-- and --> = scroll, M = add mark",
		log_prepare, { tab_panel,tab_log,0 } },
	{ MODE_MAINMENU, 0,
		{ "menu" },
		L"MAIN MENU", 0,
		menu_prepare, { tab_panel,tab_mainmenu,tab_mainmenu2,0 } },
	{ MODE_NOTIF, 1,
		{ "notify" },
		L"NOTIFICATIONS", 0,
		notif_prepare, { tab_panel,tab_notif,0 } },
	{ MODE_PASTE, 0,
		{ "paste" },
		L"COMPLETE/INSERT NAME", 0,
		paste_prepare, { tab_panel,tab_pastemenu,0 } },
	{ MODE_PREVIEW, 0,
		{ "preview" },
		0,L"<enter> = close preview",
		preview_prepare, { tab_panel,tab_preview,0 } },
	{ MODE_RENAME, 0,
		{ "rename" },
		L"RENAME FILE", 0,
		rename_prepare, { tab_rename,0 } },
	{ MODE_SELECT, 0,
		{ "select" },
		L"SELECT FILES", L"wildcards: ? * and [..], see help",
		select_prepare, { tab_panel,tab_select,0 } },
	{ MODE_SORT, 1,
		{ "sort" },
		L"SORT ORDER", 0,
		sort_prepare, { tab_panel,tab_sort,0 } },
	{ MODE_USER, 0,
		{ "user" },
		L"USER INFORMATION", L"<tab> = insert the user name",
		user_prepare, { tab_panel,tab_user,0 } },
	{ 0, 0, {0} , 0, 0, 0, { 0 } }
};

/* linked list of all control loop instances */
struct operation_mode {
	MODE_DEFINITION *modedef;
	PANEL_DESC *panel;
	TEXTLINE *textline;
	struct operation_mode *previous;
};

static struct operation_mode mode_init = { &mode_definition[ARRAY_SIZE(mode_definition) - 1],0,0,0 };
static struct operation_mode *clex_mode = &mode_init;

int
get_current_mode(void)
{
	return clex_mode->modedef->mode;
}

int
get_previous_mode(void)
{
	return clex_mode->previous->modedef->mode;
}

void
fopt_change(void)
{
	struct operation_mode *pmode;

	for (pmode = clex_mode; pmode->modedef->mode; pmode = pmode->previous)
		if (pmode->panel->filter && pmode->panel->filtering)
			pmode->panel->filter->changed = 1;
}

static MODE_DEFINITION *
get_modedef(int mode)
{
	MODE_DEFINITION *p;

	for (p = mode_definition; p->mode; p++)
		if (p->mode == mode)
			return p;

	err_exit("BUG: operation mode %d is invalid",mode);

	/* NOTREACHED */
	return 0;
}

const char **
mode2help(int mode)
{
	return get_modedef(mode)->helppages;
}

static KEY_BINDING *
callfn(KEY_BINDING *tab, int idx)
{
	PANEL_DESC *pd;

	if ((tab[idx].options & OPT_CURS) && !VALID_CURSOR(panel))
		return 0;

	/* set cursor for tables that correspond with their respective panels (menu) */
	if ((tab == tab_mainmenu || tab == tab_mainmenu2) && get_current_mode() == MODE_MAINMENU)
		pd = panel_mainmenu.pd;
	else if (tab == tab_pastemenu)
		pd = panel_paste.pd;
	else
		pd = 0;
	if (pd && idx != pd->curs && idx < pd->cnt) {
		pd->curs = idx;
		pan_adjust(pd);
		win_panel_opt();
	}

	(*tab[idx].fn)();
	return tab;
}

static KEY_BINDING *
do_action(wint_t key, KEY_BINDING **tables)
{
	int i, t1, t2, noesc_idx;
	wint_t key_lc;
	FLAG fkey, filt;
	EXTRA_LINE *extra;
	KEY_BINDING *tab, *noesc_tab;
	static KEY_BINDING *append[4] = { tab_edit, tab_common, tab_mouse, 0 };

	fkey = kinp.fkey;
	filt = panel->filtering == 1;

	/* key substitutions to simplify the program */
	if (fkey == 1) {
		if (key == KEY_ENTER) {
			fkey = 0;
			key = WCH_CTRL('M');
		}
#ifdef KEY_SUSPEND
		/* Ctrl-Z (undo) is sometimes mapped to KEY_SUSPEND, that is undesired */
		else if (key == KEY_SUSPEND) {
			fkey = 0;
			key = WCH_CTRL('Z');
		}
#endif
#ifdef KEY_UNDO
		else if (key == KEY_UNDO) {
			fkey = 0;
			key = WCH_CTRL('Z');
		}
#endif
#ifdef KEY_REDO
		else if (key == KEY_REDO) {
			fkey = 0;
			key = WCH_CTRL('Y');
		}
#endif
	}
	else if (fkey == 0) {
		if (key == WCH_CTRL('G'))
			key = WCH_CTRL('C');
		else if (key == WCH_CTRL('H')) {
			key = KEY_BACKSPACE;
			fkey = 1;
		}
		else if (key == '\177') {
			/* CURSES should have translated the \177 code.
			   We are doing it only because losing the DEL key is quite annoying */
			key = disp_data.bs177 ? KEY_BACKSPACE : KEY_DC;
			fkey = 1;
		}
		/* end of key substitutions */

		/* cancel filter ? */
		if (filt && ((key == WCH_CTRL('M') && !kinp.prev_esc) || key == WCH_CTRL('C'))) {
			if (panel->type == PANEL_TYPE_DIR && panel->filter->size > 0)
				panel->filtering = 2;
				/* not turning it off because of some annoying side effects in the dir panel */
			else {
				filter_off();
				filter_help();
			}
			return 0;
		}
		if (panel->filtering == 2 && key == WCH_CTRL('C') && panel->type == PANEL_TYPE_FILE) {
			filter_off();
			filter_help();
			return 0;
		}
	}

	/* extra panel lines */
	if (panel->min < 0 && panel->curs < 0 && ((fkey == 0 && key == WCH_CTRL('M')) ||
	  (fkey == 2 && MI_DC(1) && MI_AREA(PANEL)
	  && panel->top + minp.ypanel < 0 && panel->top + minp.ypanel == panel->curs))) {
		extra = panel->extra + (panel->curs - panel->min);
		next_mode = extra->mode_next;
		if (extra->fn)
			(*extra->fn)();
		return 0;
	}

	/* mouse event substitutions */
	if (fkey == 2 && MI_DC(1)) {
		/* double click in a panel or on the input line -> enter */
		if (MI_CURSBAR || (MI_AREA(LINE) && textline)) {
			key = WCH_CTRL('M');
			fkey = 0;	/* kinp.fkey is still 2 */
		}
	}

	key_lc = fkey != 0 ? key : towlower(key);

	noesc_tab = 0;
	noesc_idx = 0;	/* prevents compiler warning */
	for (t1 = t2 = 0; (tab = tables[t1]) || (tab = append[t2]); tables[t1] ? t1++ : t2++) {
		if (tab == tab_edit) {
			if  (filt)
				tab = tab_filteredit;
			else if (textline == 0)
				continue;
		}

		for (i = 0; tab[i].fn; i++)
			if (fkey == tab[i].fkey && key_lc == tab[i].key
			  && (!filt || (tab[i].options & OPT_NOFILT) == 0)) {
				if (tab[i].options & OPT_ALL)
					callfn(tab,i);
				else if (kinp.prev_esc && !tab[i].escp) {
					/*
					 * an entry with 'escp' flag has higher priority,
					 * we must continue to search the tables to see
					 * if such entry for the given key exists
					 */
					if (noesc_tab == 0) {
						/* accept only the first definition */
						noesc_tab = tab;
						noesc_idx = i;
					}
				}
				else if (kinp.prev_esc || !tab[i].escp)
					return callfn(tab,i);
			}
	}
	if (noesc_tab)
		return callfn(noesc_tab,noesc_idx);

	/* key not found in the tables */
	if (fkey == 0 && !kinp.prev_esc && !iswcntrl(key)) {
		if (filt) {
			 filteredit_insertchar(key);
			 return tab_filteredit;
		}
		if (textline) {
			edit_insertchar(key);
			return tab_insertchar;
		}
	}

	if (fkey != 2)
		msgout(MSG_i,"pressed key has no function ");
	return 0;
}

/*
 * main control loop for a selected mode 'mode'
 * control loops for different modes are nested whenever necessary
 */
void
control_loop(int mode)
{
	KEY_BINDING *kb_tab;
	struct operation_mode current_mode, *pmode;
	FLAG filter, nr;

	for (pmode = clex_mode; pmode->modedef->mode; pmode = pmode->previous)
		if (pmode->modedef->mode == mode) {
			msgout(MSG_i,"The requested panel is already in use");
			return;
		}

	current_mode.previous = clex_mode;
	/* do not call any function that might call get_current_mode()
		while current_mode is in inconsistent state (modedef undefined) */
	clex_mode = &current_mode;
	/* panel and textline inherited the from previous mode */
	clex_mode->panel = clex_mode->previous->panel;
	clex_mode->textline = clex_mode->previous->textline;

	for (next_mode = mode; /* until break */; ) {
		clex_mode->modedef = get_modedef(next_mode);
		next_mode = 0;
		win_sethelp(HELPMSG_BASE,0);
		win_sethelp(HELPMSG_TMP,0);
		if ((*clex_mode->modedef->prepare_fn)() < 0)
			break;
		win_sethelp(HELPMSG_BASE,clex_mode->modedef->help);
		win_settitle(clex_mode->modedef->title);
		win_bar();
		if (panel != clex_mode->panel) {
			if (panel->filtering || (clex_mode->panel && clex_mode->panel->filtering))
				win_filter();
			pan_adjust(panel);
			win_panel();

			clex_mode->panel = panel;
		}

		if (textline != clex_mode->textline) {
			undo_reset();
			edit_adjust();
			win_edit();
			clex_mode->textline = textline;
		}

		for (; /* until break */;) {
			undo_before();
			kb_tab = do_action(kbd_input(),clex_mode->modedef->table);
			undo_after();
			if (next_mode) {
				if (next_mode == MODE_SPECIAL_RETURN
				  && clex_mode->previous->modedef->mode == 0) {
					msgout(MSG_i,"to quit CLEX press <esc> Q");
					next_mode = 0;
				}
				else
					break;
			}

			/* some special handling not implemented with tables */
			switch (clex_mode->modedef->mode) {
			case MODE_COMPL:
				if (kb_tab == tab_edit || kb_tab == tab_insertchar)
					next_mode = MODE_SPECIAL_RETURN;
				break;
			case MODE_DIR:
			case MODE_DIR_SPLIT:
				if (textline->size == 0 || kb_tab == tab_panel)
					nr = 0;
				else if (kb_tab == tab_mouse)
					nr = minp.area > AREA_BAR;
				else
					nr = 1;
				if (panel->norev != nr) {
					panel->norev = nr;
					win_edit();
					win_panel_opt();
				}
				break;
			case MODE_MAINMENU:
				if (kb_tab == tab_mainmenu || kb_tab == tab_mainmenu2)
					next_mode = MODE_SPECIAL_RETURN;
				break;
			default:
				;	/* shut-up the compiler */
			}
			if (panel->filtering && panel->filter->changed)
				filter_update();

			if (next_mode)
				break;
		}

		if (next_mode == MODE_SPECIAL_QUIT)
			err_exit("Normal exit");
		if (next_mode == MODE_SPECIAL_RETURN) {
			if (clex_mode->modedef->saveopt)
				opt_save();
			next_mode = 0;
			break;
		}
	}

	clex_mode = clex_mode->previous;
	win_bar();
	if (panel != clex_mode->panel) {
		filter = panel->filtering || clex_mode->panel->filtering;
		panel = clex_mode->panel;
		if (filter)
			win_filter();
		pan_adjust(panel);		/* screen size might have changed */
		win_panel();
	}
	if (textline != clex_mode->textline) {
		textline = clex_mode->textline;
		edit_adjust();
		win_edit();
	}
	win_sethelp(HELPMSG_BASE,0);
	win_sethelp(HELPMSG_BASE,clex_mode->modedef->help);
	win_settitle(clex_mode->modedef->title);
}

static int
menu_prepare(void)
{
	/* leave cursor position unchanged */
	panel = panel_mainmenu.pd;
	textline = 0;
	return 0;
}

static void
cx_menu_pick(void)
{
	(*tab_mainmenu[panel_mainmenu.pd->curs].fn)();
	if (next_mode == 0)
		next_mode = MODE_SPECIAL_RETURN;
}

static int
paste_prepare(void)
{
	/* leave cursor position unchanged */
	panel_paste.wordstart = 0;
	panel = panel_paste.pd;
	/* textline unchanged */
	return 0;
}

static void
cx_paste_pick(void)
{
	(*tab_pastemenu[panel_paste.pd->curs].fn)();
}

void
cx_version(void)
{
	msgout(MSG_i,"Welcome to CLEX " VERSION " !");
}

/*
 * err_exit() is the only exit function that terminates CLEX main
 * process. It is used for normal (no error) termination as well.
 */
void
err_exit(const char *format, ...)
{
	va_list argptr;

	/*
	 * all cleanup functions used here:
	 *  - must not call err_exit()
	 *  - must not require initialization
	 */
	fw_cleanup();
	opt_save();
	xterm_title_restore();
	mouse_restore();
	if (disp_data.curses)
		curses_stop();
	tty_reset();

	fputs("\nTerminating CLEX: ",stdout);
	msgout(MSG_AUDIT,"Terminating CLEX, reason is given below");
	msgout(MSG_HEADING,0);
	va_start(argptr,format);
	vmsgout(MSG_I,format,argptr);
	va_end(argptr);
	putchar('\n');
	logfile_close();

	jc_reset();	/* this puts CLEX into background, no terminal I/O possible any more */
	exit(EXIT_SUCCESS);
	/* NOTREACHED */
}
