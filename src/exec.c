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

#include <sys/wait.h>		/* waitpid() */
#include <errno.h>			/* errno */
#include <signal.h>			/* sigaction() */
#include <stdarg.h>			/* log.h */
#include <stdio.h>			/* puts() */
#include <stdlib.h>			/* exit() */
#include <string.h>			/* strcmp() */
#include <unistd.h>			/* fork() */

#include "exec.h"

#include "cfg.h"			/* cfg_str() */
#include "control.h"		/* get_current_mode() */
#include "edit.h"			/* edit_islong() */
#include "inout.h"			/* win_edit() */
#include "filepanel.h"		/* changedir() */
#include "history.h"		/* hist_save() */
#include "lex.h"			/* cmd2lex() */
#include "list.h"			/* list_directory() */
#include "log.h"			/* msgout() */
#include "mbwstring.h"		/* convert2mb() */
#include "mouse.h"			/* mouse_set() */
#include "tty.h"			/* tty_setraw() */
#include "userdata.h"		/* dir_tilde() */
#include "ustringutil.h"	/* us_getcwd() */
#include "util.h"			/* jshash() */
#include "xterm_title.h"	/* xterm_title_set() */

static FLAG promptdir = 0;	/* %p in prompt ? */

void
exec_initialize(void)
{
	set_shellprompt();
}

/* short form of a directory name */
static const wchar_t *
short_dir(const wchar_t *fulldir)
{
	int len;
	wchar_t *pdir, *pshort;
	static USTRINGW dir = UNULL;

	pdir = usw_copy(&dir,fulldir);

	/* homedir -> ~ */
	len = wcslen(user_data.homedirw);
	if (wcsncmp(pdir,user_data.homedirw,len) == 0
	  && (pdir[len] == L'\0' || pdir[len] == L'/')) {
		pdir += len - 1;
		*pdir = L'~';
	}

	/* short enough ? */
	len = wcslen(pdir);
	if (len <= MAX_SHORT_CWD_LEN)
		return pdir;

	pshort = pdir += len - MAX_SHORT_CWD_LEN;
	/* truncate at / (directory boundary) */
	while (*pdir != L'\0')
		if (*pdir++ == L'/')
			return pdir;

	wcsncpy(pshort,L"...",3);
	return pshort;
}

void
set_shellprompt(void)
{
	static USTRINGW prompt = UNULL;
	static const wchar_t *promptchar[3]
	  = { L"$#", L"%#", L">>" };	/* for each CLEX_SHELLTYPE */
	wchar_t ch, appendch, *dst;
	const wchar_t *src, *append;
	int len1, len2, alloc, size;
	FLAG var;

	alloc = usw_setsize(&prompt,ALLOC_UNIT);
	dst = USTR(prompt);
	size = 0;

	if (user_data.isroot) {
		wcscpy(dst,L"ROOT ");
		dst  += 5;
		size += 5;
	}

	for (var = promptdir = 0, src = cfg_str(CFG_PROMPT); (ch = *src++) != L'\0';) {
		append   = 0;
		appendch = L'\0';
		if (TCLR(var)) {
			switch (ch) {
			case L'h':
				append = user_data.hostw;
				break;
			case L'p':
				appendch = promptchar[user_data.shelltype][user_data.isroot];
				break;
			case L's':
				append = user_data.shellw;
				break;
			case L'u':
				append = user_data.loginw;
				break;
			case L'w':
				promptdir = 1;
				append = short_dir(USTR(ppanel_file->dirw));
				break;
			case L'$':
				append = L"$";
				break;
			default:
				append = L"$";
				appendch = ch;
			}
			len1 = append ? wcslen(append) : 0;
			len2 = appendch != L'\0';
		}
		else if (ch == L'$') {
			var = 1;
			continue;
		}
		else {
			appendch = ch;
			len1 = 0;
			len2 = 1;
		}

		if (size + len1 + len2 + 1 > alloc) {
			alloc = usw_resize(&prompt,size + len1 + len2 + 1);
			dst = USTR(prompt) + size;
		}
		if (append) {
			wcscpy(dst,append);
			dst += len1;
		}
		if (appendch)
			*dst++ = appendch;
		size += len1 + len2;
	}
	*dst = L'\0';
	edit_setprompt(&line_cmd,USTR(prompt));
}

void
update_shellprompt(void)
{
	if (promptdir) {
		set_shellprompt();
		win_edit();
	}
}

/* return value: 0 = command executed with exit code 0, otherwise -1 */
static int
execute(const char *command, const wchar_t *commandw)
{
	pid_t childpid;
	int status, code, retval = -1 /* OK = 0 */;
	struct sigaction act;
	const char *signame;

	xterm_title_set(1,command,commandw);
	childpid = fork();
	if (childpid == -1)
		msgout(MSG_W,"EXEC: Cannot create new process (%s)",strerror(errno));
	else if (childpid == 0) {
		/* child process = command */
#ifdef _POSIX_JOB_CONTROL
		/* move this process to a new foreground process group */
		childpid = getpid();
		setpgid(childpid,childpid);
		tcsetpgrp(STDIN_FILENO,childpid);
#endif

		/* reset signal dispositions */
		act.sa_handler = SIG_DFL;
		act.sa_flags = 0;
		sigemptyset(&act.sa_mask);
		sigaction(SIGINT,&act,0);
		sigaction(SIGQUIT,&act,0);
#ifdef _POSIX_JOB_CONTROL
		sigaction(SIGTSTP,&act,0);
		sigaction(SIGTTIN,&act,0);
		sigaction(SIGTTOU,&act,0);
#endif

		/* execute the command */
		logfile_close();
		execl(user_data.shell,user_data.shell,"-c",command,(char *)0);
		printf("EXEC: Cannot execute shell %s (%s)\n",user_data.shell,strerror(errno));
		exit(99);
		/* NOTREACHED */
	}
	else {
		/* parent process = CLEX */
#ifdef _POSIX_JOB_CONTROL
		/* move child process to a new foreground process group */
		setpgid(childpid,childpid);
		tcsetpgrp(STDIN_FILENO,childpid);
#endif
		msgout(MSG_AUDIT,"Command: \"%s\", working directory: \"%s\"",command,USTR(ppanel_file->dir));

		for (; /* until break */;) {
			/* wait for child process exit or stop */
			while (waitpid(childpid,&status,WUNTRACED) < 0)
				/* ignore EINTR */;
#ifdef _POSIX_JOB_CONTROL
			/* put CLEX into the foreground */
			tcsetpgrp(STDIN_FILENO,clex_data.pid);
#endif
			if (!WIFSTOPPED(status))
				break;
			tty_save();
			tty_reset();
			if (tty_dialog('\0',
			  "CLEX: The command being executed has been suspended.\n"
			  "      Press S to start a shell session or any\n"
			  "      other key to resume the command: ") == 's') {
				printf("Suspended process PID = %d\n"
					"Type 'exit' to end the shell session\n"
					,childpid);
				fflush(stdout);
				msgout(MSG_AUDIT,"The command has been stopped."
				  " Starting an interactive shell session");
				system(user_data.shell);
				msgout(MSG_AUDIT,"The interactive shell session has terminated."
				  " Restarting the stopped command");
				tty_press_enter();
			}
			tty_restore();

#ifdef _POSIX_JOB_CONTROL
			/* now place the command into the foreground */
			tcsetpgrp(STDIN_FILENO,childpid);
#endif
			kill(-childpid,SIGCONT);
		}

		tty_reset();
		putchar('\n');
		if (WIFEXITED(status)) {
			code = WEXITSTATUS(status);
			msgout(MSG_AUDIT," Exit code: %d%s",code,
			  code == 99 ? " (might be a shell execution failure)" : "");
			if (code == 0) {
				retval = 0;
				fputs("Command successful. ",stdout);
			}
			else
				printf("Exit code = %d. ",code);
		}
		else {
			code = WTERMSIG(status);
#ifdef HAVE_STRSIGNAL
			signame = strsignal(code);
#elif defined HAVE_DECL_SYS_SIGLIST
			signame = sys_siglist[code];
#else
			signame = "signal name unknown";
#endif
			printf("Abnormal termination, signal %d (%s)",code,signame);
			msgout(MSG_AUDIT," Abnormal termination, signal %d (%s)",code,signame);
#ifdef WCOREDUMP
			if (WCOREDUMP(status))
				fputs(", core image dumped",stdout);
#endif
			putchar('\n');
		}
	}

	/*
	 * List the directory while the user stares at the screen reading
	 * the command output. This way CLEX appears to restart faster.
	 */
	xterm_title_set(0,command,commandw);
	if (ppanel_file->pd->filtering && ppanel_file->filtype == 0)
		ppanel_file->pd->filtering = 0;
	list_directory();
	ppanel_file->other->expired = 1;

	if (retval < 0)
		disp_data.noenter = 0;
	tty_press_enter();
	xterm_title_set(0,0,0);

	return retval;
}

/* check for a "rm" command (1 = found, 0 = not found) */
static int
check_rm(const wchar_t *str, const char *lex)
{
	char lx;
	int i;
	CODE state;	/* 0 = no match, 1 = command name follows */
				/* 2 = "r???" found, 3 = "rm???" found */

	for (state = 1, i = 0; /* until return */; i++) {
		lx = lex[i];
		if (lx == LEX_QMARK)
			continue;
		if (lx == LEX_CMDSEP) {
			state = 1;
			continue;
		}
		if (state == 3) {
			if (lx != LEX_PLAINTEXT)
				return 1;
			state = 0;
			continue;
		}
		if (IS_LEX_END(lx))
			return 0;
		if (state == 1) {
			if (IS_LEX_SPACE(lx))
				continue;
			state = (lx == LEX_PLAINTEXT && str[i] == L'r') ? 2 : 0;
		}
		else if (state == 2)
			state = (lx == LEX_PLAINTEXT && str[i] == L'm') ? 3 : 0;
	}
}

/* return value: 0 = no warning issued, otherwise 1 */
static int
print_warnings(const wchar_t *cmd, const char *lex)
{
	static USTRING cwd = UNULL;
	int warn;
	const char *dir;

	warn = 0;

	if (us_getcwd(&cwd) < 0) {
		msgout(MSG_W,"WARNING: current working directory is not accessible");
		us_copy(&cwd,"???");
		warn = 1;
	}
	else if (strcmp(USTR(ppanel_file->dir),dir = USTR(cwd))) {
		msgout(MSG_w,"WARNING: current working directory has been renamed:\n"
			   "  old name: %s\n"
			   "  new name: %s",USTR(ppanel_file->dir),dir);
		us_copy(&ppanel_file->dir,dir);
		convert_dir();
		warn = 1;
	}

	if ((!NOPT(NOTIF_RM) || edit_isauto()) && check_rm(cmd,lex)) {
		msgout(MSG_w | MSG_NOTIFY,"working directory: %s\n"
			   "WARNING: rm command deletes files, please confirm",USTR(cwd));
		warn = 1;
	}

	/* note1: following warning is appropriate in the file mode only */
	/* note2: edit_islong() works with 'textline' instead of 'cmd', that's ok, see note1 */
	if (!NOPT(NOTIF_LONG) && get_current_mode() == MODE_FILE
	  && edit_islong() && !edit_isauto()) {
		msgout(MSG_w | MSG_NOTIFY,"WARNING: This long command did not fit on the command line");
		warn = 1;
	}

	return warn;
}

/* check for a simple "cd [dir]" command */
static const char *
check_cd(const wchar_t *str, const char *lex)
{
	static USTRINGW cut = UNULL;
	static USTRINGW  dq = UNULL;
	const wchar_t *dirstr;
	char lx;
	int i, start = 0, len = 0;	/* prevent compiler warning */
	FLAG chop, tilde, dequote;
	CODE state;

	for (dequote = chop = 0, state = 0, i = 0; /* until return or break */; i++) {
		lx = lex[i];
		if (lx == LEX_QMARK) {
			if (state >= 3)
				dequote = 1;
			continue;
		}
		switch (state) {
		case 0:	/* init */
			if (IS_LEX_SPACE(lx))
				continue;
			if (lx != LEX_PLAINTEXT || str[i] != L'c')
				return 0;
			state = 1;
			break;
		case 1:	/* "c???" */
			if (lx != LEX_PLAINTEXT || str[i] != L'd')
				return 0;
			state = 2;
			break;
		case 2:	/* "cd???" */
			if (!IS_LEX_EMPTY(lx))
				return 0;
			state = 3;
			/* no break */
		case 3:	/* "cd" */
			if (IS_LEX_SPACE(lx))
				continue;
			if (IS_LEX_END(lx))
				return lx == LEX_END_OK ? user_data.homedir : 0;	/* cd without args */
			start = i;
			state = 4;
			break;
		case 4:	/* "cd dir???" */
			if (lx == LEX_PLAINTEXT)
				continue;
			if (!IS_LEX_EMPTY(lx))
				return 0;
			len = i - start;
			state = 5;
			/* no break */
		case 5: /* "cd dir ???" */
			if (IS_LEX_SPACE(lx)) {
				chop = 1;
				continue;
			}
			if (lx != LEX_END_OK)
				return 0;

			/* check successfull */

			/* cut out the directory part and dequote it */
			dirstr = str + start;

			if (chop)
				dirstr = usw_copyn(&cut,dirstr,len);
			if (dequote) {
				tilde = is_dir_tilde(dirstr);
				usw_dequote(&dq,dirstr,len);
				dirstr = USTR(dq);
			}
			else
				tilde = *dirstr == L'~';
			return convert2mb(tilde ? dir_tilde(dirstr) : dirstr);
		}
	}
}

/*
 * function returns 1 if the command has been executed
 * (successfully or not), otherwise 0
 */
int
execute_cmd(const wchar_t *cmdw)
{
	static FLAG fail, do_it;
	const char *cmd, *lex, *dir;

	/* intercept single 'cd' command */
	lex = cmd2lex(cmdw);
	if ( (dir = check_cd(cmdw,lex)) ) {
		if (changedir(dir) != 0)
			return 0;
		hist_save(cmdw,0);
		win_title();
		win_panel();
		msgout(MSG_i,"directory changed");
		return 1;
	}

	if (disp_data.noenter && disp_data.noenter_hash != jshash(cmdw))
		disp_data.noenter = 0;
	cmd = convert2mb(cmdw);
	curses_stop();
	mouse_restore();
	putchar('\n');
	puts(cmd);
	putchar('\n');
	fflush(stdout);
	do_it = print_warnings(cmdw,lex) == 0 || tty_dialog('y',"Execute the command?");
	if (do_it) {
		fail = execute(cmd,cmdw) < 0;
		hist_save(cmdw,fail);
	}
	mouse_set();
	curses_restart();

	return do_it;
}
