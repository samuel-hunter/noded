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
enum
{
	BUFFER_NODE_MAX = UINT8_MAX+1,
	LITERAL_MAX = BUFFER_NODE_MAX*4, /* Max byte size for a literal.  */

	PORT_MAX = 4,
	VAR_MAX = 4,
};

typedef enum
{
	/* Let ILLEGAL be zero, such that an uninitialized struct at
	 * zero-value will be registered as invalid. */
	ILLEGAL,
	TOK_EOF,
	/* Internal token for scanner.c that isn't returned */
	SCAN_AGAIN,

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
	NUMBER,  /* 123, 0x123, 0123 */
	CHAR,    /* 'a', '\0123' */
	STRING,  /* "abc" */

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

	NUM_TOKENS,
} TokenType;

typedef enum
{
	/* Let OP_INVALID be zero so that any zero-value buffer that
	 * wasn't initialized can be caught early. */
	OP_INVALID,
	OP_NOOP,

	OP_PUSH,
	OP_DUP,
	OP_POP,

	OP_NEG,
	OP_LNOT,
	OP_NOT,

	OP_LOR,
	OP_LAND,
	OP_OR,
	OP_XOR,
	OP_AND,
	OP_EQL,
	OP_LSS,
	OP_LTE,
	OP_SHL,
	OP_SHR,
	OP_ADD,
	OP_SUB,
	OP_MUL,
	OP_DIV,
	OP_MOD,

	OP_JMP,
	OP_FJMP,

	/* OP_LOAD# should match the number of vars defined in VAR_MAX */
	OP_LOAD0,
	OP_LOAD1,
	OP_LOAD2,
	OP_LOAD3,

	/* Same here */
	OP_SAVE0,
	OP_SAVE1,
	OP_SAVE2,
	OP_SAVE3,

	/* OP_SEND# should match the number of ports in PORT_MAX */
	OP_SEND0,
	OP_SEND1,
	OP_SEND2,
	OP_SEND3,

	/* etc etc */
	OP_RECV0,
	OP_RECV1,
	OP_RECV2,
	OP_RECV3,

	OP_HALT,
} Opcode;

	

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
	bool buffered;
	Token peek;
};

typedef struct SymDict SymDict;
struct SymDict {
	char **syms;
	size_t len;
	size_t cap;
};

typedef struct ByteVec ByteVec;
struct ByteVec {
	uint8_t *buf;
	size_t len;
	size_t cap;
};

typedef struct AddrVec AddrVec;
struct AddrVec {
	uint16_t *buf;
	size_t len;
	size_t cap;
};


/* alloc.c */

void *ecalloc(size_t nmemb, size_t size);
void *erealloc(void *ptr, size_t size);


/* compiler.c */

const char *opstr(Opcode op);
uint8_t *compile(Scanner *s, SymDict *dict, uint16_t *n);


/* dict.c */

size_t sym_id(SymDict *dict, const char *sym);
const char *id_sym(const SymDict *dict, size_t id);
void clear_dict(SymDict *dict);


/* err.c */

void init_error(FILE *f, char *fname);
void send_error(const Position *pos, ErrorType type, const char *fmt, ...);
bool has_errors(void);


/* parse.c */

uint8_t parse_int(const Token *tok);
uint8_t parse_escape(const Token *tok, int offset, int *advance, bool *ok);
uint8_t parse_char(const Token *tok);


/* scanner.c */

void init_scanner(Scanner *s, FILE *f);
void scan(Scanner *s, Token *dest);
void peek(Scanner *s, Token *dest);
TokenType peektype(Scanner *s);
void expect(Scanner *s, TokenType expected, Token *dest);
void zap_to(Scanner *s, TokenType target);


/* token.c */

TokenType lookup(char ident[]);
const char *tokstr(TokenType type);


/* vec.c */

void bytevec_append(ByteVec *vec, uint8_t val);
size_t bytevec_reserve(ByteVec *vec, size_t nmemb);
void bytevec_shrink(ByteVec *vec);

void addrvec_append(AddrVec *vec, uint16_t val);
void addrvec_clear(AddrVec *vec);


#endif /* NODED_H */
