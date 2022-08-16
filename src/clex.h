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

/*
 * naming convention for strings and buffers:
 *   #define XXX_STR is a buffer size, i.e. with the trailing null byte
 *   #define XXX_LEN is max string length, i.e. without the null byte
 */

/* useful macros */
#define ARRAY_SIZE(X)	((int)(sizeof(X) / sizeof(X[0])))
#define TOGGLE(X)		((X) = !(X))
#define TSET(X)			((X) ? 1 : ((X) = 1, 0))	/* test & set */
#define TCLR(X)			((X) ? ((X) = 0, 1) : 0)	/* test & clear */
#define LIMIT_MIN(X,MIN)	do if ((X) < (MIN)) (X) = (MIN); while (0)
#define LIMIT_MAX(X,MAX)	do if ((X) > (MAX)) (X) = (MAX); while (0)
#define CH_CTRL(X)		((X) & 0x1F)				/* byte  ASCII ctrl-X */
#define WCH_CTRL(X)		((wchar_t)CH_CTRL(X))		/* wide char ASCII ctrl-X */
#define WCH_ESC			L'\033'						/* wchar ASCII escape */
/* CMP: no overflow and result is an int even if V1 and V2 are not */
#define CMP(V1,V2) ((V1) == (V2) ? 0 : (V1) < (V2) ? -1 : 1)
#define STR(X)	STRINGIZE(X)
#define STRINGIZE(X)	#X

/* typedefs */
typedef unsigned short int FLAG;	/* true or false */
typedef short int CODE;				/* usually enum or some #define-d value */

/* minimal required screen size */
#define MIN_COLS	64
#define MIN_LINES	12

/* limits for prompts */
#define MAX_SHORT_CWD_LEN	((MIN_COLS * 2) / 5)	/* 40% */
#define MAX_PROMPT_WIDTH	((MIN_COLS * 4) / 5)	/* 80% */

/* max textline lines - valid values: 3, 4 and 5 */
#define MAX_CMDLINES	4

/* operation modes */
enum MODE_TYPE {
	/*
	 * value 0 is reserved, it means mode unchanged in control_loop()
	 * and also mode not set during startup
	 */
	MODE_VALUE_RESERVED = 0,
	/* regular modes */
	MODE_BM, MODE_BM_EDIT0, MODE_BM_EDIT1, MODE_BM_EDIT2,
	MODE_CFG, MODE_CFG_EDIT_NUM, MODE_CFG_EDIT_TXT, MODE_CFG_MENU,
	MODE_COMPL, MODE_CMP, MODE_CMP_SUM, MODE_DESELECT,
	MODE_DIR, MODE_DIR_SPLIT, MODE_FILE, MODE_FOPT, MODE_GROUP, MODE_HELP,
	MODE_HIST, MODE_INSCHAR, MODE_LOG, MODE_MAINMENU, MODE_NOTIF, MODE_PASTE,
	MODE_PREVIEW, MODE_RENAME, MODE_SELECT, MODE_SORT, MODE_USER,
	/* pseudo-modes */
	MODE_SPECIAL_QUIT, MODE_SPECIAL_RETURN
};

/* info about screen display/layout/appearance */
typedef struct {
	FLAG curses;		/* curses active */
	FLAG wait;			/* a message has been written to the
						   text-mode screen, wait for a keypress
						   before starting curses */
	FLAG noenter;		/* if set, disable "press enter to continue" after command execution */
	unsigned int noenter_hash;		/* a hash value of the command line is saved in order to detect
									   modifications which cancel the 'noenter' feature */
	FLAG bs177;			/* KEY_BACKSPACE sends \177 (see control.c) */
	FLAG xterm;			/* TERM is xterm or similar type which can change window title */
	FLAG noxterm;		/* TERM is unable to change the window title */
						/* note: if not known, both xterm and noxterm are 0 */
	FLAG xwin;			/* running in X Window environment */
	FLAG mouse;			/* KEY_MOUSE suitable for xterm mouse tracking */
	FLAG mouse_swap;	/* left-handed mouse (swap left and right buttons) */
	int scrcols;		/* number of columns */
	int pancols;		/* number of columns in the panel area */
	int panrcol;		/* pancols adjusted for putwcs_trunc_col() */
	int scrlines;		/* number of lines */
	int cmdlines;		/* number of lines in the textline area */
	int panlines;		/* number of lines in the panel area */
	int date_len;		/* length of date/time field */
	int dir1end, dir2start;	/* columns of the directory names in the file panel title */
	wchar_t *layout_panel;	/* layout: file panel part */
	wchar_t *layout_line;	/* layout: info line part */
} DISP_DATA;

/* info about language/encoding */
typedef struct {
	FLAG utf8;			/* UTF-8 mode */
	wchar_t sep000;		/* thousands separator */
	wchar_t repl;		/* replacement for unprintable characters */
	const wchar_t *time_fmt, *date_fmt;	/* time/date format strings (for strftime) */
} LANG_DATA;

/* info about the user */
#define SHELL_SH	0	/* Bourne shell or similar */
#define SHELL_CSH	1	/* C-shell or similar */
#define SHELL_OTHER	2	/* other */
typedef struct {
	const char *login;			/* my login name */
	const wchar_t *loginw;		/* my login name */
	const char *host;			/* my host */
	const wchar_t *hostw;		/* my host */
	const char *homedir;		/* my home directory */
	const wchar_t *homedirw;	/* my home directory */
	const char *shell;			/* my login shell - full path */
	const wchar_t *shellw;		/* my login shell - basename only */
	const char *subdir;			/* configuration directory */
	const char *file_cfg;		/* configuration file */
	const char *file_opt;		/* options file */
	const char *file_bm;		/* bookmarks file */
	CODE shelltype;				/* one of SHELL_XXX */
	FLAG isroot;				/* effective uid is 0(root) */
	FLAG nowrite;				/* do not write config/options/bookmark file */
	FLAG noconfig;				/* no config file, cfg-clex recommended */
} USER_DATA;

typedef struct {
	pid_t pid;					/* process ID */
	char pidstr[16];			/* PID as a string */
	mode_t umask;				/* umask value */
} CLEX_DATA;

/* description of an editing operation */
enum OP_TYPE {
	OP_NONE = 0,	/* no change (cursor movement is OK) - value must be zero */
	OP_INS,			/* simple insert */
	OP_DEL,			/* simple deletion */
	OP_CHANGE		/* modification other than OP_INS or OP_DEL */
};
typedef struct {
	enum OP_TYPE code;
/* 'pos' and 'len' are used with OP_INSERT and OP_DELETE only */
	int pos;		/* position within the edited string */
	int len;		/* length of the inserted/deleted part */
} EDIT_OP;

#define UNDO_LEVELS 10	/* undo steps */

/* line of text where the user can enter and edit his/her input */
typedef struct {
	USTRINGW prompt;	/* prompt */
	int promptwidth;	/* prompt width in display columns */
	USTRINGW line;		/* user's input */
	int size;			/* number of chars in the line */
	int curs;			/* cursor position from 0 to 'size' */
	int offset;			/* offset - when the line is too long,
						   first 'offset' characters are hidden */
	/* values for the UNDO function */
	struct {
		USTRINGW save_line;
		int save_size;
		int save_curs;
		int save_offset;
	} undo [UNDO_LEVELS];	/* used in a circular manner */
	int undo_base;			/* index of the first entry */
	int undo_levels;		/* occupied entries for undo */
	int redo_levels;		/* freed entries usable for redo */
	EDIT_OP last_op;		/* last editing operation */
} TEXTLINE;

/* minimalistic version of TEXTLINE used for panel filters */
#define INPUT_STR 23
typedef struct {
	wchar_t line[INPUT_STR];	/* user's input */
	int size;					/* number of chars in the line */
	int curs;					/* cursor position from 0 to 'size' */
	FLAG changed;				/* 'line' has been modified */
} INPUTLINE;

/********************************************************************/

/* keyboard and mouse input */

typedef struct {
	CODE fkey;		/* 2 = not a key, but a mouse event, 1 = keypad or 0 = character */
	wint_t key;
	FLAG prev_esc;	/* previous key was an ESCAPE */
} KBD_INPUT;

enum AREA_TYPE {
	/* from top to bottom */
	AREA_TITLE = 0, AREA_TOPFRAME, AREA_PANEL, AREA_BOTTOMFRAME,
	AREA_INFO, AREA_HELP, AREA_BAR, AREA_PROMPT, AREA_LINE,
	AREA_NONE
};
typedef struct {
	/* mouse data */
	int y, x;			/* zero-based coordinates: line and column */
	CODE button;		/* regular buttons: 1, 2 and 3, wheel: 4 and 5 */
	FLAG doubleclick;	/* a doubleclick (NOTE: first click is aslo reported!) */
	FLAG motion;		/* the mouse was in motion (drag) */

	/* computed values */
	int area;			/* one of AREA_xxx */
	int ypanel;			/* panel screen line (AREA_PANEL only): 0 = top */
	int cursor;			/* calculated cursor position, see mouse_data() in inout.c */
} MOUSE_INPUT;

/* shortcuts */
#define MI_B(X)		(minp.button == (X))
#define MI_DC(X)	(MI_B(X) && minp.doubleclick)
#define MI_CLICK	(minp.button == 1 || minp.button == 3)
#define MI_WHEEL	(minp.button == 4 || minp.button == 5)
#define MI_AREA(X)	(minp.area == AREA_ ## X)
#define MI_CURSBAR	(MI_AREA(PANEL) && VALID_CURSOR(panel) && panel->top + minp.ypanel == panel->curs)
#define MI_DRAG		(minp.motion)
#define MI_PASTE	(MI_B(3) && !MI_DRAG && MI_CURSBAR)

/********************************************************************/

/* panel types */
enum PANEL_TYPE {
	PANEL_TYPE_BM = 0, PANEL_TYPE_CFG, PANEL_TYPE_CFG_MENU, PANEL_TYPE_CMP, PANEL_TYPE_CMP_SUM,
	PANEL_TYPE_COMPL, PANEL_TYPE_DIR, PANEL_TYPE_DIR_SPLIT, PANEL_TYPE_FILE,
	PANEL_TYPE_FOPT, PANEL_TYPE_GROUP, PANEL_TYPE_HELP, PANEL_TYPE_HIST,
	PANEL_TYPE_LOG, PANEL_TYPE_MAINMENU, PANEL_TYPE_NOTIF, PANEL_TYPE_PASTE,
	PANEL_TYPE_PREVIEW, PANEL_TYPE_SORT, PANEL_TYPE_USER
};

/*
 * extra lines appear in a panel before the first regular line,
 * extra lines:  -MIN .. -1
 * regular lines:   0 .. MAX
 */
typedef struct {
	const wchar_t *text;	/* text to be displayed in the panel */
							/* default (if null): "Exit this panel" */
	const wchar_t *info;	/* text to be displayed in the info line */
	/* when this extra line is selected: */
	CODE mode_next;			/* set next_mode to this mode and then ... */
	void (*fn)(void); 		/* ... invoke this function */
} EXTRA_LINE;

/* description of a panel */
typedef struct {
	int cnt, top;	/* panel lines: total count, top of the screen */
	int curs, min;	/* panel lines: cursor bar, top of the panel */
	/*
	 * 'min' is used to insert extra lines before the real first line
     * which is always line number 0; to insert N extra lines set
	 * 'min' to -N; the number of extra lines is not included in 'cnt' 
	 */
	enum PANEL_TYPE type;
	FLAG norev;		/* do not show the current line in reversed video */
	EXTRA_LINE *extra;	/* extra panel lines */
	INPUTLINE *filter;	/* filter (if applicable to this panel type) */
	void (*drawfn)(int);/* function drawing one line of this panel */
	CODE filtering;		/* filter: 0 = off */
						/* 1 = on - focus on the filter string */
						/* 2 = on - focus on the command line */
	const wchar_t *help;	/* helpline override */
} PANEL_DESC;

#define VALID_CURSOR(P) ((P)->cnt > 0 && (P)->curs >= 0 && (P)->curs < (P)->cnt)

/********************************************************************/

/*
 * file types recognized in the file panel,
 * if you change this, you must also update
 * the type_symbol[] array in inout.c
 */
#define FT_PLAIN_FILE		 0
#define FT_PLAIN_EXEC		 1
#define FT_PLAIN_SUID		 2
#define FT_PLAIN_SUID_ROOT	 3
#define FT_PLAIN_SGID		 4
#define FT_DIRECTORY		 5
#define FT_DIRECTORY_MNT	 6
#define FT_DEV_BLOCK		 7
#define FT_DEV_CHAR			 8
#define FT_FIFO				 9
#define FT_SOCKET			10
#define FT_OTHER			11
#define FT_NA				12

/* file type tests */
#define IS_FT_PLAIN(X)		((X) >= 0 && (X) <= 4)
#define IS_FT_EXEC(X)		((X) >= 1 && (X) <= 4)
#define IS_FT_DIR(X)		((X) >= 5 && (X) <= 6)
#define IS_FT_DEV(X)		((X) >= 7 && (X) <= 8)

/*
 * if you change any of the FE_XXX_STR #defines, you must change
 * the corresponding stat2xxx() function(s) in list.c accordingly
 */
/* text buffer sizes */			/* examples: */
#define FE_LINKS_STR	 4		/* 1  999  max  */
#define FE_TIME_STR		23		/* 11:34:30_am_2010/12/01 */
#define FE_AGE_STR		10		/* -01:02:03 */
#define FE_SIZE_DEV_STR	12		/* 3.222.891Ki */
#define FE_MODE_STR 	 5		/* 0644 */
#define FE_NAME_STR		17		/* root */
#define FE_OWNER_STR	(2 * FE_NAME_STR)	/* root:mail */

/*
 * file description - exhausting, isn't it ?
 * we allocate many of these, bitfields save memory
 */
typedef struct {
	SDSTRING  file;			/* file name - as it is */
	SDSTRINGW filew;		/* file name - converted to wchar for the screen output */
	USTRING link;			/* where the symbolic link points to */
	USTRINGW linkw;			/* ditto */
	const char *extension;	/* file name extension (suffix) */
	time_t mtime;			/* last file modification */
	off_t size;				/* file size */
	dev_t devnum;			/* major/minor numbers (devices only) */
	CODE file_type;			/* one of FT_XXX */
	uid_t uid, gid;			/* owner and group */
	short int mode12;		/* file mode - low 12 bits */
	unsigned int select:1;		/* flag: this entry is selected */
	unsigned int symlink:1;		/* flag: it is a symbolic link */
	unsigned int dotdir:2;		/* . (1) or .. (2) directory */
	unsigned int fmatch:1;		/* flag: matches the filter */
	/*
	 * note: the structure members below are used
	 * only when the file panel layout requires them
	 */
	unsigned int normal_mode:1;	/* file mode same as "normal" file */
	unsigned int links:1;		/* has multiple hard links */
	wchar_t atime_str[FE_TIME_STR];	/* access time */
	wchar_t ctime_str[FE_TIME_STR];	/* inode change time */
	wchar_t mtime_str[FE_TIME_STR];	/* file modification time */
	wchar_t owner_str[FE_OWNER_STR];/* owner and group */
	char age_str[FE_AGE_STR];		/* time since the last modification ("file age") */
	char links_str[FE_LINKS_STR];	/* number of links */
	char mode_str[FE_MODE_STR];		/* file mode - octal number */
	char size_str[FE_SIZE_DEV_STR];	/* file size or dev major/minor */
} FILE_ENTRY;

/*
 * When a filter or the selection panel is activated or when panels are switched, the
 * file panel will be refreshed if the contents are older than PANEL_EXPTIME seconds.
 * This time is not configurable because it would confuse a typical user.
 */
#define PANEL_EXPTIME 60

typedef struct ppanel_file {
	PANEL_DESC *pd;
	USTRING dir;			/* working directory */
	USTRINGW dirw;			/* working directory for screen output */
	struct ppanel_file *other;	/* primary <--> secondary panel ptr */
	time_t timestamp;		/* when was the directory listed */
	FLAG expired;			/* expiration: panel needs to be re-read */
	FLAG filtype;			/* filter type: 0 = substring, 1 = pattern */
	CODE order;				/* sort order: one of SORT_XXX */
	CODE group;				/* group by type: one of GROUP_XXX */
	CODE hide;				/* ignore hidden .files: one of HIDE_XXX */
	FLAG hidden;			/* there exist hidden .files not shown */
	/* unfiltered data - access only in list.c and sort.c */
	int all_cnt;			/* number of all files */
	int all_alloc;			/* allocated FILE_ENTRies in 'all_files' below */
	FILE_ENTRY **all_files;	/* list of files in panel's working directory 'dir' */
	/* filtered data */
	int filt_alloc;
	int selected_out;		/* number of selected entries filtered out */
	FILE_ENTRY **filt_files;/* list of files selected by the filter */
	/* presented data */
	int selected;
	FILE_ENTRY **files;		/* 'all_files' or 'filt_files' depending on the filter status */
	/* column width information (undefined for unused fields) */
	/* number of blank leading characters */
	int cw_ln1;			/* $l */
	int cw_sz1;			/* $r,$s,$R,$S */
	int cw_ow1;			/* $o */
	int cw_age;			/* $g */
	/* field width */
	int cw_mod;			/* $M */
	int cw_lns;			/* $> */
	int cw_lnh;			/* $L */
	int cw_sz2;			/* $r,$s,$R,$S */
	int cw_ow2;			/* $o */
} PANEL_FILE;

/********************************************************************/

typedef struct bookmark {
	SDSTRINGW name;
	USTRING  dir;
	USTRINGW dirw;
} BOOKMARK;

typedef struct {
	PANEL_DESC *pd;
	BOOKMARK **bm;
	int cw_name;	/* field width */
} PANEL_BM;

typedef struct {
	PANEL_DESC *pd;
	BOOKMARK *bm;
} PANEL_BM_EDIT;

/********************************************************************/

/* notifications */
enum NOTIF_TYPE {
	NOTIF_RM = 0, NOTIF_LONG, NOTIF_DOTDIR, NOTIF_SELECTED, NOTIF_FUTURE,
	NOTIF_TOTAL_
};

typedef struct {
	PANEL_DESC *pd;
	FLAG option[NOTIF_TOTAL_];
} PANEL_NOTIF;

/* IMPORTANT: value 1 = notification disabled, 0 = enabled (default) */
#define NOPT(X)		(panel_notif.option[X])

/********************************************************************/

/*
 * file sort order and grouping- if you change this, you must also update
 * panel initization in start.c and descriptions in inout.c
 */
enum SORT_TYPE {
	SORT_NAME_NUM = 0, SORT_NAME, SORT_EXT, SORT_SIZE,
	SORT_SIZE_REV, SORT_TIME, SORT_TIME_REV, SORT_EMAN,
	SORT_TOTAL_
};

enum GROUP_TYPE {
	GROUP_NONE = 0, GROUP_DSP, GROUP_DBCOP,
	GROUP_TOTAL_
};

enum HIDE_TYPE {
	HIDE_NEVER, HIDE_HOME, HIDE_ALWAYS,
	HIDE_TOTAL_
};

typedef struct {
	PANEL_DESC *pd;
	/* current values: */
	CODE group;				/* group by type: one of GROUP_XXX */
	CODE order;				/* default file sort order: one of SORT_XXX */
	CODE hide;				/* do not show hidden .files */
	/* for the user interface menu: */
	CODE newgroup;
	CODE neworder;
	CODE newhide;
} PANEL_SORT;

/********************************************************************/

typedef struct {
	const char *name;		/* directory name */
	const wchar_t *namew;	/* directory name for display */
	int shlen;				/* length of the repeating 'namew' part */
} DIR_ENTRY;

typedef struct {
	PANEL_DESC *pd;
	DIR_ENTRY *dir;			/* list of directories to choose from */
} PANEL_DIR;

typedef struct {
	PANEL_DESC *pd;
	const char *name;		/* directory name */
} PANEL_DIR_SPLIT;

/********************************************************************/
/* configuration variables in config panel order */
enum CFG_TYPE {
	/* appearance */
	CFG_FRAME, CFG_CMD_LINES, CFG_XTERM_TITLE, CFG_PROMPT,
	CFG_LAYOUT1, CFG_LAYOUT2, CFG_LAYOUT3, CFG_LAYOUT, CFG_KILOBYTE,
	CFG_FMT_TIME, CFG_FMT_DATE, CFG_TIME_DATE,
	/* command execution */
	CFG_CMD_F3, CFG_CMD_F4, CFG_CMD_F5, CFG_CMD_F6, CFG_CMD_F7,
	CFG_CMD_F8, CFG_CMD_F9, CFG_CMD_F10, CFG_CMD_F11, CFG_CMD_F12,
	/* mouse */
	CFG_MOUSE, CFG_MOUSE_SCROLL, CFG_DOUBLE_CLICK,
	/* other */
	CFG_QUOTE, CFG_C_SIZE, CFG_D_SIZE, CFG_H_SIZE,
	/* total count*/
	CFG_TOTAL_
};

typedef struct {
	const char *var;		/* name of the variable */
	const wchar_t *help;	/* one line help */
	void *table;			/* -> internal table with details */
	unsigned int isnum:1;	/* is numeric (not string) */
	unsigned int changed:1;	/* value changed */
	unsigned int saveit:1;	/* value should be saved to disk */
} CFG_ENTRY;

typedef struct {
	PANEL_DESC *pd;
	CFG_ENTRY *config;		/* list of all configuration variables */
} PANEL_CFG;

typedef struct {
	PANEL_DESC *pd;
	const wchar_t **desc;	/* list of textual descriptions */
} PANEL_CFG_MENU;

/********************************************************************/

typedef struct {
	USTRINGW cmd;			/* command text */
	FLAG failed;			/* command failed or not */
} HIST_ENTRY;

typedef struct {
	PANEL_DESC *pd;
	HIST_ENTRY **hist;		/* list of previously executed commands */
} PANEL_HIST;

/********************************************************************/

typedef struct {
	CODE type;				/* one of HL_XXX defined in help.h */
	const char *data;		/* value of HL_LINK, HL_PAGE or HL_VERSION */
	const wchar_t *text;	/* text of HL_TEXT... or HL_TITLE */
	int links;				/* number of links in the whole line
								(valid for leading HL_TEXT only) */
} HELP_LINE;

typedef struct {
	PANEL_DESC *pd;
	CODE pagenum;			/* internal number of current page */
	const wchar_t *title;	/* title of the current help page */
	int lnk_act, lnk_ln;	/* multiple links in a single line:
							lnk_act = which link is active
							lnk_ln = for which line is lnk_act valid */
	HELP_LINE **line;
} PANEL_HELP;

/********************************************************************/

typedef struct {
	SDSTRINGW str;			/* name suitable for a completion */
	FLAG is_link;			/* filenames only: it is a symbolic link */
	CODE file_type;			/* filenames only: one of FT_XXX */
	const wchar_t *aux;		/* additional data (info line) */
} COMPL_ENTRY;

typedef struct {
	PANEL_DESC *pd;
	FLAG filenames;			/* stored names are names of files */
	const wchar_t *aux;		/* type of additional data - as a string */
	const wchar_t *title;	/* panel title */
	COMPL_ENTRY **cand;		/* list of completion candidates */
} PANEL_COMPL;

/********************************************************************/

/* - must correspond with descriptions in draw_line_fopt() in inout.c */
/* - must correspond with panel_fopt initializer in start.c */
/* - fopt_saveopt(), fopt_restoreopt() must remain backward compatible */
enum FOPT_TYPE {
	FOPT_IC, FOPT_ALL, FOPT_SHOWDIR,
	FOPT_TOTAL_
};

typedef struct {
	PANEL_DESC *pd;
	FLAG option[FOPT_TOTAL_];
} PANEL_FOPT;
#define FOPT(X)		(panel_fopt.option[X])

/********************************************************************/

/* - must correspond with descriptions in draw_line_cmp() in inout.c */
/* - must correspond with panel_cmp initializer in start.c */
/* - cmp_saveopt(), cmp_restoreopt() must remain backward compatible */
enum CMP_TYPE {
	CMP_REGULAR, CMP_SIZE, CMP_MODE, CMP_OWNER, CMP_DATA,
	CMP_TOTAL_
};

typedef struct {
	PANEL_DESC *pd;
	FLAG option[CMP_TOTAL_];
} PANEL_CMP;
#define COPT(X)		(panel_cmp.option[X])

/********************************************************************/

#define LOG_LINES		50
#define TIMESTAMP_STR	48

typedef struct {
	CODE level;					/* one of MSG_xxx (defined in log.h) */
	const char *levelstr;		/* level as a string */
	char timestamp[TIMESTAMP_STR];	/* time/date as a string */
	USTRINGW msg;				/* message */
	int cols;					/* message width in screen columns */
} LOG_ENTRY;

typedef struct {
	PANEL_DESC *pd;			/* no additional data */
	int scroll;				/* amount of horizontal text scroll, normally 0 */
	int maxcols;			/* max message width */
	LOG_ENTRY *line[LOG_LINES];
} PANEL_LOG;

/********************************************************************/

typedef struct {
	PANEL_DESC *pd;			/* no additional data */
} PANEL_MENU;

/********************************************************************/

typedef struct {
	PANEL_DESC *pd;
	FLAG wordstart;			/* complete from: 0 = beginning of the word, 1 = cursor position */
} PANEL_PASTE;

/********************************************************************/

#define PREVIEW_LINES	400
#define PREVIEW_BYTES	16383	/* fr_open() adds 1 */

typedef struct {
	PANEL_DESC *pd;
	int realcnt;				/* lines with real data, used for --end-- mark */
	wchar_t *title;				/* name of the file */
	USTRINGW line[PREVIEW_LINES];
} PANEL_PREVIEW;

/********************************************************************/

typedef struct {
	uid_t uid;
	const wchar_t *login;
	const wchar_t *gecos;
} USER_ENTRY;
	
typedef struct {
	PANEL_DESC *pd;
	USER_ENTRY *users;
	int usr_alloc;			/* allocated entries in 'users' */
	size_t maxlen;			/* length of the longest name */
} PANEL_USER;

typedef struct {
	gid_t gid;
	const wchar_t *group;
} GROUP_ENTRY;
	
typedef struct {
	PANEL_DESC *pd;
	GROUP_ENTRY *groups;
	int grp_alloc;			/* allocated entries in 'groups' */
} PANEL_GROUP;

/********************************************************************/
	
typedef struct {
	PANEL_DESC *pd;
	int nonreg1, nonreg2, errors, names, equal;
} PANEL_CMP_SUM;

/********************************************************************/

/* global variables */

extern const void *pcfg[CFG_TOTAL_];

extern DISP_DATA disp_data;
extern LANG_DATA lang_data;
extern USER_DATA user_data;
extern CLEX_DATA clex_data;

extern TEXTLINE *textline;		/* -> active line */
extern TEXTLINE line_cmd, line_dir, line_tmp, line_inschar;

extern MOUSE_INPUT minp;
extern KBD_INPUT kinp;

extern PANEL_DESC *panel;		/* -> description of the active panel */
extern PANEL_FILE *ppanel_file;
extern PANEL_CFG panel_cfg;
extern PANEL_CFG_MENU panel_cfg_menu;
extern PANEL_BM_EDIT panel_bm_edit;
extern PANEL_BM panel_bm;
extern PANEL_CMP panel_cmp;
extern PANEL_COMPL panel_compl;
extern PANEL_CMP_SUM panel_cmp_sum;
extern PANEL_DIR panel_dir;	
extern PANEL_DIR_SPLIT panel_dir_split;	
extern PANEL_FOPT panel_fopt;
extern PANEL_GROUP panel_group;
extern PANEL_HELP panel_help;
extern PANEL_HIST panel_hist;
extern PANEL_LOG panel_log;
extern PANEL_MENU panel_mainmenu;
extern PANEL_NOTIF panel_notif;
extern PANEL_PASTE panel_paste;
extern PANEL_PREVIEW panel_preview;
extern PANEL_SORT panel_sort;
extern PANEL_USER panel_user;

extern CODE next_mode;			/* see control.c comments */
extern volatile FLAG ctrlc_flag;
