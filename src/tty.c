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

#include <ctype.h>		/* tolower() */
#include <errno.h>		/* errno */
#include <fcntl.h>		/* fcntl */
#include <stdio.h>		/* fputs() */
#include <signal.h>		/* SIGTTIN */
#include <termios.h>	/* struct termios */
#include <unistd.h>		/* STDIN_FILENO */

#include "tty.h"

#include "control.h"	/* err_exit() */

static struct termios *p_raw, *p_text = 0, *p_save = 0;

#ifdef _POSIX_JOB_CONTROL
static pid_t save_pgid = 0;
#endif

void
jc_initialize(void)
{
#ifdef _POSIX_JOB_CONTROL
	struct sigaction act;

	/* Wait until we are in the foreground */
	while (tcgetpgrp(STDIN_FILENO) != (save_pgid = getpgrp()))
		kill(-save_pgid,SIGTTIN);

	/* ignore job control signals */
	act.sa_handler = SIG_IGN;
	act.sa_flags = 0;
	sigemptyset(&act.sa_mask);
	sigaction(SIGTSTP,&act,0);
	sigaction(SIGTTIN,&act,0);
	sigaction(SIGTTOU,&act,0);

	/* put CLEX into its own process group */
	setpgid(clex_data.pid,clex_data.pid);
	/* make it the foreground process group */
	tcsetpgrp(STDIN_FILENO,clex_data.pid);
#endif
}

/* this is a cleanup function (see err_exit() in control.c) */
void
jc_reset(void)
{
#ifdef _POSIX_JOB_CONTROL
	if (save_pgid)
		tcsetpgrp(STDIN_FILENO,save_pgid);
#endif
}

void
tty_initialize(void)
{
	static struct termios text, raw;

	if (!isatty(STDIN_FILENO))
		err_exit("This is an interactive program, but the standard input is not a terminal");

	if (tcgetattr(STDIN_FILENO,&text) < 0)
		err_exit("Cannot read the terminal parameters");

	raw = text;		/* struct copy */
	raw.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN);
	raw.c_cc[VMIN] = 1;
	raw.c_cc[VTIME] = 0;
	p_text = &text;
	p_raw  = &raw;
}

void
tty_save()
{
	static struct termios save;

	/* errors are silently ignored */
	p_save = tcgetattr(STDIN_FILENO,&save) == 0 ? &save : 0;
}

void
tty_restore(void)
{
	if (p_save)
		tcsetattr(STDIN_FILENO,TCSAFLUSH,p_save);
}

/*
 * make sure interrupt key is ctrl-C
 * usage: tty_save(); tty_ctrlc();
 *          install-SIGINT-handler
 *            do-stuff
 *          disable-SIGINT
 *        tty_restore();
 */
void
tty_ctrlc()
{
	struct termios ctrlc;

	if (p_save) {
		ctrlc = *p_save;
		ctrlc.c_cc[VINTR] = CH_CTRL('C');
		tcsetattr(STDIN_FILENO,TCSAFLUSH,&ctrlc);
	}
}

/* noncanonical, no echo */
void
tty_setraw(void)
{
	if (p_raw)
		tcsetattr(STDIN_FILENO,TCSAFLUSH,p_raw);
}

/* this is a cleanup function (see err_exit() in control.c) */
void
tty_reset(void)
{
	if (p_text)
		tcsetattr(STDIN_FILENO,TCSAFLUSH,p_text);
}

static int
tty_getchar(void)
{
	int in, flags, loops = 0;

	while ((in = getchar()) == EOF) {
		if (errno == EINTR)
			continue;
		if (errno == EAGAIN) {
			flags = fcntl(STDIN_FILENO,F_GETFL);
			if ((flags & O_NONBLOCK) == O_NONBLOCK) {
				/* clear the non-blocking flag */
				fcntl(STDIN_FILENO,F_SETFL,flags & ~O_NONBLOCK);
				continue;
			}
		}
		if (++loops >= 3)
			err_exit("Cannot read from standard input");
	}
	return in;
}

void
tty_press_enter(void)
{
	int in;

	if (disp_data.noenter) {
		fputs("Returning to CLEX.",stdout);
		disp_data.noenter = 0;
	}
	else {
		fputs("Press <enter> to continue. ",stdout);
		fflush(stdout);
		tty_setraw();
		while ((in = tty_getchar()) != '\n' && in != '\r')
			;
		tty_reset();
	}

	puts("\n----------------------------------------------");
	fflush(stdout);
	disp_data.wait = 0;
}

/*
 * - if 'yeschar' is set, msg is a yes/no question where 'yeschar' (in lower or upper case)
 *   means confirmation ('yeschar' parameter itself should be entered in lower case)
 */
int
tty_dialog(int yeschar, const char *msg)
{
	int code;

	putchar('\n');
	fputs(msg,stdout);
	if (yeschar)
		fprintf(stdout," (%c = %s) ",yeschar,"yes");
	fflush(stdout);
	tty_setraw();
	code = tolower(tty_getchar());
	tty_reset();
	if (yeschar) {
		code = code == yeschar;
		puts(code ? "yes" : "no");
	}
	putchar('\n');
	fflush(stdout);
	return code;
}
