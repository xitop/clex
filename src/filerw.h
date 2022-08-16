#define FR_OK			 0
#define FR_TRUNCATED	 1	/* data exceeding given limits was ignored, otherwise OK */
#define FR_NOFILE		-1	/* file does not exist, caller decides if it is an error */
#define FR_LINELIMIT	-2	/* too many lines; this is partial success, that's why it is not FR_ERROR */
#define FR_ERROR		-9	/* an error has occurred and was logged */

extern int fr_open(const char *, size_t);
extern int fr_open_preview(const char *, size_t);
extern int fr_close(int);
extern int fr_is_text(int);
extern int fr_is_truncated(int);
extern int fr_split(int, size_t);
extern int fr_split_preview(int, size_t);
extern int fr_linecnt(int);
extern const char *fr_line(int, int);

extern FILE *fw_open(const char *);
extern int fw_close(FILE *);
extern void fw_cleanup(void);
