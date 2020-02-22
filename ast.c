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

void walk_expr(struct expr *x, expr_func func, int depth, void *dat)
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
		walk_expr(x->data.binary.x, func, depth, dat);
		x = x->data.binary.y;
		goto start;
	default:
		break; // Do nothing
	}
}

void walk_stmt(struct stmt *x, stmt_func func, int depth, void *dat)
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
			walk_stmt(
				&x->data.block.stmt_list[i], func, depth, dat);
		}
		break;
	case IF_STMT:
		depth++;
		walk_stmt(x->data.if_stmt.body, func, depth, dat);
		x = x->data.if_stmt.otherwise;
		goto start;
	case SWITCH_STMT:
		depth++;
		x = x->data.switch_stmt.body;
		goto start;
	case LOOP_STMT:
		depth++;
		walk_stmt(x->data.loop.init, func, depth, dat);
		walk_stmt(x->data.loop.post, func, depth, dat);
		x = x->data.loop.body;
		goto start;
	default:
		break; // Do nothing
	}
}
