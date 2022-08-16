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

#include <sys/time.h>		/* struct timeval */
#include <stdarg.h>			/* log.h */
#include <string.h>			/* strcpy() */
#include <stdlib.h>			/* getenv() */
#include <time.h>			/* time() */
#include <wctype.h>			/* iswdigit() */
#ifdef HAVE_TERM_H
# include <term.h>			/* enter_bold_mode */
#endif
#include "curses.h"

#include "inout.h"

#include "cfg.h"			/* cfg_num() */
#include "control.h"		/* get_current_mode() */
#include "directory.h"		/* dir_split_dir() */
#include "edit.h"			/* edit_adjust() */
#include "log.h"			/* msgout() */
#include "mbwstring.h"		/* convert2w() */
#include "panel.h"			/* pan_adjust() */
#include "signals.h"		/* signal_initialize() */
#include "tty.h"			/* tty_press_enter() */

#ifndef A_NORMAL
# define A_NORMAL 0
#endif

#ifndef A_BOLD
# define A_BOLD A_STANDOUT
#endif

#ifndef A_REVERSE
# define A_REVERSE A_STANDOUT
#endif

#ifndef A_UNDERLINE
# define A_UNDERLINE A_STANDOUT
#endif

#ifndef ACS_HLINE
# define ACS_HLINE '-'
#endif

static chtype attrr, attrb;		/* reverse and bold or substitutes */
static const wchar_t *title;	/* panel title (if not generated) */
static int framechar;			/* panel frame character (note that ACS_HLINE is an int) */

/* win_position() controls */
static struct {
	CODE resize;	/* --( COLSxLINES )-- window size */
	CODE wait;		/* --< PLEASE WAIT >-- message */
	FLAG wait_ctrlc;/* append "Ctrl-C TO ABORT" to the message above */
	FLAG update;	/* --< CURSOR/TOTAL >-- normal info */
					/* resize, wait:
					 *  2 = msg should be displayed
					 *  1 = msg is displayed and should be cleared
					 *  0 = msg is not displayed
					 * update:
					 *  1 = data changed --> update the screen
					 *  0 = no change
					 */
} posctl;

/* line numbers and column numbers for better readability */
#define LNO_TITLE		0
#define LNO_FRAME1		1
#define LNO_PANEL		2
#define LNO_FRAME2		(disp_data.panlines + 2)
#define LNO_INFO		(disp_data.panlines + 3)
#define LNO_HELP		(disp_data.panlines + 4)
#define LNO_BAR			(disp_data.panlines + 5)
#define LNO_EDIT		(disp_data.panlines + 6)
#define MARGIN1			1	/* left or right margin - 1 column */
#define MARGIN2			2	/* left or right margin - 2 columns */
#define BOX4			4	/* checkbox or radiobutton */
#define CNO_FILTER		15	/* user's filter string starting column */
static int filter_width = 0;	/* columns written by win_filter()  */

/* help line (the second info line) controls */
#define HELPTMPTIME 	5		/* duration of help_tmp in seconds */
static struct {
	const wchar_t *help_base;	/* default help string for the current mode */
	/* help_base can be overriden by panel->help */
	const wchar_t *help_tmp;	/* temporary message, highlighted, dismissed automatically
									after HELPTMPTIME when a key is pressed */
	time_t exp_tmp;				/* expiration time for 'help_tmp' */
	const wchar_t *info;		/* "-- info message --",
									dismissed automatically when a key is pressed */
	const wchar_t *warning;		/* "Warning message. Press any key to continue" */
} helpline;

/* number of chars written to display lines, used for cursor position calculations */
static int chars_in_line[MAX_CMDLINES];

static wchar_t *bar;			/* win_bar() output */

static void win_helpline(void);	/* defined below */
static void win_position(void);	/* defined below */

static char type_symbol[][5] = {
	"    ", "exec", "suid", "Suid", "sgid", "/DIR", "/MNT",
	"Bdev", "Cdev", "FIFO", "sock", "spec", "  ??"
};	/* must correspond with FT_XXX */

#define CHECKBOX(X)		do { addstr(X ? "[x] " : "[ ] "); } while (0)
#define RADIOBUTTON(X)	do { addstr(X ? "(x) " : "( ) "); } while (0)

#define OFFSET0 (textline->offset ? 1 : textline->promptwidth)

/* draw everything from scratch */
static void
screen_draw_all(void)
{
	int y, x;

	for (;/* until break */;) {
		clear();
		getmaxyx(stdscr,y,x);
		disp_data.scrcols  = x;
		disp_data.scrlines = y;
		disp_data.pancols  = x - 2 * MARGIN2;
		disp_data.panrcol  = x - MARGIN2;
		disp_data.cmdlines = cfg_num(CFG_CMD_LINES);
		disp_data.panlines = y - disp_data.cmdlines - 6;
		/*
		 * there are 2 special positions: the bottom-right corner
		 * is always left untouched to prevent automatic scrolling
		 * and the position just before it is reserved for the
		 * '>' continuation mark
		 */
		if (x >= MIN_COLS && y >= MIN_LINES)
			break;
		printw("CLEX: this %dx%d window is too small. "
		  "Press ctrl-C to exit or enlarge the window to at least " STR(MIN_LINES) "x" STR(MIN_COLS)
#ifndef KEY_RESIZE
		  " and press a key to continue"
#endif
		  ". ",y,x);
		refresh();
		if (getch() == CH_CTRL('C'))
			err_exit("Display window is too small");
	}
	attrset(A_NORMAL);
	win_frame();
	win_bar();
	if (panel) {
		/* panel is NULL only when starting clex */
		win_title();
		pan_adjust(panel);
		win_panel();
		win_infoline();
		win_helpline();
		win_filter();
	}
	edit_adjust();
	win_edit();
}

/* start CURSES */
void
curses_initialize(void)
{
	int i;
	const char *term, *astr;
	static const char *compat[] = { "xterm","kterm","Eterm","dtterm","rxvt","aixterm" };
	static const char *not_compat[] = { "ansi","vt","linux","dumb" };

	if (disp_data.wait)
		tty_press_enter();

	initscr();			/* restores signal dispositions on FreeBSD ! */
	signal_initialize();	/* FreeBSD initscr() bug workaround ! */
	raw();
	nonl();
	noecho();
	keypad(stdscr,TRUE);
	notimeout(stdscr,TRUE);
	scrollok(stdscr,FALSE);
	clear();
	refresh();
	disp_data.curses = 1;

	if (enter_reverse_mode && *enter_reverse_mode)
		attrr = A_REVERSE;
	else
		attrr = A_STANDOUT;
	if (enter_bold_mode && *enter_bold_mode)
		attrb = A_BOLD;
	else if (enter_underline_mode && *enter_underline_mode)
		attrb = A_UNDERLINE;
	else
		attrb = A_STANDOUT;

	win_frame_reconfig();
	screen_draw_all();

	disp_data.bs177 = key_backspace && strcmp(key_backspace,"\177") == 0;

	astr = "not known";
	if ( (term = getenv("TERM")) ) {
		for (i = 0; i < ARRAY_SIZE(compat); i++)
			if (strncmp(term,compat[i],strlen(compat[i])) == 0) {
				disp_data.xterm = 1;
				astr = "yes";
				break;
			}
		if (!disp_data.xterm)
				for (i = 0; i < ARRAY_SIZE(not_compat); i++)
					if (strncmp(term,not_compat[i],strlen(not_compat[i])) == 0) {
						disp_data.noxterm = 1;
						astr = "no";
						break;
					}
	}
	msgout(MSG_DEBUG,"Terminal type: \"%s\", can change the window title: %s",
		term ? term : "undefined", astr);

	disp_data.xwin = getenv("WINDOWID") && getenv("DISPLAY");
	msgout(MSG_DEBUG,"X Window: %s",
	  disp_data.xwin ? "detected" : "not detected (no $DISPLAY and/or no $WINDOWID)");

#ifdef KEY_MOUSE
# if NCURSES_MOUSE_VERSION >= 2
	/* version 1 does not work with the mouse wheel */
	if (key_mouse && *key_mouse) {
		msgout(MSG_DEBUG,"Mouse interface: ncurses mouse version %d", NCURSES_MOUSE_VERSION);
# else
	if (key_mouse && strcmp(key_mouse,"\033[<") == 0)
		msgout(MSG_DEBUG,"Mouse interface: xterm SGR 1006 mode is NOT supported by CLEX");
		/* reason: ncurses mouse is the preferred interface */
	if (key_mouse && strcmp(key_mouse,"\033[M") == 0) {
		msgout(MSG_DEBUG,"Mouse interface: xterm normal tracking mode");
# endif
		disp_data.mouse = 1;
	}
	else
		msgout(MSG_DEBUG,"Mouse interface: not found");
#endif /* KEY_MOUSE */
}

/* restart CURSES */
void
curses_restart(void)
{
	if (disp_data.wait)
		tty_press_enter();

	reset_prog_mode();
	touchwin(stdscr);
	disp_data.curses = 1;
	screen_draw_all();
}

/* this is a cleanup function (see err_exit() in control.c) */
void
curses_stop(void)
{
	clear();
	refresh();
	endwin();
	disp_data.curses = 0;
}

void
curses_cbreak(void)
{
	cbreak();
	posctl.wait_ctrlc = 1;
}

void
curses_raw(void)
{
	raw();
	posctl.wait_ctrlc = 0;
}

/* set cursor to the proper position and refresh screen */
static void
screen_refresh(void)
{
	int i, posx, posy, offset;

	if (posctl.wait || posctl.resize || posctl.update)
		win_position();		/* display/clear message */

	if (panel->filtering == 1)
		move(LNO_FRAME2,CNO_FILTER + wc_cols(panel->filter->line,0,panel->filter->curs));
	else {
		posy = LNO_EDIT;
		posx = 0;
		if (textline != 0) {
			for (i = 0, offset = textline->offset; i < disp_data.cmdlines - 1
			  && textline->curs >= offset + chars_in_line[i]; i++)
				offset += chars_in_line[i];
			posx = wc_cols(USTR(textline->line),offset,textline->curs);
			if (i == 0)
				posx += OFFSET0;
			else
				posy += i;
		}
		move(posy,posx);
	}
	refresh();
}

/****** mouse input functions ******/

/* which screen area belongs the line 'ln' to ? */
static int
screen_area(int ln, int col)
{
	if (ln < 0 || col < 0 || ln >= disp_data.scrlines || col >= disp_data.scrcols)
		return -1;
	if (ln == 0)
		return AREA_TITLE;
	if (ln == 1)
		return AREA_TOPFRAME;
	if ((ln -= 2) < disp_data.panlines)
		return AREA_PANEL;
	if ((ln -= disp_data.panlines) == 0)
		return AREA_BOTTOMFRAME;
	if (ln == 1)
		return AREA_INFO;
	if (ln == 2)
		return AREA_HELP;
	if (ln == 3)
		return AREA_BAR;
	if (ln == 4 && textline && col < textline->promptwidth)
		return AREA_PROMPT;
	return AREA_LINE;
}

/* screen position -> input line cursor */
static int
scr2curs(int y, int x)
{
	int first, last, i;
	wchar_t *line;

	if (y == 0 && (x -= OFFSET0) < 0)	/* compensate for the prompt/continuation mark */
		return -1;				/* in front of the text */

	/* first = index of the first character in the line */
	for (first = textline->offset, i = 0; i < y; i++)
		first += chars_in_line[i];
	if (first > textline->size)
		return -1;				/* unused line */

	/* last = index of the last character in the line (may be the terminating null) */
	last = first + chars_in_line[y] - 1;
	LIMIT_MAX(last,textline->size);

	line = USTR(textline->line);
	for (i = first; i <= last; i++) {
		x -= WCW(line[i]);
		if (x <= 0)
			return i;
	}
	return -1;					/* after the text */
}

/* screen position -> filter line cursor */
static int
scr2curs_filt(int x)
{
	int i;
	wchar_t *line;

	if ((x -= CNO_FILTER) < 0)
		return -1;				/* in front of the text */

	line = panel->filter->line;
	for (i = 0; i <= panel->filter->size; i++) {
		x -= WCW(line[i]);
		if (x <= 0)
			return i;
	}
	return -1;					/* after the text */
}

/* screen position -> help link link number */
static int
scr2curs_help(int y, int x)
{
	int i, width, curs, links;
	HELP_LINE *ph;

	curs = panel->top + y;
	if (curs < 0 || curs >= panel->cnt)
		return -1;

	ph = panel_help.line[curs];
	links = ph->links;
	if (links <= 1)
		return links - 1;

	/* multiple links */
	for (width = wc_cols(ph->text,0,-1), i = 0; i < links - 1; i++) {
		x -= wc_cols(ph[3 * i + 2].text,0,-1) + width;
		width = wc_cols(ph[3 * i + 3].text,0,-1);
		if (x <= width / 2)
			return i;
	}
	return i;
}

/* note: single width characters assumed */
static int
scr2curs_bar(int x)
{
	int curs;

	x--;	/* make 0-based */
	if (x < 0 || x >= wcslen(bar) || bar[x] == L' ')
		return -1;

	for (curs = 0; --x > 0; ) {
		if (bar[x] == L'|')
			return -1;
		if (bar[x] == L' ' && bar[x-1] != L' ')
			curs++;
	}
	return curs;
}

/* read the mouse tracking data */
static int
mouse_data(void)
{
	FLAG click;
	static struct timeval prev, now;
	static MOUSE_INPUT miprev;

#if NCURSES_MOUSE_VERSION >= 2
	mmask_t mstat;
	MEVENT mevent;
	static CODE btn;	/* active real mouse button (i.e. not wheel) or 0 */

	if (getmouse(&mevent) != OK)
		return -1;
	minp.x = mevent.x;
	minp.y = mevent.y;
	mstat = mevent.bstate;
	minp.motion = (mstat & REPORT_MOUSE_POSITION) != 0;
	if (minp.motion)
		minp.button = btn;
	else if (mstat & BUTTON1_PRESSED)
		btn = minp.button = disp_data.mouse_swap ? 3 : 1;
	else if (mstat & BUTTON2_PRESSED)
		btn = minp.button = 2;
	else if (mstat & BUTTON3_PRESSED)
		btn = minp.button = disp_data.mouse_swap ? 1 : 3;
	else if (mstat & BUTTON4_PRESSED)
		minp.button = 4;
	else if (mstat & BUTTON5_PRESSED)
		minp.button = 5;
	else {
		 /* button release */
		if (mstat & BUTTON1_RELEASED) {
			if (btn == 1)
				btn = 0;
		}
		else if (mstat & BUTTON2_RELEASED) {
			if (btn == 2)
				btn = 0;
		}
		else if (mstat & BUTTON3_RELEASED) {
			if (btn == 3)
				btn = 0;
		}
		return -1;
	}

#else
	/* fallback: XTERM mouse */
	int mstat;

	keypad(stdscr,FALSE);
	mstat = getch() - 32;
	minp.x = getch() - 33;
	minp.y = getch() - 33;
	keypad(stdscr,TRUE);
	if (mstat < 0)
		return -1;

	/* button */
	switch (mstat & 0x43) {
	case 0:
		minp.button = disp_data.mouse_swap ? 3 : 1; break;
	case 1:
		minp.button = 2; break;
	case 2:
		minp.button = disp_data.mouse_swap ? 1 : 3; break;
	case 64:
		minp.button = 4; break;
	case 65:
		minp.button = 5; break;
	default:
		return -1;		/* button release or a bogus event */
	}
	minp.motion = mstat & 0x20; /* mouse is in motion */
#endif

	if ((minp.area = screen_area(minp.y, minp.x)) < 0)
		return -1;

	/* event accepted */
	miprev = minp;
	prev = now;
	gettimeofday(&now,0);

	click = minp.button >= 1 && minp.button <= 3;
	minp.doubleclick = !miprev.doubleclick && click && minp.button == miprev.button && !minp.motion
	  && minp.x == miprev.x && minp.y == miprev.y && now.tv_sec - prev.tv_sec < 2
	  && 1000 * (int)(now.tv_sec - prev.tv_sec) + (int)(now.tv_usec - prev.tv_usec) / 1000
		<= cfg_num(CFG_DOUBLE_CLICK);

	minp.ypanel = (click && minp.area == AREA_PANEL) ? minp.y - LNO_PANEL : -1;
	minp.cursor = -1;
	if (click)
		switch (minp.area) {
		case AREA_LINE:
			if (textline)
				/* input line cursor */
				minp.cursor = scr2curs(minp.y - LNO_EDIT,minp.x);
			break;
		case AREA_BAR:
			minp.cursor = scr2curs_bar(minp.x);
			break;
		case AREA_BOTTOMFRAME:
			if (panel->filter)
				/* filter expression cursor */
				minp.cursor = scr2curs_filt(minp.x);
			break;
		case AREA_PANEL:
			if (panel == panel_help.pd)
				/* help link index */
				minp.cursor = scr2curs_help(minp.y - LNO_PANEL,minp.x - MARGIN2);
			break;
	}

	return 0;
}

/****** keyboard input functions ******/

void
kbd_rawkey(void)
{
	int retries, type;

	screen_refresh();

	kinp.prev_esc = kinp.fkey == 0 && kinp.key == WCH_ESC;
	do {
		retries = 10;
		do {
			if (--retries < 0)
				err_exit("Cannot read the keyboard input");
			type = get_wch(&kinp.key);
		} while (type == ERR || kinp.key == WEOF);
		if (type == KEY_CODE_YES) {
#ifdef KEY_MOUSE
			if (kinp.key == KEY_MOUSE) {
				kinp.fkey = 2;
				kinp.key = 0;		/* no more #ifdef KEY_MOUSE */
			}
			else
#endif
				kinp.fkey = 1;
		}
		else
			kinp.fkey = 0;
	} while ((kinp.fkey == 0 && kinp.key == L'\0') || (kinp.fkey == 2 && mouse_data() < 0));
}

/*
 * get next input char, no processing except screen resize and screen redraw,
 * use this input function to get unfiltered input
 */
static wint_t
kbd_getany(void)
{
	for (;/* until return */;) {
		kbd_rawkey();

		if (kinp.fkey == 0 && kinp.key == WCH_CTRL('L'))
			wrefresh(curscr);	/* redraw screen */
		else if (kinp.fkey == 0 && kinp.key == WCH_ESC)
			/* ignore */;
#ifdef KEY_RESIZE
		else if (kinp.fkey == 1 && kinp.key == KEY_RESIZE) {
			posctl.resize = 2;
			screen_draw_all();
		}
#endif
		else
			return kinp.key;
	}
}

const char *
char_code(int value)
{
	static char buffer[24];

	if (*buffer == '\0')
		strcpy(buffer,lang_data.utf8 ? "U+" : "\\x");
	sprintf(buffer + 2,"%0*X",value > 0xFF ? 4 : 2,value);
	return buffer;
}

static const char *
ascii_code(int value)
{
	static char *ascii[] = {
		"", /* null */
		", ctrl-A, SOH (start of heading)",
		", ctrl-B, STX (start of text)",
		", ctrl-C, ETX (end of text)",
		", ctrl-D, EOT (end of transmission)",
		", ctrl-E, ENQ (enquiry)",
		", ctrl-F, ACK (acknowledge)",
		", ctrl-G, BEL (bell)",
		", ctrl-H, BS (backspace)",
		", ctrl-I, HT (horizontal tab)",
		", ctrl-J, LF (new line) (line feed)",
		", ctrl-K, VT (vertical tab)",
		", ctrl-L, FF (form feed)",
		", ctrl-M, CR (carriage return)",
		", ctrl-N, SO (shift out)",
		", ctrl-O, SI (shift in)",
		", ctrl-P, DLE (data link escape)",
		", ctrl-Q, DC1 (device control 1)",
		", ctrl-R, DC2 (device control 2)",
		", ctrl-S, DC3 (device control 3)",
		", ctrl-T, DC4 (device control 4)",
		", ctrl-U, NAK (negative acknowledgment)",
		", ctrl-V, SYN (synchronous idle)",
		", ctrl-W, ETB (end of transmission block)",
		", ctrl-X, CAN (cancel)",
		", ctrl-Y, EM  (end of medium)",
		", ctrl-Z, SUB (substitute)",
		", ESC (escape)",
		", FS (file separator)",
		", GS (group separator)",
		", RS (record separator)",
		", US (unit separator)"
	};

	if (lang_data.utf8 && value == L'\xAD')
		return ", SHY (soft hyphen)";
	if (lang_data.utf8 && value == L'\xA0')
		return ", NBSP (non-breaking space)";
	return (value > 0 && value < ARRAY_SIZE(ascii)) ? ascii[value] : "";
}

/* check for key + shift combination not understood by the curses library */
static wint_t
shift_key(wint_t key)
{
	int i;
	const char *name;
	static struct {
		const char *n;
		wint_t k;
	} keytable[] = {
		{ "LFT",	KEY_LEFT	},
		{ "RIT",	KEY_RIGHT	},
		{ "UP",		KEY_UP		},
		{ "DN",		KEY_DOWN	},
		{ "HOM",	KEY_HOME	},
		{ "END",	KEY_END		},
		{ "IC",		KEY_IC		},
		{ "DC",		KEY_DC		},
		{ "PRV",	KEY_PPAGE	},
		{ "NXT",	KEY_NPAGE	}
	};

	name = keyname(key);
	if (*name != 'k')
		return 0;

	name++;
	for (i = 0; i < ARRAY_SIZE(keytable); i++)
		if (strncmp(name,keytable[i].n,strlen(keytable[i].n)) == 0)
			return keytable[i].k;

	return 0;
}

/* get next input char */
wint_t
kbd_input(void)
{
	wchar_t ch;
	wint_t shkey;

	/* show ASCII code */
	if (helpline.info == 0
	  && ((panel->filtering == 1 && (ch = panel->filter->line[panel->filter->curs]) != L'\0')
	    || (textline != 0 && (ch = USTR(textline->line)[textline->curs]) != L'\0'))
	  && !ISWPRINT(ch))
		msgout(MSG_i,"special character %s%s",char_code(ch),ascii_code(ch));

	kbd_getany();

	/* <esc> 1 --> <F1>, <esc> 2 --> <F2>, ... <esc> 0 --> <F10> */
	if (kinp.prev_esc && kinp.fkey == 0 && iswdigit(kinp.key)) {
		kinp.prev_esc = 0;
		kinp.fkey = 1;
		kinp.key = KEY_F(((kinp.key - L'0') + 9) % 10 + 1);
	}

	if (kinp.fkey == 1 && (shkey = shift_key(kinp.key))) {
		kinp.prev_esc = 1;
		kinp.key = shkey;
	}

	/* dismiss remarks (if any) */
	if (helpline.info)
		win_sethelp(HELPMSG_INFO,0);
	else if (helpline.help_tmp && time(0) > helpline.exp_tmp)
		win_sethelp(HELPMSG_TMP,0);

	return kinp.key;
}

/****** output functions ******/

/*
 * following functions write to these screen areas:
 *
 * win_title, win_settitle	/usr/local
 * win_frame					----------
 * win_panel					 DIR bin
 * win_panel					>DIR etc <
 * win_panel					 DIR man
 * win_frame, win_filter,
 *  win_waitmsg, win_position	-< filt >----< 3/9 >-
 * win_infoline					0755 rwxr-xr-x
 * win_sethelp							alt-M for menu
 * win_bar						[ CLEX file manager ]
 * win_edit						shell $ ls -l_
 */

#define BLANK(X)       do { char_line(' ',X); } while (0)
/* write a line of repeating chars */
static void
char_line(int ch, int cnt)
{
	while (cnt-- > 0)
		addch(ch);
}

/*
 * putwcs_trunc() writes wide string 'str' padding or truncating it
 * to the total size of 'maxwidth' display columns. Its behavior
 * may be altered by OPT_XXX options (may be OR-ed together).
 * Non-printable characters are replaced by a question mark
 */
#define OPT_NOPAD	1	/* do not pad */
#define OPT_NOCONT	2	/* truncate without the continuation mark '>' */
#define OPT_SQUEEZE	4	/* squeeze long string in the middle: abc...xyz */
/*
 * return value:
 * if OPT_SQUEEZE is used, the return value is:
 *    -1   if the string had to be squeezed in the middle
 *    >= 0 if the string was short enough to display it normally
 * if OPT_NOPAD is given, it returns the number of display columns left unused
 * otherwise it returns the number of characters written including padding,
 *    but excluding the continuation mark
 */
static int
putwcs_trunc(const wchar_t *str, int maxwidth, int options)
{
	wchar_t ch;
	int i, chcnt, remain, width, p2, len, dots, part1, part2;
	FLAG printable;

	if (maxwidth <= 0)
		return 0;
	if (str == 0)
		str = L"(null!)";	/* should never happen */

	if (options & OPT_SQUEEZE) {
		len = wcslen(str);
		if (wc_cols(str,0,len) > maxwidth) {
			dots = maxwidth >= 6 ? 4 : 1;
			part1 = 3 * (maxwidth - dots) / 8;
			part2 = maxwidth - dots - part1;
			part2 += putwcs_trunc(str,part1,OPT_NOCONT | OPT_NOPAD);
			/* fine-tune the width */
			p2 = len - part2;
			LIMIT_MIN(p2,0);
			width = wc_cols(str,p2,len);
			while (width < part2 && p2 > 0) {
				p2--;
				width += WCW(str[p2]);
			}
			while (width > part2 && p2 < len) {
				width -= WCW(str[p2]);
				p2++;
			}
			while (utf_iscomposing(str[p2]))
				p2++;
			char_line('.',dots + part2 - width);
			putwcs_trunc(str + p2,part2,0);
			return -1;
		}
		/* else: SQUEEZE option is superfluous --> ignored */
	}

	chcnt = 0;	/* char counter */
	for (remain = maxwidth, i = 0; (ch = str[i]) != L'\0';) {
		width = (printable = ISWPRINT(ch)) ? wcwidth(ch) : 1;
		if (width > 0 && width == remain && !(options & OPT_NOCONT) && str[i + 1] != L'\0')
			break;
		if (width > remain)
			break;
		remain -= width;
		if (printable)
			i++;
		else {
			if (i > 0)
				addnwstr(str,i);
			addnwstr(&lang_data.repl,1);
			str   += i + 1;
			chcnt += i + 1;
			i = 0;
		}
	}
	if (i > 0) {
		addnwstr(str,i);
		chcnt += i;
	}

	if (ch == L'\0')
		/* more space than text -> padding */
		chcnt += remain;
	else
		/* more text than space -> truncating */
		if (!(options & OPT_NOCONT)) {
			addch('>' | attrb);	/* continuation mark in bold font */
			remain--;
		}

	if (remain && !(options & OPT_NOPAD))
		BLANK(remain);
	
	return (options & OPT_NOPAD) ? remain : chcnt;
}

/*
 * normal (not wide) char version of putwcs_trunc()
 * - does not support OPT_SQUEEZE
 * - does not detect unprintable characters
 * - assumes 1 char = 1 column, should not be used for internationalized text
 */
static int
putstr_trunc(const char *str, int maxwidth, int options)
{
	int chcnt, remain;

	if (maxwidth <= 0)
		return 0;

	chcnt = strlen(str);
	if (chcnt < maxwidth) {
		addstr(str);
		remain = maxwidth - chcnt;
	}
	else {
		if (options & OPT_NOCONT)
			addnstr(str,chcnt = maxwidth);
		else {
			addnstr(str,chcnt = maxwidth - 1);
			addch('>' | attrb);	/* continuation mark in bold font */
		}
		remain = 0;
	}
	if (remain && !(options & OPT_NOPAD))
		BLANK(remain);
	
	return (options & OPT_NOPAD) ? remain : chcnt;
}


#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"

/* like putwcs_trunc, but stopping just before the 'endcol' screen column */
static int
putwcs_trunc_col(const wchar_t *str, int endcol, int options)
{
	int y, x;

	getyx(stdscr,y,x);
	return putwcs_trunc(str,endcol - x,options);
}

static int
putstr_trunc_col(const char *str, int endcol, int options)
{
	int y, x;

	getyx(stdscr,y,x);
	return putstr_trunc(str,endcol - x,options);
}

#pragma GCC diagnostic pop

void
win_frame_reconfig(void)
{
	switch(cfg_num(CFG_FRAME)) {
	case 0:
		framechar = '-';
		break;
	case 1:
		framechar = '=';
		break;
	default:
		framechar = ACS_HLINE;
	}
}

void
win_frame(void)
{
	move(LNO_FRAME1,0);
	char_line(framechar,disp_data.scrcols);
	move(LNO_FRAME2,0);
	char_line(framechar,disp_data.scrcols);
}

/* file panel title: primary + secondary panel's directory */
static void
twodirs(void)
{
	int w1, w2, rw1, opt1, opt2, width;
	const wchar_t *dir1, *dir2;

	/* directory names are separated by 2 spaces; their width is kept in ratio 5:3 */
	width = disp_data.scrcols - 2;			/* available space */
	dir1 = USTR(ppanel_file->dirw);
	rw1 = w1 = wc_cols(dir1,0,-1);
	dir2 = USTR(ppanel_file->other->dirw);
	w2 = wc_cols(dir2,0,-1);
	opt1 = opt2 = 0;
	if (w1 + w2 <= width)
		w1 = width - w2;					/* enough space */
	else if (w1 <= (5 * width) / 8) {
		w2 = width - w1;					/* squeeze second */
		opt2 = OPT_SQUEEZE;
	}
	else if (w2 <= (3 * width) / 8) {
		w1 = width - w2;					/* squeeze first */
		opt1 = OPT_SQUEEZE;
	}
	else {
		w1 = (5 * width) / 8;				/* squeeze both */
		w2 = width - w1;
		opt1 = opt2 = OPT_SQUEEZE;
	}
	attron(attrb);
	putwcs_trunc(dir1,w1,opt1);
	attroff(attrb);
	addstr("  ");
	putwcs_trunc(dir2,w2,opt2);

	disp_data.dir1end = w1 < rw1 ? w1 : rw1;
	disp_data.dir2start = w1 + 2;			/* exactly +3, but +2 feels more comfortable */
}

/* panel title - top screen line */
void
win_title(void)
{
	move(LNO_TITLE,0);
	switch (get_current_mode()) {
	case MODE_COMPL:
		addch(' ');
		putwcs_trunc(panel_compl.title,disp_data.scrcols,0);
		break;
	case MODE_FILE:
		twodirs();
		break;
	case MODE_HELP:
		addstr(" HELP: ");
		attron(attrb);
		putwcs_trunc_col(panel_help.title,disp_data.scrcols,0);
		attroff(attrb);
		break;
	case MODE_PREVIEW:
		addstr(" PREVIEW: ");
		attron(attrb);
		putwcs_trunc_col(panel_preview.title,disp_data.scrcols,0);
		attroff(attrb);
		break;
	default:
		/* static title */
		addch(' ');
		putwcs_trunc(title,disp_data.scrcols,0);
	}
}

void
win_settitle(const wchar_t *newtitle)
{
	title = newtitle;
	win_title();
}

static void
print_position(const wchar_t *msg, int bold)
{
	static int prev_pos_start = 0;
	int pos_start, filter_stop;

	pos_start = disp_data.scrcols - wc_cols(msg,0,-1) - MARGIN2;

	filter_stop = MARGIN2 + filter_width;
	if (filter_stop > pos_start) {
		/* clash with the filter -> do not display position */
		move (LNO_FRAME2,filter_stop);
		char_line(framechar,disp_data.scrcols - filter_stop - MARGIN2);
		return;
	}

	if (pos_start > prev_pos_start) {
		move(LNO_FRAME2,prev_pos_start);
		char_line(framechar,pos_start - prev_pos_start);
	}
	else
		move(LNO_FRAME2,pos_start);
	prev_pos_start = pos_start;

	if (bold)
		attron(attrb);
	addwstr(msg);
	if (bold)
		attroff(attrb);
}

static void
win_position(void)
{
	wchar_t buffer[64], selected[32], *hidden;

	if (posctl.resize == 2) {
		swprintf(buffer,ARRAY_SIZE(buffer),L"( %dx%d )",disp_data.scrcols,disp_data.scrlines);
		print_position(buffer,1);
		posctl.resize = 1;
		return;
	}
		
	if (posctl.wait == 2) {
		if (posctl.wait_ctrlc)
			print_position(L"< PLEASE WAIT - CTRL-C TO ABORT >",1);
		else
			print_position(L"< PLEASE WAIT >",1);
		posctl.wait = 1;
		return;
	}

	posctl.wait = posctl.resize = posctl.update = 0;

	if (panel->cnt == 0) {
		print_position(L"< NO DATA >",1);
		return;
	}

	if (panel->curs < 0) {
		print_position(L"",0);
		return;
	}

	if (panel->type == PANEL_TYPE_FILE && ppanel_file->selected)
		swprintf(selected,ARRAY_SIZE(selected),L" [%d]",ppanel_file->selected);
	else
		selected[0] = L'\0';
	hidden = panel->type == PANEL_TYPE_FILE && ppanel_file->hidden ? L"HIDDEN " : L"";

	swprintf(buffer,ARRAY_SIZE(buffer),L"<%ls %d/%d %ls>",
		  selected,panel->curs + 1,panel->cnt,hidden);
	print_position(buffer,0);
}

void
win_waitmsg(void)
{
	if (disp_data.curses && posctl.wait <= 1) {
		posctl.wait = 2;
		screen_refresh();
	}
}

void
win_filter(void)
{
	int width;
	const wchar_t *label, *close;
	FLAG filepanel;

	move(LNO_FRAME2,MARGIN2);
	if (!panel->filtering)
		width = 0;
	else {
		width = CNO_FILTER + wc_cols(panel->filter->line,0,-1);
		filepanel = panel->type == PANEL_TYPE_FILE;

		if (panel->type == PANEL_TYPE_HELP) {
			label = L"( find text: ";
			close = L" )";
		}
		else if (filepanel && ppanel_file->filtype) {
			label = L"[ pattern: ";
			close = L" ]";
		}
		else {
			label = L"< filter: ";
			close = L" >";
		}
		char_line(framechar,CNO_FILTER - MARGIN2 - wc_cols(label,0,-1));
		addwstr(label);
		attron(attrb);
		putwcs_trunc(panel->filter->line,width - CNO_FILTER,0);
		attroff(attrb);
		addwstr(close);
	}

	if (width < filter_width)
		char_line(framechar,filter_width - width);
	filter_width = width;
}

/* "0644" -> "rw-r--r--" */
static const char *
print_perms(const char *octal)
{
	static const char
		*set1[8] =
			{ "---","--x","-w-","-wx","r--","r-x","rw-","rwx" },
		*set2[8] =
			{ "--S","--s","-wS","-ws","r-S","r-s","rwS","rws" },
		*set3[8] =
			{ "--T","--t","-wT","-wt","r-T","r-t","rwT","rwt" };
	static char perms[10];

	strcpy(perms + 0,((octal[0] - '0') & 4 ? set2 : set1)[(octal[1] - '0') & 7]);
	strcpy(perms + 3,((octal[0] - '0') & 2 ? set2 : set1)[(octal[2] - '0') & 7]);
	strcpy(perms + 6,((octal[0] - '0') & 1 ? set3 : set1)[(octal[3] - '0') & 7]);
	return perms;
}

/* see CFG_LAYOUT1 for more information about 'fields' */
static void
print_fields(FILE_ENTRY *pfe, int width, const wchar_t *fields)
{
	const char *txt;
	const wchar_t *wtxt;
	FLAG field, left_align;
	wchar_t ch;
	int i, fw;

	for (field = left_align = 0; width > 0 && (ch = *fields++); ) {
		if (field == 0) {
			if (ch == L'$') {
				field = 1;
				continue;
			}
			if (!ISWPRINT(ch)) {
				ch = lang_data.repl;
				fw = 1;
				left_align = 1;
			}
			else {
				fw = wcwidth(ch);
				if (fw > width)
					return;
				/* choose proper alignment (left or right) */
				left_align = (ch != L' ');
			}
			addnwstr(&ch,1);
			width -= fw;
		}
		else {
			field = 0;
			txt = 0;
			wtxt = 0;
			switch (ch) {
			case L'a':	/* access date/time */
				fw = disp_data.date_len;
				wtxt = pfe->atime_str;
				break;
			case L'd':	/* modification date/time */
				fw = disp_data.date_len;
				wtxt = pfe->mtime_str;
				break;
			case L'g':	/* file age */
				fw = FE_AGE_STR - 1 - ppanel_file->cw_age;
				txt = pfe->age_str;
				if (txt[0])
					txt += ppanel_file->cw_age;
				break;
			case L'i':	/* inode change date/time */
				fw = disp_data.date_len;
				wtxt = pfe->ctime_str;
				break;
			case L'l':	/* links (total number) */
				fw = FE_LINKS_STR - 1 - ppanel_file->cw_ln1;
				txt = pfe->links_str;
				if (txt[0])
					txt += ppanel_file->cw_ln1;
				break;
			case L'L':	/* links (flag) */
				fw = ppanel_file->cw_lnh;
				txt = pfe->links ? "LNK" : "";
				break;
			case L'm':	/* file mode */
				fw = FE_MODE_STR - 1;
				txt = pfe->mode_str;
				break;
			case L'M':	/* file mode (alternative format) */
				fw = ppanel_file->cw_mod;
				txt = pfe->normal_mode ? "" : pfe->mode_str;
				break;
			case L'o':	/* owner */
				fw = ppanel_file->cw_ow2;
				wtxt = pfe->owner_str;
				if (wtxt[0])
					wtxt += ppanel_file->cw_ow1;
				break;
			case L'P':	/* permissions (alternative format) */
				if (pfe->normal_mode) {
					fw = ppanel_file->cw_mod ? 9 : 0;
					txt = "";
					break;
				}
				/* no break */
			case L'p':	/* permissions */
				fw = 9;	/* rwxrwxrwx */
				txt = pfe->file_type == FT_NA ? "" : print_perms(pfe->mode_str);
				break;
			case L'r':	/* file size (or device major/minor) */
			case L'R':
			case L's':
			case L'S':
				fw = ppanel_file->cw_sz2;
				txt = pfe->size_str;
				if (txt[0])
					txt += ppanel_file->cw_sz1;
				break;
			case L't':	/* file type */
				fw = 4;
				txt = type_symbol[pfe->file_type];
				break;
			case L'>':	/* symbolic link */
				fw = ppanel_file->cw_lns;
				txt = pfe->symlink ? "->" : "";
				break;
			case L'*':	/* selection mark */
				fw = 1;
				txt = pfe->select ? "*" : " ";
				break;
			case L'$':	/* literal $ */
				fw = 1;
				txt = "$";
				break;
			case L'|':	/* literal | */
				fw = 1;
				txt = "|";
				break;
			default:	/* syntax error */
				fw = 2;
				txt = "$?";
			}

			if (fw > width)
				return;

			if (txt) {
				if (*txt == '\0')
					/* txt == "" - leave the field blank */
					BLANK(fw);
				else if (left_align && *txt == ' ') {
					/* change alignment from right to left */
					for (i = 1; txt[i] == ' '; i++)
						;
					addstr(txt + i);
					BLANK(i);
				}
				else
					addstr(txt);
			}
			else {
				if (*wtxt == '\0')
					/* txt == "" - leave the field blank */
					BLANK(fw);
				else if (left_align && *wtxt == L' ') {
					/* change alignment from right to left */
					for (i = 1; wtxt[i] == L' '; i++)
						;
					addwstr(wtxt + i);
					BLANK(i);
				}
				else
					addwstr(wtxt);
			}
			width -= fw;
		}
	}
}

/* information line */
void
win_infoline(void)
{
    static const wchar_t
	*info_cmp[] = {
        0, 0,
        L"The mode is also known as access rights or permissions"
    },
	*info_sort[] = {
		0,
		0,
		0,
		0,	/* ------ */
		0,
		L"Note: directories . and .. are always on the top, despite the sort order",
		L"Notes: . and .. always on the top, devices sorted by device number",
		0,	/* ------ */
		L"Example: file42.txt comes after file9.txt, because 42 > 9",
		0,
		L"The extension is also known as a file name suffix",
		0,0,0,0,
		L"Useful in a sendmail queue directory"
	};
	FILE_ENTRY *pfe;
	const wchar_t *msg, *ts;
	wchar_t *pch;
	int curs;

	move(LNO_INFO,0);
	addstr("  ");		/* MARGIN2 */

	/* extra panel lines */
	if (panel->curs < 0 && panel->min < 0 && !panel->filtering
	  && (msg = panel->extra[panel->curs - panel->min].info) != 0) {
		putwcs_trunc_col(msg,disp_data.scrcols,0);
		return;
	}

	if (!VALID_CURSOR(panel)) {
		clrtoeol();
		return;
	}

	/* regular panel lines */
	curs = panel->curs;
	msg = 0;
	switch (panel->type) {
		case PANEL_TYPE_CFG:
			msg = panel_cfg.config[curs].help;
			break;
		case PANEL_TYPE_COMPL:
			if ((msg = panel_compl.cand[curs]->aux) != 0 && panel_compl.aux != 0)
				addwstr(panel_compl.aux);
			break;
		case PANEL_TYPE_FILE:
			pfe = ppanel_file->files[curs];
			if (pfe->file_type == FT_NA)
				msg = L"no status information available";
			else
				print_fields(pfe,disp_data.scrcols - 2 * MARGIN2,disp_data.layout_line);
			break;
		case PANEL_TYPE_LOG:
			putwcs_trunc(convert2w(panel_log.line[curs]->levelstr),16,0);
			ts = convert2w(panel_log.line[curs]->timestamp);
			/* some locales use non-breaking spaces, replace them in-place by regular spaces */
			for (pch = (wchar_t *)ts; *pch; pch++)
				if (*pch == L'\xa0')
					*pch = L' ';
			putwcs_trunc_col(ts,disp_data.scrcols - MARGIN2,0);
			break;
		case PANEL_TYPE_CMP:
			if (curs < ARRAY_SIZE(info_cmp))
				msg = info_cmp[curs];
			break;
		case PANEL_TYPE_SORT:
			if (curs < ARRAY_SIZE(info_sort))
				msg = info_sort[curs];
			break;
		default:
			;
	}

	if (msg)	
		putwcs_trunc(msg,disp_data.scrcols - MARGIN2,0);
	else
		clrtoeol();
}

/* information line */
static void
win_helpline(void)
{
	const wchar_t *msg;
	FLAG bold = 0;

	move(LNO_HELP,0);

	/* warnmsg has greater priority than a remark */
	if (helpline.warning) {
		flash();
		msg = L" Press any key.";
		attron(attrb);
		putwcs_trunc(helpline.warning,disp_data.scrcols - wc_cols(msg,0,-1) - 1 /* dot */,OPT_NOPAD);
		addch('.');
		attroff(attrb);
		putwcs_trunc_col(msg,disp_data.scrcols,0);
		return;
	}

	if (helpline.info) {
		attron(attrb);
		addstr("-- ");
		putwcs_trunc(helpline.info,disp_data.scrcols - 6 /* dashes */,OPT_NOPAD);
		putstr_trunc_col(" --",disp_data.scrcols,0);
		attroff(attrb);
		return;
	}

	if ((msg = helpline.help_tmp) != 0) {
		bold = 1;
		if (helpline.exp_tmp == 0)
			helpline.exp_tmp = time(0) + HELPTMPTIME;
	}
	else if ((msg = panel->help) == 0 && (msg = helpline.help_base) == 0) {
		clrtoeol();
		return;
	}

	addch(' ');
	BLANK(disp_data.scrcols - wc_cols(msg,0,-1) - 2 * MARGIN1);
	if (bold)
		attron(attrb);
	putwcs_trunc_col(msg,disp_data.scrcols,0);
	if (bold)
		attroff(attrb);
}

/* HELPMSG_XXX defined in inout.h */
void
win_sethelp(int type, const wchar_t *msg)
{
	switch (type) {
	case HELPMSG_BASE:
		if (msg != 0 && helpline.help_base != 0)
			return;		/* must reset to 0 first! */
		helpline.help_base = msg;
		break;
	case HELPMSG_OVERRIDE:
		panel->help = msg;
		break;
	case HELPMSG_TMP:
		if ((helpline.help_tmp = msg) == 0)
			helpline.exp_tmp = 0;
		/* exception: HELPMSG_TMP can be called while curses is disabled */
		if (!disp_data.curses)
			return;
		break;
	case HELPMSG_INFO:
		helpline.info = msg;
		break;
	case HELPMSG_WARNING:
		helpline.warning = msg;
		win_helpline();
		kbd_getany();
		helpline.warning = 0;
	}
	win_helpline();
}

void
win_bar(void)
{
	int pad;
	static int len = 0;

	if (len == 0)
		len = wc_cols(user_data.loginw,0,-1) + 1 /* at sign */
		  + wc_cols(user_data.hostw,0,-1) + MARGIN1 ;

	attron(attrr);
	move(LNO_BAR,0);

	switch (get_current_mode()) {
	case MODE_FILE:
		bar = L" F1=help  alt-M=menu  |      CLEX file manager " ;
		break;
	case MODE_HELP:
		bar = L" F1=help  ctrl-C=exit  <-- | CLEX file manager ";
		break;
	default:
		bar = L" F1=help  ctrl-C=exit |      CLEX file manager ";
	}
	pad = putwcs_trunc_col(bar, disp_data.scrcols,OPT_NOPAD) - len;
	if (pad < 0)
		char_line(' ',len + pad);
	else {
		BLANK(pad);
		putwcs_trunc(user_data.loginw,len,OPT_NOPAD);
		addch('@');
		putwcs_trunc_col(user_data.hostw,disp_data.scrcols,0);
	}
	attroff(attrr);
}

void
win_edit(void)
{
	int len, width, i, last, written;
	const wchar_t *str;

	/* special case: no textline */
	if (textline == 0) {
		move(LNO_EDIT,0);
		clrtobot();
		return;
	}

	str = USTR(textline->line) + textline->offset;
	len = wcslen(str);
	for (i = 0; i < disp_data.cmdlines; i++) {
		move(LNO_EDIT + i,0);
		last = i == disp_data.cmdlines - 1;	/* 1 or 0 */
		width = disp_data.scrcols - last;	/* avoid writing to the bottom right corner */

		if (i == 0) {
			/* prompt */
			if (textline->offset == 0) {
				if (textline->size > 0
				  && ( (panel->type != PANEL_TYPE_DIR_SPLIT
				    && panel->type != PANEL_TYPE_DIR) || panel->norev) )
					attrset(attrb);
				addwstr(USTR(textline->prompt));
				width -= textline->promptwidth;
			}
			else {
				attrset(attrb);
				addch('<');
				width--;
			}
			attroff(attrb);
		}

		if (len == 0) {
			clrtoeol();
			written = width;	/* all spaces */
		}
		else {
			written = putwcs_trunc(str,width,last ? 0 : OPT_NOCONT);
			if (written > len)
				len = 0;
			else {
				str += written;
				len -= written;
			}
		}
		chars_in_line[i] = written;
	}
}

/* number of textline characters written to by win_edit() */
int
sum_linechars(void)
{
	int i, sum;

	sum = chars_in_line[0];
	for (i = 1; i < disp_data.cmdlines; i++)
		sum += chars_in_line[i];

	return sum;
}

/****** win_panel() and friends  ******/

void
draw_line_bm(int ln)
{
	putwcs_trunc(SDSTR(panel_bm.bm[ln]->name),panel_bm.cw_name,0);
	addstr("  ");
	putwcs_trunc_col(USTR(panel_bm.bm[ln]->dirw),disp_data.panrcol,OPT_SQUEEZE);
}

void
draw_line_bm_edit(int ln)
{
	wchar_t *tag, *msg;

	if (ln == 0) {
		tag = L"     name: ";
		msg = SDSTR(panel_bm_edit.bm->name);
	} else {
		/* ln == 1 */
		tag = L"directory: ";
		msg = USTR(panel_bm_edit.bm->dir) ? USTR(panel_bm_edit.bm->dirw) : L"";
	}

	addwstr(tag);
	if (*msg == L'\0')
		msg = L"-- none --";
	putwcs_trunc_col(msg,disp_data.panrcol,0);
}

void
draw_line_cfg(int ln)
{
	putstr_trunc(panel_cfg.config[ln].var,CFGVAR_LEN,0);
	addstr(" = ");
	putwcs_trunc_col(cfg_print_value(ln),disp_data.panrcol,0);
}

void
draw_line_cfg_menu(int ln)
{
	putwcs_trunc(panel_cfg_menu.desc[ln],disp_data.pancols,0);
}

void
draw_line_cmp(int ln)
{
	/* see also win_infoline() */
	static const wchar_t *description[CMP_TOTAL_ + 1] = {
		L"restrict to regular files only",
		L"compare file size",
		L"compare file mode",
		L"compare file ownership (user and group)",
		L"compare file data (contents)",
		L"--> Compare name, type and attributes selected above"
	};

	if (ln < CMP_TOTAL_)
		CHECKBOX(COPT(ln));	
	putwcs_trunc_col(description[ln],disp_data.panrcol,0);
}

void
draw_line_cmp_sum(int ln)
{
	static const wchar_t *description[] = {
		L"total number of files in panels",
		L"\\_ UNIQUE FILENAMES         ",
		L"\\_ pairs of files compared  ",
		L"\\_ DIFFERING",
		L"\\_ ERRORS   ",	/* line #4 is hidden if there are no errors */
		L"\\_ equal    "
	};
	wchar_t *txt, buf[64];
	int p1, p2;
	FLAG marked;

	if (ln >= 4 && panel->cnt != ARRAY_SIZE(description))
		ln++;

	txt = buf;
	marked = 0;
	switch (ln) {
	case 0:
		p1 = ppanel_file->pd->cnt        - panel_cmp_sum.nonreg1;
		p2 = ppanel_file->other->pd->cnt - panel_cmp_sum.nonreg2;
		swprintf(txt,ARRAY_SIZE(buf),L"%4d + %d%ls",p1,p2,
		  COPT(CMP_REGULAR) ? L" (regular files only)" : L"");
		break;
	case 1:
		p1 = ppanel_file->pd->cnt        - panel_cmp_sum.nonreg1 - panel_cmp_sum.names;
		p2 = ppanel_file->other->pd->cnt - panel_cmp_sum.nonreg2 - panel_cmp_sum.names;
		if ( (marked = p1 > 0 || p2 > 0) )
			swprintf(txt,ARRAY_SIZE(buf),L"%4d + %d",p1,p2);
		else
			txt = L"     -";
		break;
	case 2:
		swprintf(txt,ARRAY_SIZE(buf),L"  %4d",panel_cmp_sum.names);
		break;
	case 3:
		p1 = panel_cmp_sum.names - panel_cmp_sum.equal - panel_cmp_sum.errors;
		if ( (marked = p1 > 0) )
			swprintf(txt,ARRAY_SIZE(buf),L"  %4d",p1);
		else
			txt = L"     -";
		break;
	case 4:
		swprintf(txt,ARRAY_SIZE(buf),L"  %4d",panel_cmp_sum.errors);
		marked = 1;
		break;
	case 5:
		swprintf(txt,ARRAY_SIZE(buf),L"  %4d",panel_cmp_sum.equal);
		break;
	}
	BLANK(32 - wc_cols(description[ln],0,-1));
	addwstr(description[ln]);
	addstr(":  ");
	if (marked)
		attron(attrb);
	putwcs_trunc_col(txt,disp_data.panrcol,0);
	if (marked)
		attroff(attrb);
}

void
draw_line_compl(int ln)
{
	COMPL_ENTRY *pcc;

	pcc = panel_compl.cand[ln];
	if (panel_compl.filenames) {
		addstr(pcc->is_link ? "-> " : "   " );	/* 3 */
		addstr(type_symbol[pcc->file_type]);	/* 4 */
		BLANK(2);
	}
	else
		BLANK(9);
	putwcs_trunc(SDSTR(pcc->str),disp_data.pancols - 9,0);
}

void
draw_line_dir(int ln)
{
	int shlen, width;
	const wchar_t *dir;

	dir = panel_dir.dir[ln].namew;
	shlen = panel_dir.dir[ln].shlen;

	if (shlen && (ln == panel_dir.pd->top || shlen >= disp_data.pancols))
		shlen = 0;
	if (shlen == 0)
		width = 0;
	else {
		width = wc_cols(dir,0,shlen);
		BLANK(width - 2);
		addstr("__");
	}
	putwcs_trunc_col(dir + shlen,disp_data.panrcol,0);
}

void
draw_line_dir_split(int ln)
{
	putwcs_trunc(convert2w(dir_split_dir(ln)),disp_data.pancols,0);
}

void
draw_line_file(int ln)
{
	FILE_ENTRY *pfe;

	pfe = ppanel_file->files[ln];
	if (pfe->select && !pfe->dotdir)
		attron(attrb);

	/* 10 columns reserved for the filename */
	print_fields(pfe,disp_data.pancols - 10,disp_data.layout_panel);
	if (!pfe->symlink)
		putwcs_trunc_col(SDSTR(pfe->filew),disp_data.panrcol,0);
	else {
		putwcs_trunc_col(SDSTR(pfe->filew),disp_data.panrcol,OPT_NOPAD);
		putwcs_trunc_col(L" -> ",disp_data.panrcol,OPT_NOPAD);
		putwcs_trunc_col(USTR(pfe->linkw),disp_data.panrcol,0);
	}

	if (pfe->select)
		attroff(attrb);
}

void
draw_line_fopt(int ln)
{
	static const wchar_t *description[FOPT_TOTAL_] = {
		L"substring matching: ignore the case of the characters",
		L"pattern matching: wildcards match the dot in hidden .files",
		L"file panel filtering: always show directories",
		/* must correspond with FOPT_XXX */
	};

	CHECKBOX(FOPT(ln));
	putwcs_trunc(description[ln],disp_data.pancols - BOX4,0);
}

void
draw_line_group(int ln)
{
	printw("%6u  ",(unsigned int)panel_group.groups[ln].gid);
	putwcs_trunc_col(panel_group.groups[ln].group,disp_data.panrcol,0);
}

void
draw_line_help(int ln)
{
	HELP_LINE *ph;
	int i, active, links;

	ph = panel_help.line[ln];
	links = ph->links;
	putwcs_trunc(ph->text,disp_data.pancols,links ? OPT_NOPAD : 0);
	if (links == 0)
		return;

	active = (ln != panel->curs || panel->filtering) ? -1
	  : ln == panel_help.lnk_ln ? panel_help.lnk_act : 0;
	for (i = 0; i < links; i++) {
		attron(i == active ? attrr : attrb);
		putwcs_trunc_col(ph[3 * i + 2].text,disp_data.panrcol,OPT_NOPAD);
		attroff(i == active ? attrr : attrb);
		putwcs_trunc_col(ph[3 * i + 3].text,disp_data.panrcol,i == links - 1 ? 0 : OPT_NOPAD);
	}
}

void
draw_line_hist(int ln)
{
	static const wchar_t *failstr;
	static int faillen = 0;

	if (faillen == 0) {
		failstr = L"failed: ";
		faillen = wc_cols(failstr,0,-1);
	}
	if (panel_hist.hist[ln]->failed)
		addwstr(failstr);
	else
		BLANK(faillen);
	putwcs_trunc(USTR(panel_hist.hist[ln]->cmd),disp_data.pancols - faillen,0);
}

void
draw_line_log(int ln)
{
	int i, len;
	const wchar_t *msg;
	FLAG warn;

	msg = USTR(panel_log.line[ln]->msg);
	if ( (warn = panel_log.line[ln]->level == MSG_W) )
		attron(attrb);
	if (panel_log.scroll == 0)
		putwcs_trunc(msg,disp_data.pancols,0);
	else {
		addch('<' | attrb);
		for (i = len = 0; msg[i] != L'\0' && len < panel_log.scroll; i++)
			len += WCW(msg[i]);
		while (utf_iscomposing(msg[i]))
			i++;
		putwcs_trunc(msg + i,disp_data.pancols - 1,0);
	}
	if (warn)
		attroff(attrb);
}

void
draw_line_mainmenu(int ln)
{
	static const wchar_t *description[] = {
		L"help                                     <F1>",
		L"change working directory                 alt-W",
		L"  change into root directory             alt-/",
		L"  change into parent directory           alt-.",
		L"  change into home directory             alt-~ or alt-`",
		L"  bookmarks                              alt-K",
		L"Bookmark the current directory           ctrl-D",
		L"command history                          alt-H",
		L"sort order for filenames                 alt-S",
		L"re-read the current directory            ctrl-R",
		L"compare directories                      alt-=",
		L"filter on/off                            ctrl-F",
		L"select files:  select using pattern      alt-+",
		L"               deselect using pattern    alt--",
		L"               invert selection          alt-*",
		L"filtering and pattern matching options   alt-O",
		L"user (group) information                 alt-U (alt-G)",
		L"message log                              alt-L",
		L"notifications                            alt-N",
		L"configure CLEX                           alt-C",
		L"program version                          alt-V",
		L"quit                                     alt-Q"
		/* must correspond with tab_mainmenu[] in control.c */
	};

	putwcs_trunc(description[ln],disp_data.pancols,0);
}

void
draw_line_notif(int ln)
{
	static const wchar_t *description[NOTIF_TOTAL_] = {
		L"Warning:  rm command deletes files (not 100% reliable)",
		L"Warning:  command line is too long to be displayed",
		L"Reminder: selection marks on . and .. are not honored",
		L"Reminder: selected file(s) vs. current file",
		L"Notice:   file with a timestamp in the future encountered"
		/* must correspond with NOPT_XXX */
	};

	CHECKBOX(!NOPT(ln));
	putwcs_trunc(description[ln],disp_data.pancols - BOX4,0);
}

void
draw_line_paste(int ln)
{
	static const wchar_t *description[] = {
		L"the name to be completed starts at the cursor position",
		L"complete a name: automatic",
		L"                 file: any type",
		L"                 file: directory",
		L"                 file: executable",
		L"                 user",
		L"                 group",
		L"                 environment variable",
		L"complete a command from the command history        alt-P",
		L"insert: the current filename                       <F2>",
		L"        all selected filenames               <esc> <F2>",
		L"        the full pathname of current file          ctrl-A",
		L"        the secondary working directory name       ctrl-E",
		L"        the current working directory name   <esc> ctrl-E",
		L"        the target of a symbolic link              ctrl-O"
		/* must correspond with tab_pastemenu[] in control.c */
	};
	if (ln == 0)
		CHECKBOX(panel_paste.wordstart);
	putwcs_trunc_col(description[ln],disp_data.panrcol,0);
}

void
draw_line_preview(int ln)
{
	if (ln >= panel_preview.realcnt) {
		attron(attrb);
		putwcs_trunc(L" --- end of preview ---",disp_data.pancols,0);
		attroff(attrb);
		return;
	}
		
	putwcs_trunc(USTR(panel_preview.line[ln]),disp_data.pancols,0);
}

void
draw_line_sort(int ln)
{
	int size;

	/* see also win_infoline() */
	static const wchar_t
	*description0[HIDE_TOTAL_] = {
		L"show hidden .files",
		L"show hidden .files, but not in the home directory",
		L"do not show hidden .files"
		/* must correspond with HIDE_XXX */
	},
	*description1[GROUP_TOTAL_] = {
		L"do not group files by type",
		L"group: directories, special files, plain files",
		L"group: directories, devices, special files, plain files"
		/* must correspond with GROUP_XXX */
	},
	*description2[SORT_TOTAL_] = {
		L"sort by name and number",
		L"sort by name",
		L"sort by filename.EXTENSION",
		L"sort by size [small -> large]",
		L"sort by size [large -> small]",
		L"sort by time of last modification [recent -> old]",
		L"sort by time of last modification [old -> recent]",
		L"sort by reversed name"
		/* must correspond with SORT_XXX */
	},
	*description3[3] = {
		L"--> save & apply globally",
		L"--> apply temporarily to the current file panel's contents"
	};

	size = ARRAY_SIZE(description0);
	if (ln < size) {
		RADIOBUTTON(panel_sort.newhide == ln);
		putwcs_trunc(description0[ln],disp_data.pancols - BOX4,0);
		return;
	}
	if (ln == size) {
		putstr_trunc("----------------",disp_data.pancols,0);
		return;
	}
	ln -= size + 1;

	size = ARRAY_SIZE(description1);
	if (ln < size) {
		RADIOBUTTON(panel_sort.newgroup == ln);
		putwcs_trunc(description1[ln],disp_data.pancols - BOX4,0);
		return;
	}
	if (ln == size) {
		putstr_trunc("----------------",disp_data.pancols,0);
		return;
	}
	ln -= size + 1;

	size = ARRAY_SIZE(description2);
	if (ln < size) {
		RADIOBUTTON(panel_sort.neworder == ln);
		putwcs_trunc(description2[ln],disp_data.pancols - BOX4,0);
		return;
	}
	ln -= size;

	putwcs_trunc(description3[ln],disp_data.pancols,0);
	return;
}

#define MIN_GECOS 10	/* columns reserved for the gecos field */

void
draw_line_user(int ln)
{
	int col;

	printw("%6u  ",(unsigned int)panel_user.users[ln].uid);
	col = MARGIN2 + 8 /*printw above*/ + panel_user.maxlen;
	if (col > disp_data.panrcol - MIN_GECOS - 1 /* 1 space */)
		col = disp_data.panrcol - MIN_GECOS - 1;
	putwcs_trunc_col(panel_user.users[ln].login,MARGIN2 + 8 + panel_user.maxlen,0);
	addch(' ');
	putwcs_trunc_col(panel_user.users[ln].gecos,disp_data.panrcol,0);
}

static void
draw_panel_line(int curs)
{
	const wchar_t *msg;

	move(LNO_PANEL + curs - panel->top,0);
	if (curs >= panel->cnt) {
		clrtoeol();
		return;
	}

	if (panel->curs == curs) {
		addch('>');
		if (!panel->norev)
			attron(attrr);
		addch(' ');
	}
	else
		addstr("  ");
	if (curs < 0) {
		/* extra line */
		addstr("--> ");
		msg = panel->extra[curs - panel->min].text;
		putwcs_trunc(msg ? msg : L"Exit this panel",disp_data.pancols - 4 /* arrow */,0);
	}
	else
		(*panel->drawfn)(curs);
	if (panel->curs == curs) {
		addch(' ');
		attroff(attrr);
		addch('<');
	}
	else
		addstr("  ");
}

static void
draw_panel(int optimize)
{
	static int save_top, save_curs, save_ptype = -1;
	int curs;

	if (panel->type != save_ptype) {
		/* panel type has changed */
		optimize = 0;
		save_ptype = panel->type;
	}

	if (optimize && save_top == panel->top) {
		/* redraw only the old and new current lines */
		draw_panel_line(save_curs);
		if (save_curs != panel->curs) {
			posctl.update = 1;
			draw_panel_line(panel->curs);
			save_curs = panel->curs;
		}
	}
	else {
		posctl.update = 1;
		/* redraw all lines */
		for (curs = panel->top; curs < panel->top + disp_data.panlines; curs++)
			draw_panel_line(curs);
		save_top = panel->top;
		save_curs = panel->curs;
	}

	win_infoline();
}

/* win_panel() without optimization */
void
win_panel(void)
{
	draw_panel(0);
}

/*
 * win_panel() with optimization
 *
 * use this win_panel() version if the only change made since last
 * win_panel() call is a cursor movement or a modification of the
 * current line
 */
void
win_panel_opt(void)
{
	draw_panel(1);
}
