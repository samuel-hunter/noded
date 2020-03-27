#ifndef NODED_H
#define NODED_H

#include <stdarg.h>
#include <stdbool.h> // bool
#include <stddef.h> // size_t
#include <stdint.h> // uint8_t, UINT8_MAX
#include <stdio.h>

// Precedence-based binary operator scanning.
#define NON_OPERATOR   -1
#define LOWEST_PREC     0
#define HIGHEST_BINARY 13
#define HIGHEST_PREC   15

typedef void (*send_handler)(uint8_t val, int port, void *dat);
typedef uint8_t (*recv_handler)(int port, void *dat);

enum error_type { WARN, ERR, FATAL };

// Note: LITERAL_MAX is calculated by multiplying the buffer size with
// the longest escape sequence for a string (\x##, or \###). This is
// the longest literal size that any program would reasonably need.
enum {
	BUFFER_NODE_MAX = UINT8_MAX+1,
	LITERAL_MAX = BUFFER_NODE_MAX*4 // Max byte size for a literal.
};

enum token {
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
	STACK,
	keyword_end
};


struct position {
	int lineno; // 1-based
	int colno; // 0-based
};

struct fulltoken {
	enum token tok;
	char lit[LITERAL_MAX+1];
	struct position pos;
};

struct scanner {
	// The file it reads, and a buffer to store some state.
	FILE *f; // Not owned by the scanner.
	char buf[512];
	size_t offset;
	size_t nread;

	char chr;      // Current character
	struct position pos;
};

struct symdict {
	char **syms;
	size_t len;
	size_t cap;
};

struct parser {
	struct scanner scanner;
	struct symdict dict;

	// Look one token ahead
	struct fulltoken current;
};


// noded.c (or friends)

void send_error(const struct position *pos, enum error_type type,
	const char *fmt, ...);
bool has_errors(void);


// err.c

void vprint_error(const char *srcname, FILE *f, const struct position *pos,
	enum error_type type, const char *fmt, va_list ap);


// util.c

void *emalloc(size_t size);
void *erealloc(void *ptr, size_t size);


// token.c

enum token lookup(char ident[]);
int precedence(enum token op);
bool isltr(enum token op);
bool isliteral(enum token tok);
bool isoperator(enum token tok);
bool iskeyword(enum token tok);
bool isoperand(enum token tok);
bool isunary(enum token tok);
bool issuffix(enum token tok);
const char *strtoken(enum token tok);


// scanner.c

void init_scanner(struct scanner *scanner, FILE *f);
void scan(struct scanner *scanner, struct fulltoken *dest);


// dict.c

size_t sym_id(struct symdict *dict, const char *sym);
const char *id_sym(const struct symdict *dict, size_t id);
size_t dict_size(const struct symdict *dict);
void clear_dict(struct symdict *dict);

#endif /* NODED_H */
