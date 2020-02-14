#ifndef NODED_H
#define NODED_H

#include <stdbool.h>

enum limits {
	LITERAL_MAX = 4096, // Max byte size for a literal.
	ERROR_MAX = 512  // Max byte size for an error message.
};

struct position {
	int lineno; // 1-based
	int colno; // 0-based
};

struct noded_error {
	const char *filename;
	struct position pos;

	char msg[ERROR_MAX+1];
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
	LNOT, // !
	NOT,  // ~

	MUL, // *
	DIV, // /
	MOD, // %
	ADD, // +
	SUB, // -

	SHL, // <<
	SHR, // >>

	LSS, // <
	LTE, // <=
	GTR, // >
	GTE, // >=

	EQL, // ==
	NEQ, // !=

	AND, // &
	XOR, // ^
	OR,  // |

	LAND, // &&
	LOR,  // ||

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

// Precedence-based binary operator scanning.
#define NON_OPERATOR   -1
#define LOWEST_PREC     0
#define HIGHEST_BINARY 13
#define HIGHEST_PREC   15

// util.c
void *emalloc(size_t size);

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
struct scanner {
	char *src;
	size_t src_len;

	char chr; // Current character
	size_t offset;     // byte offset

	struct position pos;

        bool has_errored; // Whether an error occurred during scanning
	struct noded_error err;
};


void init_scanner(struct scanner *scanner, const char filename[],
                  char src[], size_t src_len);
enum token scan(struct scanner *scanner, char *dest_literal, struct position *pos);

#endif /* NODED_H */
