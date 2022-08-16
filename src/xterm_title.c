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

#include <sys/wait.h>	/* waitpid() */
#include <sys/stat.h>	/* open() */
#include <ctype.h>		/* isprint() */
#include <fcntl.h>		/* open() */
#include <signal.h>		/* sigaction() */
#include <stdarg.h>		/* va_list */
#include <stdio.h>		/* fputs() */
#include <stdlib.h>		/* getenv() */
#include <string.h>		/* strchr() */
#include <unistd.h>		/* fork() */
#include <wctype.h>		/* iswprint() */

#include "xterm_title.h"

#include "cfg.h"		/* cfg_num() */
#include "log.h"		/* msgout() */
#include "mbwstring.h"	/* convert2mb() */
#include "util.h"		/* estrdup() */

static FLAG enabled = 0;
static const char *old_title = 0, default_title[] = "terminal";

#define XPROP_TIMEOUT	4	/* timeout for the xprop command in seconds */
#define CMD_STR			64	/* buffer size for the executed command's name in the title */

void
xterm_title_initialize(void)
{
	xterm_title_reconfig();
	xterm_title_set(0,0,0);
}

/*
 * run the command: xprop -id $WINDOWID WM_NAME 2>/dev/null
 * to get the current xterm title
 */
static char *
get_title(void)
{
	int fd[2], efd;
	ssize_t rd;
	char *p1, *p2, title[192];
	const char *wid;
	pid_t pid;
	struct sigaction act;

	if ( (wid = getenv("WINDOWID")) == 0)
		return 0;

	if (pipe(fd) < 0 || (pid = fork()) < 0)
		return 0;

	if (pid == 0) {
		/* this is the child process */
		logfile_close();

		close(fd[0]);	/* close read end */
		if (fd[1] != STDOUT_FILENO) {
			if (dup2(fd[1], STDOUT_FILENO) != STDOUT_FILENO)
				_exit(126);
			close(fd[1]);
		}

		efd = open("/dev/null",O_RDONLY);
		if (efd != STDERR_FILENO) {
			if (dup2(efd, STDERR_FILENO) != STDERR_FILENO)
				_exit(126);
			close(efd);
		}

		act.sa_handler = SIG_DFL;
		act.sa_flags = 0;
		sigemptyset(&act.sa_mask);
		sigaction(SIGALRM,&act,0);
		alarm(XPROP_TIMEOUT);

		execlp("xprop", "xprop", "-id", wid, "WM_NAME", (char *)0);
		_exit(127);
	}

	/* parent continues here */
	/* do not return before waitpid() - otherwise you'll create a zombie */
	close(fd[1]);	/* close write end */
	rd = read_fd(fd[0],title,sizeof(title) - 1);
	close(fd[0]);

	if (waitpid(pid, 0, 0) < 0)
		return 0;

	if (rd == -1)
		return 0;

	title[rd] = '\0';
	/* get the window title in quotation marks */
	p1 = strchr(title,'\"');
	if (p1 == 0)
		return 0;
	p2 = strchr(++p1,'\"');
	if (p2 == 0)
		return 0;
	*p2 = '\0';

	return estrdup(p1);
}

void
xterm_title_reconfig(void)
{
	enabled = cfg_num(CFG_XTERM_TITLE);
	if (!enabled || (old_title != 0 && old_title != default_title))
		return;

	if (disp_data.noxterm || (!disp_data.xterm && !disp_data.xwin)) {
		msgout(MSG_NOTICE,"Disabling the terminal title change feature, because required support is missing.");
		enabled = 0;
		return;
	}

	if ((old_title = get_title()) == 0) {
		msgout(MSG_NOTICE,"Could not get the current terminal window title"
		  " because the command \"xprop -id $WINDOWID WM_NAME\" has failed."
		  " CLEX will not be able to restore the original title when it terminates");
		old_title = default_title;
	}
}

static void
set_xtitle(const char *str1, ...)
{
	va_list argptr;
	const char *strn;

	fputs("\033]0;",stdout);

	fputs(str1,stdout);
	va_start(argptr,str1);
	while ( (strn = va_arg(argptr, char *)) )
		if (*strn)
			fputs(strn,stdout);
	va_end(argptr);

	/* Xterm FAQ claims \007 is incorrect, but everybody uses it */
	fputs("\007",stdout);
	fflush(stdout);
}

void
xterm_title_set(int busy, const char *cmd, const wchar_t *cmdw)
{
	wchar_t wch, title_cmdw[CMD_STR];
	static USTRING local = UNULL;
	const char *title_cmd;
	FLAG islong, isnonprint;
	int i;

	if (!enabled)
		return;

	if (cmd == 0) {
		/* CLEX is idle */
		set_xtitle("clex: ", user_data.login,"@",user_data.host, (char *)0);
		return;
	}

	/* CLEX is executing (busy is true) or was executing (busy is false) 'cmd' */
	for (islong = isnonprint = 0, i = 0; (wch = cmdw[i]); i++) {
		if (i == CMD_STR - 1) {
			islong = 1;
			break;
		}
		if  (!iswprint(wch)) {
			isnonprint = 1;
			wch = L'?';
		}
		title_cmdw[i] = wch;
	}
	title_cmdw[i] = '\0';

	title_cmd = (islong || isnonprint) ? us_convert2mb(title_cmdw,&local) : cmd;
	set_xtitle(busy ? "" : "[", "clex: ", title_cmd, islong ? "..." : "",
	  busy ? (char *)0 : "]", (char *)0);
}

/* this is a cleanup function (see err_exit() in control.c) */
void
xterm_title_restore(void)
{
	if (enabled)
		set_xtitle(old_title,(char *)0);
}
