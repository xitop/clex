extern void userdata_initialize(void);
extern void userdata_expire(void);
extern int userdata_refresh(void);
extern const wchar_t *lookup_login(uid_t);
extern const wchar_t *lookup_group(gid_t);
extern void username_find_init(const wchar_t *, size_t);
extern const wchar_t *username_find(const wchar_t **);
extern void groupname_find_init(const wchar_t *, size_t);
extern const wchar_t *groupname_find(void);
extern int is_dir_tilde(const wchar_t *);
extern const wchar_t *dir_tilde(const wchar_t *);
extern int user_prepare(void);
extern void user_panel_data(void);
extern int group_prepare(void);
extern void group_panel_data(void);
extern void cx_user_paste(void);
extern void cx_user_mouse(void);
extern void cx_group_paste(void);
extern void cx_group_mouse(void);