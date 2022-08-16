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
#include <stdio.h>		/* printf() */
#include <string.h>		/* strerror() */

#include "opt.h"

#include "cfg.h"		/* cfg_num() */
#include "cmp.h"		/* cmp_saveopt() */
#include "filerw.h"		/* fr_open() */
#include "filter.h"		/* fopt_saveopt() */
#include "log.h"		/* msgout() */
#include "notify.h"		/* notif_saveopt() */
#include "sort.h"		/* sort_saveopt() */

/* limits to protect resources */
#define OPT_FILESIZE_LIMIT	150
#define OPT_LINES_LIMIT		 15

static FLAG changed = 0;			/* options have changed */

/* return value: 0 = ok, -1 = error */
static int
opt_read(void)
{
	int i, tfd, split, code;
	const char *line, *value;
	FLAG corrupted;

	tfd = fr_open(user_data.file_opt,OPT_FILESIZE_LIMIT);
	if (tfd == FR_NOFILE)
		return 0;	/* missing optional file is ok */
	if (tfd < 0)
		return -1;
	msgout(MSG_DEBUG,"OPTIONS: Processing options file \"%s\"",user_data.file_opt);

	split = fr_split(tfd,OPT_LINES_LIMIT);
	if (split < 0 && split != FR_LINELIMIT) {
		fr_close(tfd);
		return -1;
	}

	for (corrupted = 0, i = 0; (line = fr_line(tfd,i)); i++) {
		/* split VARIABLE and VALUE */
		if ( (value = strchr(line,'=')) == 0) {
			corrupted = 1;
			continue;
		}
		value++;
		if (strncmp(line,"COMPARE=",8) == 0)
			code = cmp_restoreopt(value);
		else if (strncmp(line,"FILTER=",7) == 0)
			code = fopt_restoreopt(value);
		else if (strncmp(line,"SORT=",5) == 0)
			code = sort_restoreopt(value);
		else if (strncmp(line,"NOTIFY=",7) == 0)
			code = notif_restoreopt(value);
		else
			code = -1;
		if (code < 0)
			corrupted = 1;
	}
	fr_close(tfd);

	if (split < 0 || corrupted) {
		msgout(MSG_NOTICE,"Invalid contents, the options file is outdated or corrupted");
		return -1;
	}
	return 0;
}

void
opt_initialize(void)
{
	if (opt_read() == 0)
		return;

	if (!user_data.nowrite) {
		/* automatic recovery */
		msgout(MSG_NOTICE,"Attempting to overwrite the invalid options file");
		changed = 1;
		opt_save();
		msgout(MSG_NOTICE,changed ? "Attempt failed" : "Attempt succeeded");
	}
	msgout(MSG_W,"OPTIONS: An error occurred while reading data, details in log");
}

void
opt_changed(void)
{
	changed = 1;
}

/* this is a cleanup function (see err_exit() in control.c) */
/* errors are not reported to the user, because this action is not initiated by him/her */
void
opt_save(void)
{
	FILE *fp;

	if (!changed || user_data.nowrite)
		return;

	if ( (fp = fw_open(user_data.file_opt)) == 0)
		return;

	fprintf(fp,
	  "#\n# CLEX options file\n#\n"
	  "COMPARE=%s\n"
	  "FILTER=%s\n"
	  "SORT=%s\n"
	  "NOTIFY=%s\n",cmp_saveopt(),fopt_saveopt(),
	  sort_saveopt(),notif_saveopt());

	if (fw_close(fp) == 0)
		changed = 0;
}
