#define COMPL_TYPE_AUTO		0	/* autodetect */
#define COMPL_TYPE_DIRPANEL	1	/* whole textline is one directory name */
#define COMPL_TYPE_FILE		2	/* any file */
#define COMPL_TYPE_DIR		3	/* directory */
#define COMPL_TYPE_CMD		4	/* executable */
#define COMPL_TYPE_USER		5	/* user name */
#define COMPL_TYPE_GROUP	6	/* group name */
#define COMPL_TYPE_ENV		7	/* environment variable */
#define COMPL_TYPE_HIST		8	/* command history */
#define COMPL_TYPE_DRYRUN	9	/* NO COMPLETION, just parse the line */

extern void compl_initialize(void);
extern void compl_reconfig(void);
extern int  compl_prepare(void);
extern void compl_panel_data(void);
extern int  compl_text(int);
extern void cx_compl_enter(void);
extern void cx_compl_wordstart(void);
extern void cx_complete_auto(void);
extern void cx_complete_file(void);
extern void cx_complete_dir(void);
extern void cx_complete_cmd(void);
extern void cx_complete_user(void);
extern void cx_complete_group(void);
extern void cx_complete_env(void);
extern void cx_complete_hist(void);
