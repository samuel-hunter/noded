#ifndef AST_H
#define AST_H

#include "noded.h" // struct position, enum token
#include <stddef.h> // size_t
#include <stdint.h> // uint8_t, UINT8_MAX, uint16_t

struct expr {
	enum expr_type {
		BAD_EXPR, NUM_LIT_EXPR, UNARY_EXPR,
		BINARY_EXPR, COND_EXPR, STORE_EXPR
	} type;
	union {
		struct bad_expr {
			struct position start;
		} bad;

		struct num_lit {
			struct position start;
			uint8_t value;
		} num_lit;

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
		SWITCH_STMT, LOOP_STMT, SEND_STMT, HALT_STMT
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
			struct stmt **stmts;
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

			// Array of struct stmt pointers
			struct stmt **stmts;
			size_t nstmts;
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

		struct send_stmt {
			struct store_expr dest;
			struct expr *src;
			struct position oppos;
		} send;

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

struct symdict {
	char **syms;
	size_t len;
	size_t cap;
};

struct parser {
	struct scanner scanner;
	struct symdict *dict; // Not owned by the struct

	// Look one token ahead
	struct fulltoken current;
};


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


// ast.c

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


// dict.c

size_t sym_id(struct symdict *dict, const char *sym);
const char *id_sym(const struct symdict *dict, size_t id);
size_t dict_size(const struct symdict *dict);
void clear_dict(struct symdict *dict);


// parser.c

void init_parser(struct parser *parser, FILE *f, struct symdict *dict);
bool parser_eof(const struct parser *parser);
struct decl *parse_decl(struct parser *parser);


// compiler.c
uint16_t bytecode_size(const struct proc_decl *d);
uint8_t *compile(const struct proc_decl *d, uint8_t *n);

#endif // AST_H
