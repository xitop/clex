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

/* text file read/write functions */

#include "clexheaders.h"

#include <sys/stat.h>		/* open() */
#include <errno.h>			/* errno */
#include <fcntl.h>			/* open() */
#include <stdarg.h>			/* log.h */
#include <stdio.h>			/* fopen() */
#include <stdlib.h>			/* free() */
#include <string.h>			/* strerror() */
#include <unistd.h>			/* fstat() */

#include "filerw.h"

#include "log.h"			/* msgout() */
#include "util.h"			/* emalloc() */

/* file read functions */

#define TFDESC_CNT	2		/* number of available descriptors (CLEX needs only 1) */
typedef struct {
	FLAG inuse;				/* non-zero if valid */
	FLAG truncated;
	const char *filename;	/* file name */
	char *buff;				/* raw file data */
	size_t size;			/* file data size */
	char **line;			/* result of fr_split() */
	size_t linecnt;			/* number of lines */
} TFDESC;
static TFDESC tfdesc[TFDESC_CNT];

/*
 * read text file into memory
 * output (success): txtfile descriptor >= 0
 * output (failure): error code < 0; error logged
 */
static int
_fr_open(const char *filename, size_t maxsize, int preview)
{
	int i, fd, tfd, errcode;
	struct stat stbuf;
	size_t filesize;	/* do not need off_t here */
	ssize_t rd;
	char *buff;

	if ( (fd = open(filename,O_RDONLY | O_NONBLOCK)) < 0) {
		if ((errcode = errno) == ENOENT)
			return FR_NOFILE;		/* File does not exist */
		msgout(MSG_NOTICE,"Could not open \"%s\" for reading",filename);
		msgout(MSG_DEBUG," System error: %s",strerror(errcode));
		return FR_ERROR;
	}

	tfd = -1;
	for (i = 0; i < TFDESC_CNT; i++)
		if (!tfdesc[i].inuse) {
			tfd = i;
			break;
		}
	if (tfd == -1) {
		close(fd);
		msgout(MSG_NOTICE,"Internal descriptor table is full in fr_open()");
		return FR_ERROR;
	}

	fstat(fd,&stbuf);	/* cannot fail with valid descriptor */
	if (!S_ISREG(stbuf.st_mode)) {
		close(fd);
		msgout(MSG_NOTICE,"File \"%s\" is not a plain file",filename);
		return FR_ERROR;
	}
	if ((stbuf.st_mode & S_IWOTH) == S_IWOTH && !preview) {
		close(fd);
		msgout(MSG_NOTICE,"File \"%s\" is world-writable, i.e. unsafe",filename);
		return FR_ERROR;
	}
	if ((filesize = stbuf.st_size) <= maxsize)
		tfdesc[tfd].truncated = 0;
	else {
		if (preview) {
			tfdesc[tfd].truncated = 1;
			filesize = maxsize;
		}
		else {
			close(fd);
			msgout(MSG_NOTICE,"File \"%s\" is too big (too many characters)",filename);
			return FR_ERROR;
		}
	}

	/* read into memory */
	buff = emalloc(filesize + 1);	/* 1 extra byte for fr_split() */
	rd = read_fd(fd,buff,filesize);
	if (rd == -1)
		errcode = errno;
	close(fd);
	if (rd == -1) {
		free(buff);
		msgout(MSG_NOTICE,"Error reading data from \"%s\"",filename);
		msgout(MSG_DEBUG," System error: %s",strerror(errcode));
		return FR_ERROR;
	}

	tfdesc[tfd].inuse    = 1;
	tfdesc[tfd].filename = filename;
	tfdesc[tfd].buff     = buff;
	tfdesc[tfd].size     = filesize;
	tfdesc[tfd].line     = 0;
	tfdesc[tfd].linecnt  = 0;
	return tfd;
}

int
fr_open(const char *filename, size_t maxsize)
{
	/* reading data for internal use -> strict checking */
	return _fr_open(filename,maxsize,0);
}

int
fr_open_preview(const char *filename, size_t maxsize)
{
	/* reading data for a preview -> relaxed checking */
	return _fr_open(filename,maxsize,1);
}

static int
badtfd(int tfd)
{
	if ((tfd) < 0 || (tfd) >= TFDESC_CNT || !tfdesc[tfd].inuse) {
		msgout(MSG_NOTICE,
		  "BUG: fr_xxx() called without a valid descriptor from fr_open()");
		return 1;
	}
	return 0;
}

int
fr_close(int tfd)
{
	if (badtfd(tfd))
		return FR_ERROR;

	free(tfdesc[tfd].buff);
	efree(tfdesc[tfd].line);
	tfdesc[tfd].inuse = 0;
	return FR_OK;
}

int
fr_is_text(int tfd)
{
	size_t filesize;
	const char *buff, *ptr;
	int ch, ctrl;

	if (badtfd(tfd))
		return FR_ERROR;

	buff = tfdesc[tfd].buff;
	filesize = tfdesc[tfd].size;
	ctrl = 0;
	for (ptr = buff; ptr < buff + filesize; ptr++) {
		ch = *ptr & 0xFF;
		if (ch == '\0' || (lang_data.utf8 && (ch == 0xFE || ch == 0xFF)))
			return 0;
		if (ch < 32) {
			if (ch != '\n' && ch != '\t' && ch != '\r')
				ctrl++;
		}
		else if (!lang_data.utf8 && ch >= 127)
			ctrl++;
	}
	return 10 * ctrl < 3 * filesize;		/* treshold = 30% ctrl + 70% text */
}

int
fr_is_truncated(int tfd)
{
	if (badtfd(tfd))
		return FR_ERROR;

	return tfdesc[tfd].truncated;
}

static int
_fr_split(int tfd, size_t maxlines, int preview)
{
	size_t filesize;
	char *buff, *line, *ptr;
	int ch, ln;
	int comment;	/* -1 = don't know yet, 0 = no, 1 = yes */

	if (badtfd(tfd))
		return FR_ERROR;

	if (tfdesc[tfd].line)
		return FR_OK;	/* everything is done already */

	buff = tfdesc[tfd].buff;
	filesize = tfdesc[tfd].size;

	/* terminate the last line if necessary */
	if (filesize && buff[filesize - 1] != '\n')
		buff[filesize++] = '\n';	/* this is the extra byte allocated in fr_open() */


	/* split to lines */
	tfdesc[tfd].line = emalloc(maxlines * sizeof(const char *));
	for (ln = 0, ptr = buff; ptr < buff + filesize; ) {
		line = ptr;
		comment = preview ? 0 : -1;
		while ( (ch = *ptr) && ch != '\r' && ch != '\n') {
			if (ch == '\t' && !preview)
				ch = *ptr = ' ';
			if (comment < 0) {
				if (ch == '#')
					comment = 1;
				else if (ch != ' ')
					comment = 0;
			}
			ptr++;
		}
		if (ch == '\r' && ptr[1] == '\n')
			*ptr++ = '\0';
		*ptr++ = '\0';

		if (comment == 0) {
			if (ln >= maxlines) {
				tfdesc[tfd].linecnt = maxlines;
				if (preview) {
					tfdesc[tfd].truncated = 1;
					return FR_OK;
				}
				msgout(MSG_NOTICE,"File \"%s\" is too big (too many lines)",tfdesc[tfd].filename);
				return FR_LINELIMIT;
			}
			tfdesc[tfd].line[ln++] = line;
		}
	}

	tfdesc[tfd].linecnt = ln;
	return FR_OK;
}

/* split into lines, strip comments and empty lines */
int
fr_split(int tfd, size_t maxlines)
{
	return _fr_split(tfd, maxlines,0);
}

/* split into lines, but do not modify */
int
fr_split_preview(int tfd, size_t maxlines)
{
	return _fr_split(tfd, maxlines,1);
}

int
fr_linecnt(int tfd)
{
	return badtfd(tfd) ? -1 : tfdesc[tfd].linecnt;
}

const char *
fr_line(int tfd, int lnum)
{
	if (badtfd(tfd))
		return 0;
	if (tfdesc[tfd].line == 0 || lnum < 0 || lnum >= tfdesc[tfd].linecnt)
		return 0;
	return tfdesc[tfd].line[lnum];
}

/* file write functions */

#define WFILE_CNT	2	/* files being written at the same time (CLEX needs only 1) */
typedef struct {
	FILE *fp;				/* fp returned by open() or NULL when unused */
	USTRING file, tmpfile;	/* destination file name, temporary file name */
} WFILE;
static WFILE wfile[WFILE_CNT];

/*
 * output goes to the temporary file opened by fw_open
 *
 * if everything goes well, data is then moved to the final
 * destination file in one atomic operation (fw_close)
 *
 * in the case of an error, the temporary file is removed
 * and the destination file is left untouched
 */
FILE *
fw_open(const char *file)
{
	int i, errcode;

	for (i = 0; i < WFILE_CNT; i++)
		if (wfile[i].fp == 0) {
			us_copy(&wfile[i].file,file);
			us_cat(&wfile[i].tmpfile,file,"-",clex_data.pidstr,".tmp",(char *)0);
			break;
		}
	if (i == WFILE_CNT) {
		msgout(MSG_NOTICE,"Internal file table is full in fw_open()");
		return 0;
	}

	umask(clex_data.umask | 022);
	errno = 0;
	wfile[i].fp = fopen(USTR(wfile[i].tmpfile),"w");
	errcode = errno;
	umask(clex_data.umask);

	if (wfile[i].fp == 0) {
		msgout(MSG_NOTICE,"Cannot open \"%s\" for writing",USTR(wfile[i].tmpfile));
		if (errcode)
			msgout(MSG_DEBUG," System error: %s",strerror(errcode));
		us_reset(&wfile[i].tmpfile);
	}
	return wfile[i].fp;
}

int
fw_close(FILE *fp)
{
	int i, errcode;
	const char *file, *tmpfile;
	FLAG errflag;

	if (fp == 0)
		return -1;

	for (i = 0; i < WFILE_CNT; i++)
		if (wfile[i].fp == fp)
			break;
	if (i == WFILE_CNT) {
		msgout(MSG_NOTICE,"BUG: fw_close() called without fw_open()");
		return -1;
	}
	file = USTR(wfile[i].file);
	tmpfile = USTR(wfile[i].tmpfile);

	/* errflag = ferror(fp) || fclose(fp), but both functions must be called */
	if ( (errno = 0, errflag = ferror(fp), errflag = fclose(fp) || errflag) ) {
		errcode = errno;
		msgout(MSG_NOTICE,"Could not write data to \"%s\"",tmpfile);
	}
	else if ( errno = 0, errflag = rename(tmpfile,file) ) {
		errcode = errno;
		msgout(MSG_NOTICE,"Could not rename \"%s\" to \"%s\"",tmpfile,base_name(file));
	}
	else
		errcode = 0;
	if (errcode)
		msgout(MSG_DEBUG," System error: %s",strerror(errcode));

	unlink(tmpfile);
	us_reset(&wfile[i].tmpfile);
	wfile[i].fp = 0;

	return errflag ? -1 : 0;
}

void
fw_cleanup(void)
{
	int i;
	const char *tmpfile;

	for (i = 0; i < WFILE_CNT; i++) {
		tmpfile = USTR(wfile[i].tmpfile);
		if (tmpfile && *tmpfile == '/')
			unlink(tmpfile);
	}
}
