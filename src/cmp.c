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

/* comparing the contents of two directories (filepanels) */

#include "clexheaders.h"

#include <sys/stat.h>	/* stat() */
#include <errno.h>		/* errno */
#include <fcntl.h>		/* open() */
#include <stdarg.h>		/* log.h */
#include <stdlib.h>		/* qsort() */
#include <string.h>		/* strcmp() */
#include <unistd.h>		/* close() */

#include "select.h"

#include "inout.h"		/* win_waitmsg() */
#include "list.h"		/* list_both_directories() */
#include "log.h"		/* msgout() */
#include "opt.h"		/* opt_changed() */
#include "panel.h"		/* pan_adjust() */
#include "signals.h"	/* signal_ctrlc_on() */
#include "util.h"		/* pathname_join() */

int
cmp_prepare(void)
{
	if (strcmp(USTR(ppanel_file->dir),USTR(ppanel_file->other->dir)) == 0) {
		msgout(MSG_i,"COMPARE: same directory in both panels");
		return -1;
	}

	panel_cmp.pd->top = panel_fopt.pd->min;
	panel_cmp.pd->curs = panel_cmp.pd->cnt - 1;	/* last line */
	panel = panel_cmp.pd;
	pan_adjust(panel);
	textline = 0;

	return 0;
}

int
cmp_summary_prepare(void)
{
	panel_cmp_sum.pd->top = panel_cmp_sum.pd->curs = panel_cmp_sum.pd->min;
	panel_cmp_sum.pd->cnt = panel_cmp_sum.errors ? 6 : 5;
	panel = panel_cmp_sum.pd;
	textline = 0;

	if (panel_cmp_sum.errors)
		win_sethelp(HELPMSG_BASE,L"Error messages can be found in the log (alt-L)");

	return 0;
}

/* write options to a string */
const char *
cmp_saveopt(void)
{
	int i, j;
	static char buff[CMP_TOTAL_ + 1];

	for (i = j = 0; i < CMP_TOTAL_; i++)
		if (COPT(i))
			buff[j++] = 'A' + i;
	buff[j] = '\0';

	return buff;
}

/* read options from a string */
int
cmp_restoreopt(const char *opt)
{
	int i;
	unsigned char ch;

	for (i = 0; i < CMP_TOTAL_; i++)
		COPT(i) = 0;

	while ( (ch = *opt++) ) {
		if (ch < 'A' || ch >= 'A' + CMP_TOTAL_)
			return -1;
		COPT(ch - 'A') = 1;
	}

	return 0;
}

static int
qcmp(const void *e1, const void *e2)
{
	return strcmp( /* not strcoll() */
	  SDSTR((*(FILE_ENTRY **)e1)->file),
	  SDSTR((*(FILE_ENTRY **)e2)->file));
}

#define CMP_BUF_STR	16384

/* return value: -1 error, 0 compare ok, +1 compare failed */
static int
data_cmp(int fd1, const char *file1, int fd2, const char *file2)
{
	struct stat st1, st2;
	static char buff1[CMP_BUF_STR], buff2[CMP_BUF_STR];
	off_t filesize;
	size_t chunksize;

	if (fstat(fd1,&st1) < 0 || !S_ISREG(st1.st_mode)) {
		msgout(MSG_NOTICE,"COMPARE: File \"./%s\" is not a regular file",file1);
		return -1;
	}
	if (fstat(fd2,&st2) < 0 || !S_ISREG(st2.st_mode)) {
		msgout(MSG_NOTICE,"COMPARE: File \"%s\" is not a regular file",file2);
		return -1;
	}
	if (st1.st_dev == st2.st_dev && st1.st_ino == st2.st_ino)
		/* same file */
		return 0;
	if ((filesize = st1.st_size) != st2.st_size)
		return 1;

	while (filesize > 0) {
		chunksize = filesize > CMP_BUF_STR ? CMP_BUF_STR : filesize;
		if (ctrlc_flag)
			return -1;
		if (read_fd(fd1,buff1,chunksize) != chunksize) {
			msgout(MSG_NOTICE,"COMPARE: Cannot read from \"./%s\" (%s)",file1,strerror(errno));
			return -1;
		}
		if (read_fd(fd2,buff2,chunksize) != chunksize) {
			msgout(MSG_NOTICE,"COMPARE: Cannot read from \"%s\" (%s)",file2,strerror(errno));
			return -1;
		}
		if (memcmp(buff1,buff2,chunksize) != 0)
			return 1;
		filesize -= chunksize;
	}

	return 0;
}

/* return value: -1 error, 0 compare ok, +1 compare failed */
static int
file_cmp(const char *file1, const char *file2)
{
	int cmp, fd1, fd2;

	fd1 = open(file1,O_RDONLY | O_NONBLOCK);
	if (fd1 < 0) {
		msgout(MSG_NOTICE,"COMPARE: Cannot open \"./%s\" (%s)",file1,strerror(errno));
		return -1;
	}
	fd2 = open(file2,O_RDONLY | O_NONBLOCK);
	if (fd2 < 0) {
		msgout(MSG_NOTICE,"COMPARE: Cannot open \"%s\" (%s)",file2,strerror(errno));
		close(fd1);
		return -1;
	}
	cmp = data_cmp(fd1,file1,fd2,file2);
	close(fd1);
	close(fd2);
	return cmp;
}

static void
cmp_directories(void)
{
	int min, med, max, cmp, i, j, cnt1, selcnt1, selcnt2;
	const char *name2;
	FILE_ENTRY *pfe1, *pfe2;
	static FILE_ENTRY **p1 = 0;	/* copy of panel #1 sorted for binary search */
	static int p1_alloc = 0;

	/*
	 * - select all files and both panels
	 * - for each file from panel #2:
	 *		- find a matching file from panel #1
	 *		- if a pair is found, compare them according to the selected options
	 *		- if the compared files are equal, deselect them
	 */

	panel_cmp_sum.errors = panel_cmp_sum.names = panel_cmp_sum.equal = 0;

	/* reread panels */
	list_both_directories();

	ctrlc_flag = 0;
	if (COPT(CMP_DATA)) {	/* going to compare data */
		signal_ctrlc_on();
		win_waitmsg();
		pathname_set_directory(USTR(ppanel_file->other->dir));
	}

	if (COPT(CMP_REGULAR)) {
		for (cnt1 = i = 0; i < ppanel_file->pd->cnt; i++) {
			pfe1 = ppanel_file->files[i];
			if ( (pfe1->select = IS_FT_PLAIN(pfe1->file_type)) )
				cnt1++;
		}
		panel_cmp_sum.nonreg1 = ppanel_file->pd->cnt - cnt1;
	}
	else {
		cnt1 = ppanel_file->pd->cnt;
		panel_cmp_sum.nonreg1 = 0;
	}
	selcnt1 = cnt1;

	if (cnt1) {
		if (p1_alloc < cnt1) {
			efree(p1);
			p1_alloc = cnt1;
			p1 = emalloc(p1_alloc * sizeof(FILE_ENTRY *));
		}

		if (COPT(CMP_REGULAR))
			for (i = j = 0; i < ppanel_file->pd->cnt; i++) {
				pfe1 = ppanel_file->files[i];
				if (pfe1->select)
					p1[j++] = pfe1;
			}
		else
			for (i = 0; i < ppanel_file->pd->cnt; i++) {
				pfe1 = p1[i] = ppanel_file->files[i];
				pfe1->select = 1;
		}

		qsort(p1,cnt1,sizeof(FILE_ENTRY *),qcmp);
	}

	panel_cmp_sum.nonreg2 = 0;
	selcnt2 = 0;
	for (i = 0; i < ppanel_file->other->pd->cnt; i++) {
		pfe2 = ppanel_file->other->files[i];
		if ( !(pfe2->select = !COPT(CMP_REGULAR) || IS_FT_PLAIN(pfe2->file_type)) ) {
			panel_cmp_sum.nonreg2++;
			continue;
		}
		selcnt2++;

		if (panel_cmp_sum.names == cnt1)
			/* we have seen all files from panel#1 */
			continue;	/* not break */

		name2 = SDSTR(pfe2->file);
		for (pfe1 = 0, min = 0, max = cnt1 - 1; min <= max; ) {
			med = (min + max) / 2;
			cmp = strcmp(name2,SDSTR(p1[med]->file));
			if (cmp == 0) {
				pfe1 = p1[med];
				/* entries *pfe1 and *pfe2 have the same name */
				break;
			}
			if (cmp < 0)
				max = med - 1;
			else
				min = med + 1;
		}
		if (pfe1 == 0 || !pfe1->select)
			continue;

		panel_cmp_sum.names++;

		/* always comparing type */
		if (pfe1->file_type == FT_NA || !(
			(IS_FT_PLAIN(pfe1->file_type) && IS_FT_PLAIN(pfe2->file_type))
			|| (IS_FT_DIR(pfe1->file_type) && IS_FT_DIR(pfe2->file_type))
			|| (pfe1->file_type == pfe2->file_type) )
		  )
			continue;
		if (pfe1->symlink != pfe2->symlink)
			continue;

		/* comparing size (or device numbers) */
		if (COPT(CMP_SIZE)
		  && ((IS_FT_DEV(pfe1->file_type) && pfe1->devnum != pfe2->devnum)
		  || (IS_FT_PLAIN(pfe1->file_type) && pfe1->size != pfe2->size)))
			continue;

		if (COPT(CMP_OWNER)
		  && (pfe1->uid != pfe2->uid || pfe1->gid != pfe2->gid))
			continue;

		if (COPT(CMP_MODE) && pfe1->mode12 != pfe2->mode12)
			continue;

		if (COPT(CMP_DATA) && IS_FT_PLAIN(pfe1->file_type)) {
			if (pfe1->size != pfe2->size)
				continue;
			if ( (cmp = file_cmp(SDSTR(pfe1->file),pathname_join(name2))) ) {
				if (ctrlc_flag)
					break;
				if (cmp < 0)
					panel_cmp_sum.errors++;
				continue;
			}
		}

		/* pair of matching files found */
		pfe1->select = 0;
		selcnt1--;
		pfe2->select = 0;
		selcnt2--;
		panel_cmp_sum.equal++;
	}
	ppanel_file->selected = selcnt1;
	ppanel_file->other->selected = selcnt2;

	if (COPT(CMP_DATA))
		signal_ctrlc_off();

	if (ctrlc_flag) {
		msgout(MSG_i,"COMPARE: operation canceled");
		/* clear all marks */
		for (i = 0; i < cnt1; i++)
			ppanel_file->files[i]->select = 0;
		ppanel_file->selected = 0;
		for (i = 0; i < ppanel_file->other->pd->cnt; i++)
			ppanel_file->other->files[i]->select = 0;
		ppanel_file->other->selected = 0;

		next_mode = MODE_SPECIAL_RETURN;
		return;
	}

	next_mode = MODE_CMP_SUM;
}

void
cx_cmp(void)
{
	int sel;

	sel = panel_cmp.pd->curs;
	if (sel < CMP_TOTAL_) {
		TOGGLE(COPT(sel));
		opt_changed();
		win_panel_opt();
	}
	else
		cmp_directories();
}
