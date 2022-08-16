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

#include <sys/stat.h>	/* stat() */
#include <errno.h>		/* errno */
#include <stdarg.h>		/* log.h */
#include <stdio.h>		/* rename() */
#include <string.h>		/* strchr() */
#include <unistd.h>		/* stat() */
#include <wctype.h>		/* iswprint() */

#include "rename.h"

#include "edit.h"		/* edit_nu_putstr() */
#include "inout.h"		/* win_panel() */
#include "list.h"		/* list_directory() */
#include "log.h"		/* msgout() */
#include "mbwstring.h"	/* convert2mb() */

static FILE_ENTRY *pfe;

int
rename_prepare(void)
{
	wchar_t ch, *pch;

	/* inherited panel = ppanel_file->pd */
	if (panel->filtering == 1)
		panel->filtering = 2;

	pfe = ppanel_file->files[ppanel_file->pd->curs];
	if (pfe->dotdir) {
		msgout(MSG_w,"RENAME: refusing to rename the . and .. directories");
		return -1;
	}

	edit_setprompt(&line_tmp,L"Rename the current file to: ");
	textline = &line_tmp;
	edit_nu_putstr(SDSTR(pfe->filew));
	for (pch = USTR(textline->line); (ch = *pch) != L'\0'; pch++)
		if (!ISWPRINT(ch) || (lang_data.utf8 && ch == L'\xFFFD'))
			*pch = L'_';

	return 0;
}

void
cx_rename(void)
{
	const char *oldname, *newname;
	const wchar_t *newnamew;
	struct stat st;

	if (line_tmp.size == 0) {
		next_mode = MODE_SPECIAL_RETURN;
		return;
	}

	oldname = SDSTR(pfe->file);
	newname = convert2mb(newnamew = USTR(textline->line));
	if (strcmp(newname,oldname) == 0) {
		msgout(MSG_i,"file not renamed");
		next_mode = MODE_SPECIAL_RETURN;
		return;
	}
	if (strchr(newname,'/')) {
		msgout(MSG_i,"please enter the name without a directory part");
		return;
	}
	if (stat(newname,&st) == 0) {
		msgout(MSG_i,"a file with this name exists already");
		return;
	}
	if (rename(oldname,newname) < 0)
		msgout(MSG_w,"Renaming has failed: %s",strerror(errno));
		/* NFS: it does not mean the file is not renamed */
	else {
		msgout(MSG_AUDIT,"Rename: \"%s\" --> \"%s\" in \"%s\"",
		  oldname,newname,USTR(ppanel_file->dir));
		sd_copy(&pfe->file,newname);
		sdw_copy(&pfe->filew,newnamew);
	}
	list_directory();
	win_panel();
	next_mode = MODE_SPECIAL_RETURN;
}
