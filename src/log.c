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

#include <errno.h>			/* errno */
#include <stdarg.h>			/* va_list */
#include <stdio.h>			/* fprintf() */
#include <string.h>			/* strerror() */
#include <time.h>			/* strftime() */

#include "log.h"

#include "control.h"		/* err_exit() */
#include "inout.h"			/* win_sethelp() */
#include "match.h"			/* match_substr() */
#include "mbwstring.h"		/* usw_convert2w() */
#include "panel.h"			/* panel_adjust() */
#include "ustringutil.h"	/* us_vprintf() */

static FILE *logfp = 0;
static LOG_ENTRY logbook[LOG_LINES];	/* circular buffer */
static int base = 0, cnt = 0;

static int
find_nl(const char *str)
{
	const char *nl;

	nl = strchr(str,'\n');
	return nl ? nl - str : -1;
}

static const char *
strip_nl(const char *str)
{
	static USTRING nonl = UNULL;
	char *p;
	int nl;

	if ((nl = find_nl(str)) < 0)
		return str;

	p = us_copy(&nonl,str);
	do {
		p += nl;
		*p++ = ' ';
		nl = find_nl(p);
	} while (nl >= 0);
	return USTR(nonl);
}

static void
append_record(const char *timestamp, const char *levelstr, const char *msg)
{
	fprintf(logfp,"%s %-15s %s\n",timestamp,levelstr,msg);
	fflush(logfp);
}

void
logfile_open(const char *logfile)
{
	int i;
	LOG_ENTRY *plog;

	if ( (logfp = fopen(logfile,"a")) == 0)
		msgout(MSG_W,"Could not open the logfile \"%s\" (%s)",logfile,strerror(errno));
	else {
		/* write records collected so far */
		for (i = 0; i < cnt; i++) {
			plog = logbook + (base + i) % LOG_LINES;
			append_record(plog->timestamp,plog->levelstr,convert2mb(USTR(plog->msg)));
		}
		msgout(MSG_DEBUG,"Logfile: \"%s\"",logfile);
	}
}

/* this is a cleanup function (see err_exit() in control.c) */
void
logfile_close(void)
{
	if (logfp) {
		fclose(logfp);
		logfp = 0;
	}
}

static void
store_timestamp(char *dst)
{
	static FLAG format_ok = 1;
	time_t now;

	now = time(0);
	if (format_ok && strftime(dst,TIMESTAMP_STR,"%c",localtime(&now)) == 0) {
		format_ok = 0;
		msgout(MSG_NOTICE,"LOG: Using YYYY-MM-DD HH:MM:SS date/time format "
		  "because the default format (defined by locale) is too long");
	}
	if (!format_ok)
		strftime(dst,TIMESTAMP_STR,"%Y-%m-%d %H:%M:%S",localtime(&now));
}

static void
log_record(int level, const char *logmsg)
{
	static struct {
		const char *str;	/* should not exceed 15 chars */
		FLAG panel;			/* insert into panel + append to the logfile */
		FLAG screen;		/* display on the screen */
		FLAG iswarning;		/* is a warning */
	} levdef [_MSG_TOTAL_] = {
		{ /* heading */ 0,0,0,0 },
		{ "DEBUG",		1, 0, 0 },
		{ "NOTICE",		1, 0, 1 },
		{ "AUDIT",		1, 0, 0 },
		{ "INFO",		1, 1, 0 },
		{ "INFO",		0, 1, 0 },
		{ "WARNING",	1, 1, 1 },
		{ "WARNING",	0, 1, 1 }
	};
	static USTRING heading_buff = UNULL;
	static int notify_hint = 2;	/* display the hint twice */
	USTRINGW wmsg_buff;
	const wchar_t *wmsg;
	const char *msg, *heading;
	LOG_ENTRY *plog;
	FLAG notify = 0;
	int i;

	/* modifiers */
	if ((level & MSG_NOTIFY) && notify_hint)
		notify = 1;
	level &= MSG_MASK;

	if (level < 0 || level >= _MSG_TOTAL_)
		err_exit("BUG: invalid message priority level %d",level);

	if (level == MSG_HEADING) {
		/* if 'msg' is a null ptr, the stored heading is invalidated */
		us_copy(&heading_buff,logmsg);
		return;
	}
	if (levdef[level].iswarning && (heading = USTR(heading_buff)) && logmsg != heading) {
		/* emit the heading string first */
		log_record(level,heading);
		us_reset(&heading_buff);
	}

	msg = strip_nl(logmsg);
	US_INIT(wmsg_buff);
	wmsg = usw_convert2w(msg,&wmsg_buff);

	/* append it to the log panel and log file */
	if (levdef[level].panel) {
		if (cnt < LOG_LINES)
			/* adding new record */
			plog = logbook + (base + cnt++) % LOG_LINES;
		else {
			/* replacing the oldest one */
			plog = logbook + base;
			base = (base + 1) % LOG_LINES;
			if (plog->cols == panel_log.maxcols)
				/* replacing the longest message -> must recalculate the max */
				panel_log.maxcols = 0;
		}

		plog->level = level;
		plog->levelstr = levdef[level].str;
		usw_copy(&plog->msg,wmsg);
		store_timestamp(plog->timestamp);
		plog->cols = wc_cols(wmsg,0,-1);

		/* max length */
		if (cnt < LOG_LINES || panel_log.maxcols > 0) {
			if (plog->cols > panel_log.maxcols)
				panel_log.maxcols = plog->cols;
		}
		else
			for (i = 0; i < LOG_LINES; i++)
				if (logbook[i].cols > panel_log.maxcols)
					panel_log.maxcols = logbook[i].cols;

		/* live view */
		if (get_current_mode() == MODE_LOG) {
			log_panel_data();
			panel->curs = panel->cnt - 1;
			pan_adjust(panel);
			win_panel();
		}

		if (logfp)
			append_record(plog->timestamp,plog->levelstr,msg);
	}

	/* display it on the screen */
	if (levdef[level].screen) {
		if (disp_data.curses)
			win_sethelp(levdef[level].iswarning ? HELPMSG_WARNING : HELPMSG_INFO,wmsg);
		else {
			puts(logmsg);	/* original message possibly with newlines */
			fflush(stdout);
			disp_data.wait = 1;
		}
		if (notify) {
			notify_hint--;	/* display this hint only N times */
			win_sethelp(HELPMSG_TMP,L"alt-N = notification panel");
		}
	}
}

void
vmsgout(int level, const char *format, va_list argptr)
{
	static USTRING buff = UNULL;

	us_vprintf(&buff,format,argptr);
	log_record(level,USTR(buff));
}

void
msgout(int level, const char *format, ...)
{
	va_list argptr;

	if (format && strchr(format,'%')) {
		va_start(argptr,format);
		vmsgout(level,format,argptr);
		va_end(argptr);
	}
	else
		log_record(level,format);
}

void
log_panel_data(void)
{
	int i, j;
	LOG_ENTRY *plog, *curs;

	curs = VALID_CURSOR(panel_log.pd) ? panel_log.line[panel_log.pd->curs] : 0;
	if (panel_log.pd->filtering)
		match_substr_set(panel_log.pd->filter->line);

	for (i = j = 0; i < cnt; i++) {
		plog = logbook + (base + i) % LOG_LINES;
		if (plog == curs)
			panel_log.pd->curs = j;
		if (panel_log.pd->filtering && !match_substr(USTR(plog->msg)))
			continue;
		panel_log.line[j++] = plog;
	}
	panel_log.pd->cnt = j;
}

int
log_prepare(void)
{
	panel_log.pd->filtering = 0;
	panel_log.pd->curs = -1;
	log_panel_data();
	panel_log.pd->top = panel_user.pd->min;
	panel_log.pd->curs = panel_log.pd->cnt - 1;

	panel = panel_log.pd;
	textline = 0;
	return 0;
}

#define SCROLL_UNIT	12

void
cx_log_right(void)
{
	if (panel_log.scroll < panel_log.maxcols - disp_data.scrcols / 2) {
		panel_log.scroll += SCROLL_UNIT;
		win_panel();
	}
}

void
cx_log_left(void)
{
	if (panel_log.scroll >= SCROLL_UNIT) {
		panel_log.scroll -= SCROLL_UNIT;
		win_panel();
	}
}

void
cx_log_mark(void)
{
	msgout(MSG_DEBUG,"-- mark --");
}

void
cx_log_home(void)
{
	panel_log.scroll = 0;
	win_panel();
}
