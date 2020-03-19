#ifndef NODED_H
#define NODED_H

#include <stdarg.h>
#include <stdbool.h> // bool
#include <stddef.h> // size_t
#include <stdint.h> // UINT8_MAX

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
enum limits {
	BUFFER_NODE_MAX = UINT8_MAX+1,
	LITERAL_MAX = BUFFER_NODE_MAX*4, // Max byte size for a literal.
	ERROR_MAX = 512,  // Max byte size for an error message.

	PROC_VARS  = 4,
	PROC_PORTS = 4
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

enum opcode {
	OP_INVALID,
	OP_NOOP,

	OP_PUSH,
	OP_POP,
	OP_DUP,

	OP_NEGATE,

	// All opcodes from OP_MUL to OP_LNOT *MUST* be aligned with
	// MUL to LNOT.
	OP_MUL,
	OP_DIV,
	OP_MOD,
	OP_ADD,
	OP_SUB,

	OP_SHL,
	OP_SHR,

	OP_AND,
	OP_XOR,
	OP_OR,

	OP_LSS,
	OP_LTE,
	OP_GTR,
	OP_GTE,

	OP_EQL,
	OP_NEQ,

	OP_LAND,
	OP_LOR,
	OP_LNOT,

	OP_JMP,
	OP_TJMP,
	OP_FJMP,

	// Keep ACTION# in order so someone can math the rest via
	// ACTION0 + n (e.g. SAVE2 == SAVE0 + 2)
	OP_SAVE0,
	OP_SAVE1,
	OP_SAVE2,
	OP_SAVE3,

	OP_LOAD0,
	OP_LOAD1,
	OP_LOAD2,
	OP_LOAD3,

	OP_INC0,
	OP_INC1,
	OP_INC2,
	OP_INC3,

	OP_DEC0,
	OP_DEC1,
	OP_DEC2,
	OP_DEC3,

	OP_SEND0,
	OP_SEND1,
	OP_SEND2,
	OP_SEND3,

	OP_RECV0,
	OP_RECV1,
	OP_RECV2,
	OP_RECV3,

	OP_HALT
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
	const char *src; // not owned by the struct
	size_t src_len;

	char chr;      // Current character
	size_t offset; // byte offset

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

struct expr {
	enum expr_type {
		BAD_EXPR, NUM_LIT_EXPR, PAREN_EXPR,
		UNARY_EXPR, BINARY_EXPR, COND_EXPR, STORE_EXPR
	} type;
	union {
		struct bad_expr {
			struct position start;
		} bad;

		struct num_lit {
			struct position start;
			uint8_t value;
		} num_lit;

		struct paren_expr {
			struct position start;
			struct expr *x;
		} paren;

		struct unary_expr {
			struct position start;
			enum token op; // operator
			struct expr *x; // operand
			bool is_suffix;
		} unary;

		struct binary_expr {
			struct expr *x; // left operand
			enum token op; // operator
			struct position oppos;
			struct expr *y; // right operand
		} binary;

		struct cond_expr {
			struct expr *cond;
			struct expr *when;
			struct expr *otherwise;
		} cond;

		struct store_expr {
			struct position start;
			enum token kind; // VARIABLE, PORT
			size_t name_id;
		} store;
	} data;
};

struct stmt {
	enum stmt_type {
		BAD_STMT, EMPTY_STMT, LABELED_STMT, EXPR_STMT,
		BRANCH_STMT, BLOCK_STMT, IF_STMT, CASE_CLAUSE,
		SWITCH_STMT, LOOP_STMT, HALT_STMT
	} type;
	union {
		struct bad_stmt {
			struct position start;
		} bad;

		struct empty_stmt {
			struct position start;
		} empty;

		struct labeled_stmt {
			struct position start;
			size_t label_id;
			struct stmt *stmt;
		} labeled;

		struct expr_stmt {
			struct position start;
			struct expr *x;
		} expr;

		struct branch_stmt {
			struct position start;
			enum token tok; // BREAK, GOTO, CONTINUE
			struct position label_pos;
			size_t label_id;
		} branch;

		struct block_stmt {
			struct position start;

			// Array of struct stmt pointers
			struct stmt **stmt_list;
			size_t nstmts;
		} block;

		struct if_stmt {
			struct position start;
			struct expr *cond;
			struct stmt *body;
			struct stmt *otherwise; // Can be NULL
		} if_stmt;

		struct case_clause {
			struct position start;
			bool is_default;
			uint8_t x;
		} case_clause;

		struct switch_stmt {
			struct position start;
			struct expr *tag;
			struct stmt *body;
		} switch_stmt;

		struct loop_stmt {
			struct position start;

			bool exec_body_first; // do { ... } while ();

			// init, post: For a for loop. Can be
			// NULL. Always NULL if
			// exec_body_first == true
			struct stmt *init;
			struct stmt *post;

			struct expr *cond;
			struct stmt *body;
		} loop;

		struct halt_stmt {
			struct position start;
		} halt;
	} data;
};

struct port {
	struct position node_pos;
	size_t node_id;

	struct position name_pos;
	size_t name_id;
};

struct decl {
	enum decl_type {
		BAD_DECL, PROC_DECL, PROC_COPY_DECL,
		BUF_DECL, STACK_DECL, WIRE_DECL
	} type;
	union {
		struct bad_decl {
			struct position start;
		} bad;

		struct proc_decl {
			struct position start;

			struct position name_pos;
			size_t name_id;

			struct stmt *body;
		} proc;

		struct proc_copy_decl {
			struct position start;

			struct position name_pos;
			size_t name_id;

			struct position source_pos;
			size_t source_id;
		} proc_copy;

		struct buf_decl {
			struct position start;

			struct position name_pos;
			size_t name_id;

			struct position array_start;
			uint8_t data[BUFFER_NODE_MAX];

			// For debugging; how much of the string is
			// initialized by the program?
			size_t len;
		} buf;

		struct stack_decl {
			struct position start;

			struct position name_pos;
			size_t name_id;
		} stack;

		struct wire_decl {
			struct port source;
			struct port dest;
		} wire;
	} data;
};

struct proc_node {
	uint16_t isp; // instruction pointer
	const uint8_t *code; // machine code
	uint16_t code_size;

	uint8_t *stack;
	size_t stack_top;
	size_t stack_cap;

	send_handler send;
	recv_handler recv;

	uint8_t mem[PROC_VARS];
};

// noded.c (or friends)
void send_error(const struct position *pos, enum error_type type,
	const char *fmt, ...);
bool has_errors(void);

// err.c
void vprint_error(const char *srcname, const char *src, const struct position *pos,
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
void init_scanner(struct scanner *scanner,
                  const char src[], size_t src_len);
void scan(struct scanner *scanner, struct fulltoken *dest);

// dict.c

size_t sym_id(struct symdict *dict, const char *sym);
const char *id_sym(const struct symdict *dict, size_t id);
size_t dict_size(const struct symdict *dict);
void clear_dict(struct symdict *dict);

// ast.c
struct expr *new_expr(enum expr_type type);
struct stmt *new_stmt(enum stmt_type type);
struct decl *new_decl(enum decl_type type);

const char *strexpr(const struct expr *e);
const char *strstmt(const struct stmt *s);
const char *strdecl(const struct decl *d);

typedef void (*expr_func)(struct expr *, void *, int depth);
typedef void (*stmt_func)(struct stmt *, void *, int depth);
typedef void (*decl_func)(struct decl *, void *);

enum call_order {
	PARENT_FIRST, // Handle parent nodes before child nodes
	CHILD_FIRST   // Vice versa
};

void walk_expr(expr_func func, struct expr *e, void *dat,
	enum call_order order);
void walk_stmt(stmt_func sfunc, expr_func efunc,
	struct stmt *s, void *dat, enum call_order order);
void walk_decl(decl_func dfunc, stmt_func sfunc, expr_func efunc,
	struct decl *d, void *dat, enum call_order order);

// Frees the node and all of its children.
void free_expr(struct expr *e);
void free_stmt(struct stmt *s);
void free_decl(struct decl *d);

// parser.c
void init_parser(struct parser *parser,
	const char src[], size_t src_len);
bool parser_eof(const struct parser *parser);
struct decl *parse_decl(struct parser *parser);
void clear_parser(struct parser *parser);

// vm.c
struct proc_node *new_proc_node(const uint8_t code[], size_t code_size,
	send_handler send, recv_handler recv);
void run(struct proc_node *node, void *handler_dat);

// compiler.c
uint8_t *compile(const struct proc_decl *s, uint16_t *n);

#endif /* NODED_H */
