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
#include <string.h>			/* strcmp() */
#include <wctype.h>			/* iswlower() */

#include "edit.h"

#include "cfg.h"			/* cfg_str() */
#include "control.h"		/* get_current_mode() */
#include "filter.h"			/* cx_filter() */
#include "filepanel.h"		/* cx_files_enter() */
#include "inout.h"			/* win_edit() */
#include "history.h"		/* hist_reset_index() */
#include "log.h"			/* msgout() */
#include "mbwstring.h"		/* wc_cols() */
#include "util.h"			/* jshash() */

static FLAG cmd_auto = 0;	/* $! control sequence */

/* adjust 'offset' so the cursor is visible */
/* returns 1 if offset has changed, otherwise 0 */
#define OFFSET_STEP	16
int
edit_adjust(void)
{
	int old_offset, offset, screen, curs, pwidth, cols, o2c;
	wchar_t *str;

	if (textline == 0)
		return 0;

	offset = textline->offset;
	curs = textline->curs;
	pwidth = textline->promptwidth;
	str = USTR(textline->line);

	if (curs > 0 && curs < textline->size) {
		/* composed UTF-8 characters: place the cursor on the base character */
		while (curs > 0 && utf_iscomposing(str[curs]))
			curs--;
		textline->curs = curs;
	}

	/*
	 * screen = number of display columns available,
	 * substract columns possibly left unused because of double-width chars
	 * not fitting at the right border, reserved position for the '>' mark,
	 * and the bottom right corner position that could cause a scroll
	 */
	screen = (disp_data.scrcols - 1) * disp_data.cmdlines - 2;

	if (offset <= curs && wc_cols(str,offset,curs) < screen - (offset ? 1 : pwidth))
		/* no need for a change */
		return 0;

	old_offset = offset;
	if (offset && wc_cols(str,0,curs) < screen - pwidth) {
		/* 1. zero offset is preferred whenever possible */
		textline->offset = 0;
		return 1;
	}
	/* o2c = desired width in columns from the offset to the cursor */
	if (wc_cols(str,curs,-1) < screen - 1)
		/* 2. if EOL is visible, minimize the unused space after the EOL */
		o2c = screen - 1;
	else if (curs < offset)
		/* 3. if the cursor appears to move left, put it on the first line */
		o2c = disp_data.scrcols - 1;
	else
		/* 4. if the cursor appears to move right, put it on the last line */
		o2c = (disp_data.cmdlines - 1) * disp_data.scrcols + OFFSET_STEP;

	/* estimate the offset, then fine adjust it in few iteration rounds */
	offset = curs <= o2c ? 0 : (curs - o2c) / OFFSET_STEP * OFFSET_STEP;
	cols = wc_cols(str,offset,curs);
	while (offset > OFFSET_STEP && cols < o2c) {
		cols += wc_cols(str,offset - OFFSET_STEP,offset);
		offset -= OFFSET_STEP;
	}
	while (offset == 0 || cols >= o2c) {
		cols -= wc_cols(str,offset,offset + OFFSET_STEP);
		offset += OFFSET_STEP;
	}
	return old_offset != (textline->offset = offset);
}

/* make changes to 'textline' visible on the screen */
void
edit_update(void)
{
	edit_adjust();
	win_edit();
}

/*
 * if you have only moved the cursor, use this optimized
 * version of edit_update() instead
 */
void
edit_update_cursor(void)
{
	if (edit_adjust())
		win_edit();
}

/*
 * edit_islong() can be called after win_edit() and it returns 0
 * if the entire 'textline' is displayed from the beginning to the end,
 * or 1 if it does not fit due to excessive length
 */
int
edit_islong(void)
{
	return textline->offset || sum_linechars() < textline->size;
}

int
edit_isauto(void)
{
	return cmd_auto;
}

void
cx_edit_begin(void)
{
	textline->curs = 0;
	edit_update_cursor();
}

void
cx_edit_end(void)
{
	textline->curs = textline->size;
	edit_update_cursor();
}

/*
 * "_nu_" means "no update" version, the caller is responsible
 * for calling the update function edit_update().
 *
 * The point is to invoke several _nu_ functions and then make the 'update' just once.
 *
 * Note: The edit_update() consists of edit_adjust() followed by * win_edit().
 * If the 'offset' is fine (e.g. after edit_nu_kill), the edit_adjust() may be skipped.
 */
static void
edit_nu_left(void)
{
	FLAG move = 1;

	/* composed UTF-8 characters: move the cursor also over combining chars */
	while (move && textline->curs > 0)
		move = utf_iscomposing(USTR(textline->line)[--textline->curs]);
}

void
cx_edit_left(void)
{
	edit_nu_left();
	edit_update_cursor();
}

static void
edit_nu_right(void)
{
	FLAG move = 1;

	/* composed UTF-8 characters: move the cursor also over combining chars */
	while (move && textline->curs < textline->size)
		move = utf_iscomposing(USTR(textline->line)[++textline->curs]);
}

void
cx_edit_right(void)
{
	edit_nu_right();
	edit_update_cursor();
}

void
cx_edit_up(void)
{
	int width, curs;
	wchar_t *line;

	line = USTR(textline->line);
	width = disp_data.scrcols;
	curs = textline->curs;
	while (curs > 0 && width > 0) {
		curs--;
		width -= WCW(line[curs]);
	}
	textline->curs = curs;
	edit_update_cursor();
}

void
cx_edit_down(void)
{
	int width, curs;
	wchar_t *line;

	line = USTR(textline->line);
	width = disp_data.scrcols;
	curs = textline->curs;
	while (curs < textline->size && width > 0) {
		curs++;
		width -= WCW(line[curs]);
	}
	textline->curs = curs;
	edit_update_cursor();
}

static wchar_t
wordsep(void)
{
	if (textline == &line_cmd)
		return L' ';
	if (textline == &line_dir)
		return L'/';
	return get_current_mode() == MODE_BM_EDIT2 ? L'/' : L' ';
}

/* move one word left */
void
cx_edit_w_left(void)
{
	const wchar_t *line;
	wchar_t wsep;
	int curs;

	wsep = wordsep();
	if ((curs = textline->curs) > 0) {
		line = USTR(textline->line);
		while (curs > 0 && line[curs - 1] == wsep)
			curs--;
		while (curs > 0 && line[curs - 1] != wsep)
			curs--;
		textline->curs = curs;
		edit_update_cursor();
	}
}

/* move one word right */
void
cx_edit_w_right(void)
{
	const wchar_t *line;
	wchar_t wsep;
	int curs;

	wsep = wordsep();
	if ((curs = textline->curs) < textline->size) {
		line = USTR(textline->line);
		while (curs < textline->size && line[curs] != wsep)
			curs++;
		while (curs < textline->size && line[curs] == wsep)
			curs++;
		textline->curs = curs;
		edit_update_cursor();
	}
}

void
cx_edit_mouse(void)
{
	int mode;

	if (!MI_AREA(LINE))
		return;
	if (!MI_CLICK && !MI_WHEEL)
		return;

	if (textline->size) {
		if (MI_CLICK) {
			if (minp.cursor < 0)
				return;
			textline->curs = minp.cursor;
		}
		else {
			mode = get_current_mode();
			if (mode == MODE_SELECT || mode == MODE_DESELECT || mode == MODE_CFG_EDIT_NUM)
				MI_B(4) ? cx_edit_left()   : cx_edit_right();
			else
				MI_B(4) ? cx_edit_w_left() : cx_edit_w_right();
		}
	}
	if (panel->filtering == 1)
		cx_filter();
}

void
edit_nu_kill(void)
{
	if (textline == &line_cmd)
		hist_reset_index();

	textline->curs = textline->size = textline->offset = 0;
	/*
	 * usw_copy() is called to possibly shrink the allocated memory block,
	 * other delete functions don't do that
	 */
	usw_copy(&textline->line,L"");
	disp_data.noenter = 0;
}

void
cx_edit_kill(void)
{
	edit_nu_kill();
	win_edit();
}

/* delete 'cnt' chars at cursor position */
static void
delete_chars(int cnt)
{
	int i;
	wchar_t *line;

	line = USTR(textline->line);
	textline->size -= cnt;
	for (i = textline->curs; i <= textline->size; i++)
		line[i] = line[i + cnt];
}

void
cx_edit_backsp(void)
{
	int pos;
	FLAG del = 1;

	if (textline->curs == 0)
		return;

	pos = textline->curs;
	/* composed UTF-8 characters: delete also the combining chars */
	while (del && textline->curs > 0)
		del = utf_iscomposing(USTR(textline->line)[--textline->curs]);
	delete_chars(pos - textline->curs);
	edit_update();
}

void
cx_edit_delchar(void)
{
	int pos;
	FLAG del = 1;

	if (textline->curs == textline->size)
		return;

	pos = textline->curs;
	/* composed UTF-8 characters: delete also the combining chars */
	while (del && textline->curs < textline->size)
		del = utf_iscomposing(USTR(textline->line)[++pos]);
	delete_chars(pos - textline->curs);
	edit_update();
}

/* delete until the end of line */
void
cx_edit_delend(void)
{
	USTR(textline->line)[textline->size = textline->curs] = L'\0';
	edit_update();
}

/* delete word */
void
cx_edit_w_del(void)
{
	int eow;
	wchar_t *line;

	eow = textline->curs;
	line = USTR(textline->line);
	if (line[eow] == L' ' || line[eow] == L'\0')
		return;

	while (textline->curs > 0 && line[textline->curs - 1] != L' ')
		textline->curs--;
	while (eow < textline->size && line[eow] != L' ')
		eow++;
	while (line[eow] == L' ')
		eow++;
	delete_chars(eow - textline->curs);
	edit_update();
}

/* make room for 'cnt' chars at cursor position */
static wchar_t *
insert_space(int cnt)
{
	int i;
	wchar_t *line, *ins;

	usw_resize(&textline->line,textline->size + cnt + 1);
	line = USTR(textline->line);
	ins = line + textline->curs;	/* insert new character(s) here */
	textline->size += cnt;
	textline->curs += cnt;
	for (i = textline->size; i >= textline->curs; i--)
		line[i] = line[i - cnt];

	return ins;
}

void
edit_nu_insertchar(wchar_t ch)
{
	*insert_space(1) = ch;
}

void
edit_insertchar(wchar_t ch)
{
	edit_nu_insertchar(ch);
	edit_update();
}

void
edit_nu_putstr(const wchar_t *str)
{
	usw_copy(&textline->line,str);
	textline->curs = textline->size = wcslen(str);
}

void
edit_putstr(const wchar_t *str)
{
	edit_nu_putstr(str);
	edit_update();
}

/*
 * returns number of quoting characters required:
 *   2 = single quotes 'X' (newline)
 *   1 = backslash quoting \X (shell metacharacters)
 *   0 = no quoting (regular characters)
 * in other words: it returns non-zero if 'ch' is a special character
 */
int
edit_isspecial(wchar_t ch)
{
	if (ch == WCH_CTRL('J') || ch == WCH_CTRL('M'))
		return 2;	/* 2 extra characters: X -> 'X' */
	/* built-in metacharacters (let's play it safe) */
	if (wcschr(L"\t ()<>[]{}#$&\\|?*;\'\"`~",ch))
		return 1;	/* 1 extra character: X -> \X */
	/* C-shell */
	if (user_data.shelltype == SHELL_CSH && (ch == L'!' || ch == L':'))
		return 1;
	/* additional special characters to be quoted */
	if (*cfg_str(CFG_QUOTE) && wcschr(cfg_str(CFG_QUOTE),ch))
		return 1;

	return 0;
}

/* return value: like edit_isspecial() */
static int
how_to_quote(wchar_t ch, int qlevel)
{
	if (qlevel == QUOT_NORMAL) {
		/* full quoting */
		if (ch == L'=' || ch == L':')
			/* not special, but quoted to help the name completion */
			return 1;
		return edit_isspecial(ch);
	}
	if (qlevel == QUOT_IN_QUOTES) {
		/* inside double quotes */
		if (ch == L'\"' || ch == L'\\' || ch == L'$' || ch == L'`')
			return 1;
	}
	return 0;
}

void
edit_nu_insertstr(const wchar_t *str, int qlevel)
{
	int len, mch;
	wchar_t ch, *ins;

	for (len = mch = 0; (ch = str[len]) != L'\0'; len++)
		if (qlevel != QUOT_NONE)
			mch += how_to_quote(ch,qlevel);
	if (len == 0)
		return;

	ins = insert_space(len + mch);
	while ((ch = *str++) != L'\0') {
		if (qlevel != QUOT_NONE) {
			mch = how_to_quote(ch,qlevel);
			if (mch == 2) {
				*ins++ = L'\'';
				*ins++ = ch;
				ch = L'\'';
			}
			else if (mch == 1)
				*ins++ = L'\\';
		}
		*ins++ = ch;
	}

	/*
	 * if there is no quoting this is equivalent of:
	 * len = wcslen(str); wcsncpy(insert_space(len),src,len);
	 */
}

void
edit_insertstr(const wchar_t *str, int qlevel)
{
	edit_nu_insertstr(str,qlevel);
	edit_update();
}

/*
 * insert string, expand $x variables:
 *   $$ -> literal $
 *   $1 -> current directory name (primary panel's directory)
 *   $2 -> secondary directory name (secondary panel's directory)
 *   $F -> current file name
 *   $S -> names of all selected file(s)
 *   $f -> $S - if the <ESC> key was pressed and
 *              at least one file has been selected
 *         $F - otherwise
 *   everything else is copied literally
 */
void
edit_macro(const wchar_t *macro)
{
	wchar_t ch, *ins;
	const wchar_t *src;
	int i, cnt, curs, sel;
	FLAG prefix, noenter = 0, automatic = 0, warn_dotdir = 0;
	FILE_ENTRY *pfe;

	/*
	 * implementation note: avoid char by char inserts whenever
	 * possible because of the overhead
	 */

	if (textline->curs == 0 || wcschr(L" :=",USTR(textline->line)[textline->curs - 1]))
		while (*macro == L' ')
			macro++;

	curs = -1;
	for (src = macro, prefix = 0; (ch = *macro++) != L'\0'; ) {
		if (TCLR(prefix)) {
			/* insert everything seen before the '$' prefix */
			cnt = macro - src - 2 /* two chars in "$x" */;
			if (cnt > 0)
				wcsncpy(insert_space(cnt),src,cnt);
			src = macro;

			/* now handle $x */
			if (ch == L'f' && panel->cnt > 0) {
				ch = ppanel_file->selected && kinp.prev_esc ? L'S' : L'F';
				if (ch == L'F' && ppanel_file->selected
				  && !NOPT(NOTIF_SELECTED))
					msgout(MSG_i | MSG_NOTIFY,"press <ESC> before <Fn> if you "
					  "want to work with selected files");
			}
			switch (ch) {
			case L'$':
				edit_nu_insertchar(L'$');
				break;
			case L'1':
				edit_nu_insertstr(USTR(ppanel_file->dirw),QUOT_NORMAL);
				break;
			case L'2':
				edit_nu_insertstr(USTR(ppanel_file->other->dirw),QUOT_NORMAL);
				break;
			case L'c':
				curs = textline->curs;
				break;
			case L'S':
				if (panel->cnt > 0) {
					for (i = cnt = 0, sel = ppanel_file->selected; cnt < sel; i++) {
						pfe = ppanel_file->files[i];
						if (pfe->select) {
							if (pfe->dotdir) {
								sel--;
								warn_dotdir = 1;
								continue;
							}
							if (cnt++)
								edit_nu_insertchar(L' ');
							edit_nu_insertstr(SDSTR(pfe->filew),QUOT_NORMAL);
						}
					}
				}
				break;
			case L'f':
				break;
			case L'F':
				if (panel->cnt > 0) {
					pfe = ppanel_file->files[ppanel_file->pd->curs];
					edit_nu_insertstr(SDSTR(pfe->filew),QUOT_NORMAL);
				}
				break;
			case L':':
				curs = -1;
				edit_nu_kill();
				break;
			case L'!':
				automatic = 1;
				break;
			case L'~':
				noenter = 1;
				break;
			default:
				ins = insert_space(2);
				ins[0] = L'$';
				ins[1] = ch;
			}
		}
		else if (ch == L'$')
			prefix = 1;
	}

	/* insert the rest */
	edit_insertstr(src,QUOT_NONE);

	if (curs >= 0)
		textline->curs = curs;

	if (noenter) {
		disp_data.noenter = 1;
		disp_data.noenter_hash = jshash(USTR(textline->line));
	}

	if (warn_dotdir && !NOPT(NOTIF_DOTDIR))
		msgout(MSG_i | MSG_NOTIFY,"directory names . and .. not inserted");

	if (panel->filtering == 1)
		cx_filter();

	if (automatic && textline->size) {
		cmd_auto = 1;
		cx_files_enter();
		cmd_auto = 0;
	}
}

void cx_edit_cmd_f2(void)	{ edit_macro(L"$f "); 				}
void cx_edit_cmd_f3(void)	{ edit_macro(cfg_str(CFG_CMD_F3));	}
void cx_edit_cmd_f4(void)	{ edit_macro(cfg_str(CFG_CMD_F4));	}
void cx_edit_cmd_f5(void)	{ edit_macro(cfg_str(CFG_CMD_F5));	}
void cx_edit_cmd_f6(void)	{ edit_macro(cfg_str(CFG_CMD_F6));	}
void cx_edit_cmd_f7(void)	{ edit_macro(cfg_str(CFG_CMD_F7));	}
void cx_edit_cmd_f8(void)	{ edit_macro(cfg_str(CFG_CMD_F8));	}
void cx_edit_cmd_f9(void)	{ edit_macro(cfg_str(CFG_CMD_F9)); 	}
void cx_edit_cmd_f10(void)	{ edit_macro(cfg_str(CFG_CMD_F10));	}
void cx_edit_cmd_f11(void)	{ edit_macro(cfg_str(CFG_CMD_F11));	}
void cx_edit_cmd_f12(void)	{ edit_macro(cfg_str(CFG_CMD_F12));	}

static void
paste_exit()
{
	/* when called from the complete/insert panel: exit the panel */
	if (get_current_mode() == MODE_PASTE)
		next_mode = MODE_SPECIAL_RETURN;
}

void
cx_edit_paste_path(void)
{
	if (ppanel_file->pd->cnt)
		edit_macro(strcmp(USTR(ppanel_file->dir),"/") ? L" $1/$F " : L" /$F ");
	paste_exit();
}

void
cx_edit_paste_link(void)
{
	FILE_ENTRY *pfe;

	if (ppanel_file->pd->cnt) {
		pfe = ppanel_file->files[ppanel_file->pd->curs];
		if (!pfe->symlink)
			msgout(MSG_i,"not a symbolic link");
		else {
			edit_nu_insertstr(USTR(pfe->linkw),QUOT_NORMAL);
			edit_insertchar(' ');
		}
	}
	paste_exit();
}

void
cx_edit_paste_currentfile(void)
{
	if (ppanel_file->pd->cnt)
		edit_macro(L"$F ");
	paste_exit();
}

void
cx_edit_paste_filenames(void)
{
	if (ppanel_file->pd->cnt) {
		if (ppanel_file->selected)
			edit_macro(L" $S ");
		else
			msgout(MSG_i,"no selected files");
	}
	paste_exit();
}

void
cx_edit_paste_dir1(void)
{
	edit_macro(L" $1");
	paste_exit();
}

void
cx_edit_paste_dir2(void)
{
	edit_macro(L" $2");
	paste_exit();
}

void
cx_edit_flipcase(void)
{
	wchar_t ch;

	ch = USTR(textline->line)[textline->curs];
	if (ch == L'\0')
		return;
	if (iswlower(ch))
		ch = towupper(ch);
	else if (iswupper(ch))
		ch = towlower(ch);
	else {
		cx_edit_right();
		return;
	}
	USTR(textline->line)[textline->curs] = ch;
	edit_nu_right();
	edit_update();
}

void
edit_setprompt(TEXTLINE *pline, const wchar_t *prompt)
{
	wchar_t *p;
	int width;

	for (p = usw_copy(&pline->prompt,prompt), width = 0; *p != L'\0'; p++) {
		width += WCW(*p);
		if (width > MAX_PROMPT_WIDTH) {
			p -= 2;
			width = width - wc_cols(p,0,3) + 2;
			wcscpy(p,L"> ");
			msgout(MSG_NOTICE,"Long prompt string truncated: \"%ls\"",USTR(pline->prompt));
			break;
		}
	}
	pline->promptwidth = width;
}
