#ifndef NODED_H
#define NODED_H

#include <stdbool.h> // bool
#include <stddef.h> // size_t
#include <stdint.h> // UINT8_MAX

// Precedence-based binary operator scanning.
#define NON_OPERATOR   -1
#define LOWEST_PREC     0
#define HIGHEST_BINARY 13
#define HIGHEST_PREC   15

// Note: LITERAL_MAX is calculated by multiplying the buffer size with
// the longest escape sequence for a string (\x##, or \###). This is
// the longest literal size that any program would reasonably need.
enum limits {
	BUFFER_NODE_MAX = UINT8_MAX+1,
	LITERAL_MAX = BUFFER_NODE_MAX*4, // Max byte size for a literal.
	ERROR_MAX = 512  // Max byte size for an error message.
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

struct position {
	int lineno; // 1-based
	int colno; // 0-based
};

struct fulltoken {
	enum token tok;
	char lit[LITERAL_MAX+1];
	struct position pos;
};

struct noded_error {
	char msg[ERROR_MAX+1];
	const char *filename;
	struct position pos;
};

struct scanner {
	const char *filename;
	const char *src;
	size_t src_len;

	char chr;      // Current character
	size_t offset; // byte offset

	struct position pos;

	bool has_errored; // Whether an error occurred during scanning
};

struct symdict {
	char **syms;
	size_t len;
	size_t cap;
};

struct parser {
	struct scanner scanner;
	struct symdict dict;
	size_t errors;

	// Look one token ahead
	struct fulltoken current;
};

// noded.c
void handle_error(const struct noded_error *err);

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
void init_scanner(struct scanner *scanner, const char filename[],
                  const char src[], size_t src_len);
void scan(struct scanner *scanner, struct fulltoken *dest);

// dict.c

size_t sym_id(struct symdict *dict, const char *sym);
const char *id_sym(const struct symdict *dict, size_t id);
size_t dict_size(const struct symdict *dict);

// parser.c
void init_parser(struct parser *parser, const char filename[],
                 const char src[], size_t src_len);
bool parser_eof(const struct parser *parser);
struct decl *parse_decl(struct parser *parser);

#endif /* NODED_H */
