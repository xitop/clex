#define SDSTRING_LEN 15
typedef struct {
	char *SDname;					/* string if long, otherwise null ptr */
	char SDmem[SDSTRING_LEN + 1];	/* string if short, otherwise null string */
} SDSTRING;

typedef struct {
	wchar_t *SDname;					/* string if long, otherwise null ptr */
	wchar_t SDmem[SDSTRING_LEN + 1];	/* string if short, otherwise null string */
} SDSTRINGW;

#define SD_INIT(X)	do { (X).SDname = 0; (X).SDmem[0] = '\0';} while (0)
#define PSDSTR(X)	((X)->SDname ? (X)->SDname : (X)->SDmem)
#define  SDSTR(X)	((X).SDname  ? (X).SDname  : (X).SDmem)
#define SDNULL(STR)	{0,STR}

extern void sd_reset(SDSTRING *);
extern void sd_copy(SDSTRING *,  const char *);
extern void sd_copyn(SDSTRING *, const char *, size_t);

extern void sdw_reset(SDSTRINGW *);
extern void sdw_copy(SDSTRINGW *,  const wchar_t *);
extern void sdw_copyn(SDSTRINGW *, const wchar_t *, size_t);
