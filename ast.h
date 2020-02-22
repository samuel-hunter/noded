#ifndef AST_H
#define AST_H

#include <stdbool.h>
#include <stdint.h>

#include "noded.h"

struct expr {
	enum expr_type {
		BAD_EXPR, NUM_LIT_EXPR, PAREN_EXPR,
		UNARY_EXPR, BINARY_EXPR, STORE_EXPR
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
			struct position start;
			struct expr *x; // left operand
			enum token op; // operator
			struct expr *y; // right operand
		} binary;

		struct store_expr {
			struct position start;
			enum token kind; // VARIABLE, PORT
			char name[LITERAL_MAX+1];
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
			char label[LITERAL_MAX+1];
			struct stmt *stmt;
		} labeled;

		struct expr_stmt {
			struct expr *x;
		} expr;

		struct branch_stmt {
			struct position start;
			enum token tok; // BREAK, GOTO, CONTINUE
			struct position label_pos;
			char label[LITERAL_MAX+1];
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
			struct stmt *otherwise;
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
			struct stmt *init;
			struct expr *cond;
			struct stmt *post;
			struct stmt *body;
		} loop;

		struct halt_stmt {
			struct position start;
		} halt;
	} data;
};

struct port {
	struct position node_pos;
	char node_name[LITERAL_MAX+1];

	struct position name_pos;
	char name[LITERAL_MAX+1];
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

		struct proc_node_decl {
			struct position start;

			struct position name_pos;
			char name[LITERAL_MAX+1];

			struct stmt *body;
		} proc;

		struct proc_node_copy_decl {
			struct position start;

			struct position name_pos;
			char name[LITERAL_MAX+1];

			struct position source_pos;
			char source[LITERAL_MAX+1];
		} proc_copy;

		struct buf_node_decl {
			struct position start;

			struct position name_pos;
			char name[LITERAL_MAX+1];

			struct position array_start;
			uint8_t data[BUFFER_NODE_MAX];

			// For debugging; how much of the string is
			// initialized by the program?
			size_t len;
		} buf;

		struct stack_node_decl {
			struct position start;

			struct position name_pos;
			char name[LITERAL_MAX+1];
		} stack;

		struct wire_decl {
			struct port source;
			struct port dest;
		} wire;
	} data;
};

// ast.c
struct expr *new_expr(enum expr_type type);
struct stmt *new_stmt(enum stmt_type type);
struct decl *new_decl(enum decl_type type);

typedef void (*expr_func)(struct expr *, int depth, void *);
typedef void (*stmt_func)(struct stmt *, int depth, void *);

// Handle higher nodes before deeper nodes.
void walk_expr(expr_func func, struct expr *x, int depth, void *dat);
void walk_stmt(stmt_func func, struct stmt *x, int depth, void *dat);

// Handle deeper ndoes before higher nodes.
void walk_expr2(expr_func func, struct expr *x, int depth, void *dat);
void walk_stmt2(stmt_func func, struct stmt *x, int depth, void *dat);

// Frees the node and all of its children.
void free_expr(struct expr *x);
void free_stmt(struct stmt *x);
void free_decl(struct decl *x);

#endif /* AST_H */
