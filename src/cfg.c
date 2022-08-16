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

#include <sys/stat.h>		/* mkdir() */
#include <stdarg.h>			/* va_list */
#include <stdio.h>			/* printf() */
#include <string.h>			/* strchr() */

#include "cfg.h"

#include "completion.h"		/* compl_reconfig() */
#include "control.h"		/* control_loop() */
#include "directory.h"		/* dir_reconfig() */
#include "edit.h"			/* edit_setprompt() */
#include "exec.h"			/* set_shellprompt() */
#include "filerw.h"			/* fr_open() */
#include "history.h"		/* hist_reconfig() */
#include "inout.h"			/* win_panel_opt() */
#include "list.h"			/* kb_reconfig() */
#include "log.h"			/* msgout() */
#include "mbwstring.h"		/* convert2w() */
#include "mouse.h"			/* mouse_reconfig() */
#include "panel.h"			/* cx_pan_home() */
#include "xterm_title.h"	/* xterm_title_reconfig() */

#define CFG_FILESIZE_LIMIT 2000	/* config file size limit (in bytes) */
#define CFG_LINES_LIMIT		100	/* config file size limit (in lines) */
#define CFG_ERRORS_LIMIT	 10	/* error limit */

static int error_cnt;			/* error counter */

typedef struct {
	enum CFG_TYPE code;			/* CFG_XXX */
	const wchar_t *extra_val;	/* if defined - name of the special value (represented as 0) */
	int min, initial, max;		/* allowed range and the default value */
	const wchar_t *desc[4];		/* if defined - show this text instead
								   of numbers (enumerated type) */
	int current, new;			/* values */
} CNUM;

static CNUM table_numeric[] = {
	/* enumerated */
	{ CFG_CMD_LINES,	0,
		2, 2, MAX_CMDLINES,
		{	L"2 screen lines",
			L"3 screen lines",
			L"4 screen lines",
			L"5 screen lines" } },
	{ CFG_FRAME,		0,
		0, 0, 2,
		{	L"--------",
			L"========",
			L"line graphics (not supported on some terminals)" } },
	{ CFG_KILOBYTE,		0,
		0, 0, 1,
		{	L"1 KiB is 1024 bytes (IEC standard)",
			L"1 KB  is 1000 bytes (SI standard)" } },
	{ CFG_LAYOUT,		0,
		1, 1, 3,
		{	L"Layout #1",
			L"Layout #2",
			L"Layout #3" } },
	{ CFG_MOUSE,		0,
		0, 1, 2,
		{	L"Disabled",
			L"Enabled, right-handed",
			L"Enabled, left-handed" } },
	{ CFG_TIME_DATE,	0,
		0, 0, 2,
		{	L"Short format: time or date",
			L"Long format: time and date",
			L"Long format: date and time" } },
	{ CFG_XTERM_TITLE,	0,
		0, 1, 1,
		{	L"Disabled",
			L"Enabled" } },
	/* really numeric */
	{ CFG_C_SIZE,		0,			10,  120, 200 },
	{ CFG_D_SIZE,		L"AUTO",	10,    0, 200 },
	{ CFG_H_SIZE,		0,			10,   60, 200 },
	{ CFG_MOUSE_SCROLL,	0,			1,     3, 8   },
	{ CFG_DOUBLE_CLICK,	0,			200, 400, 800 }
};

typedef struct {
	enum CFG_TYPE code;			/* CFG_XXX */
	const wchar_t *extra_val;	/* if defined - name of special value (represented as L"") */
	wchar_t *initial;
	wchar_t current[CFGVALUE_LEN + 1], new[CFGVALUE_LEN + 1];
} CSTR;

static CSTR table_string[] = {
	{ CFG_CMD_F3,		0,	L"more $f"			},
	{ CFG_CMD_F4,		0,	L"vi $f"			},
	{ CFG_CMD_F5,		0,	L"cp -ir $f $2"		},
	{ CFG_CMD_F6,		0,	L"mv -i $f $2"		},
	{ CFG_CMD_F7,		0,	L"mkdir "			},
	{ CFG_CMD_F8,		0,	L"rm $f"			},
	{ CFG_CMD_F9,		0,	L"lpr $f"			},
	{ CFG_CMD_F10,		0,	L""					},
	{ CFG_CMD_F11,		0,	L""					},
	{ CFG_CMD_F12,		0,	L""					},
	{ CFG_FMT_TIME,		L"AUTO",	L""			},
	{ CFG_FMT_DATE,		L"AUTO",	L""			},
	{ CFG_LAYOUT1,		0,	L"$d $S $>$t $M $*|$p $o $L"	},
	{ CFG_LAYOUT2,		0,	L"$d $R $t $*|$p $o",			},
	{ CFG_LAYOUT3,		0,	L"$p $o $s $d $>$t $*|mode=$m atime=$a ctime=$i links=$l" },
	{ CFG_PROMPT,		0,	L"$s $p "			},
	{ CFG_QUOTE,		0,	L""					}
};

/* everything in one place and alphabetically sorted for easy editing */
static struct {
	CODE code;				/* internal code */
	const char *name;		/* name in cfg file - may not exceed max length of CFGVAR_LEN */
	const wchar_t *help;	/* help text - should fit on minimal width screen */
} table_desc[CFG_TOTAL_] = {
	{ CFG_C_SIZE,		"C_PANEL_SIZE",
		L"Advanced: Completion panel size" },
	{ CFG_CMD_F3,		"CMD_F3",
		L"Command F3 = view file(s)" },
	{ CFG_CMD_F4,		"CMD_F4",
		L"Command F4 = edit file(s)" },
	{ CFG_CMD_F5,		"CMD_F5",
		L"Command F5 = copy file(s)" },
	{ CFG_CMD_F6,		"CMD_F6",
		L"Command F6 = move file(s)" },
	{ CFG_CMD_F7,		"CMD_F7",
		L"Command F7 = make directory" },
	{ CFG_CMD_F8,		"CMD_F8",
		L"Command F8 = remove file(s)" },
	{ CFG_CMD_F9,		"CMD_F9",
		L"Command F9 = print file(s)" },
	{ CFG_CMD_F10,		"CMD_F10",
		L"Command F10 = user defined" },
	{ CFG_CMD_F11,		"CMD_F11",
		L"Command F11 = user defined" },
	{ CFG_CMD_F12,		"CMD_F12",
		L"Command F12 = user defined" },
	{ CFG_CMD_LINES,	"CMD_LINES",
		L"Appearance: How many lines are occupied by the input line" },
	{ CFG_D_SIZE,		"D_PANEL_SIZE",
		L"Advanced: Directory panel size (AUTO = screen size)" },
	{ CFG_DOUBLE_CLICK,	"DOUBLE_CLICK",
		L"Mouse double click interval in milliseconds" },
	{ CFG_FRAME,		"FRAME",
		L"Appearance: Panel frame: ----- or ===== or line graphics" },
	{ CFG_FMT_TIME,		"TIME_FMT",
		L"Appearance: Time format string (e.g. %H:%M) or AUTO" },
	{ CFG_FMT_DATE,		"DATE_FMT",
		L"Appearance: Date format string (e.g. %Y-%m-%d) or AUTO" },
	{ CFG_H_SIZE,		"H_PANEL_SIZE",
		L"Advanced: History panel size" },
	{ CFG_KILOBYTE,		"KILOBYTE",
		L"Appearance: Filesize unit definition" },
	{ CFG_LAYOUT,		"LAYOUT_ACTIVE",
		L"Appearance: Which file panel layout is active" },
	{ CFG_LAYOUT1,		"LAYOUT1",
		L"Appearance: File panel layout #1, see help" },
	{ CFG_LAYOUT2,		"LAYOUT2",
		L"Appearance: File panel layout #2" },
	{ CFG_LAYOUT3,		"LAYOUT3",
		L"Appearance: File panel layout #3" },
	{ CFG_MOUSE,		"MOUSE",
		L"Mouse input (supported terminals only)" },
	{ CFG_MOUSE_SCROLL,	"MOUSE_SCROLL",
		L"Mouse wheel scrolls by this number of lines" },
	{ CFG_PROMPT,		"PROMPT",
		L"Appearance: Command line prompt, see help" },
	{ CFG_QUOTE,		"QUOTE",
		L"Advanced: Additional filename chars to be quoted, see help" },
	{ CFG_TIME_DATE,	"TIME_DATE",
		L"Appearance: Time and date display mode" },
	{ CFG_XTERM_TITLE,	"XTERM_TITLE",
		L"Appearance: Change the X terminal window title" }
};

static CFG_ENTRY config[CFG_TOTAL_];

/* 'copy' values CP_X2Y understood by set_value() */
#define CP_SRC_CURRENT	 1
#define CP_SRC_INITIAL	 2
#define CP_SRC_NEW		 4
#define CP_DST_CURRENT	 8
#define CP_DST_NEW		16
#define CP_N2C	(CP_SRC_NEW		| CP_DST_CURRENT)
#define CP_C2N	(CP_SRC_CURRENT | CP_DST_NEW)
#define CP_I2N	(CP_SRC_INITIAL | CP_DST_NEW)
#define CP_I2C	(CP_SRC_INITIAL | CP_DST_CURRENT)

static void
set_value(int code, int cp)
{
	int src_num, *dst_num;
	const wchar_t *src_str;
	wchar_t *dst_str;
	CNUM *pnum;
	CSTR *pstr;

	if (config[code].isnum) {
		pnum = config[code].table;
		if (cp & CP_SRC_CURRENT)
			src_num = pnum->current;
		else if (cp & CP_SRC_INITIAL)
			src_num = pnum->initial;
		else
			src_num = pnum->new;
		if (cp & CP_DST_CURRENT)
			dst_num = &pnum->current;
		else
			dst_num = &pnum->new;
		*dst_num = src_num;
	}
	else {
		pstr = config[code].table;
		if (cp & CP_SRC_CURRENT)
			src_str = pstr->current;
		else if (cp & CP_SRC_INITIAL)
			src_str = pstr->initial;
		else
			src_str = pstr->new;
		if (cp & CP_DST_CURRENT)
			dst_str = pstr->current;
		else
			dst_str = pstr->new;
		wcscpy(dst_str,src_str);
	}
}

static CFG_ENTRY *
get_variable_by_name(const char *var, int len)
{
	int i;

	for (i = 0; i < CFG_TOTAL_; i++)
		if (strncmp(table_desc[i].name,var,len) == 0)
			return config + table_desc[i].code;
	return 0;
}

static CNUM *
get_numeric(int code)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(table_numeric); i++)
		if (table_numeric[i].code == code)
			return table_numeric + i;
	return 0;
}

static CSTR *
get_string(int code)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(table_string); i++)
		if (table_string[i].code == code)
			return table_string + i;
	return 0;
}

static int
get_desc(int code)
{
	int i;

	for (i = 0; i < CFG_TOTAL_; i++)
		if (table_desc[i].code == code)
			return i;
	return -1;
}

static void
parse_error(const char *format, ...)
{
	va_list argptr;

	error_cnt++;
	va_start(argptr,format);
	vmsgout(MSG_NOTICE,format,argptr);
	va_end(argptr);

}

static int
cfgfile_read(void)
{
	int i, len, nvalue, tfd, split;
	const char *line, *value;
	const wchar_t *wvalue;
	CFG_ENTRY *pce;
	CNUM *pnum;

	tfd = fr_open(user_data.file_cfg,CFG_FILESIZE_LIMIT);
	if (tfd == FR_NOFILE) {
		if (!user_data.nowrite) {
			mkdir(user_data.subdir,0755);	/* might exist already */
			msgout(MSG_w,"Configuration file not found.\n"
			  "It is recommended to run the \"cfg-clex\" utility.");
			user_data.noconfig = 1;			/* cfg-clex will appear on the command line */
		}
		return 0;	/* missing optional file is ok */
	}
	if (tfd < 0)
		return -1;
	msgout(MSG_DEBUG,"CONFIG: Processing configuration file \"%s\"",user_data.file_cfg);

	split = fr_split(tfd,CFG_LINES_LIMIT);
	if (split < 0 && split != FR_LINELIMIT) {
		fr_close(tfd);
		return -1;
	}

	error_cnt = 0;
	for (i = 0; (line = fr_line(tfd,i)); i++) {
		/* split VARIABLE and VALUE */
		if ( (value = strchr(line,'=')) == 0) {
			parse_error("Syntax error (expected was \"VARIABLE=value\") in \"%s\"",line);
			continue;
		}

		pce = get_variable_by_name(line,value - line);
		if (pce == 0) {
			/* --- begin 4.6 transition --- */
			if (strncmp(line,"DIR2=",5) == 0) {
				msgout(MSG_w,
				  "NOTE: DIR2 is no longer a valid configuration parameter.\n"
				  "If you want to use \"%s\" as the secondary panel's initial directory:\n"
				  "  * please create a bookmark named DIR2 with this value\n"
				  "  * save your configuration to purge DIR2 from the configuration file\n", value + 1);
				continue;
			}
			/* --- end --- */
			parse_error("Unknown variable in \"%s\"",line);
			continue;
		}
		value++;

		if (pce->isnum) {
			pnum = pce->table;
			if (sscanf(value," %d %n",&nvalue,&len) < 1 || len != strlen(value))
				parse_error("Invalid number in \"%s\"",line);
			else if ((nvalue < pnum->min || nvalue > pnum->max)
			  && (nvalue != 0 || pnum->extra_val == 0))
				parse_error("Numeric value out of range in \"%s\"",line);
			else
				pnum->current = nvalue;
		} else {
			wvalue = convert2w(value);
			if (wcslen(wvalue) > CFGVALUE_LEN)
				parse_error("String value too long in \"%s\"",line);
			else
				wcscpy(((CSTR *)pce->table)->current,wvalue);
		}

		if (error_cnt > CFG_ERRORS_LIMIT) {
			parse_error("Too many errors, ignoring the rest of the file");
			break;
		}
	}
	fr_close(tfd);

	return (split < 0 || error_cnt ) ?  -1 : 0;
}

void
cfg_initialize(void)
{
	int i, desc;
	CNUM *pnum;
	CSTR *pstr;

	/* initialize 'config' & 'pcfg' tables */
	for (i = 0; i < CFG_TOTAL_; i++) {
		if ( (pnum = get_numeric(i)) ) {
			config[i].isnum = 1;
			config[i].table = pnum;
			pcfg[i] = &pnum->current;
		}
		else if ( (pstr = get_string(i)) ) {
			config[i].table = pstr;
			pcfg[i] = &pstr->current;
		}
		else
			err_exit("BUG: config variable not defined (code %d)",i);

		if ( (desc = get_desc(i)) < 0)
			err_exit("BUG: no description for config variable (code %d)",i);

		config[i].var = table_desc[desc].name;
		config[i].help = table_desc[desc].help;
		if (wcslen(config[i].help) > MIN_COLS - 4)
			msgout(MSG_NOTICE,"CONFIG: variable %s: help string \"%s\" is too long",
			  config[i].var,convert2mb(config[i].help));
	}

	/* initialize and read values */
	for (i = 0; i < CFG_TOTAL_; i++)
		set_value(i,CP_I2C);
	if (cfgfile_read() < 0) {
		if (!user_data.nowrite)
			msgout(MSG_NOTICE,"This might help: Main menu -> Configure CLEX -> Apply+Save");
		msgout(MSG_W,"CONFIG: An error occurred while reading data, details in log");
	}

	panel_cfg.config = config;
}

/* the 'new' value in readable text form */
static const wchar_t *
print_str_value(int i)
{
	CSTR *pstr;

	pstr = config[i].table;
	if (pstr->extra_val != 0 && *pstr->new == L'\0')
		return pstr->extra_val;
	return pstr->new;
}

/* the 'new' value in readable text form */
static const wchar_t *
print_num_value(int i)
{
	static wchar_t buff[16];
	CNUM *pnum;

	pnum = config[i].table;
	if (pnum->extra_val != 0 && pnum->new == 0)
		return pnum->extra_val;
	if (pnum->desc[0])
		/* enumerated */
		return pnum->desc[pnum->new - pnum->min];
	/* really numeric */
	swprintf(buff,ARRAY_SIZE(buff),L"%d",pnum->new);
	return buff;
}

const wchar_t *
cfg_print_value(int i)
{
	return config[i].isnum ? print_num_value(i) : print_str_value(i);
}

static void
cfgfile_save(void)
{
	int i;
	FILE *fp;

	if (user_data.nowrite) {
		msgout(MSG_W,"CONFIG: Saving data to disk is prohibited");
		return;
	}

	if ( (fp = fw_open(user_data.file_cfg)) ) {
		fprintf(fp,"#\n# CLEX configuration file\n#\n");
		for (i = 0; i < CFG_TOTAL_; i++)
			if (config[i].saveit) {
				fprintf(fp,"%s=",config[i].var);
				if (config[i].isnum)
					fprintf(fp,"%d\n",((CNUM *)config[i].table)->new);
				else
					fprintf(fp,"%s\n",convert2mb(((CSTR *)config[i].table)->new));
			}
	}
	if (fw_close(fp)) {
		msgout(MSG_W,"CONFIG: Could not save data, details in log");
		return;
	}

	msgout(MSG_I,"CONFIG: Data saved");
}

int
cfg_prepare(void)
{
	int i;

	for (i = 0; i < CFG_TOTAL_; i++)
		set_value(i,CP_C2N);
	panel_cfg.pd->top = panel_cfg.pd->curs = panel_cfg.pd->min;
	panel = panel_cfg.pd;
	textline = 0;
	return 0;
}

int
cfg_menu_prepare(void)
{
	CNUM *pnum;

	pnum = config[panel_cfg.pd->curs].table;
	panel_cfg_menu.pd->top = 0;
	panel_cfg_menu.pd->cnt = pnum->max - pnum->min + 1;
	panel_cfg_menu.pd->curs = pnum->new - pnum->min;
	panel_cfg_menu.desc = pnum->desc;
	panel = panel_cfg_menu.pd;
	textline = 0;

	return 0;
}

int
cfg_edit_num_prepare(void)
{
	wchar_t prompt[CFGVAR_LEN + 48];
	CNUM *pnum;

	/* inherited panel = panel_cfg.pd */
	pnum = config[panel_cfg.pd->curs].table;
	textline = &line_tmp;
	swprintf(prompt,ARRAY_SIZE(prompt),L"%s (range: %d - %d%s%ls): ",
	  config[panel_cfg.pd->curs].var,pnum->min,pnum->max,
	  pnum->extra_val != 0 ? " or " : "",
	  pnum->extra_val != 0 ? pnum->extra_val : L"");
	edit_setprompt(textline,prompt);
	edit_nu_putstr(print_num_value(panel_cfg.pd->curs));
	return 0;
}

int
cfg_edit_str_prepare(void)
{
	wchar_t prompt[CFGVAR_LEN + 32];
	CSTR *pstr;

	/* inherited panel = panel_cfg.pd */
	pstr = config[panel_cfg.pd->curs].table;
	textline = &line_tmp;
	swprintf(prompt,ARRAY_SIZE(prompt),L"%s (" STR(CFGVALUE_LEN) " chars max%s%ls): ",
	  config[panel_cfg.pd->curs].var,
	  pstr->extra_val != 0 ? " or " : "",
	  pstr->extra_val != 0 ? pstr->extra_val : L"");
	edit_setprompt(textline,prompt);
	edit_nu_putstr(print_str_value(panel_cfg.pd->curs));
	return 0;
}

void
cx_cfg_menu_enter(void)
{
	CNUM *pnum;

	pnum = config[panel_cfg.pd->curs].table;
	pnum->new = pnum->min + panel_cfg_menu.pd->curs;
	next_mode = MODE_SPECIAL_RETURN;
}

void
cx_cfg_num_enter(void)
{
	int nvalue, len;
	CNUM *pnum;

	pnum = config[panel_cfg.pd->curs].table;
	if (pnum->extra_val != 0 && wcscmp(USTR(textline->line),pnum->extra_val) == 0) {
		pnum->new = 0;
		next_mode = MODE_SPECIAL_RETURN;
		return;
	}

	if (swscanf(USTR(textline->line),L" %d %n",&nvalue,&len) < 1 || len != textline->size)
		msgout(MSG_i,"numeric value required");
	else if (nvalue < pnum->min || nvalue > pnum->max)
		msgout(MSG_i,"value is out of range");
	else {
		pnum->new = nvalue;
		next_mode = MODE_SPECIAL_RETURN;
	}
}

void
cx_cfg_str_enter(void)
{
	CSTR *pstr;

	pstr = config[panel_cfg.pd->curs].table;
	if (pstr->extra_val != 0 && wcscmp(USTR(textline->line),pstr->extra_val) == 0) {
		*pstr->new = L'\0';
		next_mode = MODE_SPECIAL_RETURN;
		return;
	}

	if (textline->size > CFGVALUE_LEN)
		msgout(MSG_i,"string is too long");
	else {
		wcscpy(pstr->new,USTR(textline->line));
		next_mode = MODE_SPECIAL_RETURN;
	}
}

void
cx_cfg_default(void)
{
	set_value(panel_cfg.pd->curs,CP_I2N);
	win_panel_opt();
}

void
cx_cfg_original(void)
{
	set_value(panel_cfg.pd->curs,CP_C2N);
	win_panel_opt();
}

/* detect what has changed */
static void
detect_changes(void)
{
	int i;
	CNUM *pnum;
	CSTR *pstr;

	for (i = 0; i < CFG_TOTAL_; i++)
		if (config[i].isnum) {
			pnum = config[i].table;
			config[i].changed = pnum->new != pnum->current;
			config[i].saveit  = pnum->new != pnum->initial;
		}
		else {
			pstr = config[i].table;
			config[i].changed = wcscmp(pstr->new,pstr->current) != 0;
			config[i].saveit  = wcscmp(pstr->new,pstr->initial) != 0;
		}
}

static void
apply_changes(void)
{
	int i;
	FLAG reread = 0;

	for (i = 0; i < CFG_TOTAL_; i++)
		if (config[i].changed)
			set_value(i,CP_N2C);

	if (config[CFG_FRAME].changed) {
		win_frame_reconfig();
		win_frame();
	}
	if (config[CFG_CMD_LINES].changed) {
		curses_stop();
		msgout(MSG_i,"SCREEN: changing geometry");
		curses_restart();
	}
	if (config[CFG_XTERM_TITLE].changed) {
		xterm_title_restore();
		xterm_title_reconfig();
		xterm_title_set(0,0,0);
	}
	if (config[CFG_MOUSE].changed) {
		mouse_restore();
		mouse_reconfig();
		mouse_set();
	}
	if (config[CFG_PROMPT].changed)
		set_shellprompt();
	if (config[CFG_LAYOUT].changed || config[CFG_LAYOUT1].changed
	  || config[CFG_LAYOUT2].changed || config[CFG_LAYOUT3].changed) {
		layout_reconfig();
		reread = 1;
	}
	if (config[CFG_FMT_TIME].changed || config[CFG_FMT_DATE].changed
	  || config[CFG_TIME_DATE].changed) {
		td_fmt_reconfig();
		reread = 1;
	}
	if (config[CFG_KILOBYTE].changed) {
		kb_reconfig();
		reread = 1;
	}
	if (config[CFG_C_SIZE].changed)
		compl_reconfig();
	if (config[CFG_D_SIZE].changed)
		dir_reconfig();
	if (config[CFG_H_SIZE].changed)
		hist_reconfig();

	if (reread) {
		list_directory();
		ppanel_file->other->expired = 1;
	}
}

void
cx_cfg_apply(void)
{
	detect_changes();
	apply_changes();
}

void
cx_cfg_apply_save(void)
{
	detect_changes();
	apply_changes();
	cfgfile_save();
}

void
cx_cfg_enter(void)
{
	CNUM *pnum;

	if (config[panel_cfg.pd->curs].isnum) {
		pnum = config[panel_cfg.pd->curs].table;
		control_loop(pnum->desc[0] ? MODE_CFG_MENU : MODE_CFG_EDIT_NUM);
	}
	else
		control_loop(MODE_CFG_EDIT_TXT);
	win_panel_opt();
}

void
cx_cfg_noexit(void)
{
	msgout(MSG_i,"please select Cancel, Apply or Save");
	cx_pan_home();
}
