#include <stdlib.h>
#include <string.h>

#include "noded.h"
#include "ast.h"

struct expr *new_expr(enum expr_type type)
{
	struct expr *result = emalloc(sizeof(*result));
	result->type = type;
	return result;
}

struct stmt *new_stmt(enum stmt_type type)
{
	struct stmt *result = emalloc(sizeof(*result));
	result->type = type;
	return result;
}

struct decl *new_decl(enum decl_type type)
{
	struct decl *result = emalloc(sizeof(*result));
	result->type = type;
	return result;
}

// Root first walking
void walk_expr(expr_func func, struct expr *x, int depth, void *dat)
{
start:
	func(x, depth, dat);

	switch (x->type) {
	case PAREN_EXPR:
		depth++;
		x = x->data.paren.x;
		goto start;
	case UNARY_EXPR:
		depth++;
		x = x->data.unary.x;
		goto start;
	case BINARY_EXPR:
		depth++;
		walk_expr(func, x->data.binary.x, depth, dat);
		x = x->data.binary.y;
		goto start;
	default:
		break; // Do nothing
	}
}

void walk_stmt(stmt_func func, struct stmt *x, int depth, void *dat)
{
start:
	func(x, depth, dat);

	switch (x->type) {
	case LABELED_STMT:
		depth++;
		x = x->data.labeled.stmt;
		goto start;
	case BLOCK_STMT:
		depth++;
		for (size_t i = 0; i < x->data.block.nstmts; i++) {
			walk_stmt(func, x->data.block.stmt_list[i],
			          depth, dat);
		}
		break;
	case IF_STMT:
		depth++;
		walk_stmt(func, x->data.if_stmt.body, depth, dat);
		x = x->data.if_stmt.otherwise;
		goto start;
	case SWITCH_STMT:
		depth++;
		x = x->data.switch_stmt.body;
		goto start;
	case LOOP_STMT:
		depth++;
		walk_stmt(func, x->data.loop.init, depth, dat);
		walk_stmt(func, x->data.loop.post, depth, dat);
		x = x->data.loop.body;
		goto start;
	default:
		break; // Do nothing
	}
}

void walk_expr2(expr_func func, struct expr *x, int depth, void *dat)
{
	switch (x->type) {
	case PAREN_EXPR:
		walk_expr2(func, x->data.paren.x, depth+1, dat);
		break;
	case UNARY_EXPR:
		walk_expr2(func, x->data.unary.x, depth+1, dat);
		break;
	case BINARY_EXPR:
		walk_expr2(func, x->data.binary.x, depth+1, dat);
		walk_expr2(func, x->data.binary.y, depth+1, dat);
		break;
	default:
		break; // Do nothing
	}

	func(x, depth, dat);
}

void walk_stmt2(stmt_func func, struct stmt *x, int depth, void *dat)
{
	switch (x->type) {
	case LABELED_STMT:
		walk_stmt2(func, x->data.labeled.stmt, depth, dat);
		break;
	case BLOCK_STMT:
		for (size_t i = 0; i < x->data.block.nstmts; i++) {
			walk_stmt2(func, x->data.block.stmt_list[i],
			           depth, dat);
		}
		break;
	case IF_STMT:
		walk_stmt2(func, x->data.if_stmt.body, depth, dat);
		walk_stmt2(func, x->data.if_stmt.otherwise, depth, dat);
		break;
	case SWITCH_STMT:
		walk_stmt2(func, x->data.switch_stmt.body, depth, dat);
		break;
	case LOOP_STMT:
		walk_stmt2(func, x->data.loop.init, depth, dat);
		walk_stmt2(func, x->data.loop.post, depth, dat);
		walk_stmt2(func, x->data.loop.body, depth, dat);
		break;
	default:
		break; // Do nothng
	}

	func(x, depth, dat);
}

static void free_expr_helper(struct expr *x, int depth, void *dat)
{
	(void)dat;
	(void)depth;

	free(x);
}

static void free_stmt_helper(struct stmt *x, int depth, void *dat)
{
	(void)dat;
	(void)depth;

	switch (x->type) {
	case EXPR_STMT:
		walk_expr2(&free_expr_helper, x->data.expr.x, 0, NULL);
		break;
	case IF_STMT:
		walk_expr2(&free_expr_helper,
		           x->data.if_stmt.cond, 0, NULL);
		break;
	case SWITCH_STMT:
		walk_expr2(&free_expr_helper,
		           x->data.switch_stmt.tag, 0, NULL);
		break;
	case LOOP_STMT:
		walk_expr2(&free_expr_helper,
		           x->data.loop.cond, 0, NULL);
		break;
	default:
		break; // Do nothing
	}

	free(x);
}

void free_expr(struct expr *x)
{
	walk_expr2(&free_expr_helper, x, 0, NULL);
}

void free_stmt(struct stmt *x)
{
	walk_stmt2(&free_stmt_helper, x, 0, NULL);
}

void free_decl(struct decl *x)
{
	if (x->type == PROC_DECL) {
		walk_stmt2(&free_stmt_helper, x->data.proc.body,
		           0, NULL);
	}

	free(x);
}
