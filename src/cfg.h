extern void cfg_initialize(void);
extern int cfg_prepare(void);
extern int cfg_menu_prepare(void);
extern int cfg_edit_num_prepare(void);
extern int cfg_edit_str_prepare(void);
extern const wchar_t *cfg_print_value(int);
extern void cx_cfg_enter(void);
extern void cx_cfg_menu_enter(void);
extern void cx_cfg_num_enter(void);
extern void cx_cfg_str_enter(void);
extern void cx_cfg_default(void);
extern void cx_cfg_original(void);
extern void cx_cfg_apply(void);
extern void cx_cfg_apply_save(void);
extern void cx_cfg_noexit(void);

#define cfg_num(X) (*(const int *)pcfg[X])
#define cfg_str(X) ((const wchar_t *)pcfg[X])
#define cfg_layout (cfg_str(CFG_LAYOUT1 + cfg_num(CFG_LAYOUT) - 1))

/* max string lengths */
#define CFGVAR_LEN		16	/* name */
#define CFGVALUE_LEN	80	/* string value */
