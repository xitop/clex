dist_man_MANS = clex.1 cfg-clex.1 kbd-test.1
dist_bin_SCRIPTS = cfg-clex

CLEANFILES = help.inc
EXTRA_DIST = help_en.hlp convert.sed
BUILT_SOURCES = help.inc
bin_PROGRAMS = clex kbd-test
clex_SOURCES = bookmarks.c bookmarks.h cfg.c cfg.h \
	clex.h clexheaders.h cmp.c cmp.h completion.c completion.h \
	control.c control.h directory.c directory.h edit.c edit.h \
	exec.c exec.h filepanel.c filepanel.h filter.c filter.h \
	filerw.c filerw.h help.c help.h history.c history.h inout.c inout.h \
	inschar.c inschar.h lang.c lang.h lex.c lex.h list.c list.h \
	log.c log.h notify.c notify.h opt.c opt.h match.c match.h \
	mbwstring.c mbwstring.h mouse.c mouse.h panel.c panel.h \
	preview.c preview.h rename.c rename.h \
	sdstring.c sdstring.h select.c select.h signals.c signals.h \
	sort.c sort.h start.c tty.c tty.h undo.c undo.h userdata.c userdata.h \
	ustring.c ustring.h ustringutil.c ustringutil.h util.c util.h \
	xterm_title.c xterm_title.h
kbd-test_SOURCES: kbd-test.c

# convert the on-line help text to a C language array of strings
help.inc: help_en.hlp convert.sed
	sed -f convert.sed help_en.hlp > help.inc
