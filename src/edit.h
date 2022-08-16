extern void edit_update(void);
extern void edit_update_cursor(void);
extern int  edit_adjust(void);

extern int edit_islong(void);
extern int edit_isauto(void);
extern int edit_isspecial(wchar_t);

extern void edit_nu_insertchar(wchar_t);
extern void edit_insertchar(wchar_t);
extern void edit_nu_insertstr(const wchar_t *, int);
extern void edit_insertstr(const wchar_t *, int);
extern void edit_nu_putstr(const wchar_t *);
extern void edit_putstr(const wchar_t *);
extern void edit_nu_kill(void);
extern void edit_macro(const wchar_t *);
extern void edit_setprompt(TEXTLINE *, const wchar_t *);

/* the second argument of edit_insertstr() */
#define QUOT_NONE		0	/* no quoting */
#define QUOT_NORMAL		1	/* regular quoting */
#define QUOT_IN_QUOTES	2	/* inside "double quotes" */

/* move */
extern void cx_edit_begin(void);
extern void cx_edit_end(void);
extern void cx_edit_left(void);
extern void cx_edit_right(void);
extern void cx_edit_up(void);
extern void cx_edit_down(void);
extern void cx_edit_w_left(void);
extern void cx_edit_w_right(void);
extern void cx_edit_mouse(void);

/* delete */
extern void cx_edit_backsp(void);
extern void cx_edit_delchar(void);
extern void cx_edit_delend(void);
extern void cx_edit_w_del(void);
extern void cx_edit_kill(void);

/* insert */
extern void cx_edit_cmd_f2(void);
extern void cx_edit_cmd_f3(void);
extern void cx_edit_cmd_f4(void);
extern void cx_edit_cmd_f5(void);
extern void cx_edit_cmd_f6(void);
extern void cx_edit_cmd_f7(void);
extern void cx_edit_cmd_f8(void);
extern void cx_edit_cmd_f9(void);
extern void cx_edit_cmd_f10(void);
extern void cx_edit_cmd_f11(void);
extern void cx_edit_cmd_f12(void);
extern void cx_edit_paste_link(void);
extern void cx_edit_paste_path(void);
extern void cx_edit_paste_currentfile(void);
extern void cx_edit_paste_filenames(void);
extern void cx_edit_paste_dir1(void);
extern void cx_edit_paste_dir2(void);

/* transform */
extern void cx_edit_flipcase(void);
