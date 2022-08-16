enum MSG_TYPE {		/* logged (Y or N), displayed (Y or N) */
	MSG_HEADING = 0,	/* -- - heading: valid only if a warning message follows */
	MSG_DEBUG,			/* YN - debug info */
	MSG_NOTICE,			/* YN - notice */
	MSG_AUDIT,			/* YN - user actions (audit trail) */
	MSG_I,				/* YY - informational message */
	MSG_i,				/* NY - informational message not to be logged */
	MSG_W,				/* YY - warning */
	MSG_w,				/* NY - warning not to be logged */
	_MSG_TOTAL_
};

#define MSG_MASK	15	/* 4 bits for MSG_TYPE */

/* modifiers which may be ORed with MSG_TYPE */
#define MSG_NOTIFY	16	/* mention the notification panel */

/*
 * MSG_HEADING usage example:
 *   msgout(MSG_HEADING,"Reading the xxx file");
 *   ....
 *   if (err1)
 *     msgout(MSG_W,"read error");
 *   ....
 *   if (err2)
 *     msgout(MSG_W,"parse error");
 *   ....
 *   msgout(MSG_HEADING,0);
 */

extern void logfile_open(const char *);
extern void logfile_close(void);
extern int log_prepare(void);
extern void log_panel_data(void);
extern void cx_log_right(void);
extern void cx_log_left(void);
extern void cx_log_mark(void);
extern void cx_log_home(void);
extern void msgout(int, const char *, ...);
extern void vmsgout(int, const char *, va_list);
