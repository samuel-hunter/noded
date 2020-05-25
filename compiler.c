/*
 * compiler - directly compiler tokens into bytecode
 */
#include <stdlib.h>

#include "noded.h"

typedef enum
{
	PREC_NONE,

	PREC_SEND,
	PREC_COMMA,
	PREC_ASSIGN,
	PREC_COND,
	PREC_LOR,
	PREC_LAND,
	PREC_OR,
	PREC_XOR,
	PREC_AND,
	PREC_EQL,
	PREC_CMP,
	PREC_SHIFT,
	PREC_TERM,
	PREC_FACTOR,
	PREC_UNARY,
} Precedence;

typedef struct Context Context;
struct Context {
	Scanner *s;
	SymDict *dict;

	size_t ports[PORT_MAX];
	int nports;
	size_t vars[VAR_MAX];
	int nvars;

	ByteVec bytecode;
};

typedef struct Expression Expression;
struct Expression {
	enum { EXPR_NORMAL, EXPR_PORT, EXPR_VAR, EXPR_SEND } type;
	int idx; /* Port index or variable index */
};

typedef Expression (*PrefixParselet)(Context *ctx, Token *tok);
typedef Expression (*InfixParselet)(Context *ctx, Expression left, Token *tok);

typedef struct ParseRule ParseRule;
struct ParseRule {
	PrefixParselet prefix;
	InfixParselet infix;
	Precedence prec;
};

static Expression parse_expr(Context *ctx, Precedence prec);

static Expression primary(Context *ctx, Token *tok);
static Expression group(Context *ctx, Token *tok);
static Expression prefix(Context *ctx, Token *tok);

static Expression send(Context *ctx, Expression left, Token *tok);
static Expression binary(Context *ctx, Expression left, Token *tok);
static Expression assign(Context *ctx, Expression left, Token *tok);
static Expression cond(Context *ctx, Expression left, Token *tok);
static Expression postfix(Context *ctx, Expression left, Token *tok);

/*
 * The compiler uses a Pratt Parser for compiling expressions into
 * bytecode. Its mind is defined here, and its heart is parse_expr().
 * The existence of parselets determine whether a token is part of the
 * beginning or middle of an expression.
 */
static ParseRule parse_table[NUM_TOKENS] = {
	[NUMBER] = {&primary, NULL, PREC_NONE},
	[VARIABLE] = {&primary, NULL, PREC_NONE},
	[PORT] = {&primary, NULL, PREC_NONE},
	[LPAREN] = {&group, NULL, PREC_NONE},
	[SEND] = {NULL, &send, PREC_SEND},
	[COMMA] = {NULL, &binary, PREC_COMMA},
	[ASSIGN] = {NULL, &assign, PREC_ASSIGN},
	[OR_ASSIGN] = {NULL, &assign, PREC_ASSIGN},
	[XOR_ASSIGN] = {NULL, &assign, PREC_ASSIGN},
	[AND_ASSIGN] = {NULL, &assign, PREC_ASSIGN},
	[SHL_ASSIGN] = {NULL, &assign, PREC_ASSIGN},
	[SHR_ASSIGN] = {NULL, &assign, PREC_ASSIGN},
	[ADD_ASSIGN] = {NULL, &assign, PREC_ASSIGN},
	[SUB_ASSIGN] = {NULL, &assign, PREC_ASSIGN},
	[MUL_ASSIGN] = {NULL, &assign, PREC_ASSIGN},
	[DIV_ASSIGN] = {NULL, &assign, PREC_ASSIGN},
	[MOD_ASSIGN] = {NULL, &assign, PREC_ASSIGN},
	[COND] = {NULL, &cond, PREC_COND},
	[LOR] = {NULL, &binary, PREC_LOR},
	[LAND] = {NULL, &binary, PREC_LAND},
	[OR] = {NULL, &binary, PREC_OR},
	[XOR] = {NULL, &binary, PREC_XOR},
	[AND] = {NULL, &binary, PREC_AND},
	[EQL] = {NULL, &binary, PREC_EQL},
	[NEQ] = {NULL, &binary, PREC_EQL},
	[LSS] = {NULL, &binary, PREC_CMP},
	[LTE] = {NULL, &binary, PREC_CMP},
	[GTR] = {NULL, &binary, PREC_CMP},
	[GTE] = {NULL, &binary, PREC_CMP},
	[SHL] = {NULL, &binary, PREC_SHIFT},
	[SHR] = {NULL, &binary, PREC_SHIFT},
	[ADD] = {&prefix, &binary, PREC_TERM},
	[SUB] = {&prefix, &binary, PREC_TERM},
	[MUL] = {NULL, &binary, PREC_FACTOR},
	[DIV] = {NULL, &binary, PREC_FACTOR},
	[MOD] = {NULL, &binary, PREC_FACTOR},
	[INC] = {&prefix, &postfix, PREC_UNARY},
	[DEC] = {&prefix, &postfix, PREC_UNARY},
	[LNOT] = {&prefix, NULL, PREC_NONE},
	[NOT] = {&prefix, NULL, PREC_NONE},
};

static const char *opcodes[] = {
	[OP_INVALID] = "INVALID",
	[OP_NOOP] = "NOOP",

	[OP_PUSH] = "PUSH",
	[OP_DUP] = "DUP",
	[OP_POP] = "POP",

	[OP_NEG] = "NEG",
	[OP_LNOT] = "LNOT",
	[OP_NOT] = "NOT",

	[OP_LOR] = "LOR",
	[OP_LAND] = "LAND",
	[OP_OR] = "OR",
	[OP_XOR] = "XOR",
	[OP_AND] = "AND",
	[OP_EQL] = "EQL",
	[OP_LSS] = "LSS",
	[OP_LTE] = "LTE",
	[OP_SHL] = "SHL",
	[OP_SHR] = "SHR",
	[OP_ADD] = "ADD",
	[OP_SUB] = "SUB",
	[OP_MUL] = "MUL",
	[OP_DIV] = "DIV",
	[OP_MOD] = "MOD",

	[OP_LOAD0] = "LOAD0",
	[OP_LOAD1] = "LOAD1",
	[OP_LOAD2] = "LOAD2",
	[OP_LOAD3] = "LOAD3",

	[OP_SAVE0] = "SAVE0",
	[OP_SAVE1] = "SAVE1",
	[OP_SAVE2] = "SAVE2",
	[OP_SAVE3] = "SAVE3",

	[OP_SEND0] = "SEND0",
	[OP_SEND1] = "SEND1",
	[OP_SEND2] = "SEND2",
	[OP_SEND3] = "SEND3",

	[OP_RECV0] = "RECV0",
	[OP_RECV1] = "RECV1",
	[OP_RECV2] = "RECV2",
	[OP_RECV3] = "RECV3",
};

/* Return the port# associated with tok's literal, or create a new one
 * if necessary. */
static int getport(Context *ctx, Token *tok)
{
	size_t id = sym_id(ctx->dict, tok->lit);

	/* Return a preexisting index if available */
	for (int i = 0; i < ctx->nports; i++) {
		if (id == ctx->ports[i]) return i;
	}

	/* Check if there's room, and reserve a new port. */
	if (ctx->nports == PORT_MAX) {
		send_error(&tok->pos, ERR, 
			"too many ports (maximum is %d)", PORT_MAX);
		return 0;
	}

	ctx->ports[ctx->nports] = id;
	return ctx->nports++;
}

/* Return the variable# associated with tok's literal, or create a new
 * one if necessary. */
static int getvar(Context *ctx, Token *tok)
{
	size_t id = sym_id(ctx->dict, tok->lit);

	/* Return a preexisting index if available */
	for (int i = 0; i < ctx->nvars; i++) {
		if (id == ctx->vars[i]) return i;
	}

	/* Check if there's room, and reserve a new variable. */
	if (ctx->nvars == VAR_MAX) {
		send_error(&tok->pos, ERR, 
			"too many variables (maximum is %d)", VAR_MAX);
		return 0;
	}

	ctx->vars[ctx->nvars] = id;
	return ctx->nvars++;
}

const char *opstr(Opcode op)
{
	return opcodes[op];
}

static void asm_op(Context *ctx, Opcode op)
{
	bytevec_append(&ctx->bytecode, op);
}

static void asm_push(Context *ctx, uint8_t val)
{
	uint8_t *buf = bytevec_reserve(&ctx->bytecode, 2);
	*buf++ = OP_PUSH;
	*buf++ = val & 0xFF;
}

static void asm_value(Context *ctx, Expression expr, Token *tok)
{
	switch (expr.type) {
	case EXPR_NORMAL:
		/* The expression is already assembled. */
		break;
	case EXPR_VAR:
		asm_op(ctx, OP_LOAD0 + expr.idx);
		break;
	case EXPR_PORT:
		send_error(&tok->pos, ERR, "ports are invalid operands outside send statements");
		break;
	case EXPR_SEND:
		send_error(&tok->pos, ERR, "operator received send statement as an expression");
		break;
	}
}

/* Parse a cstring into a uint8 value. Mark an error on boundary
 * issues and invalid literals. */
static uint8_t parseint(Token *tok)
{
	char *endptr;

	/* Settings base=0 will let the stdlib handle 0#, 0x#, etc. */
	unsigned long val = strtoul(tok->lit, &endptr, 0);

	if (*endptr != '\0') {
		send_error(&tok->pos, ERR, "Invalid integer");
		return 0;
	}

	if (val > UINT8_MAX) {
		send_error(&tok->pos, ERR, "Out of bounds error");
		return 0;
	}

	return (uint8_t) val;
}

static Expression primary(Context *ctx, Token *tok)
{
	switch (tok->type) {
	case NUMBER:
		asm_push(ctx, parseint(tok));
		return (Expression){EXPR_NORMAL, 0};
	case VARIABLE:
		return (Expression){EXPR_VAR, getvar(ctx, tok)};
	case PORT:
		return (Expression){EXPR_PORT, getport(ctx, tok)};
	default:
		send_error(&tok->pos, ERR, "compiler bug: unimplemented operand");
		return (Expression){EXPR_NORMAL, 0};
	}
}

static Expression group(Context *ctx, Token *tok)
{
	(void)tok;

	Expression result = parse_expr(ctx, PREC_NONE);
	expect(ctx->s, RPAREN, NULL);

	return result;
}

static Expression prefix(Context *ctx, Token *tok)
{
	Expression base;

	base = parse_expr(ctx, PREC_UNARY);

	switch (tok->type) {
		case ADD:
			asm_value(ctx, base, tok);
			/* no-op solely for symmetry with SUB */
			break;
		case SUB:
			asm_value(ctx, base, tok);
			asm_op(ctx, OP_NEG);
			break;
		case LNOT:
			asm_value(ctx, base, tok);
			asm_op(ctx, OP_LNOT);
			break;
		case NOT:
			asm_value(ctx, base, tok);
			asm_op(ctx, OP_NOT);
			break;
		case INC:
			if (base.type != EXPR_VAR)
				send_error(&tok->pos, ERR, "variable requried as increment operand");

			/* Quite a few instructions for increment/decrement. I
			 * can always make OP_INC# later if I feel like I need
			 * the performance boost. */
			asm_op(ctx, OP_LOAD0 + base.idx);
			asm_push(ctx, 1);
			asm_op(ctx, OP_ADD);
			asm_op(ctx, OP_SAVE0 + base.idx);
			return base;
		case DEC:
			if (base.type != EXPR_VAR)
				send_error(&tok->pos, ERR, "variable required as decrement operand");

			asm_op(ctx, OP_LOAD0 + base.idx);
			asm_push(ctx, 1);
			asm_op(ctx, OP_SUB);
			asm_op(ctx, OP_SAVE0 + base.idx);
			return base;
		default:
			send_error(&tok->pos, ERR, "compiler bug: unimplemented unary operator");
			break;
	}

	return (Expression){EXPR_NORMAL, 0};
}

static Expression send(Context *ctx, Expression left, Token *tok)
{
	Expression right = parse_expr(ctx, PREC_SEND);

	if (right.type == EXPR_PORT) {
		/* ($var | %port) <- %port */
		asm_op(ctx, OP_RECV0 + right.idx);

		switch (left.type) {
		case EXPR_VAR:
			asm_op(ctx, OP_SAVE0 + left.idx);
			break;
		case EXPR_PORT:
			asm_op(ctx, OP_SEND0 + left.idx);
			break;
		default:
			send_error(&tok->pos, ERR,
				"variable or port required as left operand when receiving a message");
			break;
		}
	} else if (left.type == EXPR_PORT) {
		/* %port <- expr */
		asm_value(ctx, right, tok);
		asm_op(ctx, OP_SEND0 + left.type);
	} else {
		send_error(&tok->pos, ERR,
			"send statement requries a port operand, but can't find any");
	}

	return (Expression){EXPR_SEND, 0};
}

static Expression binary(Context *ctx, Expression left, Token *tok)
{
	asm_value(ctx, left, tok);

	switch (tok->type) {
	case COMMA:
		asm_op(ctx, OP_POP);
		parse_expr(ctx, PREC_COMMA);
		break;
	case LOR:
		parse_expr(ctx, PREC_LOR);
		asm_op(ctx, OP_LOR);
		break;
	case LAND:
		parse_expr(ctx, PREC_LAND);
		asm_op(ctx, OP_LAND);
		break;
	case OR:
		parse_expr(ctx, PREC_OR);
		asm_op(ctx, OP_OR);
		break;
	case XOR:
		parse_expr(ctx, PREC_XOR);
		asm_op(ctx, OP_XOR);
		break;
	case AND:
		parse_expr(ctx, PREC_AND);
		asm_op(ctx, OP_AND);
		break;
	case EQL:
		parse_expr(ctx, PREC_EQL);
		asm_op(ctx, OP_EQL);
		break;
	case NEQ:
		parse_expr(ctx, PREC_EQL);
		asm_op(ctx, OP_EQL);
		asm_op(ctx, OP_LNOT);
		break;
	case LSS:
		parse_expr(ctx, PREC_CMP);
		asm_op(ctx, OP_LSS);
		break;
	case LTE:
		parse_expr(ctx, PREC_CMP);
		asm_op(ctx, OP_LTE);
		break;
	case GTR:
		parse_expr(ctx, PREC_CMP);
		asm_op(ctx, OP_LTE);
		asm_op(ctx, OP_LNOT);
		break;
	case GTE:
		parse_expr(ctx, PREC_CMP);
		asm_op(ctx, OP_LSS);
		asm_op(ctx, OP_LNOT);
		break;
	case SHL:
		parse_expr(ctx, PREC_SHIFT);
		asm_op(ctx, OP_SHL);
		break;
	case SHR:
		parse_expr(ctx, PREC_SHIFT);
		asm_op(ctx, OP_SHR);
		break;
	case ADD:
		parse_expr(ctx, PREC_TERM);
		asm_op(ctx, OP_ADD);
		break;
	case SUB:
		parse_expr(ctx, PREC_TERM);
		asm_op(ctx, OP_SUB);
		break;
	case MUL:
		parse_expr(ctx, PREC_FACTOR);
		asm_op(ctx, OP_MUL);
		break;
	case DIV:
		parse_expr(ctx, PREC_FACTOR);
		asm_op(ctx, OP_DIV);
		break;
	case MOD:
		parse_expr(ctx, PREC_FACTOR);
		asm_op(ctx, OP_MOD);
		break;
	default:
		send_error(&tok->pos, ERR, "compiler bug: unimplemented infix operator");
		break;
	}

	return (Expression){EXPR_NORMAL, 0};
}

static Expression assign(Context *ctx, Expression left, Token *tok)
{
	Expression right;

	if (left.type != EXPR_VAR)
		send_error(&tok->pos, ERR,
			"variable required as left operand of assignment");

	if (ASSIGN == tok->type) {
		/* -1 for LTR parsing */
		right = parse_expr(ctx, PREC_ASSIGN-1);
		asm_value(ctx, right, tok);
		asm_op(ctx, OP_SAVE0 + left.idx);
	} else {
		asm_op(ctx, OP_LOAD0 + left.idx);
		/* -1 for LTR parsing */
		right = parse_expr(ctx, PREC_ASSIGN-1);
		asm_value(ctx, right, tok);
		switch (tok->type) {
		case OR_ASSIGN:
			asm_op(ctx, OP_OR);
			break;
		case XOR_ASSIGN:
			asm_op(ctx, OP_XOR);
			break;
		case AND_ASSIGN:
			asm_op(ctx, OP_AND);
			break;
		case SHL_ASSIGN:
			asm_op(ctx, OP_SHL);
			break;
		case SHR_ASSIGN:
			asm_op(ctx, OP_SHR);
			break;
		case ADD_ASSIGN:
			asm_op(ctx, OP_ADD);
			break;
		case SUB_ASSIGN:
			asm_op(ctx, OP_SUB);
			break;
		case MUL_ASSIGN:
			asm_op(ctx, OP_MUL);
			break;
		case DIV_ASSIGN:
			asm_op(ctx, OP_DIV);
			break;
		case MOD_ASSIGN:
			asm_op(ctx, OP_MOD);
			break;
		default:
			send_error(&tok->pos, ERR,
				"compiler bug: Unimplemented assignment operator");
			break;
		}

		asm_op(ctx, OP_SAVE0 + left.idx);
	}

	return left;
}

/* Compile a conditional expression */
static Expression cond(Context *ctx, Expression left, Token *tok)
{
	Expression expr;
	(void)tok;

	/* Expressions don't have side-effects in this language, so we
	 * can fold them using boolean logic. */
	asm_value(ctx, left, tok); /* conditional */

	/* -1 because conditionals are right-associative */
	expr = parse_expr(ctx, PREC_COND-1);
	asm_value(ctx, expr, tok); /* whence */
	asm_op(ctx, OP_LAND);

	expect(ctx->s, COLON, NULL);

	/* -1 here too */
	expr = parse_expr(ctx, PREC_COND-1);
	asm_value(ctx, expr, tok); /* otherwise */
	asm_op(ctx, OP_LOR);

	return (Expression){EXPR_NORMAL, 0};
}

static Expression postfix(Context *ctx, Expression left, Token *tok)
{
	(void)ctx;
	if (left.type != EXPR_VAR)
		send_error(&tok->pos, ERR, "operator required as postfix operand");

	asm_op(ctx, OP_LOAD0 + left.idx);
	asm_op(ctx, OP_DUP);
	asm_push(ctx, 1);

	switch (tok->type) {
	case INC:
		asm_op(ctx, ADD);
		break;
	case DEC:
		asm_op(ctx, SUB);
		break;
	default:
		send_error(&tok->pos, ERR, "compiler bug: unimplemented postfix operator");
		break;
	}

	asm_op(ctx, OP_SAVE0 + left.idx);

	return (Expression){EXPR_NORMAL, 0};
}

static Expression parse_expr(Context *ctx, Precedence prec)
{
	Scanner *s = ctx->s;
	Token tok;
	PrefixParselet prefix;
	Expression left;
	InfixParselet infix;

	scan(s, &tok);
	prefix = parse_table[tok.type].prefix;
	if (!prefix) {
		send_error(&tok.pos, ERR,
			"unexpected token %s(%s) when parsing expression",
			tokstr(tok.type), tok.lit);
		zap_to(ctx->s, SEMICOLON);
		return (Expression){EXPR_NORMAL, 0};
	}

	left = prefix(ctx, &tok);

	while (prec < parse_table[peektype(s)].prec) {
		infix = parse_table[peektype(s)].infix;
		if (!infix) break; /* no infix at this point, so we're done */

		scan(s, &tok);
		left = infix(ctx, left, &tok);
	}

	return left;
}

static void parse_expr_stmt(Context *ctx)
{
	Expression expr = parse_expr(ctx, PREC_NONE);
	expect(ctx->s, SEMICOLON, NULL);

	/* Pop any remaining value */
	if (expr.type == EXPR_NORMAL)
		asm_op(ctx, OP_POP);
}

static void parse_block(Context *ctx)
{
	Scanner *s = ctx->s;

	expect(s, LBRACE, NULL); /* { */
	while (RBRACE != peektype(s))
		parse_expr_stmt(ctx);
	scan(s, NULL); /* } */
}

uint8_t *compile(Scanner *s, SymDict *dict, size_t *n)
{
	Context ctx = {.s = s, .dict = dict};

	parse_block(&ctx);

	bytevec_shrink(&ctx.bytecode);
	*n = ctx.bytecode.len;
	return ctx.bytecode.buf;
}