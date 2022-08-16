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

#include <signal.h>		/* sigaction() */
#include <unistd.h>		/* _POSIX_JOB_CONTROL */

#include "signals.h"

#include "control.h"	/* err_exit() */
#include "inout.h"		/* curses_cbreak() */
#include "tty.h"		/* tty_ctrlc() */

static RETSIGTYPE
int_handler(int sn)
{
	err_exit("Signal %s caught", sn == SIGTERM ? "SIGTERM" : "SIGHUP");
}

static RETSIGTYPE
ctrlc_handler(int unused)
{
	ctrlc_flag = 1;
}

void
signal_initialize(void)
{
	struct sigaction act;

	/* ignore keyboard generated signals */
	act.sa_handler = SIG_IGN;
	act.sa_flags = 0;
	sigemptyset(&act.sa_mask);
	sigaction(SIGINT,&act,0);
	sigaction(SIGQUIT,&act,0);

	/* catch termination signals */
	act.sa_handler = int_handler;
	sigaddset(&act.sa_mask,SIGTERM);
	sigaddset(&act.sa_mask,SIGHUP);
	sigaction(SIGTERM,&act,0);
	sigaction(SIGHUP,&act,0);
}

void
signal_ctrlc_on(void)
{
	struct sigaction act;

	curses_cbreak();
	tty_save();
	tty_ctrlc();

	act.sa_handler = ctrlc_handler;
	act.sa_flags = 0;
	sigemptyset(&act.sa_mask);
	sigaction(SIGINT,&act,0);
}

void
signal_ctrlc_off(void)
{
	struct sigaction act;

	act.sa_handler = SIG_IGN;
	act.sa_flags = 0;
	sigemptyset(&act.sa_mask);
	sigaction(SIGINT,&act,0);

	tty_restore();
	curses_raw();
}
