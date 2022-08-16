extern void curses_initialize(void);
extern void curses_stop(void);
extern void curses_restart(void);
extern void curses_cbreak(void);
extern void curses_raw(void);

extern void kbd_rawkey(void);
extern wint_t kbd_input(void);

extern const char *char_code(int);

extern void win_frame_reconfig(void);
extern void win_settitle(const wchar_t *);
extern void win_bar(void);
extern void win_edit(void);
extern int  sum_linechars(void);
extern void win_filter(void);
extern void win_frame(void);
extern void win_title(void);
extern void win_infoline(void);
extern void win_panel(void);
extern void win_panel_opt(void);
extern void win_waitmsg(void);
enum HELPMSG_TYPE {
	HELPMSG_BASE, HELPMSG_OVERRIDE, HELPMSG_TMP, HELPMSG_INFO, HELPMSG_WARNING
};
extern void win_sethelp(int, const wchar_t *);

extern void draw_line_bm(int);
extern void draw_line_bm_edit(int);
extern void draw_line_cfg(int);
extern void draw_line_cfg_menu(int);
extern void draw_line_cmp(int);
extern void draw_line_cmp_sum(int);
extern void draw_line_compl(int);
extern void draw_line_dir(int);
extern void draw_line_dir_split(int);
extern void draw_line_file(int);
extern void draw_line_fopt(int);
extern void draw_line_group(int);
extern void draw_line_help(int);
extern void draw_line_hist(int);
extern void draw_line_log(int);
extern void draw_line_mainmenu(int);
extern void draw_line_notif(int);
extern void draw_line_paste(int);
extern void draw_line_preview(int);
extern void draw_line_sort(int);
extern void draw_line_user(int);
