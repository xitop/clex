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

/*
 * Routines in userdata.c mostly provide access to data stored
 * in /etc/passwd and /etc/group. All required data is read
 * into memory to speed up lookups. The data returned by
 * lookup_xxx() is valid only until next data refresh, the
 * caller must copy the returned string value if necessary.
 */

#include "clexheaders.h"

#include <sys/stat.h>		/* stat() */
#ifdef HAVE_UNAME
# include <sys/utsname.h>	/* uname() */
#endif
#include <time.h>			/* time() */
#include <grp.h>			/* getgrent() */
#include <pwd.h>			/* getpwent() */
#include <stdarg.h>			/* log.h */
#include <stdio.h>			/* sprintf() */
#include <stdlib.h>			/* qsort() */
#include <string.h>			/* strlen() */
#include <unistd.h>			/* stat() */

#include "userdata.h"

#include "edit.h"			/* edit_insertchar() */
#include "filter.h"			/* cx_filter() */
#include "log.h"			/* msgout() */
#include "match.h"			/* match_substr() */
#include "mbwstring.h"		/* convert2w() */
#include "util.h"			/* emalloc() */

/*
 * cached user(group) records are re-read when:
 *  - /etc/passwd (/etc/group) file changes, or
 *  - the cache expires (EXPIRATION in seconds) to allow
 *    changes e.g. in NIS to get detected, or
 *  - explicitly requested
 */
#define EXPIRATION	300		/* 5 minutes */

typedef struct pwdata {
	struct pwdata *next;	/* tmp pointer */
	SDSTRINGW login;	
	SDSTRINGW homedir;
	SDSTRINGW gecos;
	uid_t uid;
} PWDATA;

typedef struct grdata {
	struct grdata *next;	/* tmp pointer */
	SDSTRINGW group;
	gid_t gid;
} GRDATA;

typedef struct {
	time_t timestamp;		/* when the data was obtained, or 0 */
	dev_t device;			/* device/inode for /etc/passwd */
	ino_t inode;
	int cnt;				/* # of entries */
	PWDATA **by_name;		/* sorted by name (for binary search, ignoring locale) */
	PWDATA **by_uid;		/* sorted by uid */
	PWDATA *ll;				/* linked list, unsorted */
} USERDATA;

typedef struct {
	time_t timestamp;		/* when the data was obtained, or 0 */
	dev_t device;			/* device/inode for /etc/group */
	ino_t inode;
	int cnt;				/* # of entries */
	GRDATA **by_name;		/* sorted by name (for binary search, ignoring locale) */
	GRDATA **by_gid;		/* sorted by gid */
	GRDATA *ll;				/* linked list, unsorted */
} GROUPDATA;

static USERDATA  utable;
static GROUPDATA gtable;

static time_t now;

static struct {
	const wchar_t *str;
	size_t len;
	int index;
} ufind, gfind;						/* used by user- and groupname_find() */

static int
qcmp_name(const void *e1, const void *e2)
{
	return wcscmp(	/* not wcscoll() */
	  SDSTR((*(PWDATA **)e1)->login),
	  SDSTR((*(PWDATA **)e2)->login));
}

static int
qcmp_uid(const void *e1, const void *e2)
{
	return CMP((*(PWDATA **)e1)->uid,(*(PWDATA **)e2)->uid);
}

static void
read_utable(void)
{
	int i, cnt;
	PWDATA *ud = 0 /* prevent compiler warning */, *old;
	struct passwd *pw;
	static FLAG err = 0;

	utable.timestamp = now;
	setpwent();
	for (old = utable.ll, cnt = 0; (pw = getpwent()); cnt++) {
		if (old) {
			/* use the old PWDATA struct */
			ud = old;
			old = ud->next;
		}
		else {
			/* create a new one */
			ud = emalloc(sizeof(PWDATA));
			ud->next = utable.ll;
			utable.ll = ud;
			SD_INIT(ud->login);
			SD_INIT(ud->homedir);
			SD_INIT(ud->gecos);
		}
		ud->uid = pw->pw_uid;
		sdw_copy(&ud->login,convert2w(pw->pw_name));
		sdw_copy(&ud->homedir,convert2w(pw->pw_dir));
		sdw_copy(&ud->gecos,convert2w(pw->pw_gecos));
	}
	endpwent();

	/* free unused PWDATA structs */
	if (cnt > 0)
		for (; old; old = ud->next) {
			ud->next = old->next;
			sdw_reset(&old->login);
			sdw_reset(&old->homedir);
			sdw_reset(&old->gecos);
			free(old);
		}

	if (utable.cnt != cnt && utable.by_name) {
		free(utable.by_name);
		free(utable.by_uid);
		utable.by_name = utable.by_uid = 0;
	}

	/* I was told using errno for error detection with getpwent() is not portable */
	if ((utable.cnt = cnt) == 0) {
		utable.timestamp = 0;
		if (!TSET(err))
			msgout(MSG_W,"USER ACCOUNTS: Cannot obtain user account data");
		return;
	}
	if (TCLR(err))
		msgout(MSG_W,"USER ACCOUNTS: User account data is now available");

	/* linked list -> two sorted arrays */
	if (utable.by_name == 0) {
		utable.by_name = emalloc(cnt * sizeof(PWDATA *));
		utable.by_uid  = emalloc(cnt * sizeof(PWDATA *));
		for (ud = utable.ll, i = 0; i < cnt; i++, ud = ud->next)
			utable.by_name[i] = utable.by_uid[i] = ud;
	}
	qsort(utable.by_name,cnt,sizeof(PWDATA *),qcmp_name);
	qsort(utable.by_uid ,cnt,sizeof(PWDATA *),qcmp_uid);
}

static int
qcmp_gname(const void *e1, const void *e2)
{
	return wcscmp(	/* not wcscoll() */
	  SDSTR((*(GRDATA **)e1)->group),
	  SDSTR((*(GRDATA **)e2)->group));
}

static int
qcmp_gid(const void *e1, const void *e2)
{
	return CMP((*(GRDATA **)e1)->gid,(*(GRDATA **)e2)->gid);
}

static void
read_gtable(void)
{
	int i, cnt;
	GRDATA *gd = 0 /* prevent compiler warning */, *old;
	struct group *gr;
	static FLAG err = 0;

	gtable.timestamp = now;
	setgrent();
	for (old = gtable.ll, cnt = 0; (gr = getgrent()); cnt++) {
		if (old) {
			/* use the old GRDATA struct */
			gd = old;
			old = gd->next;
		}
		else {
			/* create a new one */
			gd = emalloc(sizeof(GRDATA));
			gd->next = gtable.ll;
			gtable.ll = gd;
			SD_INIT(gd->group);
		}
		gd->gid = gr->gr_gid;
		sdw_copy(&gd->group,convert2w(gr->gr_name));
	}
	endgrent();

	/* free unused GRDATA structs */
	if (cnt > 0)
		for (; old; old = gd->next) {
			gd->next = old->next;
			sdw_reset(&old->group);
			free(old);
		}

	if (gtable.cnt != cnt && gtable.by_name) {
		free(gtable.by_name);
		free(gtable.by_gid);
		gtable.by_name = gtable.by_gid = 0;
	}

	if ((gtable.cnt = cnt) == 0) {
		gtable.timestamp = 0;
		if (!TSET(err))
			msgout(MSG_W,"USER ACCOUNTS: Cannot obtain user group data");
		return;
	}
	if (TCLR(err))
		msgout(MSG_W,"USER ACCOUNTS: User group data is now available");

	/* linked list -> sorted array */
	if (gtable.by_name == 0) {
		gtable.by_name = emalloc(cnt * sizeof(GRDATA *));
		gtable.by_gid  = emalloc(cnt * sizeof(GRDATA *));
		for (gd = gtable.ll, i = 0; i < cnt; i++, gd = gd->next)
			gtable.by_name[i] = gtable.by_gid[i] = gd;
	}
	qsort(gtable.by_name,cnt,sizeof(GRDATA *),qcmp_gname);
	qsort(gtable.by_gid ,cnt,sizeof(GRDATA *),qcmp_gid);
}

static int
shelltype(const char *shell)
{
	size_t len;

	len = strlen(shell);
	if (len >= 2 && shell[len - 2] == 's' && shell[len - 1] == 'h')
		return (len >= 3 && shell[len - 3] == 'c') ? SHELL_CSH : SHELL_SH;
	return SHELL_OTHER;
}

void
userdata_initialize(void)
{
	static char uidstr[24];
	static SDSTRING host = SDNULL("localhost");
	struct passwd *pw;
	const char *name, *xdg;
	uid_t myuid;

#ifdef HAVE_UNAME
	char ch, *pch, *pdot;
	FLAG ip;
	struct utsname ut;

	uname(&ut);
	sd_copy(&host,ut.nodename);

	/* strip the domain part */
	for (ip = 1, pdot = 0, pch = SDSTR(host); (ch = *pch); pch++) {
		if (ch == '.') {
			if (pdot == 0)
				pdot = pch;
		}
		else if (ch < '0' || ch > '9')
			ip = 0;	/* this is a name and not an IP address */
		if (!ip && pdot != 0) {
			*pdot = '\0';
			break;
		}
	}
#endif
	user_data.host  = SDSTR(host);
	user_data.hostw = ewcsdup(convert2w(user_data.host));

	user_data.nowrite = 0;

	msgout(MSG_AUDIT,"CLEX version: \""VERSION "\"");
	msgout(MSG_HEADING,"Examining data of your account");

	myuid = getuid();
	if ((pw = getpwuid(myuid)) == 0) {
		sprintf(uidstr,"%d",(int)myuid);
		msgout(MSG_W,"Cannot find your account (UID=%s)"
		  " in the user database",uidstr);
		sprintf(uidstr,"UID_%d",(int)myuid);
		user_data.login = uidstr;
		user_data.nowrite = 1;
	}
	else
		user_data.login = estrdup(pw->pw_name);
	user_data.loginw = ewcsdup(convert2w(user_data.login));

	if (checkabs(name = getenv("SHELL")))
		user_data.shell = name;
	else if (pw && checkabs(pw->pw_shell))
		user_data.shell = estrdup(pw->pw_shell);
	else {
		msgout(MSG_W,"Cannot obtain the name of your shell program; using \"/bin/sh\"");
		user_data.shell = "/bin/sh";
	}
	name = base_name(user_data.shell);
	user_data.shellw = ewcsdup(convert2w(name));
	user_data.shelltype = shelltype(name);
	msgout(MSG_AUDIT,"Command interpreter: \"%s\"",user_data.shell);

	if (checkabs(name = getenv("HOME"))) {
		user_data.homedir = name;
		if (strcmp(name,"/") == 0) {
			if (pw && *pw->pw_dir && strcmp(pw->pw_dir,"/") != 0) {
				msgout(MSG_W,"Your home directory is the root directory, "
				  "but according to the password file it should be \"%s\"",pw->pw_dir);
				user_data.nowrite = 1;
			}
		}
	}
	else if (pw && checkabs(pw->pw_dir))
		user_data.homedir = estrdup(pw->pw_dir);
	else {
		msgout(MSG_W,"Cannot obtain the name of your home directory; using \"/\"");
		user_data.homedir = "/";
		user_data.nowrite = 1;
	}

	if (!user_data.nowrite && strcmp(user_data.homedir,"/") == 0 && myuid != 0) {
		msgout(MSG_W,"Your home directory is the root directory, but you are not root");
		user_data.nowrite = 1;
	}

	user_data.homedirw = ewcsdup(convert2w(user_data.homedir));
	msgout(MSG_DEBUG,"Home directory: \"%s\"",user_data.homedir);

	if (user_data.nowrite)
		msgout(MSG_W,"Due to the problem reported above CLEX will not save any "
		  "data to disk. This includes configuration, options and bookmarks");

	user_data.isroot = geteuid() == 0;  /* 0 or 1 */;

	xdg = getenv("XDG_CONFIG_HOME");
	if (xdg && *xdg) {
		pathname_set_directory(xdg);
		user_data.subdir = estrdup(pathname_join("clex"));
	}
	else {
		pathname_set_directory(user_data.homedir);
		user_data.subdir = estrdup(pathname_join(".config/clex"));
	}
	msgout(MSG_DEBUG,"Configuration directory: \"%s\"",user_data.subdir);
	pathname_set_directory(user_data.subdir);
	user_data.file_cfg = estrdup(pathname_join("config"));
	user_data.file_opt = estrdup(pathname_join("options"));
	user_data.file_bm  = estrdup(pathname_join("bookmarks"));

	msgout(MSG_HEADING,0);
}

void
userdata_expire(void)
{
	 utable.timestamp = gtable.timestamp = 0;
}

/* returns 1 if data was re-read, 0 if unchanged */
int
userdata_refresh(void)
{
	FLAG stat_ok;
	int reloaded;
	struct stat st;

	reloaded = 0;

	now = time(0);
	stat_ok = stat("/etc/passwd",&st) == 0;
	if (!stat_ok || st.st_mtime >= utable.timestamp
	  || st.st_dev != utable.device || st.st_ino != utable.inode
	  || now > utable.timestamp + EXPIRATION ) {
		read_utable();
		utable.device = stat_ok ? st.st_dev : 0;
		utable.inode  = stat_ok ? st.st_ino : 0;
		reloaded = 1;
	}

	stat_ok = stat("/etc/group",&st) == 0;
	if (!stat_ok || st.st_mtime >= gtable.timestamp
	  || st.st_dev != gtable.device || st.st_ino != gtable.inode
      || now > gtable.timestamp + EXPIRATION) {
		read_gtable();
		gtable.device = stat_ok ? st.st_dev : 0;
		gtable.inode  = stat_ok ? st.st_ino : 0;
		reloaded = 1;
	}

	return reloaded;
}

/* simple binary search algorithm */
#define BIN_SEARCH(COUNT,CMPFUNC,RETVAL) \
	{ \
		int min, med, max, cmp; \
		for (min = 0, max = COUNT - 1; min <= max; ) { \
			med = (min + max) / 2; \
			cmp = CMPFUNC; \
			if (cmp == 0) \
				return RETVAL; \
			if (cmp < 0) \
				max = med - 1; \
			else \
				min = med + 1; \
		} \
		return 0; \
	}
/* end of BIN_SEARCH() macro */

/* numeric uid -> login name */
const wchar_t *
lookup_login(uid_t uid)
{
	BIN_SEARCH(utable.cnt,
	  CMP(uid,utable.by_uid[med]->uid),
	  SDSTR(utable.by_uid[med]->login))
}

/* numeric gid -> group name */
const wchar_t *
lookup_group(gid_t gid)
{
	BIN_SEARCH(gtable.cnt,
	  CMP(gid,gtable.by_gid[med]->gid),
	  SDSTR(gtable.by_gid[med]->group))
}

static const wchar_t *
lookup_homedir(const wchar_t *user, size_t len)
{
	static SDSTRINGW username = SDNULL(L"");

	if (len == 0)
		return user_data.homedirw;

	sdw_copyn(&username,user,len);
	BIN_SEARCH(utable.cnt,
	  wcscmp(SDSTR(username),SDSTR(utable.by_name[med]->login)),
	  SDSTR(utable.by_name[med]->homedir))
}

/*
 * check if 'dir' is of the form ~username/dir with a valid username
 * typical usage:
 *   tilde = is_dir_tilde(dir);
 *   ... dequote dir ...
 *   if (tilde) dir = dir_tilde(dir);
 * note: without dequoting is this sufficient:
 *   tilde = *dir == '~';
 */
int
is_dir_tilde(const wchar_t *dir)
{
	size_t i;

	if (*dir != L'~')
		return 0;

	for (i = 1; dir[i] != L'\0' && dir[i] != L'/'; i++)
		;
	return lookup_homedir(dir + 1,i - 1) != 0;
}

/*
 * dir_tilde() function performs tilde substitution. It understands
 * ~user/dir notation and transforms it to proper directory name.
 * The result of the substitution (if performed) is stored in
 * a static buffer that might get overwritten by successive calls.
 */
const wchar_t *
dir_tilde(const wchar_t *dir)
{
	size_t i;
	const wchar_t *home;
	static USTRINGW buff = UNULL;

	if (*dir != L'~')
		return dir;

	for (i = 1; dir[i] != L'\0' && dir[i] != L'/'; i++)
		;
	home = lookup_homedir(dir + 1,i - 1);
	if (home == 0)
		return dir;		/* no such user */

	usw_cat(&buff,home,dir + i,(wchar_t *)0);
	return USTR(buff);
}

/*
 * Following two functions implement username completion. First
 * username_find_init() is called to initialize the search, thereafter
 * each call to username_find() returns one matching entry.
 */
void
username_find_init(const wchar_t *str, size_t len)
{
	int min, med, max, cmp;

	ufind.str = str;
	ufind.len = len;

	if (len == 0) {
		ufind.index = 0;
		return;
	}

	for (min = 0, max = utable.cnt - 1; min <= max; ) {
		med = (min + max) / 2;
		cmp = wcsncmp(str,SDSTR(utable.by_name[med]->login),len);
		if (cmp == 0) {
			/*
			 * the binary search algorithm is slightly altered here,
			 * multiple matches are possible, we need the first one
			 */
			if (min == max) {
				ufind.index = med;
				return;
			}
			max = med;
		}
		else if (cmp < 0)
			max = med - 1;
		else
			min = med + 1;
	}

	ufind.index = utable.cnt;
}

const wchar_t *
username_find(const wchar_t **pgecos)
{
	const wchar_t *login, *gecos;

	if (ufind.index >= utable.cnt)
		return 0;
	login = SDSTR(utable.by_name[ufind.index]->login);
	if (ufind.len && wcsncmp(ufind.str,login,ufind.len))
		return 0;
	if (pgecos) {
		gecos = SDSTR(utable.by_name[ufind.index]->gecos);
		*pgecos = *gecos == L'\0' ? 0 : gecos;
	}
	ufind.index++;
	return login;
}

/* the same find functions() for groups */
void
groupname_find_init(const wchar_t *str, size_t len)
{
	int min, med, max, cmp;

	gfind.str = str;
	gfind.len = len;

	if (len == 0) {
		gfind.index = 0;
		return;
	}

	for (min = 0, max = gtable.cnt - 1; min <= max; ) {
		med = (min + max) / 2;
		cmp = wcsncmp(str,SDSTR(gtable.by_name[med]->group),len);
		if (cmp == 0) {
			/*
			 * the binary search algorithm is slightly altered here,
			 * multiple matches are possible, we need the first one
			 */
			if (min == max) {
				gfind.index = med;
				return;
			}
			max = med;
		}
		else if (cmp < 0)
			max = med - 1;
		else
			min = med + 1;
	}

	gfind.index = gtable.cnt;
}

const wchar_t *
groupname_find(void)
{
	const wchar_t *group;

	if (gfind.index >= gtable.cnt)
		return 0;
	group = SDSTR(gtable.by_name[gfind.index]->group);
	if (gfind.len && wcsncmp(gfind.str,group,gfind.len))
		return 0;
	gfind.index++;
	return group;
}

void
user_panel_data(void)
{
	int i, j;
	size_t len;
	const wchar_t *login, *gecos;
	uid_t curs;

	curs = VALID_CURSOR(panel_user.pd) ? panel_user.users[panel_user.pd->curs].uid : 0;
	if (panel_user.pd->filtering)
		match_substr_set(panel_user.pd->filter->line);

	panel_user.maxlen = 0;
	for (i = j = 0; i < utable.cnt; i++) {
		if (curs == utable.by_uid[i]->uid)
			panel_user.pd->curs = j;
		login = SDSTR(utable.by_uid[i]->login);
		gecos = SDSTR(utable.by_uid[i]->gecos);
		if (panel_user.pd->filtering && !match_substr(login) && !match_substr(gecos))
			continue;
		panel_user.users[j].uid = utable.by_uid[i]->uid;
		panel_user.users[j].login = login;
		len = wcslen(login);
		if (len > panel_user.maxlen)
			panel_user.maxlen = len;
		panel_user.users[j++].gecos = gecos;
	}

	panel_user.pd->cnt = j;
}

int
user_prepare(void)
{
	if (utable.cnt > panel_user.usr_alloc) {
		efree(panel_user.users);
		panel_user.usr_alloc = utable.cnt;
		panel_user.users = emalloc(panel_user.usr_alloc * sizeof(USER_ENTRY));
	}

	panel_user.pd->filtering = 0;
	panel_user.pd->curs = -1;
	user_panel_data();
	panel_user.pd->top = panel_user.pd->min;
	panel_user.pd->curs = 0;

	panel = panel_user.pd;
	textline = &line_cmd;

	return 0;
}


void
cx_user_paste(void)
{
	edit_nu_insertstr(panel_user.users[panel_user.pd->curs].login,QUOT_NORMAL);
	edit_insertchar(' ');
	if (panel->filtering == 1)
		cx_filter();
}

void
cx_user_mouse(void)
{
	if (MI_PASTE)
		cx_user_paste();
}

void
group_panel_data(void)
{
	int i, j;
	const wchar_t *group;
	gid_t curs;

	curs = VALID_CURSOR(panel_group.pd) ? panel_group.groups[panel_group.pd->curs].gid : 0;
	if (panel_group.pd->filtering)
		match_substr_set(panel_group.pd->filter->line);

	for (i = j = 0; i < gtable.cnt; i++) {
		if (curs == gtable.by_gid[i]->gid)
			panel_group.pd->curs = j;
		group = SDSTR(gtable.by_gid[i]->group);
		if (panel_group.pd->filtering && !match_substr(group))
			continue;
		panel_group.groups[j].gid = gtable.by_gid[i]->gid;
		panel_group.groups[j++].group = group;
	}

	panel_group.pd->cnt = j;
}

int
group_prepare(void)
{
	if (gtable.cnt > panel_group.grp_alloc) {
		efree(panel_group.groups);
		panel_group.grp_alloc = gtable.cnt;
		panel_group.groups = emalloc(panel_group.grp_alloc * sizeof(GROUP_ENTRY));
	}

	panel_group.pd->filtering = 0;
	panel_group.pd->curs = -1;
	group_panel_data();
	panel_group.pd->top = panel_group.pd->min;
	panel_group.pd->curs = 0;
	panel = panel_group.pd;
	textline = &line_cmd;

	return 0;
}

void
cx_group_paste(void)
{
	edit_nu_insertstr(panel_group.groups[panel_group.pd->curs].group,QUOT_NORMAL);
	edit_insertchar(' ');
	if (panel->filtering == 1)
		cx_filter();
}

void
cx_group_mouse(void)
{
	if (MI_PASTE)
		cx_group_paste();
}
