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

#include "panel.h"

#include "cfg.h"			/* cfg_num() */
#include "filter.h"			/* cx_filter() */
#include "inout.h"			/* win_panel_opt() */

void
pan_up_n(int n)
{
	if (panel->curs <= panel->min)
		return;

	panel->curs -= n;
	LIMIT_MIN(panel->curs,panel->min);
	LIMIT_MAX(panel->top,panel->curs);
	win_panel_opt();
}

void
cx_pan_up(void)
{
	pan_up_n(1);
}

void
pan_down_n(int n)
{
	if (panel->curs >= panel->cnt - 1)
		return;

	panel->curs += n;
	LIMIT_MAX(panel->curs,panel->cnt - 1);
	LIMIT_MIN(panel->top,panel->curs - disp_data.panlines + 1);
	win_panel_opt();
}

void
cx_pan_down(void)
{
	pan_down_n(1);
}

/* move to a screen line */
void
pan_line(int n)
{
	int newcurs;

	if (n < 0 || n >= disp_data.panlines)
		return;
	newcurs = panel->top + n;
	if (newcurs >= panel->cnt || newcurs == panel->curs)
		return;

	panel->curs = newcurs;
	win_panel_opt();
}

void
cx_pan_mouse(void)
{
	if (!MI_CLICK && !MI_WHEEL)
		return;

	switch (minp.area) {
	case AREA_PANEL:
		if (MI_CLICK)
			pan_line(minp.ypanel);
		else
			(MI_B(4) ? pan_up_n : pan_down_n)(cfg_num(CFG_MOUSE_SCROLL));
		break;
	case AREA_TOPFRAME:
		if (MI_CLICK)
			cx_pan_pgup();
		break;
	case AREA_BOTTOMFRAME:
		if (MI_CLICK)
			cx_pan_pgdown();
		break;
	}
}

void
cx_pan_home(void)
{
	panel->top = panel->curs = panel->min;
	win_panel_opt();
}

void
cx_pan_end(void)
{
	panel->curs = panel->cnt - 1;
	LIMIT_MIN(panel->top,panel->curs - disp_data.panlines + 1);
	win_panel_opt();
}

void
cx_pan_pgup(void)
{
	if (panel->curs > panel->min) {
		if (panel->curs != panel->top)
			panel->curs = panel->top;
		else {
			panel->curs -= disp_data.panlines;
			LIMIT_MIN(panel->curs,panel->min);
			panel->top = panel->curs;
		}
		win_panel_opt();
	}
}

void
cx_pan_pgdown(void)
{
	if (panel->curs < panel->cnt - 1) {
		if (panel->curs != panel->top + disp_data.panlines - 1)
			panel->curs = panel->top + disp_data.panlines - 1;
		else
			panel->curs += disp_data.panlines;
		LIMIT_MAX(panel->curs,panel->cnt - 1);
		LIMIT_MIN(panel->top,panel->curs - disp_data.panlines + 1);
		win_panel_opt();
	}
}

void
cx_pan_middle(void)
{
	panel->top = panel->curs - disp_data.panlines / 2;
	LIMIT_MAX(panel->top,panel->cnt - disp_data.panlines);
	LIMIT_MIN(panel->top,panel->min);
	win_panel_opt();
}

void
pan_adjust(PANEL_DESC *p)
{
	/* always in bounds */
	LIMIT_MAX(p->top,p->cnt - 1);
	LIMIT_MIN(p->top,p->min);
	LIMIT_MAX(p->curs,p->cnt - 1);
	LIMIT_MIN(p->curs,p->min);

	/* cursor must be visible */
	if (p->top > p->curs || p->top <= p->curs - disp_data.panlines)
		p->top = p->curs - disp_data.panlines / 3;
	/* bottom of the screen shouldn't be left blank ... */
	LIMIT_MAX(p->top,p->cnt - disp_data.panlines);
	/* ... but that is not always possible */
	LIMIT_MIN(p->top,p->min);
}
