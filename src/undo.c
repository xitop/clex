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

#include "undo.h"

#include "edit.h"			/* edit_update() */
#include "log.h"			/* msgout() */

static TEXTLINE *current;
static USTRINGW undo_line = UNULL;
static int undo_size, undo_curs, undo_offset;
static FLAG disable = 1;
		/* disable undo mechanism while performing undo operations */
static EDIT_OP this_op;	/* description of the current operation */

/* clear the undo history */
void
undo_reset(void)
{
	if (textline == 0)
		return;

	textline->last_op.code = OP_NONE;
	textline->undo_levels = textline->redo_levels = 0;
	disable = 1;
}

/*
 * check if 'longstr' is the same as 'shortstr' would be
 * with 'len' chars inserted at position 'pos'
 */
static int
cmp_strings(const wchar_t *shortstr, const wchar_t *longstr, int pos, int len)
{
	return pos >= 0 && len >=0
	  && (pos == 0 || wcsncmp(shortstr,longstr,pos) == 0)
	  && wcscmp(shortstr + pos,longstr + pos + len) == 0;
}

/* which edit operation was this one ? */
static void
tell_edit_op(void)
{
	int pos, diff;
	const wchar_t *before, *after;

	before = USTR(undo_line);
	after = USTR(textline->line);
	diff = textline->size - undo_size;
	if (diff > 0) {
		if (cmp_strings(before,after,pos = textline->curs - diff,diff)) {
			this_op.code = OP_INS;
			this_op.pos = pos;
			this_op.len = diff;
			return;
		}
	}
	else if (diff == 0) {
		if (wcscmp(before,after) == 0) {
			this_op.code = OP_NONE;
			return;
		}
	}
	else {
		if (cmp_strings(after,before,pos = textline->curs,diff = -diff)) {
			this_op.code = OP_DEL;
			this_op.pos = pos;
			this_op.len = diff;
			return;
		}
	}

	this_op.code = OP_CHANGE;
	this_op.pos = this_op.len = 0;
}

/* make a copy of 'textline' before an edit operation ... */
void
undo_before(void)
{
	if (textline == 0)
		return;

	disable = 0;
	current = textline;
	usw_copy(&undo_line,USTR(textline->line));
	undo_size   = textline->size;
	undo_curs   = textline->curs;
	undo_offset = textline->offset;
}

/* ... and now see what happened with it after the operation */
#define MERGE_MAX	30
void
undo_after(void)
{
	int idx, total, delta;

	if (TSET(disable) || textline == 0 || textline != current)
		return;

	tell_edit_op();
	if (this_op.code == OP_NONE)
		return;

	textline->redo_levels = 0;

	/*
	 * two operations of the same type at the same position
	 * can be merged into a single operation
	 */
	total = this_op.len + textline->last_op.len;
	delta = this_op.pos - textline->last_op.pos;
	if ((
		this_op.code == OP_INS && textline->last_op.code == OP_INS
		&& delta == textline->last_op.len && total < MERGE_MAX
	  ) || (
	  	this_op.code == OP_DEL && textline->last_op.code == OP_DEL
		&& (delta == 0 || (delta == -1 && this_op.len == 1)) && total < MERGE_MAX
	  )) {
		/* merge */
		if (this_op.code == OP_DEL)
			textline->last_op.pos = this_op.pos;
		textline->last_op.len = total;
		return;
	}

	textline->last_op = this_op;	/* struct copy */

	idx = (textline->undo_base + textline->undo_levels) % UNDO_LEVELS;
	if (textline->undo_levels < UNDO_LEVELS)
		textline->undo_levels++;
	else
		textline->undo_base = (textline->undo_base + 1) % UNDO_LEVELS;

	usw_xchg(&textline->undo[idx].save_line,&undo_line);
	textline->undo[idx].save_size   = undo_size;
	textline->undo[idx].save_curs   = undo_curs;
	textline->undo[idx].save_offset = undo_offset;
}

/* op: nonzero = undo, 0 = redo */
static void
undo_redo(int op)
{
	int idx;

	if (textline == 0)
		return;
	if (op) {
		if (textline->undo_levels == 0) {
			msgout(MSG_i,"undo not possible");
			return;
		}
		idx = --textline->undo_levels;
		textline->redo_levels++;
	}
	else {
		if (textline->redo_levels == 0) {
			msgout(MSG_i,"redo not possible");
			return;
		}
		idx = textline->undo_levels++;
		textline->redo_levels--;
	}
	idx = (textline->undo_base + idx) % UNDO_LEVELS;

	usw_xchg(&textline->line,&textline->undo[idx].save_line);
	textline->size   = textline->undo[idx].save_size;
	textline->curs   = textline->undo[idx].save_curs;
	textline->offset = textline->undo[idx].save_offset;
	textline->undo[idx].save_size   = undo_size;
	textline->undo[idx].save_curs   = undo_curs;
	textline->undo[idx].save_offset = undo_offset;

	edit_update();
	textline->last_op.code = OP_CHANGE;
	disable = 1;
}

void
cx_undo(void)
{
	undo_redo(1);
}

void
cx_redo(void)
{
	undo_redo(0);
}
