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

#include <wctype.h>		/* iswdigit() */
#include <stdlib.h>		/* qsort() */
#include <string.h>		/* strlen() */

/* major() */
#ifdef MAJOR_IN_MKDEV
# include <sys/mkdev.h>
#endif
#ifdef MAJOR_IN_SYSMACROS
# include <sys/sysmacros.h>
#endif

#include "sort.h"

#include "directory.h"	/* filepos_save() */
#include "inout.h"		/* win_panel() */
#include "list.h"		/* file_panel_data() */
#include "opt.h"		/* opt_changed() */

int
sort_prepare(void)
{
	panel_sort.pd->top = panel_sort.pd->min;
	panel_sort.pd->curs = HIDE_TOTAL_ + GROUP_TOTAL_ + 2 + panel_sort.order;
						  /* place the cursor at the current sort order */
	panel_sort.newgroup = panel_sort.group;
	panel_sort.neworder = panel_sort.order;
	panel_sort.newhide = panel_sort.hide;
	panel = panel_sort.pd;
	textline = 0;
	return 0;
}

void
cx_sort_set(void)
{
	int sel;

	sel = panel_sort.pd->curs;

	if (sel < HIDE_TOTAL_) {
		panel_sort.newhide = sel;
		win_panel();
		return;
	}
	if (sel == HIDE_TOTAL_)
		return;
	sel -= HIDE_TOTAL_ + 1;

	if (sel < GROUP_TOTAL_) {
		/* set grouping */
		panel_sort.newgroup = sel;
		win_panel();
		return;
	}
	if (sel == GROUP_TOTAL_)
		return;
	sel -= GROUP_TOTAL_ + 1;

	if (sel < SORT_TOTAL_) {
		/* set sort order */
		panel_sort.neworder = sel;
		win_panel();
		return;
	}

	/* activate settings */
	sel -= SORT_TOTAL_;
	if (sel == 0) {
		/* setting the default */
		if (panel_sort.order != panel_sort.neworder
		  || panel_sort.group != panel_sort.newgroup
		  || panel_sort.hide != panel_sort.newhide) {
			panel_sort.group = panel_sort.newgroup;
			panel_sort.order = panel_sort.neworder;
			panel_sort.hide = panel_sort.newhide;
			opt_changed();
		}
		if (ppanel_file->other->order != panel_sort.neworder
		  || ppanel_file->other->group != panel_sort.newgroup
		  || ppanel_file->other->hide != panel_sort.newhide) {
			ppanel_file->other->group = panel_sort.newgroup;
			ppanel_file->other->order = panel_sort.neworder;
			ppanel_file->other->hide = panel_sort.newhide;
			ppanel_file->other->expired = 1;
		}
	}
	if (ppanel_file->hide != panel_sort.newhide) {
		ppanel_file->group = panel_sort.newgroup;
		ppanel_file->order = panel_sort.neworder;
		ppanel_file->hide = panel_sort.newhide;
		list_directory();
	}
	else if (ppanel_file->order != panel_sort.neworder
	  || ppanel_file->group != panel_sort.newgroup) {
		ppanel_file->group = panel_sort.newgroup;
		ppanel_file->order = panel_sort.neworder;
		filepos_save();
		sort_files();
		filepos_set();
	}
	next_mode = MODE_SPECIAL_RETURN;
}

const char *
sort_saveopt(void)
{
	static char buff[4] = "???";

	buff[0] = 'A' + panel_sort.order;
	buff[1] = 'A' + panel_sort.group;
	buff[2] = 'A' + panel_sort.hide;

	return buff;
}

/* read options from a string */
int
sort_restoreopt(const char *opt)
{
	unsigned char ch;

	ch = opt[0];
	if (ch < 'A' || ch >= 'A' + SORT_TOTAL_)
		return -1;
	panel_sort.order = ch - 'A';
	ch = opt[1];
	if (ch < 'A' || ch >= 'A' + GROUP_TOTAL_)
		return -1;
	panel_sort.group = ch - 'A';
	ch = opt[2];
	if (ch == '\0')
		return 0;		/* up to 4.6.5 */
	if (ch < 'A' || ch >= 'A' + HIDE_TOTAL_)
		return -1;
	panel_sort.hide = ch - 'A';
	return opt[3] == '\0' ? 0 : -1;
}

/* compare reversed strings */
static int
revstrcmp(const char *s1, const char *s2)
{
	size_t i1, i2;
	int c1, c2;

	for (i1 = strlen(s1), i2 = strlen(s2); i1 > 0 && i2 > 0;) {
		c1 = (unsigned char)s1[--i1];
		c2 = (unsigned char)s2[--i2];
		/*
		 * ignoring LOCALE, this sort order has nothing to do
		 * with a human language, it is intended for sendmail
		 * queue directories
		 */
		if (c1 != c2)
			return c1 - c2;
	}
	return CMP(i1,i2);
}

/* sort_group() return values in grouping order */
enum FILETYPE_TYPE {
	FILETYPE_DOTDIR, FILETYPE_DOTDOTDIR, FILETYPE_DIR,
	FILETYPE_BDEV, FILETYPE_CDEV, FILETYPE_OTHER, FILETYPE_PLAIN
};
static int
sort_group(int gr, FILE_ENTRY *pfe)
{
	int type;

	type = pfe->file_type;
	if (IS_FT_PLAIN(type))
		return FILETYPE_PLAIN;
	if (IS_FT_DIR(type)) {
		if (pfe->dotdir == 1)
			return FILETYPE_DOTDIR;
		if (pfe->dotdir == 2)
			return FILETYPE_DOTDOTDIR;
		return FILETYPE_DIR;
	}
	if (gr == GROUP_DBCOP) {
		if (type == FT_DEV_CHAR)
			return FILETYPE_CDEV;
		if (type == FT_DEV_BLOCK)
			return FILETYPE_BDEV;
	}
	return FILETYPE_OTHER;
}

/* compare function for numeric sort */
int
num_wcscoll(const wchar_t *name1, const wchar_t *name2)
{
	wchar_t ch1, ch2;
	int i, len1, len2, len;

	for (; /* until break */; ) {
		while (*name1 && *name1 == *name2 && !iswdigit(*name1)) {
			name1++;
			name2++;
		}
		if (!iswdigit(*name1) || !iswdigit(*name2))
			break;

		/* compare two numbers (zero padded to the same length) */
		for (len1 = 1; iswdigit(name1[len1]); )
			len1++;
		for (len2 = 1; iswdigit(name2[len2]); )
			len2++;
		len = len1 > len2 ? len1 : len2;
		for (i = 0; i < len; i++) {
			ch1 = (i + len1 - len < 0) ? '0' : name1[i + len1 - len];
			ch2 = (i + len2 - len < 0) ? '0' : name2[i + len2 - len];
			if (ch1 != ch2)
				return CMP(ch1,ch2);
		}
		if (len1 != len2)
			return len2 - len1;

		name1 += len;
		name2 += len;
	}
	return wcscoll(name1,name2);
}

static int
qcmp(const void *e1, const void *e2)
{
	int cmp, gr, group1, group2;
	FILE_ENTRY *pfe1, *pfe2;

	pfe1 = (*(FILE_ENTRY **)e1);
	pfe2 = (*(FILE_ENTRY **)e2);

	/* I. file type grouping */
	gr = ppanel_file->group;
	if (gr != GROUP_NONE) {
		group1 = sort_group(gr,pfe1);
		group2 = sort_group(gr,pfe2);
		cmp = group1 - group2;
		if (cmp)
			return cmp;

		/* special sorting for devices */
		if (gr == GROUP_DBCOP && (group1 == FILETYPE_BDEV || group1 == FILETYPE_CDEV) ) {
			cmp = major(pfe1->devnum) - major(pfe2->devnum);
			if (cmp)
				return cmp;
			cmp = minor(pfe1->devnum) - minor(pfe2->devnum);
			if (cmp)
				return cmp;
		}
	}

	/* II. sort order */
	switch (ppanel_file->order) {
	case SORT_NAME_NUM:
		cmp = num_wcscoll(SDSTR(pfe1->filew),SDSTR(pfe2->filew));
		break;
	case SORT_EXT:
		cmp = strcoll(pfe1->extension,pfe2->extension);
		break;
	case SORT_SIZE:
		cmp = CMP(pfe1->size,pfe2->size);
		break;
	case SORT_SIZE_REV:
		cmp = CMP(pfe2->size,pfe1->size);
		break;
	case SORT_TIME:
		cmp = CMP(pfe2->mtime,pfe1->mtime);
		break;
	case SORT_TIME_REV:
		cmp = CMP(pfe1->mtime,pfe2->mtime);
		break;
	case SORT_EMAN:
		return revstrcmp(SDSTR(pfe1->file),SDSTR(pfe2->file));
	default:
		/* SORT_NAME */
		cmp = 0;
	}
	if (cmp)
		return cmp;

	/* III. sort by file name */
	return strcoll(SDSTR(pfe1->file),SDSTR(pfe2->file));
}

void
sort_files(void)
{
	if (ppanel_file->all_cnt == 0)
		return;
	qsort(ppanel_file->all_files,ppanel_file->all_cnt,sizeof(FILE_ENTRY *),qcmp);
	file_panel_data();
}
