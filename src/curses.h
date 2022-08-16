#ifdef HAVE_NCURSESW_H
# include <ncursesw.h>
#elif defined HAVE_NCURSES_H
# include <ncurses.h>
#elif defined HAVE_CURSESW_H
# include <cursesw.h>
#elif defined HAVE_CURSES_H
# include <curses.h>
#endif

#ifndef NCURSES_MOUSE_VERSION
# define NCURSES_MOUSE_VERSION 0
#endif
