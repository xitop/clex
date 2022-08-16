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

#include <stdarg.h>		/* log.h */
#include <stdio.h>		/* fputs() */
#include "curses.h"		/* NCURSES_MOUSE_VERSION */

#include "mouse.h"

#include "cfg.h"		/* cfg_num() */
#include "control.h"	/* control_loop() */
#include "log.h"		/* msgout() */

static FLAG enabled = 0;

void
mouse_initialize(void)
{
	mouse_reconfig();
	mouse_set();
}

void
mouse_reconfig(void)
{
	enabled = cfg_num(CFG_MOUSE) > 0;
	disp_data.mouse_swap = cfg_num(CFG_MOUSE) == 2;
	if (enabled && !disp_data.mouse) {
		msgout(MSG_NOTICE,"Cannot enable the mouse input (mouse interface not found)");
		enabled = 0;
	}
#if NCURSES_MOUSE_VERSION >= 2
	if (enabled){
		mousemask(ALL_MOUSE_EVENTS | REPORT_MOUSE_POSITION, (mmask_t *)0);
		mouseinterval(0); /* must implement double click by ourselves */
	}
	else
		mousemask(0, (mmask_t *)0);
#endif
}

void
mouse_set(void)
{
#if NCURSES_MOUSE_VERSION < 2
	if (enabled) {
		fputs("\033[?1001s" "\033[?1002h",stdout);
		fflush(stdout);
	}
#endif
}

/* this is a cleanup function (see err_exit() in control.c) */
void
mouse_restore(void)
{
#if NCURSES_MOUSE_VERSION < 2
	if (enabled) {
		fputs("\033[?1002l" "\033[?1001r",stdout);
		fflush(stdout);
	}
#endif
}

void
cx_common_mouse(void)
{
	if (MI_B(2)) {
		msgout(MSG_i,"press the shift if you want to paste or copy text with the mouse");
		return;
	}

	if (MI_AREA(BAR) && MI_DC(1)) {
		if (minp.cursor == 0)
			control_loop(MODE_HELP);
		else if (minp.cursor == 1)
			next_mode = MODE_SPECIAL_RETURN;
	}
}
