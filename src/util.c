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

/* miscellaneous utilities */

#include "clexheaders.h"

#include <limits.h>			/* SSIZE_MAX */
#include <stdlib.h>			/* malloc() */
#include <string.h>			/* strlen() */
#include <unistd.h>			/* read() */

#include "util.h"

#include "control.h"		/* err_exit() */

/* variables used in pathname_xxx() functions */
static USTRING path_buff = UNULL;
static size_t path_dirlen;

/* get rid of directory part in 'pathname' */
const char *
base_name(const char *pathname)
{
	const char *base, *pch;
	char ch;

	for (pch = base = pathname; (ch = *pch++) != '\0'; )
		if (ch == '/')
			base = pch;
	return base;
}

/* primitive check for an absolute pathname */
int
checkabs(const char *path)
{
	return path != 0 && *path == '/';
}

static void
alloc_fail(size_t size)
{
	err_exit("Memory allocation failed, could not allocate %lu bytes",
	  (unsigned long)size);
	/* NOTREACHED */
}

/* malloc with error checking */
void *
emalloc(size_t size)
{
	void *mem;

	if (size > SSIZE_MAX)
		/*
		 * possible problems with signed/unsigned int !
		 *
		 * It is not normal to request such a huge memory
		 * block anyway (16-bit systems are not supported)
		 */
		alloc_fail(size);
	if ((mem = malloc(size)) == 0)
		alloc_fail(size);
	return mem;
}

/* realloc with error checking */
void *
erealloc(void *ptr, size_t size)
{
	void *mem;

	/* not sure if really all realloc()s can handle this case */
	if (ptr == 0)
		return emalloc(size);

	if (size > SSIZE_MAX)
		/* see emalloc() above */
		alloc_fail(size);

	if ((mem = realloc(ptr,size)) == 0)
		alloc_fail(size);
	return mem;
}

/* strdup with error checking */
char *
estrdup(const char *str)
{
	char *dup;

	if (str == 0)
		return 0;
	dup = emalloc(strlen(str) + 1);
	strcpy(dup,str);
	return dup;
}

/* wcsdup with error checking */
wchar_t *
ewcsdup(const wchar_t *str)
{
	wchar_t *dup;

	if (str == 0)
		return 0;
	dup = emalloc((wcslen(str) + 1) * sizeof(wchar_t));
	wcscpy(dup,str);
	return dup;
}

void
efree(void *ptr)
{
	if (ptr)
		free(ptr);
}

/* set the directory name for pathname_join() */
void
pathname_set_directory(const char *dir)
{
	char *str;

	path_dirlen = strlen(dir);
	us_resize(&path_buff,path_dirlen + ALLOC_UNIT);
	/* extra bytes for slash and initial space for the filename */
	str = USTR(path_buff);
	strcpy(str,dir);
	if (str[path_dirlen - 1] != '/')
		str[path_dirlen++] = '/';
		/* the string is now not null terminated, that's ok */
}

/*
 * join the filename 'file' with the directory set by
 * pathname_set_directory() above
 *
 * returned data is overwritten by subsequent calls
 */
char *
pathname_join(const char *file)
{
	us_resize(&path_buff,path_dirlen + strlen(file) + 1);
	strcpy(USTR(path_buff) + path_dirlen,file);
	return USTR(path_buff);
}

/*
 * under certain condition can read() return fewer bytes than requested,
 * this wrapper function handles it
 *
 * remember: error check should be (read() == -1) and not (read() < 0)
 */
ssize_t
read_fd(int fd, char *buff, size_t bytes)
{
	size_t total;
	ssize_t rd;

	for (total = 0; bytes > 0; total += rd, bytes -= rd) {
		rd = read(fd,buff + total,bytes);
		if (rd == -1)	/* error */
			return -1;
		if (rd == 0)	/* EOF */
			break;
	}
	return total;
}

/* this is a hash function written by Justin Sobel */
unsigned int
jshash(const wchar_t *str)
{
	unsigned int len, i = 0, hash = 1315423911;

	len = wcslen(str);	
	for (i = 0; i < len; i++)
		hash ^= ((hash << 5) + str[i] + (hash >> 2));

   return hash;
}

