#ifndef NODED_H
#define NODED_H

#include <stdarg.h>  /* va_* */
#include <stdbool.h> /* bool */
#include <stddef.h>  /* size_t */
#include <stdint.h>  /* uint8_t, UINT8_MAX */
#include <stdio.h>   /* FILE */

#define DEBUG 1

typedef enum
{
	WARN,
	ERR,
	FATAL,
} ErrorType;

/*
 * Note: LITERAL_MAX is calculated by multiplying the buffer size with
 * the longest escape sequence for a string (\x##, or \###). This is
 * the longest literal size that any program would reasonably need.
 */
enum {
	BUFFER_NODE_MAX = UINT8_MAX+1,
	LITERAL_MAX = BUFFER_NODE_MAX*4, /* Max byte size for a literal.  */
};

typedef enum
{
	/* Let ILLEGAL be zero, such that an uninitialized struct at
	 * zero-value will be registered as invalid. */
	ILLEGAL,
	TOK_EOF,

	LPAREN, /* ( */
	RPAREN, /* ) */
	LBRACE, /* { */
	RBRACE, /* } */

	COLON,     /* : */
	COMMA,     /* , */
	PERIOD,    /* . */
	SEMICOLON, /* ; */
	WIRE,      /* -> */

	IDENTIFIER,
	VARIABLE,
	PORT,
	NUMBER,         /* 123, 0x123, 0123 */
	CHAR,           /* 'a', '\0123' */
	STRING_LITERAL, /* "abc" */

	/* Operators are (roughly) ordered by infix precedence. */
	SEND, /* <- */

	ASSIGN, /* = */
	OR_ASSIGN,  /* |= */
	XOR_ASSIGN, /* ^= */
	AND_ASSIGN, /* &= */
	SHR_ASSIGN, /* >>= */
	SHL_ASSIGN, /* <<= */
	SUB_ASSIGN, /* -= */
	ADD_ASSIGN, /* += */
	MOD_ASSIGN, /* %= */
	DIV_ASSIGN, /* /= */
	MUL_ASSIGN, /* *= */

	COND, /* ? */

	LOR,  /* || */
	LAND, /* && */

	OR,  /* | */
	XOR, /* ^ */
	AND, /* & */

	EQL, /* == */
	NEQ, /* != */

	GTE, /* >= */
	GTR, /* > */
	LTE, /* <= */
	LSS, /* < */

	SHR, /* >> */
	SHL, /* << */

	ADD, /* + */
	SUB, /* - */

	MUL, /* * */
	DIV, /* / */
	MOD, /* % */

	INC,  /* ++ */
	DEC,  /* -- */
	LNOT, /* ! */
	NOT,  /* ~ */

	keyword_beg,
	BREAK,
	CONTINUE,
	DO,
	ELSE,
	FOR,
	GOTO,
	HALT,
	IF,
	WHILE,

	BUFFER,
	PROCESSOR,
	STACK,
	keyword_end,
} TokenType;

typedef struct Position Position;
struct Position {
	int lineno; /* 1-based */
	int colno; /* 0-based */
};

typedef struct Token Token;
struct Token {
	TokenType type;
	char lit[LITERAL_MAX+1];
	Position pos;
};

typedef struct Scanner Scanner;
struct Scanner {
	/* The file it reads, and a buffer to store some state. */
	FILE *f; /* Not owned by the scanner. */

	char chr;      /* Current character */
	Position pos;

	/* Peek buffer */
	Token current;
};

typedef struct SymDict SymDict;
struct SymDict {
	char **syms;
	size_t len;
	size_t cap;
};


/* alloc.c */

void *ecalloc(size_t nmemb, size_t size);
void *erealloc(void *ptr, size_t size);


/* compiler.c */

void skip_codeblock(Scanner *s);


/* dict.c */

size_t sym_id(SymDict *dict, const char *sym);
const char *id_sym(const SymDict *dict, size_t id);
void clear_dict(SymDict *dict);


/* err.c */

void vprint_error(const char *srcname, FILE *f,
	const Position *pos, ErrorType type, const char *fmt, va_list ap);
void send_error(const Position *pos, ErrorType type, const char *fmt, ...);


/* scanner.c */

void init_scanner(Scanner *scanner, FILE *f);
void scan(Scanner *scanner);
void expect(Scanner *scanner, TokenType expected, Token *dest);


/* token.c */

TokenType lookup(char ident[]);
const char *tokstr(TokenType type);


#endif /* NODED_H */
