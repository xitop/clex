typedef struct {
	char *USstr;			/* space to hold some character string */
	size_t USalloc;			/* size of the allocated memory */
} USTRING;

typedef struct {
	wchar_t *USstr;			/* space to hold some wide character string */
	size_t USalloc;			/* size of the allocated memory */
} USTRINGW;

#define ALLOC_UNIT	24	/* in bytes, do not change without a good reason */
#define US_INIT(X)	do { (X).USstr = 0; (X).USalloc = 0; } while (0)
#define PUSTR(X)	((X)->USstr)
#define  USTR(X)	((X).USstr)
#define UNULL		{0,0}

extern void us_reset(USTRING *);
extern size_t us_setsize(USTRING *, size_t);
extern size_t us_resize(USTRING *, size_t);
extern void us_xchg(USTRING *, USTRING *);
extern char *us_copy(USTRING *, const char *);
extern char *us_copyn(USTRING *, const char *, size_t);
extern void us_cat(USTRING *, ...);

extern void usw_reset(USTRINGW *);
extern size_t usw_setsize(USTRINGW *, size_t);
extern size_t usw_resize(USTRINGW *, size_t);
extern void usw_xchg(USTRINGW *, USTRINGW *);
extern wchar_t *usw_copy(USTRINGW *, const wchar_t *);
extern wchar_t *usw_copyn(USTRINGW *, const wchar_t *, size_t);
extern void usw_cat(USTRINGW *, ...);
