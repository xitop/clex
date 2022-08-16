extern const char *cmd2lex(const wchar_t *);
extern int ispattern(const wchar_t *);
extern int isquoted(const wchar_t *);
extern int usw_dequote(USTRINGW *, const wchar_t *,  size_t);

enum LEX_TYPE {
	/* 1X = space */
	LEX_SPACE = 10,		/* white space */

	/* 2X = word */
	LEX_PLAINTEXT = 20,	/* text */
	LEX_QMARK,			/* quotation mark */
	LEX_VAR,			/* $ symbol (variable substitution) */

	/* 3X special characters */
	LEX_IO = 30,		/* > or < */
	LEX_CMDSEP,			/* ; & | ( or opening backtick */
	LEX_OTHER,			/* e.g. } or closing backtick */

	/* 4X begin and end */
	LEX_BEGIN = 40,		 /* begin of text */
	/* end of text + exit code */
	LEX_END_OK,			/* ok */
	LEX_END_ERR_BQ,		/* trailing backslash */
	LEX_END_ERR_SQ,		/* open single quote */
	LEX_END_ERR_DQ		/* open double quote */
};

/* basic categories and basic tests */
#define LEX_TYPE_SPACE		1
#define LEX_TYPE_WORD		2
#define LEX_TYPE_SPECIAL	3
#define LEX_TYPE_END		4
#define LEX_TYPE(X)			((X) / 10)
#define IS_LEX_SPACE(X)		(LEX_TYPE(X) == LEX_TYPE_SPACE)
#define IS_LEX_WORD(X)		(LEX_TYPE(X) == LEX_TYPE_WORD)
#define IS_LEX_SPECIAL(X)	(LEX_TYPE(X) == LEX_TYPE_SPECIAL)
#define IS_LEX_END(X)		(LEX_TYPE(X) == LEX_TYPE_END)

/* extended tests */
#define IS_LEX_EMPTY(X)		(LEX_TYPE(X) == LEX_TYPE_END  || LEX_TYPE(X) == LEX_TYPE_SPACE)
#define IS_LEX_CMDSEP(X)	((X) == LEX_CMDSEP || (X) == LEX_BEGIN)
