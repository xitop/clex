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
 * Help-file is a plain text file created from the HTML source
 * by a conversion utility which is available on request.
 * The help-file should not be edited by hand.
 *
 * Format of the help file:
 *    #comment (can appear anywhere in the file)
 *    $V=VERSION
 *    <page>
 *    <page>
 *    ....
 *    <page>
 *
 * where <page> is:
 *    $P=page_name
 *    $T=title
 *    <line>
 *    <line>
 *    ....
 *    <line>
 *
 * where <line> is either:
 *    literal_text
 * or text with one or more links (displayed as one line):
 *    text_appearing_before_the_first_link
 *    <link> (repeated 1 or more times)
 *
 *    each <link> consists of three lines:
 *        $L=page_name
 *        text_of_the_link (displayed highlighted)
 *        text_appearing_after_the_link
 */

#include "clexheaders.h"

#include <stdarg.h>		/* va_list */
#include <stdio.h>		/* vprintf() */
#include <stdlib.h>		/* free() */
#include <string.h>		/* strcmp() */

#include "help.h"

#include "cfg.h"		/* cfg_str() */
#include "control.h"	/* get_previous_mode() */
#include "inout.h"		/* win_panel() */
#include "log.h"		/* msgout() */
#include "mbwstring.h"	/* convert2mb() */
#include "panel.h"		/* pan_adjust() */
#include "util.h"		/* emalloc() */

/* limits to protect resources */
#define HELP_PAGES_LIMIT	80			/* pages max */
#define HELP_FILESIZE_LIMIT	125000		/* bytes max */

/* error handling in the parse function */
static FLAG helperror;			/* there was an error */

/* source help data: internal or external */
static const char *internal_help[] = {
#include "help.inc"
	""
};
/* NOTE: support for external help file will be added later */

/* processed help data */
static HELP_LINE *helpline = 0;

/* help data pages */
typedef struct {
	FLAG valid;
	const char *name;			/* page name (for links) */
	const wchar_t *title;		/* title to be displayed */
	int firstline;				/* number the first line */
	int size;					/* total number of lines in this page */
} HELP_PAGE;
static HELP_PAGE *helppage = 0;	/* help lines split into pages */
static int pagecnt;				/* number of pages in 'helppage[]' */

/* line numbers are helpline[] indices */
/* page numbers are helppage[] indices */

/* the "MAIN" page contains generated list of links */
static int mainpage;			/* "MAIN" page number */
static int mainsize;			/* size of the page excluding the generated links */
static int mainlink[MAIN_LINKS];/* line numbers of links */

/* help history - allows only to go back to previous page */
#define HELP_HISTORY		16
static struct {
	int pagenum, top, curs;
} history[HELP_HISTORY];
static int head, tail;			/* circular buffer indices */

/* used by the help text parser */
#define HL_TEXT		 0		/* regular text */
#define HL_TEXTLINK	 1		/* text of a link (highlighted) */
#define HL_LINK		 2		/* data of a link */
#define HL_TITLE	 3		/* page title */
#define HL_PAGE		10 		/* start of a new page */
#define HL_VERSION	11		/* help data version */
#define HL_IGNORE	20		/* invalid input, ignored */
#define HL_END		99		/* last entry in use */

#define IS_HL_CONTENTS(X)	((X) < 10)	/* this item is part of a help page */

static int
page2num(const char *pagename)
{
	int i;

	for (i = 0; i < pagecnt; i++)
		if (helppage[i].valid && strcmp(helppage[i].name,pagename) == 0)
			return i;
	return -1;		/* no such page */
}

static void
help_error(const char *format, ...)
{
	va_list argptr;

	helperror = 1;

	va_start(argptr,format);
	vmsgout(MSG_NOTICE,format,argptr);
	va_end(argptr);
}

static const char *
help_getline(int n)
{
	/* if (external) return fr_line(tfd,n); */
	return internal_help[n];
}

static int
help_linecnt(void)
{
	/* if (external) return fr_linecnt(tfd); */
	return ARRAY_SIZE(internal_help);
}

/* parse_help() returns -1 on a serious error (help unusable) */
static int
parse_help(void)
{
	FLAG pagestart;
	const char *ptr;
	int pg, page, i, j, ml, cnt, max;
	char ch;

	cnt = help_linecnt();
	if (helpline) {
		for (i = 0; helpline[i].type != HL_END; i++)
			efree((void *)helpline[i].text);	/* overriding const qualifier */
		free(helpline);
	}
	helpline = emalloc((cnt + 1) * sizeof(HELP_LINE));

	/* first pass: read and analyze the input lines, count pages */
	pagecnt = 0;
	for (i = 0; i < cnt; i++) {
		ptr = help_getline(i);
		if (ptr[0] == '$' && (ch = ptr[1]) != '\0' && ptr[2] == '=') {
			ptr += 3;
			helpline[i].data = ptr;
			helpline[i].text = 0;
			switch (ch) {
			case 'L':
				helpline[i].type = HL_LINK;
				break;
			case 'P':
				if (++pagecnt > HELP_PAGES_LIMIT) {
					helpline[i].type = HL_END;
					help_error("Too many help pages, allowed is ",STR(HELP_PAGES_LIMIT));
					return -1;
				}
				helpline[i].type = HL_PAGE;
				break;
			case 'T':
				helpline[i].type = HL_TITLE;
				helpline[i].text = ewcsdup(convert2w(ptr));
				break;
			case 'V':
				helpline[i].type = HL_VERSION;
			default:
				helpline[i].type = HL_IGNORE;
				help_error("Invalid control sequence $%c=",ch);
			}
		}
		else {
			helpline[i].type = HL_TEXT;
			helpline[i].data = 0;
			helpline[i].text = ewcsdup(convert2w(ptr));
		}
	}
	helpline[i].type = HL_END;
	helpline[i].data = 0;
	helpline[i].text = 0;

	efree(helppage);
	helppage = emalloc(pagecnt * sizeof(HELP_PAGE));

	/* second pass: break help to pages, some checks */
	for (pagestart = 0, page = -1 /* current page */, i = 0; i < cnt; i++) {
		if (page < 0 && IS_HL_CONTENTS(helpline[i].type)) {
			help_error("Unexpected text before the start of the first page");
			helpline[i].type = HL_IGNORE;
			continue;
		}
		switch (helpline[i].type) {
		case HL_TEXT:
			if (pagestart) {
				helppage[page].firstline = i;
				pagestart = 0;
			}
			helppage[page].size++;
			break;
		case HL_LINK:
			if (pagestart || helpline[i - 1].type != HL_TEXT
			  || helpline[i + 1].type != HL_TEXT || helpline[i + 2].type != HL_TEXT) {
				help_error("Link \"%s\" is not correctly embedded in text",helpline[i].data);
				helpline[i].type = HL_IGNORE;
			}
			else {
				helpline[i + 1].type = HL_TEXTLINK;
				helppage[page].size--;
			}
			break;
		case HL_PAGE:
			for (pg = 0; pg <= page; pg++)
				if (helppage[pg].valid && strcmp(helppage[pg].name,helpline[i].data) == 0) {
					help_error("Existing page \"%s\" is redefined",helpline[i].data);
					helppage[pg].valid = 0;
					break;
				}
			if (pagestart)
				help_error("Page \"%s\" is empty",helppage[page].name);

			pagestart = 1;
			page++;
			helppage[page].name = helpline[i].data;
			helppage[page].title = L"Untitled";
			helppage[page].size = 0;
			helppage[page].valid = 1;
			break;
		case HL_TITLE:
			helppage[page].title = helpline[i].text;
			break;
		case HL_VERSION:
			if (strcmp(helpline[i].data,VERSION))
				help_error("Help file version \"%s\" does not match the program version.\n"
				  "    Information in the on-line help might be inaccurate or outdated.",
				  helpline[i].data);
			break;
		}
	}

	/* final pass: check for broken links, count links per line */
	for (i = 0; i < cnt; i++) {
		if (helpline[i].type == HL_LINK && page2num(helpline[i].data) < 0)
			help_error("Broken link: %s",helpline[i].data);

		if (helpline[i].type == HL_TEXT) {
			for (j = 0; helpline[i + 3 * j + 1].type == HL_LINK ;j++)
				;
			helpline[i].links = j;
		}
	}

	/* the MAIN page */
	if ( (mainpage = page2num("MAIN")) < 0) {
		help_error("Required page \"MAIN\" is missing");
		return -1;
	}
	for (i = 0, j = helppage[mainpage].firstline, ml = 0;
	  i < helppage[mainpage].size && ml < MAIN_LINKS; i++, j++) {
		while (helpline[j].type != HL_TEXT || helpline[j - 1].type == HL_TEXTLINK)
			j++;
		if (helpline[j].links == 1 && strcmp(helpline[j + 1].data,"MAIN") == 0)
			mainlink[ml++] = j;
	}
	if (ml != MAIN_LINKS) {
		help_error("The \"MAIN\" page is invalid");
		return -1;
	}
	mainsize = helppage[mainpage].size - MAIN_LINKS;

	for (max = helppage[0].size, pg = 1; pg < pagecnt; pg++)
		if (helppage[pg].size > max)
			max = helppage[pg].size;
	efree(panel_help.line);
	panel_help.line = emalloc(max * sizeof(HELP_LINE));

	return 0;
}

void
help_initialize(void)
{
	helperror = 0;
	msgout(MSG_HEADING,"HELP: reading the help data");
	parse_help();
	msgout(MSG_HEADING,0);
	if (helperror) {
		msgout(MSG_w,"Error(s) in the help data detected, details in the log");
		err_exit("BUG: built-in help is incorrect");
	}
}

static void
set_page(int pg)
{
	int i, j;
	FLAG curs_set = 0;

	panel_help.pd->top = panel_help.pd->curs = panel_help.pd->min;
	panel_help.pd->cnt = helppage[pg].size;
	panel_help.pagenum = pg;
	for (i = 0, j = helppage[pg].firstline; i < panel_help.pd->cnt; i++) {
		while (helpline[j].type != HL_TEXT || helpline[j - 1].type == HL_TEXTLINK)
			j++;
		panel_help.line[i] = helpline + j++;
		/* place the cursor one line above the first visible link */
		if (!curs_set && i < disp_data.panlines && helpline[j].type == HL_LINK) {
			if (i > 0)
				panel_help.pd->curs = i - 1;
			curs_set = 1;
		}
	}
	panel_help.lnk_act = panel_help.lnk_ln = 0;

	panel_help.title = helppage[pg].title;
	win_title();
}

static void
help_goto(const char *pagename)
{
	int pg;

	if ( (pg = page2num(pagename)) < 0) {
		/* ERROR 404 :-) */
		msgout(MSG_w,"HELP: help-page '%s' not found",pagename);
		return;
	}

	/* save current page parameters */
	history[head].pagenum = panel_help.pagenum;
	history[head].top = panel_help.pd->top;
	history[head].curs = panel_help.pd->curs;
	head = (head + 1) % HELP_HISTORY;
	if (head == tail)
		tail = (tail + 1) % HELP_HISTORY;

	set_page(pg);
	win_panel();
}

static int
link_add(int ln, const char *page)
{
	int pg;

	if (page == 0) {
		helpline[mainlink[ln] + 0].type = HL_IGNORE;
		helpline[mainlink[ln] + 1].type = HL_IGNORE;
		helpline[mainlink[ln] + 2].type = HL_IGNORE;
		helpline[mainlink[ln] + 3].type = HL_IGNORE;
		return 0;
	}

	if ((pg = page2num(page)) < 0) {
		msgout(MSG_NOTICE,"Missing help page \"%s\"",page);
		return -1;
	}
	helpline[mainlink[ln] + 0].type = HL_TEXT;
	helpline[mainlink[ln] + 1].type = HL_LINK;
	helpline[mainlink[ln] + 1].data = page;
	helpline[mainlink[ln] + 2].type = HL_TEXTLINK;
	helpline[mainlink[ln] + 2].text = helppage[pg].title;
	helpline[mainlink[ln] + 3].type = HL_TEXT;
	return 0;
}

int
help_prepare(void)
{
	int i, link;
	const char **pages;

	/* context sensitive help */
	link = 0;
	if (panel->filtering && link_add(link,"filter") >= 0)
		link++;
	pages = mode2help(get_previous_mode());
	for (i = 0; i < MAIN_LINKS - 1 && pages[i] != 0; i++)
		if (link_add(link,pages[i]) >= 0)
			link++;
	helppage[mainpage].size = mainsize + link;
	for (; link < MAIN_LINKS; link++)
		link_add(link,0);
	set_page(mainpage);

	head = tail = 0;
	panel_help.pd->filtering = 0;
	panel = panel_help.pd;
	textline = 0;

	return 0;
}

/* follow link */
void
cx_help_link(void)
{
	HELP_LINE *ph;
	int active;

	ph = panel_help.line[panel_help.pd->curs];
	if (ph->links == 0)
		return;
	active = panel_help.pd->curs == panel_help.lnk_ln ? panel_help.lnk_act : 0;
	help_goto(ph[3 * active + 1].data);
}

/* display main page */
void
cx_help_main(void)
{
	if (panel_help.pagenum != mainpage)
		help_goto("MAIN");
}

/* go back to previous page */
void
cx_help_back(void)
{
	if (head == tail) {
		msgout(MSG_i,"there is no previous help-page");
		return;
	}

	head = (head + HELP_HISTORY - 1) % HELP_HISTORY;
	set_page(history[head].pagenum);
	panel_help.pd->top = history[head].top;
	panel_help.pd->curs = history[head].curs;
	pan_adjust(panel_help.pd);
	win_panel();
}

void
cx_help_up(void)
{
	if (panel_help.lnk_ln == panel->curs && panel_help.lnk_act > 0)
		panel_help.lnk_act--;
	else if (panel->curs <= panel->min)
		return;
	else {
		panel->curs--;
		LIMIT_MAX(panel->top,panel->curs);
		panel_help.lnk_ln = panel->curs;
		panel_help.lnk_act = panel_help.line[panel->curs]->links - 1;
	}
	win_panel_opt();
}

void
cx_help_down(void)
{
	if (panel_help.lnk_ln == panel->curs
	  && panel_help.lnk_act + 1 < panel_help.line[panel->curs]->links)
		panel_help.lnk_act++;
	else if (panel_help.lnk_ln != panel->curs && panel_help.line[panel->curs]->links > 1) {
		panel_help.lnk_ln = panel->curs;
		panel_help.lnk_act = 1;
	}
	else if (panel->curs >= panel->cnt - 1)
		return;
	else {
		panel->curs++;
		LIMIT_MIN(panel->top,panel->curs - disp_data.panlines + 1);
		panel_help.lnk_ln = panel->curs;
		panel_help.lnk_act = 0;
	}
	win_panel_opt();
}

void
cx_help_mouse(void)
{
	switch (minp.area) {
	case AREA_PANEL:
		if (MI_CLICK && VALID_CURSOR(panel) && minp.cursor >= 0) {
			panel_help.lnk_ln = panel->curs;
			panel_help.lnk_act = minp.cursor;
			win_panel_opt();
		}
		break;
	case AREA_BAR:
		if (MI_DC(1)) {
			if (minp.cursor == 0) {
				cx_help_main();
				minp.area = AREA_NONE;
			}
			else if (minp.cursor == 2) {
				cx_help_back();
				minp.area = AREA_NONE;
			}
		}
	}
}
