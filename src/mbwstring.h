extern const wchar_t *usw_convert2w(const char *, USTRINGW *);
extern const wchar_t *convert2w(const char *);
extern const char *us_convert2mb(const wchar_t *, USTRING *);
extern const char *convert2mb(const wchar_t *);
extern int utf_iscomposing(wchar_t);
extern int wc_cols(const wchar_t *, int, int);

/* should not remain invisible:
 * A0 = no-break space (shell does not understand it)
 * AD = soft-hyphen
 */
#define ISWPRINT(CH) (!(lang_data.utf8 && ((CH) == L'\xa0' || (CH) == L'\xad')) && iswprint(CH))
#define WCW(X) (ISWPRINT(X) ? wcwidth(X) : 1)
