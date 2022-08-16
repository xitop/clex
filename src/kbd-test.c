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

#include "../config.h"

#include "clexheaders.h"

#include <locale.h>
#include <stdlib.h>
#include <time.h>
#include <wchar.h>
#include <wctype.h>

#include "curses.h"

#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
int
main(void)
{
	int type, y, x, ymax, xmax, ctrlc;
	const char *term;
	wint_t key;
	wchar_t keystr[2];
	time_t last, now;

	setlocale(LC_ALL,"");

	initscr();			/* restores signal dispositions on FreeBSD ! */
	raw();
	nonl();
	noecho();
	keypad(stdscr,TRUE);
	raw();
	scrollok(stdscr,FALSE);
	getmaxyx(stdscr,ymax,xmax);

	clear();
	move(0,0);
	addstr("====== CURSES KEYBOARD TEST ======\n\n"
	  "Terminal type ($TERM) is ");
	term = getenv("TERM");
	addstr(term ? term : "undefined!");
	addstr("\n\n> Press a key (ctrl-C ctrl-C to exit) <\n\n");
	refresh();

	ctrlc = 0;
	keystr[1] = L'\0';
	for (last = 0; /* until break */;) {
		type = get_wch(&key);
		now = time(0);
		if (now >= last + 2) {
			move(6,0);
			clrtobot();
		}
		last = now;

		if (type == OK) {
			if (key != L'\x3')
				ctrlc = 0;
			if (iswprint(key)) {
				addstr(" character:        ");
				if (key == L' ')
					addstr("SPACE");
				else {
					keystr[0] = key;
					addwstr(keystr);
				}
			} else {
				addstr(" unprintable code: ");
				if (key >= L'\x1' && key <= L'\x1A')
					printw("ctrl-%c",'A' + (key - L'\x1'));
				else if (key == L'\x1B')
					addstr("ESC");
				else
					printw("\\x%X",key);
				if (key == L'\x3') {
					if (ctrlc)
						break;
					addstr("   (press again to exit)");
					ctrlc = 1;
				}
			}
		}
		else if (type == KEY_CODE_YES) {
			addstr(" function key:     ");
			addstr(keyname(key));
		}
		else
			addstr(" ERROR");
		addch('\n');
		refresh();

		getyx(stdscr,y,x);
		if (y >= ymax - 2)
			last = 0;
	}

	clear();
	refresh();
	endwin();

	return 0;
}
