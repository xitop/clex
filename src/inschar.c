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

#include "inschar.h"

#include "edit.h"			/* edit_nu_insertchar() */
#include "filter.h"			/* filteredit_nu_insertchar() */
#include "inout.h"			/* win_filter() */
#include "log.h"			/* msgout() */

#define MAXCHAR 0x10FFFF	/* enough for Unicode, who needs more ? */

/* insert literal character */
void
cx_edit_inschar(void)
{
	win_sethelp(HELPMSG_INFO,L"NOW PRESS THE KEY TO BE INSERTED ");
	kbd_rawkey();
	win_sethelp(HELPMSG_INFO,0);

	if (kinp.fkey != 0)
		msgout(MSG_i,"Function key code cannot be inserted");
	else {
		(panel->filtering == 1 ? filteredit_insertchar : edit_insertchar)(kinp.key);
		if (kinp.key == WCH_ESC)
			kinp.key = 0;	/* clear the escape from Alt-X */
	}
}

/* destination: this line or current panel's filter expression (if null) */
static TEXTLINE *dest;

void
inschar_initialize(void)
{
	edit_setprompt(&line_inschar,L"Insert characters: ");
}

int
inschar_prepare(void)
{
	if (panel->filtering == 1) {
		dest = 0;
		panel->filtering = 2;
	}
	else
		dest = textline;
	textline = &line_inschar;
	edit_nu_kill();
	return 0;
}


/* convert a decimal digit */
static int
dec_value(wchar_t ch)
{
	if (ch >= L'0' && ch <= L'9')
		return ch - L'0';
	return -1;
}

/* convert a hex digit */
static int
hex_value(wchar_t ch)
{
	if (ch >= L'0' && ch <= L'9')
		return ch - L'0';
	if (ch >= L'a' && ch <= L'f')
		return ch - L'a' + 10;
	if (ch >= L'A' && ch <= L'F')
		return ch - L'A' + 10;
	return -1;
}

/* convert ctrl-X */
static int
ctrl_value(wchar_t ch)
{
	if (ch >= L'a' && ch <= L'z')
		return ch - L'a' + 1;
	if (ch >= L'A' && ch <= L'Z')
		return ch - L'A' + 1;
	return -1;
}

static void
insert_dest(wchar_t ch)
{
	if (ch <= 0 || ch > MAXCHAR) {
		msgout(MSG_NOTICE,"Insert character: value out of bounds");
		return;
	}
	if (dest)
		edit_nu_insertchar(ch);
	else
		filteredit_nu_insertchar(ch);
}

void
cx_ins_enter(void)
{
	int mode, value, conv;
	const wchar_t *str;
	wchar_t ch;

	if (dest)
		textline = dest;

	mode = 0;
	value = 0;
	for (str = USTR(line_inschar.line); /* until break */; str++) {
		ch = *str;
		if (mode == 1) {
			/* ^ + X = ctrl-X */
			if ((conv = ctrl_value(ch)) >= 0)
				insert_dest(conv);
			else {
				insert_dest(L'^');
				insert_dest(ch);
			}
			mode = 0;
			continue;
		}
		else if (mode == 2) {
			/* decimal value */
			if ((conv = dec_value(ch)) >= 0) {
				value = 10 * value + conv;
				continue;
			}
			if (value)
				insert_dest(value);
			/* ch will be processed below */
		}
		else if (mode == 3) {
			/* hex value */
			if ((conv = hex_value(ch)) >= 0) {
				value = 16 * value + conv;
				continue;
			}
			if (value)
				insert_dest(value);
			/* ch will be processed below */
		}

		if (ch == L'\0')
			break;

		if (ch == L'^')
			mode = 1;
		else if (((ch == L'0' || ch == L'\\') && str[1] == L'x')
		  || ((ch == L'U' || ch == L'u') && str[1] == L'+')) {
			/* prefix \x or 0x or U+ or even u+ */
			str++;
			mode = 3;
			value = 0;
		}
		else if ((conv = dec_value(ch)) >= 0) {
			mode = 2;
			value = conv;
		}
		else {
			mode = 0;
			if (ch != L' ')
				insert_dest(ch);
		}
	}

	if (dest)
		textline = 0;
	else {
		panel->filtering = 1;
		win_filter();
	}

	next_mode = MODE_SPECIAL_RETURN;
}
