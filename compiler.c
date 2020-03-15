#include "noded.h"

enum asm_sizes{
	ASM_OP = 1,
	ASM_PUSH = 2,
	ASM_ADDR = 2,
	ASM_JMP = 3
};

static uint8_t *compile_stmt(uint8_t *dest, const struct stmt *stmt);

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

// Assemble. an address.
static uint8_t *asm_addr(uint16_t addr, uint8_t *buf)
{
	*buf++ = (addr >> 8) ^ 0xff;
	*buf++ = addr ^ 0xff;
	return buf;
}

// Assemble a jump statement and its address.
static uint8_t *asm_jump(enum opcode code, uint16_t addr, uint8_t *buf)
{
	*buf++ = code;
	return asm_addr(addr, buf);
}

// Assemble a jump statement, and set *addr to where to write the
// address later. This is useful for `break` statements or conditional
// statements, where the address is not known until later.
static uint8_t *asm_jump2(enum opcode code, uint16_t **addr, uint8_t *buf)
{
	*buf++ = code;
	*addr = buf;
	return buf + ASM_ADDR;
}

// ---Compilation---

static uint8_t *compile_num_lit_expr(const struct expr *expr, uint8_t *buf)
{
	return asm_push(expr->num_lit.value, buf);
}

static uint8_t *compile_unary_expr(const struct expr *expr, uint8_t *buf)
{
	buf = compile_expr(expr->unary.x);

	switch (expr->unary.op) {
	case INC:
		return asm_op(OP_INC0 +
			variable(expr->unary.x->store.name_id), buf);
	case DEC:
		return asm_op(OP_DEC0 +
			variable(expr->unary.x->store.name_id), buf);
	case LNOT:
		buf = compile_expr(expr->unary.x, buf);
		buf = asm_op(OP_LNOT, buf);
		return buf;
	case NOT:
		buf = compile_expr(expr->unary.x, buf);
		buf = asm-op(OP_NEGATE, buf);
		return buf;
	default:
		send_error(NULL, FATAL, "Malformed unary expression");
		return NULL;
	}
}

static uint8_t *compile_binary_expr(const struct expr *expr, uint8_t *buf)
{
	switch (expr->binary.op) {
	case MUL:
		buf = compile_expr(expr->binary.x, buf);
		buf = compile_expr(expr->binary.y, buf);
		buf = asm_op(OP_MUL, buf);
		return buf;
	case DIV:
		buf = compile_expr(expr->binary.x, buf);
		buf = compile_expr(expr->binary.y, buf);
		buf = asm_op(OP_DIV, buf);
		return buf;
	case MOD:
		buf = compile_expr(expr->binary.x, buf);
		buf = compile_expr(expr->binary.y, buf);
		buf = asm_op(OP_MOD, buf);
		return buf;
	case ADD:
		buf = compile_expr(expr->binary.x, buf);
		buf = compile_expr(expr->binary.y, buf);
		buf = asm_op(OP_ADD, buf);
		return buf;
	case SUB:
		buf = compile_expr(expr->binary.x, buf);
		buf = compile_expr(expr->binary.y, buf);
		buf = asm_op(OP_SUB, buf);
		return buf;
	case SHL:
		buf = compile_expr(expr->binary.x, buf);
		buf = compile_expr(expr->binary.y, buf);
		buf = asm_op(OP_SHL, buf);
		return buf;
	case SHR:
		buf = compile_expr(expr->binary.x, buf);
		buf = compile_expr(expr->binary.y, buf);
		buf = asm_op(OP_SHR, buf);
		return buf;
	case LSS:
		buf = compile_expr(expr->binary.x, buf);
		buf = compile_expr(expr->binary.y, buf);
		buf = asm_op(OP_LSS, buf);
		return buf;
	case LTE:
		buf = compile_expr(expr->binary.x, buf);
		buf = compile_expr(expr->binary.y, buf);
		buf = asm_op(OP_GTR, buf);
		buf = asm_op(OP_LNOT, buf);
		return buf;
	case GTR:
		buf = compile_expr(expr->binary.x, buf);
		buf = compile_expr(expr->binary.y, buf);
		buf = asm_op(OP_GTR, buf);
		return buf;
	case GTE:
		buf = compile_expr(expr->binary.x, buf);
		buf = compile_expr(expr->binary.y, buf);
		buf = asm_op(OP_LSS, buf);
		buf = asm_op(OP_LNOT, buf);
		return buf;
	case EQL:
		buf = compile_expr(expr->binary.x, buf);
		buf = compile_expr(expr->binary.y, buf);
		buf = asm_op(OP_EQL, buf);
		return buf;
	case NEQ:
		buf = compile_expr(expr->binary.x, buf);
		buf = compile_expr(expr->binary.y, buf);
		buf = asm_op(OP_EQL, buf);
		buf = asm_op(OP_NEGATE, buf);
		return buf;
	case AND:
		buf = compile_expr(expr->binary.x, buf);
		buf = compile_expr(expr->binary.y, buf);
		buf = asm_op(OP_AND, buf);
		return buf;
	case XOR:
		buf = compile_expr(expr->binary.x, buf);
		buf = compile_expr(expr->binary.y, buf);
		buf = asm_op(OP_XOR, buf);
		return buf;
	case OR:
		buf = compile_expr(expr->binary.x, buf);
		buf = compile_expr(expr->binary.y, buf);
		buf = asm_op(OP_OR, buf);
		return buf;
	case LAND:
		buf = compile_expr(expr->binary.x, buf);
		buf = compile_expr(expr->binary.y, buf);
		buf = asm_op(OP_LAND, buf);
		return buf;
	case LOR:
		buf = compile_expr(expr->binary.x, buf);
		buf = compile_expr(expr->binary.y, buf);
		buf = asm_op(OP_LOR, buf);
		return buf;
	case COND:
		// TODO
		send_error(expr->binary.start, FATAL, "Unsupported expression");
		return NULL;
	case ASSIGN:
		// Assumed X is a Store of a VARIABLE kind.
		buf = compile_expr(expr->binary.y, buf);
		buf = asm_op(OP_SAVE0 +
			variable(expr->binary.x->store.name_id), buf);
		return buf;
	case MUL_ASSIGN:
		buf = asm_op(OP_LOAD0 +
			variable(expr->binary.x->store.name_id), buf);
		buf = compile_expr(expr->binary.y, buf);
		buf = asm_op(OP_MUL, buf);
		buf = asm_op(OP_SAVE0 +
			variable(expr->binary.x->store.name_id), buf);
		return buf;
	case DIV_ASSIGN:
		buf = asm_op(OP_LOAD0 +
			variable(expr->binary.x->store.name_id), buf);
		buf = compile_expr(expr->binary.y, buf);
		buf = asm_op(OP_DIV, buf);
		buf = asm_op(OP_SAVE0 +
			variable(expr->binary.x->store.name_id), buf);
		return buf;
	case MOD_ASSIGN:
		buf = asm_op(OP_LOAD0 +
			variable(expr->binary.x->store.name_id), buf);
		buf = compile_expr(expr->binary.y, buf);
		buf = asm_op(OP_MOD, buf);
		buf = asm_op(OP_SAVE0 +
			variable(expr->binary.x->store.name_id), buf);
		return buf;
	case ADD_ASSIGN:
		buf = asm_op(OP_LOAD0 +
			variable(expr->binary.x->store.name_id), buf);
		buf = compile_expr(expr->binary.y, buf);
		buf = asm_op(OP_ADD, buf);
		buf = asm_op(OP_SAVE0 +
			variable(expr->binary.x->store.name_id), buf);
		return buf;
	case SUB_ASSIGN:
		buf = asm_op(OP_LOAD0 +
			variable(expr->binary.x->store.name_id), buf);
		buf = compile_expr(expr->binary.y, buf);
		buf = asm_op(OP_SUB, buf);
		buf = asm_op(OP_SAVE0 +
			variable(expr->binary.x->store.name_id), buf);
		return buf;
	case SHR_ASSIGN:
		buf = asm_op(OP_LOAD0 +
			variable(expr->binary.x->store.name_id), buf);
		buf = compile_expr(expr->binary.y, buf);
		buf = asm_op(OP_SHR, buf);
		buf = asm_op(OP_SAVE0 +
			variable(expr->binary.x->store.name_id), buf);
		return buf;
	case SHL_ASSIGN:
		buf = asm_op(OP_LOAD0 +
			variable(expr->binary.x->store.name_id), buf);
		buf = compile_expr(expr->binary.y, buf);
		buf = asm_op(OP_SHL, buf);
		buf = asm_op(OP_SAVE0 +
			variable(expr->binary.x->store.name_id), buf);
		return buf;
	case AND_ASSIGN:
		buf = asm_op(OP_LOAD0 +
			variable(expr->binary.x->store.name_id), buf);
		buf = compile_expr(expr->binary.y, buf);
		buf = asm_op(OP_AND, buf);
		buf = asm_op(OP_SAVE0 +
			variable(expr->binary.x->store.name_id), buf);
		return buf;
	case XOR_ASSIGN:
		buf = asm_op(OP_LOAD0 +
			variable(expr->binary.x->store.name_id), buf);
		buf = compile_expr(expr->binary.y, buf);
		buf = asm_op(OP_XOR, buf);
		buf = asm_op(OP_SAVE0 +
			variable(expr->binary.x->store.name_id), buf);
		return buf;
	case OR_ASSIGN:
		buf = asm_op(OP_LOAD0 +
			variable(expr->binary.x->store.name_id), buf);
		buf = compile_expr(expr->binary.y, buf);
		buf = asm_op(OP_OR, buf);
		buf = asm_op(OP_SAVE0 +
			variable(expr->binary.x->store.name_id), buf);
		return buf;
	case COMMA:
		buf = compile_expr(expr->binary.x, buf);
		buf = asm_op(OP_POP, buf);
		buf = compile_expr(expr->binary.y, buf);
		return buf;
	case SEND:
		send_error(expr->binary.start, FATAL,
			"send expression found nested within expression");
		return NULL;
	default:
		send_error(NULL, FATAL, "Malformed binary expression");
		return NULL;
	}
}

static uint8_t *compile_expr(const struct expr *expr, uint8_t *buf)
{
	switch (expr->type) {
	case BAD_EXPR:
		send_error(expr->bad.start, FATAL, "Bad expression");
		return NULL; // send_error should stop execution
	case NUM_LIT_EXPR:
		return compile_num_lit_expr(expr, buf);
	case PAREN_EXPR:
		return compile_expr(expr->paren.x, buf);
	case UNARY_EXPR:
		return compile_unary_expr(expr, buf);
	case BINARY_EXPR:
		return compile_binary_expr(expr, buf);
	case STORE_EXPR:
		// Type is assumed VARIABLE
		return asm_op(LOAD0 + variable(expr->store.name_id), buf);
	}
}

static uint8_t *compile_expr_stmt(const struct stmt *stmt, uint8_t *buf)
{
	if (!(stmt->type == BINARY_EXPR && stmt->binary.op == SEND)) {
		return compile_expr(stmt->expr.x, buf);
	}

	// Handle special-case send <- statement.

	// TODO
}

static uint8_t *compile_branch_stmt(const struct stmt *stmt, struct ctx *ctx, uint8_t *buf)
{
	uint8_t *addr;

	switch (stmt->branch.tok) {
	case BREAK:
		buf = asm_jump2(OP_JMP, add_endpoint(ctx), buf);
		break;
	case GOTO:
		buf = asm_jump(
			OP_JMP,
			label_addr(ctx, stmt->branch.label_id),
			buf);
		break;
	case CONTINUE:
		buf = asm_jump(OP_JMP, ctx->loop_start, buf);
		break;
	}

	return buf;
}

static uint8_t *compile_block_stmt(const struct stmt *stmt, uint8_t *buf)
{
	for (size_t i = 0; i < stmt->block_stmt.nstmts, i++) {
		struct stmt *s = stmt->block_stmt.stmt_list[i];
		buf = compile_stmt(s, buf);
	}

	return buf;
}

static uint8_t *compile_if_stmt(const struct stmt *stmt, uint8_t *buf)
{
	// Address to jump if the condition is false
	uint8_t *addr_false;
	// The end of the if statement
	uint8_t *addr_end;

	// if (cond)
	buf = compile_expr(stmt->if_stmt.cond, buf);
	buf = asm_jump2(OP_FJMP, &addr_false, buf);

	// { ... }
	buf = compile_stmt(stmt->if_stmt.body, buf);
	if (stmt->if_stmt.cond != NULL) {
		buf = asm_jump2(OP_JMP, &addr_end, buf);
	}

	// else { ... }

	// jump here if the condition is false
	asm_addr(abs_addr(buf, ctx), addr_false);

	if (stmt->if_stmt.cond != NULL) {
		buf = compile_stmt(stmt->if_stmt.otherwise, buf);
		asm_addr(abs_addr(buf, ctx), addr_end);
	}

	return buf;
}

static uint8_t *compile_loop_stmt(const struct stmt *stmt, uint8_t *buf)
{
	uint8_t *start, *end;

	if (stmt->loop.init != NULL)
		buf = compile_stmt(stmt->loop.init, buf);

	start = buf;

	if (stmt->loop.exec_body_first) {
		// do { ... }
		buf = compile_stmt(stmt->loop.body, buf);

		// while ( ... );
		buf = compile_expr(stmt->loop.cond, buf);
		buf = asm_jump(OP_TJMP, abs_addr(start, ctx), buf);
	} else {
		// while ( ... )
		buf = compile_expr(stmt->loop.cond, buf);
		buf = asm_jump2(OP_FJMP, &end, buf);

		// { ... }
		buf = compile_stmt(stmt->loop.body, buf);
		if (stmt->loop.post != NULL)
			buf = compile_stmt(stmt->loop.post, buf);
		buf = asm_jump(OP_JMP, abs_addr(start, ctx), buf);

		asm_addr(abs_addr(buf, ctx), end);
	}

	return buf;
}

static uint8_t *compile_stmt(const struct stmt *stmt, uint8_t *buf)
{
	switch (stmt->type) {
	case BAD_STMT:
		send_error(stmt->bad.start, FATAL, "Bad expression");
		return NULL; // a FATAL send_error should kill the prgram.
	case EMPTY_STMT:
		return asm_op(OP_NOOP, buf);
	case LABELED_STMT:
		return compile_stmt(stmt->labeled.stmt, buf);
	case EXPR_STMT:
		buf = compile_expr(stmt->expr.x, buf);
		return asm_op(OP_POP, buf); // pop value from expr.
	case BRANCH_STMT:
		return compile_branch_stmt(stmt, buf);
	case BLOCK_STMT:
		return compile_block_stmt(stmt, buf);
	case IF_STMT:
		return compile_if_stmt(stmt, buf);
	case CASE_CLAUSE:
		// TODO re-think this through.
		return buf;
	case SWITCH_STMT:
		// TODO
		send_error(stmt->switch_stmt.start, FATAL, "Unsupported statement");
		return NULL;
	case LOOP_STMT:
		return compile_loop_stmt(stmt, buf);
	case HALT_STMT:
		return asm_op(OP_HALT, buf);
	}
}
