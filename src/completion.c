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
#include <ctype.h>		/* tolower() */
#include <dirent.h>		/* readdir() */
#include <errno.h>		/* errno */
#include <stdarg.h>		/* log.h */
#include <stdlib.h>		/* qsort() */
#include <stdio.h>		/* EOF */
#include <string.h>		/* strerror() */
#include <time.h>		/* time() */
#include <unistd.h>		/* stat() */
#include <wctype.h>		/* iswalnum() */

#include "completion.h"

#include "cfg.h"		/* cfg_num() */
#include "control.h"	/* control_loop() */
#include "edit.h"		/* edit_update() */
#include "history.h"	/* get_history_entry() */
#include "inout.h"		/* win_waitmsg() */
#include "lex.h"		/* usw_dequote() */
#include "list.h"		/* stat2type() */
#include "log.h"		/* msgout() */
#include "match.h"		/* match_substr() */
#include "mbwstring.h"	/* convert2w() */
#include "sort.h"		/* num_wcscoll() */
#include "userdata.h"	/* username_find() */
#include "util.h"		/* emalloc() */

/* internal use only */
#define COMPL_TYPE_PATHCMD	100	/* search $PATH */
#define COMPL_TYPE_USERDIR	101	/* with trailing slash (~name/) */
#define COMPL_TYPE_ENV2		102	/* with trailing curly brace ${env} */

/* commands are stored in several linked lists depending on the first character */
#define LIST_NR(ch) ((ch) >= L'a' && (ch) <= L'z' ? (ch) - L'a' : 26)
#define LISTS		27
typedef struct cmd {
	SDSTRING  cmd;			/* command name */
	SDSTRINGW cmdw;			/* command name */
	struct cmd *next;		/* linked list ptr */
} CMD;
/* PATHDIR holds info about commands in a PATH member directory */
typedef struct {
	const char *dir;		/* PATH directory name */
	const wchar_t *dirw;	/* PATH directory name */
	time_t timestamp;		/* time of last successfull directory scan, or 0 */
	dev_t device;			/* device/inode from stat() */
	ino_t inode;
	CMD *commands[LISTS];	/* lists of commands in this directory */
} PATHDIR;

static PATHDIR *pd_list;	/* PATHDIRs for all members of the PATH search list */
static int pd_cnt = 0;		/* number od PATHDIRs in pd_list */

#define QFL_NONE 0
#define QFL_INQ	 1	/* inside quotes '...' or "..." */
#define QFL_MDQ	 2	/* missing closing double quote */
#define QFL_MSQ	 3	/* missing closing single quote */
/* input: completion request data */
static struct {
	CODE type;			/* type of completion - one of COMPL_TYPE_XXX */
	const wchar_t *str;	/* string to be completed */
	int strlen;			/* length of 'str' */
	const wchar_t *dirw;/* directory to attempt the completion in */
	const char *dir;	/* multibyte version of 'dirw' if available or 0 */
	int qlevel;			/* quote level for names when inserting (one of QUOT_XXX in edit.h) */
	CODE qflags;		/* other quoting issues - one of QFL_XXX */
} rq;

/* output: completion results */
static struct {
	FLAG filenames;		/* names are filenames */
	int cnt;			/* number of completion candidates */
	int err;			/* errno value (if cnt is zero) */
	size_t clen;		/* how many characters following 'str'
						   are the same for all candidates */
} compl;
/* output: candidates */

static COMPL_ENTRY *cc_list = 0;	/* list of all candidates */
static int cc_alloc = 0;			/* max number of candidates in CC_LIST */

static FLAG unfinished;		/* completion not finished (partial success) */
extern char **environ;

/* environment in wchar */
typedef struct {
	const wchar_t *var;
	const wchar_t *val;
} ENW;
static ENW *enw;

static void
path_init(void)
{
	char *path, *p;
	int i, list;

	if ( (path = getenv("PATH")) == 0) {
		msgout(MSG_NOTICE,"There is no PATH environment variable");
		return;
	}

	/* split PATH to components */
	path = estrdup(path);
	for (pd_cnt = 1, p = path; *p; p++)
		if (*p == ':') {
			*p = '\0';
			pd_cnt++;
		}
	pd_list = emalloc(pd_cnt * sizeof(PATHDIR));

	for (i = 0, p = path; i < pd_cnt; i++) {
		pd_list[i].dir = *p ? p : ".";
		pd_list[i].dirw = ewcsdup(convert2w(pd_list[i].dir));
		pd_list[i].timestamp = 0;
		for (list = 0; list < LISTS; list++)
			pd_list[i].commands[list] = 0;
		while (*p++)
			;
	}
}

/* transform the environment into wide char strings */
void
environ_init(void)
{
	int i;
	char *var, *val;
	static USTRING buff = UNULL;

	for (i = 0; environ[i] != 0; i++)
		;
	enw = emalloc(sizeof(ENW) * (i + 1));
	for (i = 0; environ[i] != 0; i++) {
		for (var = val = us_copy(&buff,environ[i]); *val != '\0'; val++)
			if (*val == '=') {
				*val++ = '\0';
				break;
			}
		enw[i].var = ewcsdup(convert2w(var));
		enw[i].val = ewcsdup(convert2w(val));
	}
	enw[i].var = enw[i].val = 0;
}

void
compl_initialize(void)
{
	path_init();
	environ_init();
	compl_reconfig();
}

void
compl_reconfig(void)
{
	int i;

	if (cc_alloc > 0) {
		for (i = 0; i < cc_alloc; i++)
			sdw_reset(&cc_list[i].str);
		free(cc_list);
		free(panel_compl.cand);
	}

	cc_alloc = cfg_num(CFG_C_SIZE);
	cc_list = emalloc(cc_alloc * sizeof(COMPL_ENTRY));
	panel_compl.cand = emalloc(cc_alloc * sizeof(COMPL_ENTRY *));
	for (i = 0; i < cc_alloc; i++)
		SD_INIT(cc_list[i].str);
}

static int
qcmp(const void *e1, const void *e2)
{
	return (panel_sort.order == SORT_NAME_NUM ? num_wcscoll : wcscoll)(
	  SDSTR(((COMPL_ENTRY *)e1)->str),
	  SDSTR(((COMPL_ENTRY *)e2)->str));
}

/* simplified version of sort_group() found in sort.c */
enum FILETYPE_TYPE {
	FILETYPE_DIR, FILETYPE_BDEV, FILETYPE_CDEV, FILETYPE_OTHER, FILETYPE_PLAIN
};
static int
sort_group(int type)
{
	if (IS_FT_PLAIN(type))
		return FILETYPE_PLAIN;
	if (IS_FT_DIR(type))
		return FILETYPE_DIR;
	if (panel_sort.group == GROUP_DBCOP) {
		if (type == FT_DEV_CHAR)
			return FILETYPE_CDEV;
		if (type == FT_DEV_BLOCK)
			return FILETYPE_BDEV;
	}
	return FILETYPE_OTHER;
}

static int
qcmp_group(const void *e1, const void *e2)
{
	int cmp, group1, group2;

	group1 = sort_group(((COMPL_ENTRY *)e1)->file_type);
	group2 = sort_group(((COMPL_ENTRY *)e2)->file_type);
	cmp = group1 - group2;
	if (cmp)
		return cmp;
	return qcmp(e1,e2);
}

void
compl_panel_data(void)
{
	int i, j;
	COMPL_ENTRY *pcc, *curs;

	curs = VALID_CURSOR(panel_compl.pd) ? panel_compl.cand[panel_compl.pd->curs] : 0;
	if (panel_compl.pd->filtering)
		match_substr_set(panel_compl.pd->filter->line);

	for (i = j = 0; i < compl.cnt; i++) {
		pcc = &cc_list[i];
		if (pcc == curs)
			panel_compl.pd->curs = j;
		if (panel_compl.pd->filtering && !match_substr(SDSTR(pcc->str)))
			continue;
		panel_compl.cand[j++] = pcc;
	}
	panel_compl.pd->cnt = j;
}

int
compl_prepare(void)
{
	const wchar_t *title, *aux;
	static wchar_t msg[80];	/* win_sethelp() stores this ptr */

	if (compl.cnt > cc_alloc) {
		swprintf(msg,ARRAY_SIZE(msg),L"%d additional entries not shown (table full)",compl.cnt - cc_alloc);
		win_sethelp(HELPMSG_TMP,msg);
		compl.cnt = cc_alloc;
	}

	if (rq.type != COMPL_TYPE_HIST)
		qsort(cc_list,compl.cnt,sizeof(COMPL_ENTRY),
		  (compl.filenames && panel_sort.group) ? qcmp_group : qcmp);

	aux = 0;
	switch (rq.type) {
	case COMPL_TYPE_FILE:
		title = L"FILENAME COMPLETION";
		break;
	case COMPL_TYPE_DIR:
		title = L"DIRECTORY NAME COMPLETION";
		break;
	case COMPL_TYPE_PATHCMD:
		aux = L"found in: ";
		/* no break */
	case COMPL_TYPE_CMD:
		title = L"COMMAND NAME COMPLETION";
		break;
	case COMPL_TYPE_HIST:
		title = L"COMMAND COMPLETION FROM HISTORY";
		win_sethelp(HELPMSG_BASE,L"commands are listed in order of their execution");
		break;
	case COMPL_TYPE_GROUP:
		title = L"GROUP NAME COMPLETION";
		break;
	case COMPL_TYPE_USER:
	case COMPL_TYPE_USERDIR:
		aux = L"name/comment: ";
		title = L"USER NAME COMPLETION";
		break;
	case COMPL_TYPE_ENV:
	case COMPL_TYPE_ENV2:
		title = L"ENVIRONMENT VARIABLE COMPLETION";
		aux  = L"value: ";
		break;
	default:
		title = L"NAME COMPLETION";
	}
	panel_compl.title = title;
	panel_compl.aux = aux;
	panel_compl.filenames = compl.filenames;

	panel_compl.pd->filtering = 0;
	panel_compl.pd->curs = -1;
	compl_panel_data();
	panel_compl.pd->top = panel_compl.pd->min;
	panel_compl.pd->curs = rq.type == COMPL_TYPE_HIST ? 0 : panel_compl.pd->min;

	panel = panel_compl.pd;
	/* textline inherited from previous mode */
	return 0;
}

static void
register_candidate(const wchar_t *cand, int is_link, int file_type, const wchar_t *aux)
{
	int i;
	static const wchar_t *cand0;

	if (rq.type == COMPL_TYPE_PATHCMD)
		/* check for duplicates like awk in both /bin and /usr/bin */
		for (i = 0; i < compl.cnt && i < cc_alloc; i++)
			if (wcscmp(SDSTR(cc_list[i].str),cand) == 0)
				return;

	if (compl.cnt < cc_alloc) {
		sdw_copy(&cc_list[compl.cnt].str,cand);
		cc_list[compl.cnt].is_link   = is_link;
		cc_list[compl.cnt].file_type = file_type;
		cc_list[compl.cnt].aux       = aux;
	}

	if (compl.cnt == 0) {
		cand0 = SDSTR(cc_list[0].str); /* cand0 = cand; would be an error */
		compl.clen = wcslen(cand0) - rq.strlen;
	}
	else
		for (i = 0; i < compl.clen ; i++)
			if (cand[rq.strlen + i] != cand0[rq.strlen + i]) {
				compl.clen = i;
				break;
			}
	compl.cnt++;
}

static void
complete_environ(void)
{
	int i;

	for (i = 0; enw[i].var != 0 ; i++)
		if (rq.strlen == 0 || wcsncmp(enw[i].var,rq.str,rq.strlen) == 0)
			register_candidate(enw[i].var,0,0,enw[i].val);
}

static void
complete_history()
{
	int i;
	const HIST_ENTRY *ph;

	for (i = 0; (ph = get_history_entry(i)); i++)
		if (wcsncmp(USTR(ph->cmd),rq.str,rq.strlen) == 0)
			register_candidate(USTR(ph->cmd),0,0,
			  ph->failed ? L"this command failed last time" : 0);
}

static void
complete_username(void)
{
	const wchar_t *login, *fullname;

	username_find_init(rq.str,rq.strlen);
	while ( (login = username_find(&fullname)) )
		register_candidate(login,0,0,fullname);
}

static void
complete_groupname(void)
{
	const wchar_t *group;

	groupname_find_init(rq.str,rq.strlen);
	while ( (group = groupname_find()) )
		register_candidate(group,0,0,0);
}

static void
complete_file(void)
{
	FLAG is_link;
	CODE type;
	const char *path, *dir, *file;
	const wchar_t *filew;
	struct stat st;
	struct dirent *direntry;
	DIR *dd;
	static USTRING mbdir = UNULL;

	if (rq.dirw == 0) {
		/* special case: bare tilde */
		if (wcscmp(rq.str,L"~") == 0) {
			register_candidate(L"~",0,FT_DIRECTORY,0);
			return;
		}
		rq.dir = ".";
		rq.dirw = L".";
	}
	dir = rq.dir ? rq.dir : us_convert2mb(rq.dirw,&mbdir);
	if ( (dd = opendir(dir)) == 0) {
		compl.err = errno;
		return;
	}

	win_waitmsg();
	pathname_set_directory(dir);
	while ( ( direntry = readdir(dd)) ) {
		filew = convert2w(file = direntry->d_name);
		if (rq.strlen == 0) {
			if (file[0] == '.' && (file[1] == '\0' || (file[1] == '.' && file[2] == '\0')))
				continue;
		}
		else if (wcsncmp(filew,rq.str,rq.strlen))
			continue;

		if (lstat(path = pathname_join(file),&st) < 0)
			continue;		/* file just deleted ? */
		if ( (is_link = S_ISLNK(st.st_mode)) && stat(path,&st) < 0)
			type = FT_NA;
		else
			type = stat2type(st.st_mode,st.st_uid);

		if (rq.type == COMPL_TYPE_DIR && !IS_FT_DIR(type))
			continue;		/* must be a directory */
		if (rq.type == COMPL_TYPE_CMD && !IS_FT_DIR(type) && !IS_FT_EXEC(type))
			continue;		/* must be a directory or executable */
		if (rq.type == COMPL_TYPE_PATHCMD && !IS_FT_EXEC(type))
			continue;		/* must be an executable */

		register_candidate(filew,is_link,type,rq.type == COMPL_TYPE_PATHCMD ? rq.dirw : 0);
	}
	closedir(dd);
}

static void
pathcmd_refresh(PATHDIR *ppd)
{
	FLAG stat_ok;
	int list;
	const wchar_t *filew;
	struct dirent *direntry;
	struct stat st;
	DIR *dd;
	CMD *pc;

	/*
	 * fstat(dirfd()) instead of stat() followed by opendir() would be
	 * better, but dirfd() is not available on some systems
	 */

	stat_ok = stat(ppd->dir,&st) == 0;
	if (stat_ok && st.st_mtime < ppd->timestamp
	  && st.st_dev == ppd->device && st.st_ino == ppd->inode)
		return;

	/* clear all command lists */
	for (list = 0; list < LISTS; list++) {
		while ( (pc = ppd->commands[list]) ) {
			ppd->commands[list] = pc->next;
			sd_reset(&pc->cmd);
			sdw_reset(&pc->cmdw);
			free(pc);
		}
	}

	ppd->timestamp = time(0);
	if (!stat_ok || (dd = opendir(ppd->dir)) == 0) {
		ppd->timestamp = 0;
		msgout(MSG_NOTICE,"Command name completion routine cannot list "
		  "directory \"%s\" (member of $PATH): %s",ppd->dir,strerror(errno));
		return;
	}
	ppd->device = st.st_dev;
	ppd->inode  = st.st_ino;

	win_waitmsg();
	while ( (direntry = readdir(dd)) ) {
		filew = convert2w(direntry->d_name);
		list = LIST_NR(*filew);
		pc = emalloc(sizeof(CMD));
		SD_INIT(pc->cmd);
		SD_INIT(pc->cmdw);
		sd_copy(&pc->cmd,direntry->d_name);
		sdw_copy(&pc->cmdw,filew);
		pc->next = ppd->commands[list];
		ppd->commands[list] = pc;
	}
	closedir(dd);
}

static void
complete_pathcmd(void)
{
	FLAG is_link;
	CODE file_type;
	int i, list;
	const char *path;
	const wchar_t *filew;
	CMD *pc;
	PATHDIR *ppd;
	struct stat st;

	/* include subdirectories of the current directory */
	rq.type = COMPL_TYPE_DIR;
	complete_file();
	rq.type = COMPL_TYPE_PATHCMD;

	list = LIST_NR(*rq.str);
	for (i = 0; i < pd_cnt; i++) {
		ppd = &pd_list[i];
		if (*ppd->dir == '/') {
			/* absolute PATH directories are cached */
			pathcmd_refresh(ppd);
			pathname_set_directory(ppd->dir);
			for (pc = ppd->commands[list]; pc; pc = pc->next) {
				filew = SDSTR(pc->cmdw);
				if (wcsncmp(filew,rq.str,rq.strlen) != 0)
					continue;
				if (lstat(path = pathname_join(SDSTR(pc->cmd)),&st) < 0)
					continue;
				if ( (is_link = S_ISLNK(st.st_mode))
				  && stat(path,&st) < 0)
					continue;
				file_type = stat2type(st.st_mode,st.st_uid);
				if (!IS_FT_EXEC(file_type))
					continue;
				register_candidate(filew,is_link,file_type,ppd->dirw);
			}
		}
		else {
			/* relative PATH directories are impossible to cache */
			rq.dir  = ppd->dir;
			rq.dirw = ppd->dirw;
			complete_file();
		}
	}
}

static void
reset_results(void)
{
	compl.cnt = 0;
	compl.err = 0;
	compl.filenames = 0;
}

static void
complete_it(void)
{
	if (rq.type == COMPL_TYPE_ENV || rq.type == COMPL_TYPE_ENV2)
		complete_environ();
	else if (rq.type == COMPL_TYPE_USER || rq.type == COMPL_TYPE_USERDIR)
		complete_username();
	else if (rq.type == COMPL_TYPE_GROUP)
		complete_groupname();
	else if (rq.type == COMPL_TYPE_HIST)
		complete_history();
	else {
		compl.filenames = 1;
		if (rq.type == COMPL_TYPE_PATHCMD)
			complete_pathcmd();
		else
			/* FILE, DIR, CMD completion */
			complete_file();
	}
}

/* insert char 'ch' if it is not already there */
static void
condinsert(wchar_t ch)
{
	if (USTR(textline->line)[textline->curs] == ch)
		textline->curs++;
	else
		edit_nu_insertchar(ch);
}


static void
insert_candidate(COMPL_ENTRY *pcc)
{
	edit_nu_insertstr(SDSTR(pcc->str) + rq.strlen,rq.qlevel);

	if ((compl.filenames && IS_FT_DIR(pcc->file_type))
	  || rq.type == COMPL_TYPE_USERDIR /* ~user is a directory */ ) {
		unfinished = 1; /* a directory may have subdirectories */
		condinsert(L'/');
	}
	else {
		if (rq.type == COMPL_TYPE_ENV2)
			condinsert(L'}');

		if (rq.qflags == QFL_INQ)
			/* move over the closing quote */
			textline->curs++;
		else if (rq.qflags == QFL_MSQ)
			edit_nu_insertchar(L'\'');
		else if (rq.qflags == QFL_MDQ)
			edit_nu_insertchar(L'\"');
		else if (compl.filenames)
			condinsert(L' ');
	}

	edit_update();
}

static const char *
code2string(int type)
{
	switch (type) {
	case COMPL_TYPE_FILE:
		return "filename";
	case COMPL_TYPE_DIR:
		return "directory name";
	case COMPL_TYPE_PATHCMD:
	case COMPL_TYPE_CMD:
		return "command name";
	case COMPL_TYPE_HIST:
		return "command";
	case COMPL_TYPE_GROUP:
		return "group name";
	case COMPL_TYPE_USER:
	case COMPL_TYPE_USERDIR:
		return "user name";
	case COMPL_TYPE_ENV:
	case COMPL_TYPE_ENV2:
		return "environment variable";
	default:
		return "string";
	}
}

static void
show_results(void)
{
	static SDSTRINGW common = SDNULL(L"");

	if (compl.cnt == 0) {
		msgout(MSG_i,"cannot complete this %s (%s)",code2string(rq.type),
			compl.err == 0 ? "no match" : strerror(compl.err));
		return;
	}

	if (compl.cnt == 1) {
		insert_candidate(&cc_list[0]);
		return;
	}

	if (compl.clen) {
		/* insert the common part of all candidates */
		sdw_copyn(&common,SDSTR(cc_list[0].str) + rq.strlen,compl.clen);
		edit_insertstr(SDSTR(common),rq.qlevel);
		/*
		 * pretend that the string to be completed already contains
		 * the chars just inserted
		 */
		rq.strlen += compl.clen;
	}

	control_loop(MODE_COMPL);
}

#define ISAZ09(CH)				((CH) == L'_' || iswalnum(CH))

/*
 * compl_name(type) is a completion routine for alphanumerical strings
 * type is one of COMPL_TYPE_AUTO, ENV, GROUP, USER
 *
 * return value:
 *    0 = completion process completed (successfully or not)
 *   -1 = nothing to complete
 */
#define ISUGCHAR(CH)			((CH) == L'.' || (CH) == L',' || (CH) == L'-')
#define ISUGTYPE(T)				((T) == COMPL_TYPE_USER || (T) == COMPL_TYPE_GROUP)
#define TESTAZ09(POS)			(lex[POS] == LEX_PLAINTEXT \
	&& (ISAZ09(pline[POS]) || (ISUGTYPE(type) && ISUGCHAR(pline[POS])) ) )
static int
compl_name(int type)
{
	static USTRINGW str_buff = UNULL;
	const char *lex;
	const wchar_t *pline;
	int start, end;

	/* find start and end */
	pline = USTR(textline->line);
	lex = cmd2lex(pline);
	start = end = textline->curs;

	if (TESTAZ09(start))
		/* complete the name at the cursor */
		while (end++, TESTAZ09(end))
			;
	else if (panel_paste.wordstart || !TESTAZ09(start - 1))
		return -1;	/* nothing to complete */
	/* else complete the name immediately before the cursor */

	if (!panel_paste.wordstart)
		while (TESTAZ09(start - 1))
			start--;

	/* set the proper COMPL_TYPE */
	if (type == COMPL_TYPE_AUTO) {
		if (lex[start - 1] == LEX_PLAINTEXT && pline[start - 1] == L'~' && !IS_LEX_WORD(lex[start - 2]))
			type = COMPL_TYPE_USERDIR;
		else if (lex[start - 1] == LEX_VAR)
			type = pline[start - 1] == L'{' ? COMPL_TYPE_ENV2 : COMPL_TYPE_ENV;
		else
			return -1;	/* try compl_file() */
	}
	else if (type == COMPL_TYPE_ENV && lex[start - 1] == LEX_VAR && pline[start - 1] == L'{')
		type = COMPL_TYPE_ENV2;

	/* fill in the 'rq' struct */
	rq.qlevel = QUOT_NONE;
	rq.qflags = QFL_NONE;
	rq.type = type;
	rq.dir = 0;
	rq.dirw = 0;
	rq.strlen = end - start;
	rq.str = usw_copyn(&str_buff,pline + start,rq.strlen);

	/* move cursor to the end of the current word */
	textline->curs = end;
	edit_update_cursor();

	reset_results();
	complete_it();
	show_results();
	return 0;
}

/*
 * compl_file() attempts to complete the partial text in the command line
 * type is on of the COMPL_TYPE_AUTO, CMD, DIR, DIRPANEL, HISTORY, FILE, or DRYRUN
 *
 * return value:
 *    0  = completion process completed (successfully or not)
 *   -1, -2 = nothing to complete (current word is an empty string):
 *       -1 = first word (usually the command)
 *       -2 = not the first word (usually one of the arguments)
 *   -3 = could complete, but not allowed to (COMPL_TYPE_DRYRUN)
 */
static int
compl_file(int type)
{
	static USTRINGW dequote_str = UNULL, dequote_dir = UNULL;
	const char *lex;
	const wchar_t *p, *pslash, *pstart, *pend, *pline;
	wchar_t ch;
	int i, start, end, dirlen;
	FLAG tilde, wholeline, userdir;

	/*
	 * pline  -> the input line
	 * pstart -> start of the string to be completed
	 * pend   -> position immediately after the last character of that string
	 * pslash -> last slash '/' in the string (if any)
	 */
	pline = USTR(textline->line);

	wholeline = type == COMPL_TYPE_DIRPANEL || type == COMPL_TYPE_HIST;
	if (wholeline) {
		/* complete the whole line */
		rq.qlevel = QUOT_NONE;
		rq.qflags = QFL_NONE;
		start = 0;
		end = textline->size;
	}
	else {
		/* find the start and end */
		rq.qlevel = QUOT_NORMAL;
		rq.qflags = QFL_NONE;
		lex = cmd2lex(pline);
		start = end = textline->curs;
		if (IS_LEX_WORD(lex[start])) {
			/* complete the name at the cursor */
			while (end++, IS_LEX_WORD(lex[end]))
				;
		}
		else if (IS_LEX_WORD(lex[start - 1]) && !panel_paste.wordstart)
			; /* complete the name immediately before the cursor */
		else if ((IS_LEX_CMDSEP(lex[start - 1]) || IS_LEX_SPACE(lex[start - 1])
			|| panel_paste.wordstart) && IS_LEX_EMPTY(lex[start])) {
			/* there is no text to complete */
			for (i = start - 1; IS_LEX_SPACE(lex[i]); i--)
				;
			return IS_LEX_CMDSEP(lex[i]) ? -1 : -2;
		} else {
			/* the text at the cursor is not a name */
			msgout(MSG_i,"cannot complete a special symbol");
			return 0;
		}

		if (type == COMPL_TYPE_DRYRUN)
			return -3;

		if (!panel_paste.wordstart)
			while (IS_LEX_WORD(lex[start - 1]))
				start--;

		for (i = start; i < end; i++)
			if (lex[i] == LEX_VAR) {
				msgout(MSG_i,"cannot complete a name containing a $variable");
				return 0;
			}

		if (type == COMPL_TYPE_AUTO) {
			if (lex[start - 1] == LEX_OTHER) {
				msgout(MSG_i,"cannot complete after a special symbol");
				return 0;
			}

			/* find out what precedes the name to be completed */
			for (i = start - 1; IS_LEX_SPACE(lex[i]); i--)
				;
			type = IS_LEX_CMDSEP(lex[i]) ? COMPL_TYPE_CMD : COMPL_TYPE_FILE;

			/* special case - complete file in expressions like
				name:file, --opt=file or user@some.host:file */
			if (!panel_paste.wordstart && type == COMPL_TYPE_FILE && lex[i] != LEX_IO) {
				for (i = start; i < end; i++) {
					if (lex[i] != LEX_PLAINTEXT)
						break;
					ch = pline[i];
					if (i > start && i < textline->curs && (ch == L':' || ch == L'=')) {
						start = i + 1;
						if (start == end)
							return -2;
						break;
					}
					if (ch != L'.' && ch != L'-' && ch != L'@' && !ISAZ09(ch))
						break;
				}
			}
		}

		if (lex[end] == LEX_END_ERR_SQ) {
			rq.qlevel = QUOT_NONE;
			rq.qflags = QFL_MSQ;
		}
		else if (lex[end] == LEX_END_ERR_DQ) {
			rq.qlevel = QUOT_IN_QUOTES;
			rq.qflags = QFL_MDQ;
		}
		else if (lex[end - 1] == LEX_QMARK) {
			if ((ch = pline[end - 1]) == L'\'')
				rq.qlevel = QUOT_NONE;
			else if (ch == L'\"')
				rq.qlevel = QUOT_IN_QUOTES;
			rq.qflags = QFL_INQ;
		}
	}
	pstart = pline + start;
	pend = pline + end;

	pslash = 0;
	if (type != COMPL_TYPE_HIST)
		/* separate the name into the directory part and the file part */
		for (p = pend; p > pstart;)
			if (*--p == L'/') {
				pslash = p;
				break;
			}
	/* set rq.dirw */
	if (pslash == 0) {
		rq.str = pstart;
		rq.dirw = 0;
	}
	else {
		rq.str = pslash + 1;
		rq.dirw = pstart;

		dirlen = pslash - pstart;
		/* dequote 'dir' (if appropriate) + add terminating null byte */
		if (type != COMPL_TYPE_DIRPANEL && isquoted(rq.dirw)) {
			tilde = is_dir_tilde(rq.dirw);
			dirlen = usw_dequote(&dequote_dir,rq.dirw,dirlen);
		}
		else {
			tilde = *rq.dirw == L'~';
			usw_copyn(&dequote_dir,rq.dirw,dirlen);
		}
		rq.dirw = USTR(dequote_dir);
		if (dirlen == 0)
			/* first slash == last slash */
			rq.dirw = L"/";
		else if (tilde)
			rq.dirw = dir_tilde(rq.dirw);
	}
	rq.dir = 0;	/* will be converted from rq.dirw on demand */

	/* set the proper completion type */
	if (type == COMPL_TYPE_DIRPANEL) {
		if ( (userdir = *pstart == L'~') ) {
			for (i = 1; (ch = pstart[i]) != L'\0' && ch != L'/'; i++)
				if (!ISAZ09(ch)) {
					userdir = 0;
					break;
				}
			if (textline->curs > i)
				userdir = 0;
		}
		if (userdir) {
			rq.str = pstart + 1;
			pend = pstart + i;
			type = COMPL_TYPE_USERDIR;
		}
		else
			type = COMPL_TYPE_DIR;
	}
	else if (type == COMPL_TYPE_CMD && pslash == 0)
		type = COMPL_TYPE_PATHCMD;

	rq.strlen = pend - rq.str;
	/* dequote 'str' (if appropriate) + add terminating null byte */
	if (!wholeline && isquoted(rq.str)) {
		rq.strlen = usw_dequote(&dequote_str,rq.str,rq.strlen);
		rq.str = USTR(dequote_str);
	}
	else if (*pend != L'\0')
		rq.str = usw_copyn(&dequote_str,rq.str,rq.strlen);

	/* move cursor to the end of the current word */
	textline->curs = (pend - pline) - (rq.qflags == QFL_INQ /* 1 or 0 */);
	edit_update_cursor();

	rq.type = type;
	reset_results();
	complete_it();
	show_results();
	return 0;
}

int
compl_text(int type)
{
	if (textline->size == 0)
		return -1;

	if (get_current_mode() != MODE_PASTE)
		panel_paste.wordstart = 0;	/* valid in the completion/insertion panel only */

	if (type == COMPL_TYPE_AUTO)
		return compl_name(COMPL_TYPE_AUTO) == 0 ? 0 : compl_file(COMPL_TYPE_AUTO);

	if (type == COMPL_TYPE_ENV || type == COMPL_TYPE_GROUP || type == COMPL_TYPE_USER)
		return compl_name(type);

	return compl_file(type);
}

static void
complete_type(int type)
{
	int mode, curs, offset;

	curs = textline->curs;
	offset = textline->offset;
	mode = get_current_mode();

	unfinished = 0;
	if (compl_text(type) != 0)
		msgout(MSG_i,"there is nothing to complete");
	if (unfinished) {
		if (mode == MODE_PASTE && panel_paste.wordstart) {
			textline->curs = curs;
			if (textline->offset != offset)
				edit_update_cursor();
		}
	}
	else if (mode != MODE_FILE)
		next_mode = MODE_SPECIAL_RETURN;
}

void cx_complete_auto(void)	{ complete_type(COMPL_TYPE_AUTO);	}
void cx_complete_file(void)	{ complete_type(COMPL_TYPE_FILE);	}
void cx_complete_dir(void)	{ complete_type(COMPL_TYPE_DIR);	}
void cx_complete_cmd(void)	{ complete_type(COMPL_TYPE_CMD);	}
void cx_complete_user(void)	{ complete_type(COMPL_TYPE_USER);	}
void cx_complete_group(void){ complete_type(COMPL_TYPE_GROUP);	}
void cx_complete_env(void)	{ complete_type(COMPL_TYPE_ENV);	}
void cx_complete_hist(void)	{ complete_type(COMPL_TYPE_HIST);	}

void
cx_compl_wordstart(void)
{
	TOGGLE(panel_paste.wordstart);
	win_panel_opt();
}

void
cx_compl_enter(void)
{
	insert_candidate(panel_compl.cand[panel_compl.pd->curs]);
	next_mode = MODE_SPECIAL_RETURN;
}
