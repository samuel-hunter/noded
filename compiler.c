/*
 * compiler - directly compiler tokens into bytecode
 */
#include <stdlib.h>
#include <string.h>

#include "noded.h"

/* Label metadata, for keeping track of labels and
 * resolving bytecode from goto statements */
typedef struct Label Label;
struct Label {
	size_t id;
	bool defined;
	uint16_t addr;

	AddrVec gotos;
	Position some_goto;
};

/* The scope of a loop block */
typedef struct Scope Scope;
struct Scope {
	Scope *parent;

	AddrVec breaks;
	uint16_t continue_addr;
};

typedef struct Context Context;
struct Context {
	Scanner *s;
	SymDict *dict;

	Scope *scope;

	size_t ports[PORT_MAX];
	int nports;
	size_t vars[VAR_MAX];
	int nvars;

	/* inline label struct vector */
	Label *labels;
	size_t nlabels;
	size_t labelcap;

	ByteVec bytecode;
};

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
static Expression comma(Context *ctx, Expression left, Token *tok);
static Expression binary(Context *ctx, Expression left, Token *tok);
static Expression assign(Context *ctx, Expression left, Token *tok);
static Expression cond(Context *ctx, Expression left, Token *tok);
static Expression postfix(Context *ctx, Expression left, Token *tok);

static void parse_stmt(Context *ctx);

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
	[CHAR] = {&primary, NULL, PREC_NONE},
	[LPAREN] = {&group, NULL, PREC_NONE},
	[SEND] = {NULL, &send, PREC_SEND},
	[COMMA] = {NULL, &comma, PREC_COMMA},
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

	[OP_JMP] = "JMP",
	[OP_FJMP] = "FJMP",

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

	[OP_HALT] = "HALT",
};

/* Return the port# associated with tok's literal, or create a new one
 * if necessary. */
static int
getport(Context *ctx, Token *tok)
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
static int
getvar(Context *ctx, Token *tok)
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

const char *
opstr(Opcode op)
{
	return opcodes[op];
}

static uint16_t
here(const Context *ctx)
{
	return (uint16_t) ctx->bytecode.len;
}

/* assemble a no-arg instruction */
static void
asm_op(Context *ctx, Opcode op)
{
	bytevec_append(&ctx->bytecode, op);
}

/* Assemble a push instruction */
static void
asm_push(Context *ctx, uint8_t val)
{
	asm_op(ctx, OP_PUSH);
	bytevec_append(&ctx->bytecode, val);
}

/* Evaluate an expression type and assemble any leftover instructions
 * to ensure the value of an expression is assembled */
static void
asm_value(Context *ctx, Expression expr, Token *tok)
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
		send_error(&tok->pos, ERR, "send statements are not expressions");
		break;
	}
}

/* patch in an address for a jump instruction */
static void
patch_addr(Context *ctx, uint16_t idx, uint16_t addr)
{
	ctx->bytecode.buf[idx++] = addr & 0xFF;
	ctx->bytecode.buf[idx++] = addr>>8 & 0xFF;
}

/* patch in the current address for a jump instruction */
static void
patch_here(Context *ctx, uint16_t idx)
{
	patch_addr(ctx, idx, here(ctx));
}

/* assemble a jump instruction with the predetermined address */
static void
asm_jump(Context *ctx, Opcode op, uint16_t addr)
{
	asm_op(ctx, op);
	patch_addr(ctx, (uint16_t) bytevec_reserve(&ctx->bytecode, 2), addr);
}

/* assemble a jump instruction and return a pointer to patch the
 * address later, for use with patch_addr() or patch_here() */
static uint16_t
asm_jump2(Context *ctx, Opcode op)
{
	asm_op(ctx, op);
	return (uint16_t) bytevec_reserve(&ctx->bytecode, 2);
}

/* push a scope in context, recording breaks and resolving continues */
static void
push_scope(Context *ctx)
{
	Scope *scope = ecalloc(1, sizeof(*scope));
	scope->parent = ctx->scope;
	scope->continue_addr = here(ctx);

	ctx->scope = scope;
}

/* partially assemble a jump outside a scope to be fully resolve when
 * the scope pops */
static void
asm_break(Context *ctx, Opcode op)
{
	addrvec_append(&ctx->scope->breaks, asm_jump2(ctx, op));
}

/* assemble a jump to the beginning of the current scope */
static void
asm_continue(Context *ctx, Opcode op)
{
	asm_jump(ctx, op, ctx->scope->continue_addr);
}

/* pop a scope from the context, resolving all breaks */
static void
pop_scope(Context *ctx)
{
	Scope *scope = ctx->scope;
	AddrVec *breaks = &scope->breaks;

	for (size_t i = 0; i < breaks->len; i++) {
		patch_here(ctx, breaks->buf[i]);
	}

	addrvec_clear(breaks);
	ctx->scope = scope->parent;
	free(ctx->scope);
}

/* Find or create a Label struct with the appropriate id */
static Label *
find_label(Context *ctx, size_t id)
{
	for (size_t i = 0; i < ctx->nlabels; i++) {
		if (id == ctx->labels[i].id) return &ctx->labels[i];
	}

	/* Expand the label vector when necessary */
	if (ctx->nlabels == ctx->labelcap) {
		ctx->labelcap = ctx->labelcap ? ctx->labelcap*2 : 8;
		ctx->labels = erealloc(ctx->labels,
			ctx->labelcap * sizeof(*ctx->labels));
	}

	/* Initialize the new label struct and return its pointer */
	ctx->labels[ctx->nlabels].id = id;
	return &ctx->labels[ctx->nlabels++];
}

/* Record the address of a label to resolve later */
static void
add_label_here(Context *ctx, size_t id)
{
	Label *label = find_label(ctx, id);
	label->defined = true;
	label->addr = here(ctx);
}

/* Partially assemble a goto to resolve at the end of compilation */
static void
asm_goto(Context *ctx, Token *labeltok)
{
	Label *label = find_label(ctx, sym_id(ctx->dict, labeltok->lit));
	addrvec_append(&label->gotos, asm_jump2(ctx, OP_JMP));
	label->some_goto = labeltok->pos;
}

/* assemble a primary expression */
static Expression
primary(Context *ctx, Token *tok)
{
	switch (tok->type) {
	case NUMBER:
		asm_push(ctx, parse_int(tok));
		return (Expression){EXPR_NORMAL, 0};
	case VARIABLE:
		return (Expression){EXPR_VAR, getvar(ctx, tok)};
	case PORT:
		return (Expression){EXPR_PORT, getport(ctx, tok)};
	case CHAR:
		asm_push(ctx, parse_char(tok));
		return (Expression){EXPR_NORMAL, 0};
	default:
		send_error(&tok->pos, ERR, "compiler bug: unimplemented operand");
		return (Expression){EXPR_NORMAL, 0};
	}
}

/* assemble an expression wrapped around a pair of parentheses */
static Expression
group(Context *ctx, Token *tok)
{
	(void)tok;

	Expression result = parse_expr(ctx, PREC_NONE);
	expect(ctx->s, RPAREN, NULL);

	return result;
}

/* assemble an expression with a prefix operand */
static Expression
prefix(Context *ctx, Token *tok)
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

/* assemble a send statement */
static Expression
send(Context *ctx, Expression left, Token *tok)
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
		asm_op(ctx, OP_SEND0 + left.idx);
	} else {
		send_error(&tok->pos, ERR,
			"send statement requries a port operand, but can't find any");
	}

	/* To preserve single-pass compilation, send statements are masked as an
	 * expression, and any attempts to embed it in a larger expression trips
     * asm_value(). */
	return (Expression){EXPR_SEND, 0};
}

static Expression
comma(Context *ctx, Expression left, Token *tok)
{
	(void)tok;

	if (left.type == EXPR_NORMAL)
		asm_op(ctx, OP_POP);
	return parse_expr(ctx, PREC_COMMA);
}

static Expression
binary(Context *ctx, Expression left, Token *tok)
{
	Expression expr;

	asm_value(ctx, left, tok);
	expr = parse_expr(ctx, parse_table[tok->type].prec);
	asm_value(ctx, expr, tok);

	switch (tok->type) {
	case LOR:
		asm_op(ctx, OP_LOR);
		break;
	case LAND:
		asm_op(ctx, OP_LAND);
		break;
	case OR:
		asm_op(ctx, OP_OR);
		break;
	case XOR:
		asm_op(ctx, OP_XOR);
		break;
	case AND:
		asm_op(ctx, OP_AND);
		break;
	case EQL:
		asm_op(ctx, OP_EQL);
		break;
	case NEQ:
		asm_op(ctx, OP_EQL);
		asm_op(ctx, OP_LNOT);
		break;
	case LSS:
		asm_op(ctx, OP_LSS);
		break;
	case LTE:
		asm_op(ctx, OP_LTE);
		break;
	case GTR:
		asm_op(ctx, OP_LTE);
		asm_op(ctx, OP_LNOT);
		break;
	case GTE:
		asm_op(ctx, OP_LSS);
		asm_op(ctx, OP_LNOT);
		break;
	case SHL:
		asm_op(ctx, OP_SHL);
		break;
	case SHR:
		asm_op(ctx, OP_SHR);
		break;
	case ADD:
		asm_op(ctx, OP_ADD);
		break;
	case SUB:
		asm_op(ctx, OP_SUB);
		break;
	case MUL:
		asm_op(ctx, OP_MUL);
		break;
	case DIV:
		asm_op(ctx, OP_DIV);
		break;
	case MOD:
		asm_op(ctx, OP_MOD);
		break;
	default:
		send_error(&tok->pos, ERR, "compiler bug: unimplemented infix operator");
		break;
	}

	return (Expression){EXPR_NORMAL, 0};
}

static Expression
assign(Context *ctx, Expression left, Token *tok)
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
static Expression
cond(Context *ctx, Expression left, Token *tok)
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

static Expression
postfix(Context *ctx, Expression left, Token *tok)
{
	(void)ctx;
	if (left.type != EXPR_VAR)
		send_error(&tok->pos, ERR, "operator required as postfix operand");

	asm_op(ctx, OP_LOAD0 + left.idx);
	asm_op(ctx, OP_DUP);
	asm_push(ctx, 1);

	switch (tok->type) {
	case INC:
		asm_op(ctx, OP_ADD);
		break;
	case DEC:
		asm_op(ctx, OP_SUB);
		break;
	default:
		send_error(&tok->pos, ERR, "compiler bug: unimplemented postfix operator");
		break;
	}

	asm_op(ctx, OP_SAVE0 + left.idx);

	return (Expression){EXPR_NORMAL, 0};
}

static Expression
parse_expr(Context *ctx, Precedence prec)
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

static void
parse_branch_stmt(Context *ctx)
{
	Token cmd;
	Token label;
	scan(ctx->s, &cmd);

	switch (cmd.type) {
	case BREAK:
		if (!ctx->scope) {
			send_error(&cmd.pos, ERR, "break statement outside a loop");
			break;
		}

		asm_break(ctx, OP_JMP);
		break;
	case CONTINUE:
		if (!ctx->scope) {
			send_error(&cmd.pos, ERR, "continue statement outside a loop");
			break;
		}

		asm_continue(ctx, OP_JMP);
		break;
	case GOTO:
		expect(ctx->s, IDENTIFIER, &label);
		asm_goto(ctx, &label);
		break;
	default:
		break;
	}

	expect(ctx->s, SEMICOLON, NULL);
}

/* parse a series of statements enclosed in a block */
static void
parse_block_stmt(Context *ctx)
{
	Scanner *s = ctx->s;

	expect(s, LBRACE, NULL);
	while (RBRACE != peektype(s))
		parse_stmt(ctx);
	scan(s, NULL);
}

static void
parse_if_stmt(Context *ctx)
{
	Scanner *s = ctx->s;
	Token tok;
	Expression expr;
	uint16_t jmpfalse, jmpend;

	expect(s, IF, NULL);      /* if */
	expect(s, LPAREN, &tok);  /* (  */
	expr = parse_expr(ctx, PREC_NONE); /* ... */
	asm_value(ctx, expr, &tok);
	expect(s, RPAREN, NULL); /* ) */
	jmpfalse = asm_jump2(ctx, OP_FJMP);
	parse_stmt(ctx); /* { ... } */

	if (peektype(s) == ELSE) {
		/* skip the otherwise clause */
		jmpend = asm_jump2(ctx, OP_JMP);

		expect(s, ELSE, NULL); /* else */
		patch_here(ctx, jmpfalse); /* jump here if the conditional is false */
		parse_stmt(ctx); /* { ... } */

		patch_here(ctx, jmpend);
	} else {
		patch_here(ctx, jmpfalse);
	}
}

static void
parse_for_stmt(Context *ctx)
{
	Scanner *s = ctx->s;
	Expression expr;
	Token tok;
	uint16_t body_jump, end_jump;
	uint16_t post_addr;

	expect(s, FOR, NULL);
	expect(s, LPAREN, NULL);

	/* initial */
	expr = parse_expr(ctx, PREC_NONE);
	if (expr.type == EXPR_NORMAL)
		asm_op(ctx, OP_POP);
	expect(s, SEMICOLON, NULL);

	/* conditional */
	peek(s, &tok);
	expr = parse_expr(ctx, PREC_NONE);
	asm_value(ctx, expr, &tok);
	end_jump = asm_jump2(ctx, OP_FJMP);
	body_jump = asm_jump2(ctx, OP_JMP);
	expect(s, SEMICOLON, NULL);

	/* push the scope here, so that continues get incremented */
	push_scope(ctx);

	/* post-body */
	post_addr = here(ctx);
	expr = parse_expr(ctx, PREC_NONE);
	if (expr.type == EXPR_NORMAL)
		asm_op(ctx, OP_POP);
	asm_continue(ctx, OP_JMP);
	expect(s, RPAREN, NULL);

	/* body */
	patch_here(ctx, body_jump);
	parse_stmt(ctx);
	asm_jump(ctx, OP_JMP, post_addr);

	/* pop the scope here */
	pop_scope(ctx);
	patch_here(ctx, end_jump);
}

static void
parse_while_stmt(Context *ctx)
{
	Scanner *s = ctx->s;
	Expression expr;
	Token start;

	push_scope(ctx);
	expect(s, WHILE, &start);
	expect(s, LPAREN, NULL);
	expr = parse_expr(ctx, PREC_NONE);
	asm_value(ctx, expr, &start);
	asm_break(ctx, OP_FJMP);
	expect(s, RPAREN, NULL);

	parse_stmt(ctx);
	asm_continue(ctx, OP_JMP);

	pop_scope(ctx);
}

static void
parse_do_stmt(Context *ctx)
{
	Scanner *s = ctx->s;
	Token tok;
	Expression cond;

	expect(s, DO, NULL);

	push_scope(ctx);
	parse_stmt(ctx);
	pop_scope(ctx);

	expect(s, WHILE, NULL);
	expect(s, LPAREN, &tok);

	cond = parse_expr(ctx, PREC_NONE);
	asm_value(ctx, cond, &tok);
	asm_op(ctx, OP_LNOT);
	asm_continue(ctx, OP_FJMP);

	expect(s, RPAREN, NULL);
	expect(s, SEMICOLON, NULL);
}

static void
parse_labeled_stmt(Context *ctx)
{
	Scanner *s = ctx->s;
	Token label;

	expect(s, IDENTIFIER, &label);
	expect(s, COLON, NULL);
	add_label_here(ctx, sym_id(ctx->dict, label.lit));
}	

static void
parse_expr_stmt(Context *ctx)
{
	Expression expr = parse_expr(ctx, PREC_NONE);
	expect(ctx->s, SEMICOLON, NULL);

	/* Pop any remaining value */
	if (expr.type == EXPR_NORMAL)
		asm_op(ctx, OP_POP);
}

static void
parse_stmt(Context *ctx)
{
	Token tok;
	peek(ctx->s, &tok);

	switch (tok.type) {
	case BREAK:
	case CONTINUE:
	case GOTO:
		parse_branch_stmt(ctx);
		break;
	case LBRACE:
		parse_block_stmt(ctx);
		break;
	case IF:
		parse_if_stmt(ctx);
		break;
	case FOR:
		parse_for_stmt(ctx);
		break;
	case WHILE:
		parse_while_stmt(ctx);
		break;
	case DO:
		parse_do_stmt(ctx);
		break;
	case IDENTIFIER:
		parse_labeled_stmt(ctx);
		break;
	case SEMICOLON:
		/* empty statement */
		scan(ctx->s, NULL);
		break;
	case HALT:
		scan(ctx->s, NULL);
		expect(ctx->s, SEMICOLON, NULL);
		asm_op(ctx, OP_HALT);
		break;
	default:
		if (parse_table[tok.type].prefix) {
			parse_expr_stmt(ctx);
		} else {
			send_error(&tok.pos, ERR,
				"Expected start of statement, but found %s(%s)",
				tokstr(tok.type), tok.lit);

			/* Zap to nearest semicolon or rbrace. */
			while (tok.type != RBRACE && tok.type != SEMICOLON)
				scan(ctx->s, &tok);
		}
		break;
	}
}

void
compile(Scanner *s, SymDict *dict, CodeBlock *block)
{
	Token tok;
	Context ctx = {.s = s, .dict = dict};

	peek(s, &tok); /* Record the beginning position for later */
	parse_block_stmt(&ctx);

	/* Resolve all gotos */
	for (size_t i = 0; i < ctx.nlabels; i++) {
		Label *label = &ctx.labels[i];
		if (!label->defined) {
			send_error(&label->some_goto, ERR, "goto used without label defined");
			continue;
		}

		for (size_t j = 0; j < label->gotos.len; j++) {
			patch_addr(&ctx, label->gotos.buf[j], label->addr);
		}
		addrvec_clear(&label->gotos);
	}
	free(ctx.labels);

	/* Verify that the byte vector fits a 16-bit number */
	if (ctx.bytecode.len > UINT16_MAX) {
		send_error(&tok.pos, ERR,
			"processor too complex; bytecode generated too large");
	}

	/* Record all port IDs */
	memcpy(block->ports, ctx.ports, sizeof(ctx.ports));
	block->nports = ctx.nports;

	/* Shrink the byte vector and return the result */
	bytevec_shrink(&ctx.bytecode);
	block->size = here(&ctx);
	block->code = ctx.bytecode.buf;
}
