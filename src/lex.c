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

#include "lex.h"

#include "edit.h"				/* edit_isspecial() */

/*
 * perform a lexical analysis of the input line 'cmd' mainly for the purpose
 * of name completion. Bear in mind that it is only a guess how an external shell
 * will parse the command.
 */
const char *
cmd2lex(const wchar_t *cmd)
{
	static USTRING buffer = UNULL;
	wchar_t ch;
	char *lex;
	size_t len;
	int i;
	FLAG sq = 0, dq = 0, bt = 0;	/* 'single quote', "double quote", `backticks` */

	len = wcslen(cmd);
	us_setsize(&buffer,len + 2);
	lex = USTR(buffer) + 1;
	lex[-1] = LEX_BEGIN;
	lex[len] = LEX_END_OK;

	for (i = 0; i < len; i++) {
		ch = cmd[i];
		if (sq) {
			if (ch == L'\'') {
				lex[i] = LEX_QMARK;
				sq = 0;
			}
			else
				lex[i] = LEX_PLAINTEXT;
		}
		else if (dq) {
			if (ch == L'\\' && (cmd[i + 1] == L'\"' || cmd[i + 1] == L'$')) {
				lex[i++] = LEX_QMARK;
				lex[i]   = LEX_PLAINTEXT;
			}
			else if (ch == L'\"') {
				lex[i] = LEX_QMARK;
				dq = 0;
			}
			else if (ch == L'$') {
				lex[i] = LEX_VAR;
				if (cmd[i + 1] == L'{')
					lex[++i] = LEX_VAR;
			}
			else
				lex[i] = LEX_PLAINTEXT;
		}
		else
			switch (ch) {
			case L'\\':
				lex[i++] = LEX_QMARK;
				lex[i] = i == len ? LEX_END_ERR_BQ : LEX_PLAINTEXT;
				break;
			case L'\'':
				lex[i] = LEX_QMARK;
				sq = 1;
				break;
			case L'\"':
				lex[i] = LEX_QMARK;
				dq = 1;
				break;
			case L'~':
				/* only special in a ~user construct */
				lex[i] = LEX_PLAINTEXT;
				break;
			case L' ':
			case L'\t':
				lex[i] = LEX_SPACE;
				break;
			case L'$':
				lex[i] = LEX_VAR;
				if (cmd[i + 1] == L'{')
					lex[++i] = LEX_VAR;
				break;
			case L'>':
			case L'<':
				lex[i] = LEX_IO;
				break;
			case L'&':
			case L'|':
			case L';':
			case L'(':
				lex[i] = LEX_CMDSEP;
				break;
			case L'`':
				lex[i] = TOGGLE(bt) ? LEX_CMDSEP /* opening */
				  : LEX_OTHER /* closing */;
				break;
			default:
				lex[i] = edit_isspecial(ch) ? LEX_OTHER : LEX_PLAINTEXT;
		}
	}

	/* open quote is an error */
	if (sq)
		lex[len] = LEX_END_ERR_SQ;
	else if (dq)
		lex[len] = LEX_END_ERR_DQ;
	return lex;
}

/* is it a pattern containing wildcards ? */
int
ispattern(const wchar_t *cmd)
{
	wchar_t ch;
	const wchar_t *list = 0;	/* [ ..list.. ] */
	FLAG sq = 0, dq = 0, bq = 0;
	/* 'single quote', "double quote", \backslash */

	while ((ch = *cmd++) != L'\0') {
		if (ch == L']' && list && !bq
		  /* check for empty (invalid) lists: [] [!] and [^] */
		  && (cmd - list > 2 || (cmd - list == 2 && *list != L'!' && *list != L'^')))
			/* ignoring sq and dq here because there is only backslash quoting
			 * inside the brackets [ ... ] */
			return 1;
		if (sq) {
			if (ch == L'\'')
				sq = 0;
		}
		else if (dq) {
			if (bq)
				bq = 0;
			else if (ch == L'\\')
				bq = 1;
			else if (ch == L'\"')
				dq = 0;
		}
		else if (bq)
			bq = 0;
		else
			switch (ch) {
			case L'\\':
				bq = 1;
				break;
			case L'\'':
				sq = 1;
				break;
			case L'\"':
				dq = 1;
				break;
			case L'[':
				if (list == 0)
					list = cmd;	/* --> after the '[' */
				break;
			case L'?':
			case L'*':
				return 1;
		}
	}

	return 0;
}

/* is it quoted ? */
int
isquoted(const wchar_t *cmd)
{
	wchar_t ch;

	while ((ch = *cmd++) != L'\0')
		if (ch == L'\\' || ch == L'\'' || ch == L'\"')
			return 1;
	return 0;
}

/*
 * dequote quoted text 'src', max. 'len' characters
 * return value: the output string length
 */
int
usw_dequote(USTRINGW *pustr, const wchar_t *src, size_t len)
{
	wchar_t ch, *dst;
	size_t i, j;
	FLAG bq = 0, sq = 0, dq = 0;

	/* dequoted text cannot be longer than the quoted original */
	usw_setsize(pustr,len + 1);
	dst = PUSTR(pustr);

	for (i = j = 0; i < len; i++) {
		ch = src[i];
		if (sq) {
			if (ch == L'\'')
				sq = 0;
			else
				dst[j++] = ch;
		}
		else if (dq) {
			if (TCLR(bq)) {
				if (ch != L'\"' && ch != L'\'' && ch != L'$' && ch != L'\n')
					dst[j++] = L'\\';
				dst[j++] = ch;
			}
			else if (ch == L'\\')
				bq = 1;
			else if (ch == L'\"')
				dq = 0;
			else
				dst[j++] = ch;
		}
		else if (TCLR(bq))
			dst[j++] = ch;
		else if (ch == L'\\')
			bq = 1;
		else if (ch == L'\'')
			sq = 1;
		else if (ch == L'\"')
			dq = 1;
		else
			dst[j++] = ch;
	}
	dst[j] = L'\0';

	return j;
}
