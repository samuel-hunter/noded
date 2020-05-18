#ifndef NODED_H
#define NODED_H

#include <stdarg.h>  // va_*
#include <stdbool.h> // bool
#include <stddef.h>  // size_t
#include <stdint.h>  // uint8_t, UINT8_MAX
#include <stdio.h>   // FILE

// Precedence-based binary operator scanning.
enum
{
	NON_OPERATOR   = -1,
	LOWEST_PREC    =  0,
	HIGHEST_BINARY = 13,
	HIGHEST_PREC   = 15,
};

typedef enum {
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
	ILLEGAL,
	TOK_EOF,

	LPAREN, // (
	RPAREN, // )
	LBRACE, // {
	RBRACE, // }

	COLON,     // :
	COMMA,     // ,
	PERIOD,    // .
	SEMICOLON, // ;
	WIRE,      // ->

	literal_beg,
	IDENTIFIER,
	VARIABLE,
	PORT,
	NUMBER,         // 123, 0x123, 0123
	CHAR,           // 'a', '\0123'
	STRING_LITERAL, // "abc"
	literal_end,

	// Operators are (roughly) ordered by precedence.
	operator_beg,

	// see token.go:(op Token) Precedence() for rationale.
	SEND, // <-

	INC,  // ++
	DEC,  // --
	NOT,  // ~

	// All tokens from MUL to LNOT *MUST* be aligned with OP_MUL
	// to OP_LNOT. All tokens from MUL to OR *MUST* be aligned
	// with MUL_ASSIGN to OR_ASSIGN.
	MUL, // *
	DIV, // /
	MOD, // %
	ADD, // +
	SUB, // -

	SHL, // <<
	SHR, // >>

	AND, // &
	XOR, // ^
	OR,  // |

	LSS, // <
	LTE, // <=
	GTR, // >
	GTE, // >=

	EQL, // ==
	NEQ, // !=

	LAND, // &&
	LOR,  // ||
	LNOT, // !

	COND, // ?

	ASSIGN, // =

	MUL_ASSIGN, // *=
	DIV_ASSIGN, // /=
	MOD_ASSIGN, // %=
	ADD_ASSIGN, // +=
	SUB_ASSIGN, // -=

	SHL_ASSIGN, // <<=
	SHR_ASSIGN, // >>=

	AND_ASSIGN, // &=
	XOR_ASSIGN, // ^=
	OR_ASSIGN,  // |=
	operator_end,

	keyword_beg,
	BREAK,
	CASE,
	CONTINUE,
	DEFAULT,
	DO,
	ELSE,
	FOR,
	GOTO,
	HALT,
	IF,
	SWITCH,
	WHILE,

	BUFFER,
	PROCESSOR,
	keyword_end
} Token;

typedef struct Position Position;
struct Position {
	int lineno; // 1-based
	int colno; // 0-based
};

typedef struct FullToken FullToken;
struct FullToken {
	Token tok;
	char lit[LITERAL_MAX+1];
	Position pos;
};

typedef struct Scanner Scanner;
struct Scanner {
	// The file it reads, and a buffer to store some state.
	FILE *f; // Not owned by the scanner.
	char buf[512];
	size_t offset;
	size_t nread;

	char chr;      // Current character
	Position pos;
};

typedef struct SymDict SymDict;
struct SymDict {
	char **syms;
	size_t len;
	size_t cap;
};


// alloc.c

void *ecalloc(size_t nmemb, size_t size);
void *erealloc(void *ptr, size_t size);


// dict.c

size_t sym_id(SymDict *dict, const char *sym);
const char *id_sym(const SymDict *dict, size_t id);
void clear_dict(SymDict *dict);


// err.c

void vprint_error(const char *srcname, FILE *f,
	const Position *pos, ErrorType type, const char *fmt, va_list ap);
void send_error(const Position *pos, ErrorType type, const char *fmt, ...);


// scanner.c

void init_scanner(Scanner *scanner, FILE *f);
void scan(Scanner *scanner, FullToken *dest);


// token.c

Token lookup(char ident[]);
int precedence(Token op);
bool isltr(Token op);
bool isliteral(Token tok);
bool isoperator(Token tok);
bool iskeyword(Token tok);
bool isoperand(Token tok);
bool isunary(Token tok);
bool issuffix(Token tok);
const char *strtoken(Token tok);

#endif /* NODED_H */
