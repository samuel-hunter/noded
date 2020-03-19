#include <err.h>
#include <stdlib.h>
#include <string.h>

#include "noded.h"

enum {
	ASM_OP = 1,                 // OP_*
	ASM_PUSH = ASM_OP + 1,      // OP_PUSH <lit>
	ASM_ADDR = 2,               // <addr lit>
	ASM_JMP = ASM_OP + ASM_ADDR // OP_*JMP <addr_lit>
};

struct context {
	size_t vars[PROC_VARS];
	size_t ports[PROC_PORTS];

	uint8_t nvars;
	uint8_t nports;

	struct {
		size_t label_id;
		uint16_t addr;
	} *labels;
	size_t nlabels;
	size_t labelcap;

	// loop block handles information necessary for `break` and `continue`.
	struct block {
		uint16_t start_addr;

		// for `break`, addresses to be filled at the end of
		// the block.
		uint8_t **endpoints;
		size_t endpointscap;
		size_t nendpoints;
	} block;

	uint8_t *start; // not owned by the struct.
};

static void scan_expr(const struct expr *expr, size_t *n);
static void scan_stmt(const struct stmt *stmt, struct context *ctx, size_t *n);

static uint8_t *compile_expr(const struct expr *expr,
	struct context *ctx, uint8_t *buf);
static uint8_t *compile_stmt(const struct stmt *stmt,
	struct context *ctx, uint8_t *buf);

/// ---Assembler Directives---

// Assemble a simple opcode.
static uint8_t *asm_op(enum opcode code, uint8_t *buf)
{
	*buf++ = code;
	return buf;
}

// Push a number to the stack.
static uint8_t *asm_push(uint8_t val, uint8_t *buf)
{
	*buf++ = OP_PUSH;
	*buf++ = val;
	return buf;
}

// Assemble an address.
static uint8_t *asm_addr(uint16_t addr, uint8_t *buf)
{
	*buf++ = (addr >> 8) & 0xff;
	*buf++ = addr & 0xff;
	return buf;
}

// Assemble a jump statement and its address.
static uint8_t *asm_jump(enum opcode code, uint16_t addr, uint8_t *buf)
{
	*buf++ = code;
	buf = asm_addr(addr, buf);
	return buf;
}

// Assemble a jump statement, and set *addr to where to write the
// address later. This is useful for `break` statements or conditional
// statements, where the address is not known until later.
static uint8_t *asm_jump2(enum opcode code, uint8_t **addr, uint8_t *buf)
{
	*buf++ = code;
	*addr = buf;
	return buf + ASM_ADDR;
}

// ---Context---

static void init_context(struct context *ctx)
{
	memset(ctx, 0, sizeof(*ctx));
	ctx->labelcap = 8;
	ctx->labels = emalloc(ctx->labelcap * sizeof(*ctx->labels));
}

static uint8_t var(struct context *ctx, const struct expr *store)
{
	// Check through pre-known vars.
	for (uint8_t i = 0; i < ctx->nvars; i++) {
		if (ctx->vars[i] == store->data.store.name_id)
			return i;
	}

	// Check if we can add another var.
	if (ctx->nvars == PROC_VARS) {
		// Too many vars.
		send_error(&store->data.store.start, ERR,
			"Too many vars in proc node (max %d per node)",
			PROC_VARS);
		return 0;
	}

	// Add another var.
	ctx->vars[ctx->nvars] = store->data.store.name_id;
	return ctx->nvars++;
}

static int port(struct context *ctx, const struct expr *store)
{
	// Check through pre-known ports.
	for (uint8_t i = 0; i < ctx->nports; i++) {
		if (ctx->ports[i] == store->data.store.name_id)
			return i;
	}

	// Check if we can add another port.
	if (ctx->nports == PROC_PORTS) {
		// Too many ports.
		send_error(&store->data.store.start, ERR,
			"Too many ports in proc node (max %d per node)",
			PROC_PORTS);
		return 0;
	}

	// Add another port.
	ctx->ports[ctx->nports] = store->data.store.name_id;
	return ctx->nports++;
}

static uint16_t abs_addr(const struct context *ctx, uint8_t *buf)
{
	return (uint16_t)(buf - ctx->start);
}

static void add_label(struct context *ctx,
	const struct labeled_stmt *label, uint16_t addr)
{
	// First, check to see if the label already exists.
	for (size_t i = 0; i < ctx->nlabels; i++) {
		if (ctx->labels[i].label_id == label->label_id) {
			send_error(&label->start, ERR,
				"Label already defined elsewhere");
			return;
		}
	}

	if (ctx->nlabels == ctx->labelcap) {
		ctx->labelcap *= 2;
		ctx->labels = erealloc(ctx->labels,
			ctx->labelcap * sizeof(*ctx->labels));
	}

	ctx->labels[ctx->nlabels].label_id = label->label_id;
	ctx->labels[ctx->nlabels].addr = addr;
	ctx->nlabels++;
}

static uint16_t label_addr(const struct context *ctx,
	const struct branch_stmt *branch)
{
	// find the label in the associative array.
	for (size_t i = 0; i < ctx->nlabels; i++) {
		if (ctx->labels[i].label_id == branch->label_id) {
			return ctx->labels[i].addr;
		}
	}

	// label not found; send error and default value.
	send_error(&branch->start, ERR,
		"Label not defined");
	return 0;
}

// Add a new block to the loop stack. Move the previous stack to *blk.
static void push_stack(struct context *ctx, struct block *blk, uint8_t *buf)
{
	*blk = ctx->block;
	memset(&ctx->block, 0, sizeof(ctx->block));

	ctx->block.start_addr = abs_addr(ctx, buf);
	ctx->block.endpointscap = 8;
	ctx->block.endpoints = emalloc(
		ctx->block.endpointscap * sizeof(ctx->block.endpoints));
}

// Reserve room for a pointer to the code, and return its address.
static uint8_t **add_endpoint(struct context *ctx)
{
	if (ctx->block.nendpoints == ctx->block.endpointscap) {
		ctx->block.endpointscap *= 2;
		ctx->block.endpoints = erealloc(ctx->block.endpoints,
			ctx->block.endpointscap * sizeof(ctx->block.endpoints));
	}

	return &ctx->block.endpoints[ctx->block.nendpoints++];
}

static bool in_block(const struct context *ctx)
{
	return ctx->block.endpoints != NULL;
}

// Take care of all endpoints from the stack. Restore the previous
// stack stored in *blk.
static void pop_stack(struct context *ctx, struct block *blk, uint8_t *buf)
{
	// Fill all `break` addresses with *buf.
	uint16_t end_addr = abs_addr(ctx, buf);
	for (size_t i = 0; i < ctx->block.nendpoints; i++) {
		asm_addr(end_addr, ctx->block.endpoints[i]);
	}

	// Free the endpoint array
	free(ctx->block.endpoints);

	// Restore the previous stack
	ctx->block = *blk;
}

static void clear_context(struct context *ctx)
{
	free(ctx->labels);
}

// ---Scanning and Compilation---

static void scan_unary_expr(const struct expr *expr, size_t *n)
{
	switch (expr->data.unary.op) {
	case INC:
	case DEC:
		// INC and DEC is only compatible with a VARIABLE StoreExpr
		if (!(expr->data.unary.x->type == STORE_EXPR &&
			expr->data.unary.x->data.store.kind == VARIABLE)) {

			send_error(&expr->data.unary.start, ERR,
				"variable required as increment/decrement operand");
			return;
		}

		if (expr->data.unary.is_suffix) {
			*n += ASM_OP + ASM_OP + ASM_OP;
		} else {
			*n += ASM_OP;
		}
		break;
	case LNOT:
	case NOT:
		scan_expr(expr->data.unary.x, n);
		*n += ASM_OP;
		break;
	default:
		send_error(&expr->data.unary.start, FATAL,
			"Parser bug: Malformed unary expression");
	}
}

static uint8_t *compile_unary_expr(const struct expr *expr,
	struct context *ctx, uint8_t *buf)
{
	enum token op = expr->data.unary.op;
	enum opcode code;

	switch (op) {
	case INC:
	case DEC:
		code = (op == INC ? OP_INC0 : OP_DEC0);

		if (expr->data.unary.is_suffix) {
			// x++, x--
			buf = asm_op(OP_LOAD0 + var(ctx, expr->data.unary.x), buf);
			buf = asm_op(code + var(ctx, expr->data.unary.x), buf);
			buf = asm_op(OP_POP, buf);
			return buf;
		} else {
			// ++x, --x
			buf = asm_op(code + var(ctx, expr->data.unary.x), buf);
			return buf;
		}
	case LNOT:
		buf = compile_expr(expr->data.unary.x, ctx, buf);
		buf = asm_op(OP_LNOT, buf);
		return buf;
	case NOT:
		buf = compile_expr(expr->data.unary.x, ctx, buf);
		buf = asm_op(OP_NEGATE, buf);
		return buf;
	default:
		send_error(&expr->data.unary.start, FATAL,
			"Parser bug: Malformed unary expression");
		return NULL;
	}
}

static void scan_binary_expr(const struct expr *expr, size_t *n)
{
	const struct binary_expr *binary = &expr->data.binary;

	enum token op = binary->op;
	if (op >= MUL && op <= LNOT) {
		// x OP y
		scan_expr(binary->x, n);
		scan_expr(binary->y, n);
		*n += ASM_OP;
	} else if (op == ASSIGN) {
		// x = y
		if (!(binary->x->type == STORE_EXPR && binary->x->data.store.kind == VARIABLE)) {
			send_error(&binary->oppos, ERR,
				"Expected variable at lvalue of assign expression");
		}

		scan_expr(binary->y, n);
		*n += ASM_OP;
	} else if (op >= MUL_ASSIGN && op <= OR_ASSIGN) {
		// x = x OP y
		if (!(binary->x->type == STORE_EXPR && binary->x->data.store.kind == VARIABLE)) {
			send_error(&binary->oppos, ERR,
				"Expected variable at lvalue of assign expression");
		}

		*n += ASM_OP;
		scan_expr(binary->y, n);
		*n += ASM_OP + ASM_OP;
	} else if (op == COMMA) {
		// x y
		scan_expr(binary->x, n);
		*n += ASM_OP;
		scan_expr(binary->y, n);
	} else if (op == SEND) {
		// x <- y
		send_error(&binary->oppos, ERR,
			"Send operation found nested within expression");
	} else {
		send_error(&binary->oppos, FATAL,
			"Parser bug: Malformed binary expression");
	}
}

static uint8_t *compile_binary_expr(const struct expr *expr,
	struct context *ctx, uint8_t *buf)
{
	enum token op = expr->data.binary.op;
	if (op >= MUL && op <= LNOT) {
		enum opcode code = OP_MUL + (op - MUL);
		buf = compile_expr(expr->data.binary.x, ctx, buf);
		buf = compile_expr(expr->data.binary.y, ctx, buf);
		buf = asm_op(code, buf);
		return buf;
	} else if (op == ASSIGN) {
		// Assumed X is a Store of VARIABLE kind.
		buf = compile_expr(expr->data.binary.y, ctx, buf);
		buf = asm_op(OP_SAVE0 + var(ctx, expr->data.binary.x), buf);
		return buf;
	} else if (op >= MUL_ASSIGN && op <= OR_ASSIGN) {
		// Assumed X is a Stoer of VARIABLE kind.

		// x OP_ASSIGN y compiles to x = x OP y.
		buf = asm_op(OP_LOAD0 + var(ctx, expr->data.binary.x), buf);
		buf = compile_expr(expr->data.binary.y, ctx, buf);
		buf = asm_op(OP_MUL + (op - MUL_ASSIGN), buf);
		buf = asm_op(OP_SAVE0 + var(ctx, expr->data.binary.x), buf);
		return buf;
	} else if (op == COMMA) {
		buf = compile_expr(expr->data.binary.x, ctx, buf);
		buf = asm_op(OP_POP, buf);
		buf = compile_expr(expr->data.binary.y, ctx, buf);
		return buf;
	} else if (op == SEND) {
		send_error(&expr->data.binary.oppos, FATAL,
			"send operation found nested within expression");
		return NULL; // fatal send_error should stop execution.
	} else {
		send_error(&expr->data.binary.oppos, FATAL,
			"Parser bug: Malformed binary expression");
		return NULL; // fatal send_error should stop execution.
	}
}

static void scan_cond_expr(const struct expr *expr, size_t *n)
{
	scan_expr(expr->data.cond.cond, n);
	*n += ASM_JMP;
	scan_expr(expr->data.cond.when, n);
	*n += ASM_JMP;
	scan_expr(expr->data.cond.otherwise, n);
}

static uint8_t *compile_cond_expr(const struct expr *expr,
	struct context *ctx, uint8_t *buf)
{
	uint8_t *faddr, *endaddr;

	buf = compile_expr(expr->data.cond.cond, ctx, buf);
	buf = asm_jump2(OP_FJMP, &faddr, buf);
	buf = compile_expr(expr->data.cond.when, ctx, buf);
	buf = asm_jump2(OP_JMP, &endaddr, buf);
	asm_addr(abs_addr(ctx, buf), faddr);
	buf = compile_expr(expr->data.cond.otherwise, ctx, buf);
	asm_addr(abs_addr(ctx, buf), endaddr);
	return buf;
}

static void scan_expr(const struct expr *expr, size_t *n)
{
	switch (expr->type) {
	case BAD_EXPR:
		send_error(&expr->data.bad.start, FATAL, "Parser bug: Bad expression");
		break;
	case NUM_LIT_EXPR:
		*n += ASM_PUSH;
		break;
	case PAREN_EXPR:
		scan_expr(expr->data.paren.x, n);
		break;
	case UNARY_EXPR:
		scan_unary_expr(expr, n);
		break;
	case BINARY_EXPR:
		scan_binary_expr(expr, n);
		break;
	case COND_EXPR:
		scan_cond_expr(expr, n);
		break;
	case STORE_EXPR:
		if (expr->data.store.kind == PORT) {
			send_error(&expr->data.store.start, ERR,
				"Unexpected port found within expression");
		}

		*n += ASM_OP;
		break;
	}
}

static uint8_t *compile_expr(const struct expr *expr,
	struct context *ctx, uint8_t *buf)
{
	switch (expr->type) {
	case BAD_EXPR:
		send_error(&expr->data.bad.start, FATAL, "Bad expression");
		return NULL; // send_error should stop execution
	case NUM_LIT_EXPR:
		return asm_push(expr->data.num_lit.value, buf);
	case PAREN_EXPR:
		return compile_expr(expr->data.paren.x, ctx, buf);
	case UNARY_EXPR:
		return compile_unary_expr(expr, ctx, buf);
	case BINARY_EXPR:
		return compile_binary_expr(expr, ctx, buf);
	case COND_EXPR:
		return compile_cond_expr(expr, ctx, buf);
	case STORE_EXPR:
		// Type is assumed VARIABLE
		buf = asm_op(OP_LOAD0 + var(ctx, expr), buf);
		return buf;
	}

	errx(1, "Unforseen expression.");
}

static void scan_expr_stmt(const struct stmt *stmt, size_t *n)
{
	struct expr *x = stmt->data.expr.x;
	if (!(x->type == BINARY_EXPR && x->data.binary.op == SEND)) {
		// Normal case: non-send expressions.
		scan_expr(stmt->data.expr.x, n);
		*n += ASM_OP;
		return;
	}

	// Handle special-case <- statmeent. They can be of either $var <- %port,
	// %port <- %port, or %port <- expr.
	if (x->data.binary.x->type != STORE_EXPR) {
		// The lvalue must be a store (port or a var).
		send_error(&stmt->data.expr.start, ERR,
			"lvalue is not a port or a variable");
	} else if (x->data.binary.x->data.store.kind == VARIABLE) {
		// $var <- %port
		if (x->data.binary.y->type == STORE_EXPR &&
			x->data.binary.y->data.store.kind == PORT) {

			*n += ASM_OP + ASM_OP + ASM_OP;
		} else {
			send_error(&x->data.binary.oppos, ERR,
				"rvalue is not a port");
		}
	} else if (x->data.binary.y->type == STORE_EXPR &&
		x->data.binary.y->data.store.kind == PORT) {
		// %port <- %port
		*n += ASM_OP + ASM_OP;
	} else {
		// %port <- expr
		scan_expr(x->data.binary.y, n);
		*n += ASM_OP;
	}
}

static uint8_t *compile_expr_stmt(const struct stmt *stmt,
	struct context *ctx, uint8_t *buf)
{
	struct expr *x = stmt->data.expr.x;
	if (!(x->type == BINARY_EXPR && x->data.binary.op == SEND)) {
		// Normal case: non `<-` expressions.
		buf = compile_expr(stmt->data.expr.x, ctx, buf);
		buf = asm_op(OP_POP, buf); // Discard the expression's value.
		return buf;
	}

	// Handle special-case send <- statement.

	// The three forms of x <- y are $var <- %port, %port <-
	// %port, and %port <- expr.
	if (x->data.binary.x->data.store.kind == VARIABLE) {
		// Let's work on $var <- %port w/ OP_RECV first!

		buf = asm_op(OP_RECV0 + port(ctx, x->data.binary.y), buf);
		buf = asm_op(OP_SAVE0 + var(ctx, x->data.binary.x), buf);
		buf = asm_op(OP_POP, buf);
		return buf;
	} else if (x->data.binary.y->type == STORE_EXPR &&
		x->data.binary.y->data.store.kind == PORT) {
		// %port <- %port
		buf = asm_op(OP_RECV0 + port(ctx, x->data.binary.y), buf);
		buf = asm_op(OP_SEND0 + port(ctx, x->data.binary.x), buf);
		return buf;
	} else {
		// %port <- expr

		buf = compile_expr(x->data.binary.y, ctx, buf);
		buf = asm_op(OP_SEND0 + port(ctx, x->data.binary.x), buf);
		return buf;
	}
}

static uint8_t *compile_branch_stmt(const struct stmt *stmt,
	struct context *ctx, uint8_t *buf)
{
	const struct branch_stmt *branch = &stmt->data.branch;

	if (branch->tok != GOTO && !in_block(ctx)) {
		send_error(&branch->start, ERR,
			"break or continue outside loop block");
		buf += ASM_JMP; // skip operation that would have
				 // been assembled.
		return buf;
	}

	switch (branch->tok) {
	case BREAK:
		buf = asm_jump2(OP_JMP, add_endpoint(ctx), buf);
		break;
	case GOTO:
		buf = asm_jump(OP_JMP, label_addr(ctx, branch), buf);
		break;
	case CONTINUE:
		buf = asm_jump(OP_JMP, ctx->block.start_addr, buf);
		break;
	default:
		send_error(&stmt->data.branch.start, FATAL,
			"Compiler bug: Malformed branch stmt");
	}

	return buf;
}

static void scan_block_stmt(const struct stmt *stmt, struct context *ctx, size_t *n)
{
	for (size_t i = 0; i < stmt->data.block.nstmts; i++) {
		scan_stmt(stmt->data.block.stmt_list[i], ctx, n);
	}
}

static uint8_t *compile_block_stmt(const struct stmt *stmt,
	struct context *ctx, uint8_t *buf)
{
	for (size_t i = 0; i < stmt->data.block.nstmts; i++) {
		struct stmt *s = stmt->data.block.stmt_list[i];
		buf = compile_stmt(s, ctx, buf);
	}

	return buf;
}

static void scan_if_stmt(const struct stmt *stmt, struct context *ctx, size_t *n)
{
	scan_expr(stmt->data.if_stmt.cond, n);
	*n += ASM_JMP;
	scan_stmt(stmt->data.if_stmt.body, ctx, n);

	if (stmt->data.if_stmt.otherwise != NULL) {
		*n += ASM_JMP;
		scan_stmt(stmt->data.if_stmt.otherwise, ctx, n);
	}
}

static uint8_t *compile_if_stmt(const struct stmt *stmt,
	struct context *ctx, uint8_t *buf)
{
	// Address to jump if the condition is false
	uint8_t *addr_false;
	// The end of the if statement
	uint8_t *addr_end;

	// if (cond)
	buf = compile_expr(stmt->data.if_stmt.cond, ctx, buf);
	buf = asm_jump2(OP_FJMP, &addr_false, buf);

	// { ... }
	buf = compile_stmt(stmt->data.if_stmt.body, ctx, buf);
	if (stmt->data.if_stmt.otherwise != NULL) {
		buf = asm_jump2(OP_JMP, &addr_end, buf);
	}

	// else { ... }

	// jump here if the condition is false
	asm_addr(abs_addr(ctx, buf), addr_false);

	if (stmt->data.if_stmt.otherwise != NULL) {
		buf = compile_stmt(stmt->data.if_stmt.otherwise, ctx, buf);
		asm_addr(abs_addr(ctx, buf), addr_end);
	}

	return buf;
}

static void scan_loop_stmt(const struct stmt *stmt, struct context *ctx, size_t *n)
{
	const struct loop_stmt *loop = &stmt->data.loop;

	if (loop->init != NULL)
		scan_stmt(loop->init, ctx, n);

	if (loop->exec_body_first) {
		// do { ... } while ( ... );
		scan_stmt(loop->body, ctx, n);
		scan_expr(loop->cond, n);
		*n += ASM_JMP;
	} else {
		// while ( ... ) { ... }
		scan_expr(loop->cond, n);
		*n += ASM_JMP;
		scan_stmt(loop->body, ctx, n);

		if (loop->post != NULL) {
			scan_stmt(loop->post, ctx, n);
		}

		*n += ASM_JMP;
	}
}

static uint8_t *compile_loop_stmt(const struct stmt *stmt,
	struct context *ctx, uint8_t *buf)
{
	uint8_t *addr_start, *end;
	struct block blk;

	const struct loop_stmt *loop = &stmt->data.loop;

	if (loop->init != NULL)
		buf = compile_stmt(loop->init, ctx, buf);

	push_stack(ctx, &blk, buf);

	addr_start = buf;

	if (loop->exec_body_first) {
		// do { ... }
		buf = compile_stmt(loop->body, ctx, buf);

		// while ( ... );
		buf = compile_expr(loop->cond, ctx, buf);
		buf = asm_jump(OP_TJMP, abs_addr(ctx, addr_start), buf);
	} else {
		// while ( ... )
		buf = compile_expr(loop->cond, ctx, buf);
		buf = asm_jump2(OP_FJMP, &end, buf);

		// { ... }
		buf = compile_stmt(loop->body, ctx, buf);
		if (loop->post != NULL)
			buf = compile_stmt(loop->post, ctx, buf);
		buf = asm_jump(OP_JMP, abs_addr(ctx, addr_start), buf);

		asm_addr(abs_addr(ctx, buf), end);
	}

	pop_stack(ctx, &blk, buf);
	return buf;
}

static void scan_stmt(const struct stmt *stmt, struct context *ctx, size_t *n)
{
	switch (stmt->type) {
	case BAD_STMT:
		send_error(&stmt->data.bad.start, FATAL, "Bad statement");
		break;
	case EMPTY_STMT:
		*n += ASM_OP;
		break;
	case LABELED_STMT:
		add_label(ctx, &stmt->data.labeled, (uint16_t)(*n));
		scan_stmt(stmt->data.labeled.stmt, ctx, n);
		break;
	case EXPR_STMT:
		scan_expr_stmt(stmt, n);
		break;
	case BRANCH_STMT:
		*n += ASM_JMP;
		break;
	case BLOCK_STMT:
		scan_block_stmt(stmt, ctx, n);
		break;
	case IF_STMT:
		scan_if_stmt(stmt, ctx, n);
		break;
	case CASE_CLAUSE:
		// TODO
		send_error(&stmt->data.case_clause.start, ERR,
			"Case clauses are currently unsupported");
		break;
	case SWITCH_STMT:
		// TODO
		send_error(&stmt->data.switch_stmt.start, ERR,
			"Switch statements are currently unsupported");
		break;
	case LOOP_STMT:
		scan_loop_stmt(stmt, ctx, n);
		break;
	case HALT_STMT:
		*n += ASM_OP;
		break;
	}
}

static uint8_t *compile_stmt(const struct stmt *stmt,
	struct context *ctx, uint8_t *buf)
{
	switch (stmt->type) {
	case BAD_STMT:
		send_error(&stmt->data.bad.start, FATAL, "Bad statement");
		return NULL; // a FATAL send_error should kill the prgram.
	case EMPTY_STMT:
		return asm_op(OP_NOOP, buf);
	case LABELED_STMT:
		return compile_stmt(stmt->data.labeled.stmt, ctx, buf);
	case EXPR_STMT:
		buf = compile_expr_stmt(stmt, ctx, buf);
		return buf;
	case BRANCH_STMT:
		return compile_branch_stmt(stmt, ctx, buf);
	case BLOCK_STMT:
		return compile_block_stmt(stmt, ctx, buf);
	case IF_STMT:
		return compile_if_stmt(stmt, ctx, buf);
	case CASE_CLAUSE:
		// TODO
		send_error(&stmt->data.case_clause.start, FATAL,
			"Unsupported statement");
		return NULL; // Fatal send_error should stop execution
	case SWITCH_STMT:
		// TODO
		send_error(&stmt->data.switch_stmt.start, FATAL,
			"Unsupported statement");
		return NULL; // Fatal send_error should stop execution
	case LOOP_STMT:
		return compile_loop_stmt(stmt, ctx, buf);
	case HALT_STMT:
		return asm_op(OP_HALT, buf);
	}

	errx(1, "Unforseen expression.");
}

uint8_t *compile(const struct proc_decl *proc, uint16_t *n)
{
	size_t size = 0; // Complete size of the program
	struct context ctx;
	uint8_t *code, *end;

	init_context(&ctx);

	scan_stmt(proc->body, &ctx, &size);

	if (has_errors()) {
		*n = 0;
		return NULL;
	}

	if (size > UINT16_MAX) {
		send_error(&proc->start, ERR,
			"Node is too complex; compilation can't fit within %d bytes",
			UINT16_MAX);

		*n = 0;
		return NULL;
	}

	code = emalloc(size);
	ctx.start = code;
	end = compile_stmt(proc->body, &ctx, code);

	// Safety-check
	if ((size_t)(end-code) > size) {
		errx(1, "Compiler error: Buffer overflow.");
	} else if ((size_t)(end-code) < size) {
		errx(1, "Compiler error: Buffer underwrite.");
	}

	clear_context(&ctx);

	if (has_errors()) {
		free(code);
		*n = 0;
		return NULL;
	} else {
		*n = (uint16_t)size;
		return code;
	}
}
