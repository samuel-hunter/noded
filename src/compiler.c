/*
 * compiler - compile an AST to bytecode
 */
#include <err.h>
#include <stdlib.h>
#include <string.h>

#include "noded.h"
#include "bytecode.h"
#include "ast.h"


enum {
	ASM_OP = 1,                  // OP_*
	ASM_PUSH = ASM_OP + 1,       // OP_PUSH <lit>
	ASM_ADDR = 2,                // <addr lit>
	ASM_JMP = ASM_OP + ASM_ADDR, // OP_*JMP <addr_lit>
	ASM_RECV = ASM_OP + 1        // OP_RECV# <data>
};

struct context {
	size_t vars[PROC_VARS];
	size_t ports[PROC_PORTS];

	uint8_t nvars;
	uint8_t nports;

	// Vector of leftover addresses from goto statements to
	// resolve at the end of compilation.
	struct {
		const struct position *pos; // Not owned.
		size_t label_id;
		uint8_t *addrbuf; // Not owned.
	} *gotos;
	size_t ngotos;
	size_t gotos_cap;

	// Vector of labels to resolve goto statments.
	struct {
		size_t label_id;
		uint16_t addr;
	} *labels;
	size_t nlabels;
	size_t labels_cap;

	// loop block handles information necessary for `break` and `continue`.
	struct block {
		bool is_stack;

		// for `continue`, addresses to be filled at the end
		// of the block.
		uint8_t **continues;
		size_t continue_cap;
		size_t ncontinues;

		// for `break`, addresses to be filled at the end of
		// the block.
		uint8_t **breaks;
		size_t break_cap;
		size_t nbreaks;
	} block;

	uint8_t *start; // not owned by the struct.
};

static void scan_expr(const struct expr *expr, size_t *n);
static void scan_stmt(const struct stmt *stmt, size_t *n);

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

	// Add in a nice value of 0xFFFF that can be very easily
	// spot-checked to be an skipped address.
	buf = asm_addr(UINT16_MAX, buf);
	return buf;
}

// Assemble a receive statement.
static uint8_t *asm_recv(uint8_t dest_store, uint8_t src_store,
	bool is_port, uint8_t *buf)
{
	*buf++ = OP_RECV0 + src_store;

	*buf = 0;
	*buf |= dest_store & RECV_STORE_MASK;
	*buf |= is_port ? RECV_PORT_FLAG : 0;
	buf++;

	return buf;
}

// ---Context---

static void init_context(struct context *ctx, uint8_t *start)
{
	memset(ctx, 0, sizeof(*ctx));
	ctx->start = start;
}

static uint8_t store(struct context *ctx, const struct store_expr *store)
{
	size_t *stores;
	uint8_t *nstores;
	uint8_t storemax;
	const char *name;

	if (store->kind == VARIABLE) {
		stores = ctx->vars;
		nstores = &ctx->nvars;
		storemax = PROC_VARS;
		name = "variables";
	} else {
		stores = ctx->ports;
		nstores = &ctx->nports;
		storemax = PROC_PORTS;
		name = "stores";
	}

	for (uint8_t i = 0; i < *nstores; i++) {
		if (stores[i] == store->name_id)
			return i;
	}

	// Check if we can add another store.
	if (*nstores == storemax) {
		// Too many stores.
		send_error(&store->start, ERR,
			"Too many %s in proc node (max %d per node)",
			name, storemax);
		return 0;
	}

	// Add another port
	stores[*nstores] = store->name_id;
	return (*nstores)++;
}

static uint16_t abs_addr(const struct context *ctx, const uint8_t *buf)
{
	return (uint16_t)(buf - ctx->start);
}

// Add a new block to the loop stack. Move the previous stack to *blk.
static void push_stack(struct context *ctx, struct block *blk)
{
	*blk = ctx->block;
	memset(&ctx->block, 0, sizeof(ctx->block));
	ctx->block.is_stack = true;
}

// Reserve room for a pointer to the code, and return its address.
static uint8_t **add_break(struct context *ctx)
{
	if (ctx->block.nbreaks == ctx->block.break_cap) {
		if (ctx->block.break_cap == 0) {
			ctx->block.break_cap = 8;
		} else {
			ctx->block.break_cap *= 2;
		}
		ctx->block.breaks = erealloc(ctx->block.breaks,
			ctx->block.break_cap * sizeof(ctx->block.breaks));
	}

	return &ctx->block.breaks[ctx->block.nbreaks++];
}

// Reserve room for a pointer to the code, and return its address.
static uint8_t **add_continue(struct context *ctx)
{
	if (ctx->block.ncontinues == ctx->block.continue_cap) {
		if (ctx->block.continue_cap == 0) {
			ctx->block.continue_cap = 8;
		} else {
			ctx->block.continue_cap *= 2;
		}
		ctx->block.continues = erealloc(ctx->block.continues,
			ctx->block.continue_cap * sizeof(ctx->block.continues));
	}

	return &ctx->block.continues[ctx->block.ncontinues++];
}

static bool in_block(const struct context *ctx)
{
	return ctx->block.is_stack;
}

// Take care of all endpoints from the stack. Restore the previous
// stack stored in *blk.
static void pop_stack(struct context *ctx, struct block *blk,
	uint8_t *breakpoint, uint8_t *continuepoint)
{
	// Fill all `break` addresses with *breakpoint.
	uint16_t break_addr = abs_addr(ctx, breakpoint);
	for (size_t i = 0; i < ctx->block.nbreaks; i++) {
		asm_addr(break_addr, ctx->block.breaks[i]);
	}
	free(ctx->block.breaks);

	// Ditto with continue
	uint16_t continue_addr = abs_addr(ctx, continuepoint);
	for (size_t i = 0; i < ctx->block.ncontinues; i++) {
		asm_addr(continue_addr, ctx->block.continues[i]);
	}
	free(ctx->block.continues);

	// Restore the previous stack
	ctx->block = *blk;
}

// Return a pointer to the label's address. If the pointer is NULL,
// the label is not found. Otherwise, dereference this to find the
// address value.
static const uint16_t *label_addr(const struct context *ctx, size_t label_id)
{
	for (size_t i = 0; i < ctx->nlabels; i++) {
		if (ctx->labels[i].label_id == label_id)
			return &ctx->labels[i].addr;
	}

	return NULL;
}

// Register a goto statement to be handled later. Return a pointer to
// write the pointer where the goto label's address should be stored.
static uint8_t **add_goto(struct context *ctx, const struct branch_stmt *branch)
{
	if (!ctx->gotos) {
		// Lazily allocate a vector only when needed (most
		// processors probably will not have any goto's)
		ctx->gotos_cap = 4;
		ctx->gotos = ecalloc(ctx->gotos_cap,
			sizeof(*ctx->gotos));
	} else if (ctx->ngotos == ctx->gotos_cap) {
		// Expand when necessary
		ctx->gotos_cap *= 2;
		ctx->gotos = erealloc(ctx->gotos,
			ctx->gotos_cap*sizeof(*ctx->gotos));
	}

	ctx->gotos[ctx->ngotos].pos = &branch->start;
	ctx->gotos[ctx->ngotos].label_id = branch->label_id;
	return &ctx->gotos[ctx->ngotos++].addrbuf;
}

// Add a label to the label dictionary. They will be used at the end
// to resolve all goto statements.
static void add_label(struct context *ctx, const struct labeled_stmt *label,
	const uint8_t *buf)
{
	// Make sure a label isn't twice-defined.
	if (label_addr(ctx, label->label_id)) {
		send_error(&label->start, ERR,
			"Label defined multiple times");
	}

	if (!ctx->labels) {
		// Lazily allocate a vector only when needed. (most
		// processors probably will not have any labels)
		ctx->labels_cap = 4;
		ctx->labels = ecalloc(ctx->labels_cap,
			sizeof(*ctx->labels));
	} else if (ctx->nlabels == ctx->labels_cap) {
		// Expand vector when necessary
		ctx->labels_cap *= 2;
		ctx->labels = erealloc(ctx->labels,
			ctx->labels_cap*sizeof(*ctx->labels));
	}

	ctx->labels[ctx->nlabels].label_id = label->label_id;
	ctx->labels[ctx->nlabels++].addr = abs_addr(ctx, buf);
}

// Fill in all empty addresses from GOTO statments with their
// associated labels and clear all goto's and labels from memory.
static void resolve_gotos(struct context *ctx)
{
	for (size_t i = 0; i < ctx->ngotos; i++) {
		const uint16_t *addrptr =
			label_addr(ctx, ctx->gotos[i].label_id);

		if (addrptr == NULL) {
			send_error(ctx->gotos[i].pos, ERR,
				"Goto to undefined label");
			continue;
		}

		asm_addr(*addrptr, ctx->gotos[i].addrbuf);
	}

	// All good! Free the label and goto vectors since we no
	// longer need them.
	free(ctx->gotos);
	ctx->gotos = NULL;
	free(ctx->labels);
	ctx->labels = NULL;
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
			buf = asm_op(OP_LOAD0 +
				store(ctx, &expr->data.unary.x->data.store),
				buf);
			buf = asm_op(code +
				store(ctx, &expr->data.unary.x->data.store),
				buf);
			buf = asm_op(OP_POP, buf);
			return buf;
		} else {
			// ++x, --x
			buf = asm_op(code +
				store(ctx, &expr->data.unary.x->data.store),
				buf);
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
		buf = asm_op(OP_SAVE0 +
			store(ctx, &expr->data.binary.x->data.store), buf);
		return buf;
	} else if (op >= MUL_ASSIGN && op <= OR_ASSIGN) {
		// Assumed X is a Stoer of VARIABLE kind.

		// x OP_ASSIGN y compiles to x = x OP y.
		buf = asm_op(OP_LOAD0 +
			store(ctx, &expr->data.binary.x->data.store), buf);
		buf = compile_expr(expr->data.binary.y, ctx, buf);
		buf = asm_op(OP_MUL + (op - MUL_ASSIGN), buf);
		buf = asm_op(OP_SAVE0 +
			store(ctx, &expr->data.binary.x->data.store), buf);
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
		buf = asm_op(OP_LOAD0 + store(ctx, &expr->data.store), buf);
		return buf;
	}

	errx(1, "Unforseen expression.");
}

static void scan_expr_stmt(const struct stmt *stmt, size_t *n)
{
	scan_expr(stmt->data.expr.x, n);
	*n += ASM_OP;
	return;
}

static uint8_t *compile_expr_stmt(const struct stmt *stmt,
	struct context *ctx, uint8_t *buf)
{
	buf = compile_expr(stmt->data.expr.x, ctx, buf);
	buf = asm_op(OP_POP, buf); // Discard the expression's value.
	return buf;
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
		buf = asm_jump2(OP_JMP, add_break(ctx), buf);
		break;
	case GOTO:
		buf = asm_jump2(OP_JMP, add_goto(ctx, branch), buf);
		break;
	case CONTINUE:
		buf = asm_jump2(OP_JMP, add_continue(ctx), buf);
		break;
	default:
		send_error(&branch->start, FATAL,
			"Compiler bug: Malformed branch stmt");
	}

	return buf;
}

static void scan_block_stmt(const struct stmt *stmt, size_t *n)
{
	for (size_t i = 0; i < stmt->data.block.nstmts; i++) {
		scan_stmt(stmt->data.block.stmt_list[i], n);
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

static void scan_if_stmt(const struct stmt *stmt, size_t *n)
{
	scan_expr(stmt->data.if_stmt.cond, n);
	*n += ASM_JMP;
	scan_stmt(stmt->data.if_stmt.body, n);

	if (stmt->data.if_stmt.otherwise != NULL) {
		*n += ASM_JMP;
		scan_stmt(stmt->data.if_stmt.otherwise, n);
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

static void scan_loop_stmt(const struct stmt *stmt, size_t *n)
{
	const struct loop_stmt *loop = &stmt->data.loop;

	if (loop->init != NULL)
		scan_stmt(loop->init, n);

	if (loop->exec_body_first) {
		// do { ... } while ( ... );
		scan_stmt(loop->body, n);
		scan_expr(loop->cond, n);
		*n += ASM_JMP;
	} else {
		// while ( ... ) { ... }
		scan_expr(loop->cond, n);
		*n += ASM_JMP;
		scan_stmt(loop->body, n);

		if (loop->post != NULL) {
			scan_stmt(loop->post, n);
		}

		*n += ASM_JMP;
	}
}

static uint8_t *compile_loop_stmt(const struct stmt *stmt,
	struct context *ctx, uint8_t *buf)
{
	uint8_t *start, *continuepoint, *end;
	struct block blk;

	const struct loop_stmt *loop = &stmt->data.loop;

	if (loop->init != NULL)
		buf = compile_stmt(loop->init, ctx, buf);

	push_stack(ctx, &blk);

	start = continuepoint = buf;

	if (loop->exec_body_first) {
		// do { ... }
		buf = compile_stmt(loop->body, ctx, buf);

		// while ( ... );
		buf = compile_expr(loop->cond, ctx, buf);
		buf = asm_jump(OP_TJMP, abs_addr(ctx, continuepoint), buf);
	} else {
		// while ( ... )
		buf = compile_expr(loop->cond, ctx, buf);
		buf = asm_jump2(OP_FJMP, &end, buf);

		// { ... }
		buf = compile_stmt(loop->body, ctx, buf);

		if (loop->post != NULL) {
			continuepoint = buf;
			buf = compile_stmt(loop->post, ctx, buf);
		}
		buf = asm_jump(OP_JMP, abs_addr(ctx, start), buf);

		asm_addr(abs_addr(ctx, buf), end);
	}

	pop_stack(ctx, &blk, buf, continuepoint);
	return buf;
}

static void scan_send_stmt(const struct stmt *stmt, size_t *n)
{
	const struct store_expr *dest = &stmt->data.send.dest;
	const struct expr *src = stmt->data.send.src;

	if (src->type == STORE_EXPR && src->data.store.kind == PORT) {
		// %port|$var <- %port
		*n += ASM_RECV;
	} else if (dest->kind == PORT) {
		// %port <- expr
		scan_expr(src, n);
		*n += ASM_OP;
	} else {
		// $var <- expr; doesn't make sense
		send_error(&stmt->data.send.oppos, ERR,
			"Expected dest or source to be a port");
	}
}

static uint8_t *compile_send_stmt(const struct stmt *stmt,
	struct context *ctx, uint8_t *buf)
{
	const struct store_expr *dest = &stmt->data.send.dest;
	const struct expr *src = stmt->data.send.src;

	if (src->type == STORE_EXPR && src->data.store.kind == PORT) {
		// %port|$var <- %port
		bool is_port = dest->kind == PORT;
		buf = asm_recv(store(ctx, dest),
			store(ctx, &src->data.store), is_port, buf);
	} else {
		// %port <- expr
		buf = compile_expr(src, ctx, buf);
		buf = asm_op(OP_SEND0 + store(ctx, dest), buf);
	}

	return buf;
}

static void scan_stmt(const struct stmt *stmt, size_t *n)
{
	switch (stmt->type) {
	case BAD_STMT:
		send_error(&stmt->data.bad.start, FATAL, "Bad statement");
		break;
	case EMPTY_STMT:
		*n += ASM_OP;
		break;
	case LABELED_STMT:
		scan_stmt(stmt->data.labeled.stmt, n);
		break;
	case EXPR_STMT:
		scan_expr_stmt(stmt, n);
		break;
	case BRANCH_STMT:
		*n += ASM_JMP;
		break;
	case BLOCK_STMT:
		scan_block_stmt(stmt, n);
		break;
	case IF_STMT:
		scan_if_stmt(stmt, n);
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
		scan_loop_stmt(stmt, n);
		break;
	case SEND_STMT:
		scan_send_stmt(stmt, n);
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
		add_label(ctx, &stmt->data.labeled, buf);
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
	case SEND_STMT:
		return compile_send_stmt(stmt, ctx, buf);
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

	scan_stmt(proc->body, &size);

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

	code = ecalloc(size, sizeof(*code));
	init_context(&ctx, code);
	end = compile_stmt(proc->body, &ctx, code);
	resolve_gotos(&ctx);

	// Safety-check
	if ((size_t)(end-code) > size) {
		errx(1, "Compiler error: Buffer overflow.");
	} else if ((size_t)(end-code) < size) {
		errx(1, "Compiler error: Buffer underwrite.");
	}

	if (has_errors()) {
		free(code);
		*n = 0;
		return NULL;
	} else {
		*n = (uint16_t)size;
		return code;
	}
}
