#include <stdlib.h>
#include <string.h>

#include "noded.h"
#include "ast.h"

static const char *exprs[] = {
	[BAD_EXPR] = "BadExpr",
	[NUM_LIT_EXPR] = "NumLitExpr",
	[PAREN_EXPR] = "ParenExpr",
	[UNARY_EXPR] = "UnaryExpr",
	[BINARY_EXPR] = "BinaryExpr",
	[STORE_EXPR] = "StoreExpr"
};

static const char *stmts[] = {
	[BAD_STMT] = "BadStmt",
	[EMPTY_STMT] = "EmptyStmt",
	[LABELED_STMT] = "LabeledStmt",
	[EXPR_STMT] = "ExprStmt",
	[BRANCH_STMT] = "BranchStmt",
	[BLOCK_STMT] = "BlockStmt",
	[IF_STMT] = "IfStmt",
	[CASE_CLAUSE] = "CaseClause",
	[SWITCH_STMT] = "SwitchStmt",
	[LOOP_STMT] = "LoopStmt",
	[HALT_STMT] = "HaltStmt"
};

static const char *decls[] = {
	[BAD_DECL] = "BadDecl",
	[PROC_DECL] = "ProcDecl",
	[PROC_COPY_DECL] = "ProcCopyDecl",
	[BUF_DECL] = "BufDecl",
	[STACK_DECL] = "StackDecl",
	[WIRE_DECL] = "WireDecl"
};

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

const char *strexpr(const struct expr *e)
{
	return exprs[e->type];
}

const char *strstmt(const struct stmt *s)
{
	return stmts[s->type];
}

const char *strdecl(const struct decl *d)
{
	return decls[d->type];
}

static void walk_expr_(expr_func func, struct expr *e, void *dat, int depth,
                       enum call_order order)
{

	if (order == PARENT_FIRST)
		func(e, dat, depth);

	// Walk through the children...
	switch (e->type) {
	case PAREN_EXPR:
		walk_expr_(func, e->data.paren.x, dat, depth+1, order);
		break;
	case UNARY_EXPR:
		walk_expr_(func, e->data.unary.x, dat, depth+1, order);
		break;
	case BINARY_EXPR:
		walk_expr_(func, e->data.binary.x, dat, depth+1, order);
		walk_expr_(func, e->data.binary.y, dat, depth+1, order);
		break;
	default:
		break; // No children here
	}

	if (order == CHILD_FIRST)
		func(e, dat, depth);
}

static void walk_stmt_(stmt_func sfunc, expr_func efunc, struct stmt *s,
                      void *dat, int depth, enum call_order order)
{

	if (order == PARENT_FIRST)
		sfunc(s, dat, depth);

	// Walk through the children...
	switch (s->type) {
	case LABELED_STMT:
		walk_stmt_(sfunc, efunc, s->data.labeled.stmt,
		           dat, depth+1, order);
		break;
	case EXPR_STMT:
		if (efunc) {
			walk_expr_(efunc, s->data.expr.x, dat, depth+1, order);
		}
		break;
	case BLOCK_STMT:
		for (size_t i = 0; i < s->data.block.nstmts; i++) {
			walk_stmt_(sfunc, efunc, s->data.block.stmt_list[i],
			           dat, depth+1, order);
		}
		break;
	case IF_STMT:
		if (efunc) {
			walk_expr_(efunc, s->data.if_stmt.cond,
			           dat, depth+1, order);
		}
		walk_stmt_(sfunc, efunc, s->data.if_stmt.body,
		           dat, depth+1, order);
		walk_stmt_(sfunc, efunc, s->data.if_stmt.otherwise,
		           dat, depth+1, order);
		break;
	case SWITCH_STMT:
		if (efunc) {
			walk_expr_(efunc, s->data.switch_stmt.tag,
			           dat, depth+1, order);
		}
		walk_stmt_(sfunc, efunc, s->data.switch_stmt.body,
		           dat, depth+1, order);
		break;
	case LOOP_STMT:
		walk_stmt_(sfunc, efunc, s->data.loop.init,
		           dat, depth+1, order);
		if (efunc) {
			walk_expr_(efunc, s->data.loop.cond,
			           dat, depth+1, order);
		}
		walk_stmt_(sfunc, efunc, s->data.loop.post, dat, depth+1, order);
		walk_stmt_(sfunc, efunc, s->data.loop.body, dat, depth+1, order);
		break;
	default:
		break; // No children here...
	}

	if (order == CHILD_FIRST)
		sfunc(s, dat, depth);
}

void walk_expr(expr_func func, struct expr *e, void *dat,
               enum call_order order)
{
	walk_expr_(func, e, dat, 0, order);
}

void walk_stmt(stmt_func sfunc, expr_func efunc,
               struct stmt *s, void *dat, enum call_order order)
{
	walk_stmt_(sfunc, efunc, s, dat, 0, order);
}

void walk_decl(decl_func dfunc, stmt_func sfunc, expr_func efunc,
               struct decl *d, void *dat, enum call_order order)
{
	if (order == PARENT_FIRST) {
		dfunc(d, dat);
	}

	// Walk through the children...
	if (d->type == PROC_DECL) {
		walk_stmt_(sfunc, efunc, d->data.proc.body, dat, 1, order);
	}

	if (order == CHILD_FIRST) {
		dfunc(d, dat);
	}
}

static void free_expr_helper(struct expr *e, void *dat, int depth)
{
	(void)dat;
	(void)depth;

	free(e);
}

static void free_stmt_helper(struct stmt *s, void *dat, int depth)
{
	(void)dat;
	(void)depth;

	free(s);
}

static void free_decl_helper(struct decl *d, void *dat)
{
	(void)dat;

	free(d);
}

void free_expr(struct expr *e)
{
	walk_expr(&free_expr_helper, e, NULL, CHILD_FIRST);
}

void free_stmt(struct stmt *s)
{
	walk_stmt(&free_stmt_helper, &free_expr_helper,
                  s, NULL, CHILD_FIRST);
}

void free_decl(struct decl *d)
{
	walk_decl(&free_decl_helper, &free_stmt_helper, &free_expr_helper,
                  d, NULL, CHILD_FIRST);
}
