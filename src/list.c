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

#include <sys/stat.h>		/* stat() */
#include <wctype.h>			/* iswprint() */
#include <dirent.h>			/* readdir() */
#include <errno.h>			/* errno */
#include <stdarg.h>			/* log.h */
#include <stdio.h>			/* sprintf() */
#include <string.h>			/* strcmp() */
#include <time.h>			/* time() */
#include <unistd.h>			/* stat() */

/* major() */
#ifdef MAJOR_IN_MKDEV
# include <sys/mkdev.h>
#endif
#ifdef MAJOR_IN_SYSMACROS
# include <sys/sysmacros.h>
#endif

#ifndef S_ISLNK
# define S_ISLNK(X)	(0)
#endif

#include "list.h"

#include "cfg.h"			/* cfg_num() */
#include "control.h"		/* err_exit() */
#include "directory.h"		/* filepos_save() */
#include "inout.h"			/* win_waitmsg() */
#include "filter.h"			/* filter_update() */
#include "lex.h"			/* ispattern() */
#include "log.h"			/* msgout() */
#include "match.h"			/* match_pattern_set() */
#include "mbwstring.h"		/* convert2w() */
#include "sort.h"			/* sort_files() */
#include "userdata.h"		/* lookup_login() */
#include "ustringutil.h"	/* us_readlink() */
#include "util.h"			/* emalloc() */

/*
 * additional FILE_ENTRIES to be allocated when the file panel is full,
 * i.e. when all existing entries in 'all_files' are occupied
 */
#define FE_ALLOC_UNIT	128

/* pre-formatted user/group name cache */
/* important: gcd(CACHE_SIZE,CACHE_REPL) must be 1 */
#define CACHE_SIZE 		24
#define CACHE_REPL		7
static int ucache_cnt = 0, gcache_cnt = 0;

/* layout */
static FLAG do_gt, do_a, do_d, do_i, do_l, do_L, do_g,
	do_m, do_m_blank, do_o, do_s, do_s_short, do_s_nodir;		/* fields in use */
static mode_t normal_file, normal_dir;

/* kilobyte definition */
static int K2, K995;				/* kilobyte/2, kilobyte*9.95 */

/* time & date output */
static time_t now;					/* current time */
static int now_day;					/* current day of month */
static struct tm testtm;			/* tm struct for format string checks */
static FLAG future;
static FLAG td_both;				/* both time and date are displayed */
#define TD_TIME	0
#define TD_DATE	1
#define TD_BOTH	2
static const wchar_t *td_fmt[3];	/* time/date formats (man strftime, man wcsftime) */
static int td_pad[3], td_len[3];	/* time/date padding + output string lengths */
									/* padding + length = disp_data.date_len */
static const wchar_t *td_fmt_fail;	/* failed time/date output format */

#define FAILSAFE_TIME		L"%H:%M"
#define FAILSAFE_DATE		L"%Y-%m-%d"
#define FAILSAFE_TIMEDATE	L"%H:%M %Y-%m-%d"
#define FAILSAFE_DATETIME	L"%Y-%m-%d %H:%M"

/* directory listing */
static FLAG mm_change;				/* minor/major format changed during the operation */
static FLAG use_pathname = 0;		/* hack for listing a directory other than the cwd */
static dev_t dirdev;				/* filesystem device of the inspected directory */

static int
check_str(const wchar_t *str)
{
	wchar_t ch;

	if (str == 0)
		return -1;

	while ((ch = *str++) != L'\0')
		if (!ISWPRINT(ch) || wcwidth(ch) != 1)
			return -1;
	return 0;
}

/* return value: output string length if it does fit, -1 otherwise */
static int
check_format(const wchar_t *format)
{
	int len;
	wchar_t tmp[80];

	if (check_str(format) < 0) {
		msgout(MSG_NOTICE,"Date/time output format \"%ls\" rejected "
		  "because of non-standard characters",format);
		return -1;
	}

	len = wcsftime(tmp,ARRAY_SIZE(tmp),format,&testtm);
	if (len == 0 || len >= FE_TIME_STR) {
		msgout(MSG_NOTICE,"Date/time output format \"%ls\" rejected "
		  "because it produces output longer than a limit of %d characters",
		  format,FE_TIME_STR - 1);
		return -1;
	}
	return len;
}

void
td_fmt_reconfig(void)
{
	int order, fmt_fail, len, diff;
	const wchar_t *fmt;
	static USTRINGW buff = UNULL;

	fmt_fail = 0;

	fmt = cfg_str(CFG_FMT_TIME);
	if (*fmt == L'\0')
		fmt = lang_data.time_fmt;
	if ((len = check_format(fmt)) < 0) {
		fmt_fail = 1;
		fmt = FAILSAFE_TIME;
		len = check_format(fmt);
	}
	td_fmt[TD_TIME] = fmt;
	td_pad[TD_TIME] = 0;
	td_len[TD_TIME] = len;

	fmt = cfg_str(CFG_FMT_DATE);
	if (*fmt == L'\0')
		fmt = lang_data.date_fmt;
	if ((len = check_format(fmt)) < 0) {
		fmt_fail = 1;
		fmt = FAILSAFE_DATE;
		len = check_format(fmt);
	}
	td_fmt[TD_DATE] = fmt;
	td_pad[TD_DATE] = 0;
	td_len[TD_DATE] = len;

	td_both = cfg_num(CFG_TIME_DATE) != 0;
	if (td_both) {
		order = cfg_num(CFG_TIME_DATE) == 1;
		usw_cat(&buff, td_fmt[order ? TD_TIME : TD_DATE], L" ",
		  td_fmt[order ? TD_DATE : TD_TIME], (wchar_t *)0);
		fmt = USTR(buff);
		if ((len = check_format(fmt)) < 0) {
			fmt_fail = 1;
			fmt = order ? FAILSAFE_TIMEDATE : FAILSAFE_DATETIME;
			len = check_format(fmt);
		}
		td_fmt[TD_BOTH] = fmt;
		td_pad[TD_BOTH] = 0;
		disp_data.date_len = td_len[TD_BOTH] = len;
	}
	else {
		diff = td_len[TD_DATE] - td_len[TD_TIME];
		if (diff > 0) {
			td_pad[TD_TIME] = diff;
			disp_data.date_len = td_len[TD_DATE];
		}
		else {
			td_pad[TD_DATE] = -diff;
			disp_data.date_len = td_len[TD_TIME];
		}
	}

	if (fmt_fail)
		msgout(MSG_w,"Problem with time/date output format, details in log");
}

void
kb_reconfig(void)
{
	if (cfg_num(CFG_KILOBYTE)) {
		/* 1000 */
		K2 = 500;
		K995 = 9950;
	} else {
		/* 1024 */
		K2 = 512;
		K995 = 10189;
	}
}

/* split layout to panel fields and line fields */
static void
split_layout(void)
{
	static USTRINGW layout = UNULL;
	FLAG fld;
	wchar_t ch, *pch;

	pch = disp_data.layout_panel = usw_copy(&layout,cfg_layout);
	for (fld = 0; (ch = *pch); pch++)
		if (!TCLR(fld)) {
			if (ch == L'$')
				fld = 1;
			else if (ch == L'|') {
				*pch = L'\0';
				disp_data.layout_line = pch + 1;
				return;	/* success */
			}
		}
	msgout(MSG_NOTICE,"CONFIG: Incomplete layout definition: \"%ls\"",
	  disp_data.layout_panel);
	disp_data.layout_line = L"";
}

void
layout_reconfig(void)
{
	FLAG field;
	const wchar_t *layout;
	wchar_t ch;

	split_layout();

	/* which fields are going to be displayed ? */
	do_gt = do_a = do_d = do_i = do_g = do_L = do_l = 0;
	do_m = do_m_blank = do_o = do_s = do_s_short = do_s_nodir = 0;
	for (field = 0, layout = cfg_layout /*macro*/; (ch = *layout++); )
		if (!TCLR(field)) {
			if (ch == L'$')
				field = 1;
		}
		else {
			switch (ch) {
			case L'>': do_gt = 1; break;
			case L'a': do_a = 1; break;
			case L'd': do_d = 1; break;
			case L'g': do_g = 1; break;
			case L'i': do_i = 1; break;
			case L'L': do_L = 1; break;
			case L'l': do_l = 1; break;
			case L'P':					/* $P -> $M */
			case L'M': do_m_blank = 1; 	/* $M -> $m */
			case L'p':					/* $p -> $m */
			case L'm': do_m = 1; break;
			case L'o': do_o = 1; break;
			/* do_s (size), do_s_short (short form), do_s_nodir (not for dirs) */
			case L'S': do_s_nodir = 1;
			case L's': do_s = 1; break;
			case L'R': do_s_nodir = 1;
			case L'r': do_s_short = do_s = 1; break;
			/* default: ignore unknown formatting character */
			}
		}
}

void
list_initialize(void)
{
	time_t sometime = 1234567890;			/* an arbitrary value */

	testtm = *gmtime(&sometime);			/* struct copy */

	normal_dir  = 0777 & ~clex_data.umask;	/* dir or executable file */
	normal_file = 0666 & ~clex_data.umask;	/* any other file */

	kb_reconfig();
	layout_reconfig();
	td_fmt_reconfig();
}

/*
 * when calling any of the stat2xxx() functions always make sure
 * the string provided as 'str' argument can hold the result.
 * Check the FE_XXX_STR #defines in clex.h for proper sizes.
 */

static void
stat2time(wchar_t *str, time_t tm)
{
	int i, td, len;

	if (td_both)
		td = TD_BOTH;
	else if (tm <= now) {
		if (tm < now - 86400 /* more than 1 day ago */)
			td = TD_DATE;
		else if (tm > now - 57600 /* less than 16 hours ago */)
			td = TD_TIME;
		else
			td = localtime(&tm)->tm_mday != now_day ? TD_DATE: TD_TIME;
	}
	else {
		if (tm > now + 300	/* 5 minutes tolerance */)
			future = 1;
		td = (tm > now + 86400 || localtime(&tm)->tm_mday != now_day) ? TD_DATE: TD_TIME;
	}
	
	for (i = 0; i < td_pad[td]; i++)
		*str++ = L' ';
	len = wcsftime(str,FE_TIME_STR - td_pad[td],td_fmt[td],localtime(&tm));
	if (len == td_len[td])
		return;

	/* problem with the format: output length is not constant */
	if (len > 0 && len < td_len[td]) {
		/* small problem: the string is shorter */
		for (i = len; i < td_len[td]; i++)
			str[i] = L' ';
		str[i] = L'\0';
	}
	else {
		/* big problem: the string does not fit */
		td_fmt_fail = td_fmt[td];
		for (i = 0; i < td_len[td]; i++)
			str[i] = L'-';
		str[i] = L'\0';
	}
}

static void
stat2age(char *str, time_t tm)
{
	time_t age;
	int h, m, s;

	age = now - tm;

	if (age < 0) {
		strcpy(str,"  future!");
		return;
	}
	if (age >= 360000 /* 100 hours */) {
		strcpy(str,"         ");
		return;
	}

	h = age / 3600;
	age -= h * 3600;
	m = age / 60;
	s = age - m * 60;
	if (h)
		sprintf(str,"%3d:%02d:%02d",-h,m,s);
	else if (m)
		sprintf(str,"   %3d:%02d",-m,s);
	else if (s)
		sprintf(str,"      %3d",-s);
	else
		strcpy(str,"       -0");
}

static void
stat2size_7(char *str, off_t size)
{
	int exp, roundup;

	for (exp = roundup = 0; size + roundup > 9999999 /* 9.999.999 */; exp++) {
		/* size = size / 1K */
		size /= K2;
		roundup = size % 2;
		size /= 2;
	}

	if (K2 != 512)
		*str++ = ' ';
	sprintf(str,"  %7ld%c",(long int)(size + roundup)," KMGTPEZY"[exp]);
	/* insert thousands separators: 1.234.567 */
	if (str[5] != ' ') {
		if (str[2] != ' ') {
			str[0] = str[2];
			str[1] = lang_data.sep000;
		}
		str[2] = str[3];
		str[3] = str[4];
		str[4] = str[5];
		str[5] = lang_data.sep000;
	}
	if (K2 == 512)
		str[10] = exp ? 'i' : ' ';
}

static void
stat2size_3(char *str, off_t size)
{
	int exp, roundup;
	FLAG dp;	/* decimal point */

	for (exp = roundup = 0, dp = 0; size + roundup > 999; exp++) {
		if ( (dp = size < K995) )
			size *= 10;
		size /= K2;
		roundup = size % 2;
		size /= 2;
	}

	if (K2 != 512)
		*str++ = ' ';
	sprintf(str,"      %3d%c",(int)(size + roundup)," KMGTPEZY"[exp]);
	if (dp) {
		str[6] = str[7];
		str[7] = lang_data.sep000;
	}
	if (K2 == 512)
		str[10] = exp ? 'i' : ' ';
}

/*
 * stat2dev() prints device major:minor numbers
 *
 * FE_SIZE_DEV_STR is 12 ==> total number of digits is 10,
 * from these 10 digits are 2 to 7 used for minor device
 * number (printed in hex) and the rest is used for major
 * device number (printed in dec)
 *
 * some major:minor splits
 *    8 :  8 bits - Linux 2.4
 *   14 : 18 bits - SunOS 5
 *    8 : 24 bits - FreeBSD
 *   12 : 20 bits - Linux 2.6
 */

#define MIN_MINOR_DIGITS	2
#define MAX_MINOR_DIGITS	7

static void
stat2dev(char *str, unsigned int dev_major, unsigned int dev_minor)
{
	static unsigned int digits_minor[] = {
	  0,
	  0xF,
	  0xFF,		/* 2 digits,  8 bits */
	  0xFFF,	/* 3 digits, 12 bits */
	  0xFFFF,	/* 4 digits, 16 bits */
	  0xFFFFF,	/* 5 digits, 20 bits */
	  0xFFFFFF,	/* 6 digits, 24 bits */
	  0xFFFFFFF,/* 7 digits, 28 bits */
	  0xFFFFFFFF
	};
	static unsigned int digits_major[] = {
      0,
	  9,
	  99,
	  999, 		/* 3 digits,  9 bits */
	  9999,		/* 4 digits, 13 bits */
	  99999,	/* 5 digits, 16 bits */
	  999999,	/* 6 digits, 19 bits */
	  9999999,	/* 7 digits, 23 bits */
	  99999999, /* 8 digits, 26 bits */
	  999999999
	};
	static int minor_len = MIN_MINOR_DIGITS;
	static int major_len = FE_SIZE_DEV_STR - MIN_MINOR_DIGITS - 2;
	int minor_of;	/* overflow */

	/* determine the major digits / minor digits split */
	while ( (minor_of = dev_minor > digits_minor[minor_len])
	  && minor_len < MAX_MINOR_DIGITS) {
		minor_len++;
		major_len--;
		mm_change = 1;
	}

	/* print major */
	if (dev_major > digits_major[major_len])
		sprintf(str,"%*s",major_len,"..");
	else
		sprintf(str,"%*d",major_len,dev_major);

	/* print minor */
	if (minor_of)
		sprintf(str + major_len,":..%0*X",
		  minor_len - 2,dev_minor & digits_minor[minor_len - 2]);
	else
		sprintf(str + major_len,":%0*X",minor_len,dev_minor);
}

int
stat2type(mode_t mode, uid_t uid)
{
	if (S_ISREG(mode)) {
		if ( (mode & S_IXUSR) != S_IXUSR
		  && (mode & S_IXGRP) != S_IXGRP
		  && (mode & S_IXOTH) != S_IXOTH)
			return FT_PLAIN_FILE;
		if ((mode & S_ISUID) == S_ISUID)
			return uid ? FT_PLAIN_SUID : FT_PLAIN_SUID_ROOT;
		if ((mode & S_ISGID) == S_ISGID)
			return FT_PLAIN_SGID;
		return FT_PLAIN_EXEC;
	}
	if (S_ISDIR(mode))
		return FT_DIRECTORY;
	if (S_ISBLK(mode))
		return FT_DEV_BLOCK;
	if (S_ISCHR(mode))
		return FT_DEV_CHAR;
	if (S_ISFIFO(mode))
		return FT_FIFO;
#ifdef S_ISSOCK
	if (S_ISSOCK(mode))
		return FT_SOCKET;
#endif
	return FT_OTHER;
}

/* format for the file panel */
static void
id2name(wchar_t *dst, int leftalign, const wchar_t *name, unsigned int id)
{
	int i, len, pad;
	wchar_t number[16];

	if (check_str(name) < 0) {
		swprintf(number,ARRAY_SIZE(number),L"%u",id);
		name = number;
	}

	len = wcslen(name);
	pad = FE_NAME_STR - 1 - len;
	if (pad >= 0) {
		if (!leftalign)
			for (i = 0 ; i < pad; i++)
				*dst++ = L' ';
		wcscpy(dst,name);
		if (leftalign) {
			dst += len;
			for (i = 0 ; i < pad; i++)
				*dst++ = L' ';
			*dst = L'\0';
		}
	}
	else {
		for (i = 0 ; i < (FE_NAME_STR - 1) / 2; i++)
			*dst++ = name[i];
		*dst++ = L'>';
		for (i = len - FE_NAME_STR / 2 + 1; i <= len ;i++)
			*dst++ = name[i];
	}
}

static const wchar_t *
uid2name(uid_t uid)
{
	static int pos = 0, replace = 0;
	static struct {
		uid_t uid;
		wchar_t name[FE_NAME_STR];
	} cache[CACHE_SIZE];

	if (pos < ucache_cnt && uid == cache[pos].uid)
		return cache[pos].name;

	for (pos = 0; pos < ucache_cnt; pos++)
		if (uid == cache[pos].uid)
			return cache[pos].name;

	if (ucache_cnt < CACHE_SIZE)
		pos = ucache_cnt++;
	else
		pos = replace = (replace + CACHE_REPL) % CACHE_SIZE;
	cache[pos].uid = uid;
	id2name(cache[pos].name,0,lookup_login(uid),(unsigned int)uid);

	return cache[pos].name;
}

static const wchar_t *
gid2name(gid_t gid)
{
	static int pos = 0, replace = 0;
	static struct {
		gid_t gid;
		wchar_t name[FE_NAME_STR];
	} cache[CACHE_SIZE];

	if (pos < gcache_cnt && gid == cache[pos].gid)
		return cache[pos].name;

	for (pos = 0; pos < gcache_cnt; pos++)
		if (gid == cache[pos].gid)
			return cache[pos].name;

	if (gcache_cnt < CACHE_SIZE)
		pos = gcache_cnt++;
	else
		pos = replace = (replace + CACHE_REPL) % CACHE_SIZE;
	cache[pos].gid = gid;
	id2name(cache[pos].name,1,lookup_group(gid),(unsigned int)gid);

	return cache[pos].name;
}

static void
stat2owner(wchar_t *str, uid_t uid, gid_t gid)
{
		wcscpy(str,uid2name(uid));
		str[FE_NAME_STR - 1] = L':';
		wcscpy(str + FE_NAME_STR,gid2name(gid));
}

static void
stat2links(char *str, nlink_t nlink)
{
	if (nlink <= 999)
		sprintf(str,"%3d",(int)nlink);
	else
		strcpy(str,"max");
}

/*
 * get the extension "ext" from "file.ext"
 * an exception: ".file" is a hidden file without an extension
 */
static const char *
get_ext(const char *filename)
{
	const char *ext;
	char ch;

	if (*filename++ == '\0')
		return "";

	for (ext = ""; (ch = *filename); filename++)
		if (ch == '.')
			ext = filename + 1;
	return ext;
}

/* this file does exist, but no other information is available */
static void
nofileinfo(FILE_ENTRY *pfe)
{
	pfe->mtime = 0;
	pfe->size = 0;
	pfe->extension = get_ext(SDSTR(pfe->file));
	pfe->file_type = FT_NA;
	pfe->size_str[0] = '\0';
	pfe->atime_str[0] = L'\0';
	pfe->mtime_str[0] = L'\0';
	pfe->ctime_str[0] = L'\0';
	pfe->links_str[0] = '\0';
	pfe->age_str[0] = '\0';
	pfe->links = 0;
	pfe->mode_str[0] = '\0';
	pfe->normal_mode = 1;
	pfe->owner_str[0] = L'\0';
}

/* fill-in all required information about a file */
static void
fileinfo(FILE_ENTRY *pfe, struct stat *pst)
{
	pfe->mtime = pst->st_mtime;
	pfe->size = pst->st_size;
	pfe->extension = get_ext(SDSTR(pfe->file));
	pfe->file_type = stat2type(pst->st_mode,pst->st_uid);
	if (IS_FT_DEV(pfe->file_type))
#ifdef HAVE_STRUCT_STAT_ST_RDEV
		pfe->devnum = pst->st_rdev;
#else
		pfe->devnum = 0;
#endif
	/* special case: active mounting point */
	if (pfe->file_type == FT_DIRECTORY && !pfe->symlink
	  && !pfe->dotdir && pst->st_dev != dirdev)
		pfe->file_type = FT_DIRECTORY_MNT;

	if (do_a)
		stat2time(pfe->atime_str,pst->st_atime);
	if (do_d)
		stat2time(pfe->mtime_str,pst->st_mtime);
	if (do_g)
		stat2age(pfe->age_str,pst->st_mtime);
	if (do_i)
		stat2time(pfe->ctime_str,pst->st_ctime);
	if (do_l)
		stat2links(pfe->links_str,pst->st_nlink);
	if (do_L)
		pfe->links = pst->st_nlink > 1 && !IS_FT_DIR(pfe->file_type);
	pfe->mode12 = pst->st_mode & 07777;
	if (do_m) {
		sprintf(pfe->mode_str,"%04o",pfe->mode12);
		if (do_m_blank) {
			if (S_ISREG(pst->st_mode))
				pfe->normal_mode = pfe->mode12 == normal_file
				  || pfe->mode12 == normal_dir /* same as exec */;
			else if (S_ISDIR(pst->st_mode))
				pfe->normal_mode = pfe->mode12 == normal_dir;
			else
				pfe->normal_mode = pfe->mode12 == normal_file;
		}
	}
	pfe->uid = pst->st_uid;
	pfe->gid = pst->st_gid;
	if (do_o)
		stat2owner(pfe->owner_str,pst->st_uid,pst->st_gid);
	if (do_s) {
		if (IS_FT_DEV(pfe->file_type))
			stat2dev(pfe->size_str,major(pfe->devnum),minor(pfe->devnum));
		else if (do_s_nodir && IS_FT_DIR(pfe->file_type))
			/* stat2size_blank() - blank x FE_SIZE_DEV_STR */
			strcpy(pfe->size_str,"           ");
		else
			(do_s_short ? stat2size_3 : stat2size_7)
			  (pfe->size_str,pst->st_size);
	}
}

/* set column widths */
static void
set_cw(void)
{
	int i, mod, lns, lnh, ln1, sz1, ow1, sz2, ow2, age;
	FILE_ENTRY *pfe;

	mod = do_m_blank;
	lns = do_gt;
	lnh = do_L;
	age = FE_AGE_STR - 2;
	ln1 = FE_LINKS_STR - 3;
	sz1 = FE_SIZE_DEV_STR - 4;
	ow1 = FE_NAME_STR - 3;
	sz2 = FE_SIZE_DEV_STR - 3;
	ow2 = FE_NAME_STR + 1;

	for (i = 0; i < ppanel_file->all_cnt; i++) {
		pfe = ppanel_file->all_files[i];
		if (mod && !pfe->normal_mode)
			mod = 0;
		if (lns && pfe->symlink)
			lns = 0;
		if (lnh && pfe->links)
			lnh = 0;
		if (do_g && *pfe->age_str)
			while (age >= 0 && pfe->age_str[age] != ' ')
				age--;
		if (do_l && *pfe->links_str)
			while (ln1 >= 0 && pfe->links_str[ln1] != ' ')
				ln1--;
		if (do_s && *pfe->size_str) {
			while (sz1 >= 0 && pfe->size_str[sz1] != ' ')
				sz1--;
			while (sz2 < FE_SIZE_DEV_STR - 1 && pfe->size_str[sz2] != ' ')
				sz2++;
		}
		if (do_o && *pfe->owner_str) {
			while (ow1 >= 0 && pfe->owner_str[ow1] != L' ')
				ow1--;
			while (ow2 < FE_OWNER_STR - 1 && pfe->owner_str[ow2] != L' ')
				ow2++;
		}
	}

	if (sz2 < FE_SIZE_DEV_STR - 1 || ow2 < FE_OWNER_STR - 1)
		for (i = 0; i < ppanel_file->all_cnt; i++) {
			pfe = ppanel_file->all_files[i];
			/* one of these assignments might be superfluous */
			pfe->size_str[sz2]  = '\0';
			pfe->owner_str[ow2] = '\0';
		}

	ppanel_file->cw_mod = mod ? 0 : FE_MODE_STR - 1;
	ppanel_file->cw_lns = lns ? 0 : 2;	/* strlen("->")  */
	ppanel_file->cw_lnh = lnh ? 0 : 3;	/* strlen("LNK") */
	ppanel_file->cw_ln1 = ln1 + 1;
	ppanel_file->cw_sz1 = sz1 + 1;
	ppanel_file->cw_ow1 = ow1 + 1;
	ppanel_file->cw_age = age + 1;
	ppanel_file->cw_sz2 = sz2 - sz1 - 1;
	ppanel_file->cw_ow2 = ow2 - ow1 - 1;
}

/* build the FILE_ENTRY '*pfe' describing the file named 'name' */
static int
describe_file(const char *name, FILE_ENTRY *pfe)
{
	struct stat stdata;

	if (use_pathname)
		name = pathname_join(name);

	if (lstat(name,&stdata) < 0) {
		if (errno == ENOENT)
			return -1;		/* file deleted in the meantime */
		pfe->symlink = 0;
		nofileinfo(pfe);
		return 0;
	}

	if ( (pfe->symlink = S_ISLNK(stdata.st_mode)) ) {
		if (us_readlink(&pfe->link,name) < 0) {
			us_copy(&pfe->link,"??");
			usw_copy(&pfe->linkw,L"??");
		}
		else
			usw_convert2w(USTR(pfe->link),&pfe->linkw);
		/* need stat() instead of lstat() */
		if (stat(name,&stdata) < 0) {
			nofileinfo(pfe);
			return 0;
		}
	}

	fileinfo(pfe,&stdata);
	return 0;
}

#define DOT_NONE		0	/* not a .file */
#define DOT_DIR			1	/* dot directory */
#define DOT_DOT_DIR		2	/* dot-dot directory */
#define DOT_HIDDEN		3	/* hidden .file */

static int
dotfile(const char *name)
{
	if (name[0] != '.')
		return DOT_NONE;
	if (name[1] == '\0')
		return DOT_DIR;
	if (name[1] == '.' && name[2] == '\0')
		return DOT_DOT_DIR;
	return DOT_HIDDEN;
}

static void
directory_read(void)
{
	int i, cnt1, cnt2;
	DIR *dd;
	FILE_ENTRY *pfe;
	FLAG hide;
	struct stat st;
	struct dirent *direntry;
	const char *name;

	name = USTR(ppanel_file->dir);
	if (stat(name,&st) < 0 || (dd = opendir(name)) == 0) {
		ppanel_file->all_cnt = ppanel_file->pd->cnt = 0;
		ppanel_file->selected = ppanel_file->selected_out = 0;
		msgout(MSG_w,"FILE LIST: cannot list the contents of the directory");
		return;
	}
	dirdev = st.st_dev;

	win_waitmsg();
	mm_change = future = 0;
	td_fmt_fail = 0;
	ppanel_file->hidden = 0;
	hide = ppanel_file->hide == HIDE_ALWAYS
		   || (ppanel_file->hide == HIDE_HOME
			   && strcmp(USTR(ppanel_file->dir),user_data.homedir) == 0);

	/*
	 * step #1: process selected files already listed in the panel
	 * in order not to lose their selection mark
	 */
	cnt1 = 0;
	ppanel_file->selected += ppanel_file->selected_out;
	ppanel_file->selected_out = 0;
	for (i = 0; cnt1 < ppanel_file->selected; i++) {
		pfe = ppanel_file->all_files[i];
		if (!pfe->select)
			continue;
		name = SDSTR(pfe->file);
		if ((hide && dotfile(name) == DOT_HIDDEN) || describe_file(name, pfe) < 0)
			/* this entry is no more valid */
			ppanel_file->selected--;
		else {
			/* OK, move it to the end of list we have so far */
			/* by swapping pointers: [cnt1] <--> [i] */
			ppanel_file->all_files[i] = ppanel_file->all_files[cnt1];
			ppanel_file->all_files[cnt1] = pfe;
			cnt1++;
		}
	}

	/* step #2: add data about new files */
	cnt2 = cnt1;
	while ( (direntry = readdir(dd)) ) {
		name = direntry->d_name;
		if (hide && dotfile(name) == DOT_HIDDEN) {
			ppanel_file->hidden = 1;
			continue;
		}

		/* didn't we process this file already in step #1 ? */
		if (cnt1) {
			for (i = 0; i < cnt1; i++)
				if (strcmp(SDSTR(ppanel_file->all_files[i]->file),name) == 0)
					break;
			if (i < cnt1)
				continue;
		}

		/* allocate new bunch of FILE_ENTRies if needed */
		if (cnt2 == ppanel_file->all_alloc) {
			ppanel_file->all_alloc += FE_ALLOC_UNIT;
			ppanel_file->all_files = erealloc(ppanel_file->all_files,
			  ppanel_file->all_alloc * sizeof(FILE_ENTRY *));
			pfe = emalloc(FE_ALLOC_UNIT * sizeof(FILE_ENTRY));
			for (i = 0; i < FE_ALLOC_UNIT; i++) {
				SD_INIT(pfe[i].file);
				SD_INIT(pfe[i].filew);
				US_INIT(pfe[i].link);
				US_INIT(pfe[i].linkw);
				ppanel_file->all_files[cnt2 + i] = pfe + i;
			}
		}

		pfe = ppanel_file->all_files[cnt2];
		sd_copy(&pfe->file,name);
		sdw_copy(&pfe->filew,convert2w(name));
		pfe->dotdir = dotfile(name);
		if (pfe->dotdir == DOT_HIDDEN)
			pfe->dotdir = DOT_NONE;
		if (describe_file(name, pfe) < 0)
			continue;
		pfe->select = 0;
		cnt2++;
	}
	ppanel_file->all_cnt = cnt2;

	closedir(dd);

	if (mm_change)
		for (i = 0; i < cnt2; i++) {
			pfe = ppanel_file->all_files[i];
			if (IS_FT_DEV(pfe->file_type))
				stat2dev(pfe->size_str,major(pfe->devnum),minor(pfe->devnum));
		}

	if (td_fmt_fail) {
		msgout(MSG_NOTICE,"Time/date format \"%ls\" produces output of variable length, "
		  "check the configuration",td_fmt_fail);
		msgout(MSG_w,"Problem with date/time output format, details in log");
	}
	if (future && !NOPT(NOTIF_FUTURE))
		msgout(MSG_i | MSG_NOTIFY,"FILE LIST: timestamp in the future encountered");

	set_cw();
}

/* invalidate file panel contents after a directory change */
void
filepanel_reset(void)
{
	ppanel_file->all_cnt = ppanel_file->pd->cnt = 0;
	ppanel_file->selected = ppanel_file->selected_out = 0;
	ppanel_file->order = panel_sort.order;
	ppanel_file->group = panel_sort.group;
	ppanel_file->hide = panel_sort.hide;
}

void
file_panel_data(void)
{
	const wchar_t *filter;
	static USTRINGW dequote = UNULL;
	FILE_ENTRY *pfe, *curs;
	int i, j, selected_in, selected_out;
	FLAG type;	/* type 0 = substring, type 1 = pattern */

	if (ppanel_file->all_cnt == 0) {
		/* panel is empty */
		ppanel_file->pd->cnt  = 0;
		return;
	}

	if (!ppanel_file->pd->filtering || ppanel_file->pd->filter->size == 0) {
		/* panel is not filtered */
		ppanel_file->pd->cnt  = ppanel_file->all_cnt;
		ppanel_file->selected += ppanel_file->selected_out;
		ppanel_file->selected_out = 0;
		if (ppanel_file->files != ppanel_file->all_files) {
			if (ppanel_file->files == 0 /* start-up */ || !VALID_CURSOR(ppanel_file->pd)) {
				ppanel_file->files = ppanel_file->all_files;
				return;
			}
			curs = ppanel_file->files[ppanel_file->pd->curs];
			ppanel_file->files = ppanel_file->all_files;
			for (i = 0; i < ppanel_file->all_cnt; i++)
				if (ppanel_file->all_files[i] == curs) {
					ppanel_file->pd->curs = i;
					break;
				}
		}
		return;
	}

	/* panel is filtered */
	if (ppanel_file->filt_alloc < ppanel_file->all_cnt) {
		efree(ppanel_file->filt_files);
		ppanel_file->filt_files
		  = emalloc((ppanel_file->filt_alloc = ppanel_file->all_cnt) * sizeof(FILE_ENTRY *));
	}

	filter = ppanel_file->pd->filter->line;
	type = ispattern(filter);
	if (ppanel_file->filtype != type) {
		ppanel_file->filtype = type;
		win_filter();
	}
	if (type)
		match_pattern_set(filter);
	else {
		if (isquoted(filter)) {
			usw_dequote(&dequote,filter,wcslen(filter));
			filter = USTR(dequote);
		}
		match_substr_set(filter);
	}

	curs = VALID_CURSOR(ppanel_file->pd) ? ppanel_file->files[ppanel_file->pd->curs] : 0;
	for (i = j = selected_in = selected_out = 0; i < ppanel_file->all_cnt; i++) {
		pfe = ppanel_file->all_files[i];
		if (pfe == curs)
			ppanel_file->pd->curs = j;
		if ((FOPT(FOPT_SHOWDIR) && IS_FT_DIR(pfe->file_type))
		  || (type ? match_pattern(SDSTR(pfe->file)) : match_substr(SDSTR(pfe->filew)))
		  || (pfe->symlink &&
		  (type ? match_pattern(USTR(pfe->link)) : match_substr(USTR(pfe->linkw))))) {
			ppanel_file->filt_files[j++] = pfe;
			if (pfe->select)
				selected_in++;
		}
		else if (pfe->select)
			selected_out++;		/* selected, but filtered out */
	}
	ppanel_file->pd->cnt = j;
	ppanel_file->selected = selected_in;
	ppanel_file->selected_out = selected_out;
	ppanel_file->files = ppanel_file->filt_files;
}

static void
filepanel_read(void)
{
	filepos_save();
	directory_read();
	sort_files();
	/* sort_files() calls file_panel_data() */
	filepos_set();
	ppanel_file->timestamp = now;
	ppanel_file->expired = 0;
}

int
list_directory_cond(int expiration_time)
{
	now = time(0);

	/* password data change invalidates data in both panels */
	if (userdata_refresh()) {
		ppanel_file->other->expired = 1;
		ucache_cnt = gcache_cnt = 0;
	}
	else if (expiration_time && now < ppanel_file->timestamp + expiration_time)
		return -1;

	now_day = localtime(&now)->tm_mday;
	filepanel_read();
	return 0;
}

void
list_directory(void)
{
	list_directory_cond(0);
}

void
list_both_directories(void)
{
	list_directory();

	/*
	 * warning: during the re-reading of the secondary panel it becomes the primary panel,
	 * but it does not correspond with the current working directory
	 */
	ppanel_file = ppanel_file->other;
	pathname_set_directory(USTR(ppanel_file->dir));
	use_pathname = 1;	/* must prepend directory name */
	filepanel_read();
	use_pathname = 0;
	ppanel_file = ppanel_file->other;
}
