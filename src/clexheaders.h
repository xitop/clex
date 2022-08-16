#if defined(__APPLE__)
# define _XOPEN_SOURCE_EXTENDED
#elif !defined(__FreeBSD__)
# define _XOPEN_SOURCE 600
#endif

#include "../config.h"

#include <sys/types.h>
#include <wchar.h>
#include "sdstring.h"
#include "ustring.h"

#include "clex.h"
