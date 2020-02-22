#include <errno.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#include "noded.h"
#include "ast.h"

static struct expr *parse_num_lit_expr(struct parser *p);
static struct expr *parse_unary_expr(struct parser *p);
static struct expr *parse_suffix_expr(struct parser *p);
static struct expr *parse_primary_expr(struct parser *p);
static struct expr *parse_binary_expr(struct parser *p, int prec);
static struct expr *parse_expr(struct parser *p);

static struct stmt *parse_labeled_stmt(struct parser *p);
static struct stmt *parse_expr_stmt(struct parser *p);
static struct stmt *parse_branch_stmt(struct parser *p);
static struct stmt *parse_block_stmt(struct parser *p);
static struct stmt *parse_if_stmt(struct parser *p);
static struct stmt *parse_case_clause(struct parser *p);
static struct stmt *parse_switch_stmt(struct parser *p);
static struct stmt *parse_while_stmt(struct parser *p);
static struct stmt *parse_do_stmt(struct parser *p);
static struct stmt *parse_for_stmt(struct parser *p);
static struct stmt *parse_stmt(struct parser *p);

static struct decl *parse_proc_node_decl(struct parser *p);
static struct decl *parse_buf_node_decl(struct parser *p);
static struct decl *parse_stack_node_decl(struct parser *p);
static struct decl *parse_wire_decl(struct parser *p);
struct decl *parse_decl(struct parser *p);


// Number of errors before the parser panics.
static const size_t max_errors = 10;

static void next(struct parser *p)
{
	scan(p->scanner, &p->current);
}

static void send_error(struct parser *p, const char *fmt, ...)
{
	struct noded_error err;
	va_list ap;

	// Prepare the error value.
	err.filename = p->scanner->filename;
	memcpy(&err.pos, &p->current.pos, sizeof(err.pos));

	va_start(ap, fmt);
	vsnprintf(err.msg, ERROR_MAX+1, fmt, ap);
	va_end(ap);

	// Send the error over
	handle_error(&err);

	// Panic if the errors are too much.
	p->errors++;
	if (p->errors >= max_errors) {
		fflush(stdout);
		fprintf(stderr, "Too many errors.\n");
		exit(1);
	}
}

void init_parser(struct parser *parser, struct scanner *scanner)
{
	memset(parser, 0, sizeof(*parser));
	parser->scanner = scanner;
	next(parser);
}

bool parser_eof(const struct parser *parser)
{
	return parser->errors > 0 || parser->current.tok == TOK_EOF;
}

// Parse Helpers

static void expect(struct parser *p, enum token expected, struct fulltoken *dest)
{
	if (dest) {
		// Copy the current token to its destination when necessary.
		*dest = p->current;
	}

	if (p->current.tok != expected) {
		send_error(p, "Expected %s, but found %s",
		         strtoken(expected), strtoken(p->current.tok));
	} else {
		next(p);
	}
}

// Convert an escape sequence into a byte. Write the number of chars
// to advance into *advance, and whether the parse is successful into
// *ok.
static uint8_t parse_escape(struct parser *p, const char *s, int *advance, bool *ok)
{
	size_t len = strlen(s);
	char buf[4] = {0};
	long val;
	char *endptr;

	// s[0] == '\', guaranteed.
	if (len < 2) {
		send_error(p, "Escape sequence '%s' too short", s);
		*advance = 0;
		*ok = false;
		return 0;
	}

	*ok = true; // Assume success at first
	*advance = 0;

	switch (s[1]) {
	case 'x':
		// Escape sequence \x##, ## = hexadecimal byte
		if (len < 4) {
			send_error(p, "Escape sequence '%s' too short", s);
			*ok = false;
			return 0;
		}

		strncpy(buf, &s[2], 2);
		errno = 0; // To distinguish success or failure after call.
		val = strtol(buf, &endptr, 16);

		// Don't check for range errors; a 2-digit hex number
		// can only go up to 255 anyway.
		if (errno != 0 && val == 0) {
			send_error(p, "Invalid value %s");
			*ok = false;
			return 0;
		}

		*advance = endptr - s;
		return (uint8_t)val;
	case '0':
	case '1':
	case '2':
		// Escape sequence \###, ### = octal byte
		if (len < 4) {
			send_error(p, "Escape sequence %s too short", s);
			*ok = false;
			return 0;
		}

		strncpy(buf, &s[1], 3);
		errno = 0; // To distinguish success or failure after call.
		val = strtol(buf, &endptr, 8);

		// Don't check for range errors; a 3-digit octal
		// number can only go up to 255 anyway.
		if (errno != 0 && val == 0) {
			send_error(p, "Invalid value %s");
			*ok = false;
			return 0;
		}

		*advance = endptr - s;
		return (uint8_t)val;
	case 'n':
		*advance = 2;
		return '\n';
	case 't':
		*advance = 2;
		return '\t';
	case 'r':
		*advance = 2;
		return '\r';
	case '\'':
		*advance = 2;
		return '\'';
	case '"':
		*advance = 2;
		return '"';
	default:
		send_error(p, "Unknown escape sequence at %s", s);
		*ok = false;
		return 0;
	}
}

// Consume a number literal token and return its number value. Send an
// error to the parser on exceptional cases.
static uint8_t parse_number(struct parser *p)
{
	struct fulltoken number;
	long val;

	expect(p, NUMBER, &number);
	if (number.tok != NUMBER) {
		// Let's not clutter with an extra error if we didn't
		// get a good token.
		return 0;
	}

	errno = 0; // To distinguish success or failure after call.
	val = strtol(number.lit, NULL, 0);

	// Handle standard strtol errors.
	if ((errno == ERANGE && (val == LONG_MAX || val == LONG_MIN)) ||
	    (errno != 0 && val == 0)) {
		send_error(p, "Invalid or out-of-range value %s");
		return 0;
	}

	// Handle noded-specific range errors.
	if (val < INT8_MIN || val > UINT8_MAX) {
		send_error(p, "Value %s out of range [%d, %d]",
		                   INT8_MIN, UINT8_MAX);
	}

	// Wrap the number where appropriate.
	if (val < 0) {
		// Convert negative values to their positive counterpart.
		return (uint8_t) (val - INT8_MIN);
	} else {
		return (uint8_t) val;
	}
}

// Convert a character literal into a byte value. Send an error to the
// parser on exceptional cases.
uint8_t parse_char(struct parser *p)
{
	struct fulltoken literal;
	size_t len;

	uint8_t val;
	int advance;
	bool ok;

	expect(p, CHAR, &literal);
	len = strlen(literal.lit);

	if (literal.tok != CHAR) {
		// No use parsing a non-char.
		return 0;
	} else if (literal.lit[0] == '\\') {
		val = parse_escape(p, &literal.lit[0], &advance, &ok);
		if (!ok)
			return 0;

		if ((size_t) advance < len) {
			send_error(p, "Character literal %s too long", literal.lit);
			return 0;
		}

		return val;
	} else if (len == 0) {
		send_error(p, "Empty character");
		return 0;
	} else if (len == 1) {
		return literal.lit[1];
	} else {
		send_error(p, "Character literal %s too long", literal.lit);
		return 0;
	}
}

uint8_t parse_constant(struct parser *p)
{
	switch (p->current.tok) {
	case NUMBER:
		return parse_number(p);
	case CHAR:
		return parse_char(p);
	default:
		send_error(p, "Expected number constant, but received %s",
		           strtoken(p->current.tok));
		return 0;
	}
}

// Write a string literal into a destination, assumed to be
// BUFFER_NODE_MAX length. Write the length of the buffer to *len.
void parse_string(struct parser *p, uint8_t dest[], size_t *len)
{
	struct fulltoken literal;
	size_t i = 0;
	size_t size = 0;
	size_t slen;

	expect(p, STRING_LITERAL, &literal);
	slen = strlen(literal.lit);

	while (i < slen) {
		uint8_t b;

		if (size == BUFFER_NODE_MAX) {
			// String too large
			send_error(p, "String too large");
			goto exit;
		}

		b = literal.lit[i];
		if (b == '\\') {
			int advance;
			bool ok;
			uint8_t val = parse_escape(p, &literal.lit[i], &advance, &ok);

			// Return early if escape isn't parsed correctly
			if (!ok) {
				goto exit;
			}

			dest[size++] = val;
			i += advance;
		} else {
			dest[size++] = b;
			i++;
		}
	}

exit:
	*len = size;
}

// Parse an array and write the contents to dest[], assumed
// BUFFER_NODE_MAX long. Write the length of the buffer to *len.
void parse_array(struct parser *p, uint8_t dest[], size_t *len)
{
	expect(p, LBRACE, NULL);
	size_t size = 0;

	while (true) {
		if (size == BUFFER_NODE_MAX) {
			send_error(p, "Array too long");
			goto exit;
		}

		dest[size++] = parse_constant(p);

		switch (p->current.tok) {
		case RBRACE:
			next(p); // Consume }
			goto exit;
		case COMMA:
			next(p); // Consume ,
			break;
		default:
			send_error(p, "Expected , or }, but found %s",
			           strtoken(p->current.tok));
			next(p); // Always progress.
		}
	}

exit:
	*len = size;
}

static void parse_port(struct parser *p, struct port *dest)
{
	struct fulltoken node;
	struct fulltoken port;

	expect(p, IDENTIFIER, &node); // node
	expect(p, PERIOD, NULL);      // .
	expect(p, IDENTIFIER, &port); // port

	dest->node_pos = node.pos;
	strcpy(dest->node_name, node.lit);
	dest->name_pos = port.pos;
	strcpy(dest->name, port.lit);
}

// ---Expressions---

static struct expr *parse_num_lit_expr(struct parser *p)
{
	struct expr *result = new_expr(NUM_LIT_EXPR);
	result->data.num_lit.start = p->current.pos;
	result->data.num_lit.value = parse_constant(p);
	return result;
}

// Unary (prefix) Expression
static struct expr *parse_unary_expr(struct parser *p)
{
	if (isunary(p->current.tok)) {
		struct expr *result = new_expr(UNARY_EXPR);
		result->data.unary.is_suffix = false;
		result->data.unary.start = p->current.pos;
		result->data.unary.op = p->current.tok;
		next(p); // Consume operator.
		result->data.unary.x = parse_unary_expr(p);
		return result;
	}

	return parse_suffix_expr(p);
}

static struct expr *parse_suffix_expr(struct parser *p)
{
	struct expr *x = parse_primary_expr(p);

	if (issuffix(p->current.tok)) {
		struct expr *result = new_expr(UNARY_EXPR);
		result->data.unary.is_suffix = true;
		result->data.unary.x = x;

		result->data.unary.start = p->current.pos;
		result->data.unary.op = p->current.tok;
		next(p); // Consume operator

		return result;
	} else {
		return x;
	}
}

static struct expr *parse_primary_expr(struct parser *p)
{
	struct expr *result;

	switch (p->current.tok) {
	case NUMBER:
	case CHAR:
		return parse_num_lit_expr(p);
	case LPAREN:
		result = new_expr(PAREN_EXPR);
		result->data.paren.start = p->current.pos;
		next(p);                              // (
		result->data.paren.x = parse_expr(p); // ...
		expect(p, RPAREN, NULL);              // )
		return result;
	case VARIABLE:
	case PORT:
		result = new_expr(STORE_EXPR);
		result->data.store.start = p->current.pos;
		result->data.store.kind = p->current.tok;
		strcpy(result->data.store.name, p->current.lit);

		next(p); // C o n s u m e
		return result;
	default:
		send_error(p, "Expected number, char, or (, but found %s",
		         strtoken(p->current.tok));
		result = new_expr(BAD_EXPR);
		result->data.bad.start = p->current.pos;
		next(p); // Always advance
		return result;
	}
}

static struct expr *parse_binary_expr(struct parser *p, int prec)
{
	struct expr *x; // left-hand expression
	enum token op;
	struct expr *y; // right-hand expression

	struct expr *tmp;

	if (prec == HIGHEST_PREC) {
		x = parse_unary_expr(p);
	} else {
		x = parse_binary_expr(p, prec+1);
	}

	// The behavior of nesting the binary expressions depends on
	// the associativity of the operator, i.e. does evaluation go
	// left-to-right, or right-to-left?
	if (isltr(p->current.tok)) {
		while (precedence(p->current.tok) == prec) {
			op = p->current.tok;
			next(p); // Consume operator.

			if (prec == HIGHEST_PREC) {
				y = parse_unary_expr(p);
			} else {
				y = parse_binary_expr(p, prec+1);
			}

			// Wrap our new binary expr around our current two.
			tmp = new_expr(BINARY_EXPR);
			tmp->data.binary.x = x;
			tmp->data.binary.op = op;
			tmp->data.binary.y = y;

			x = tmp;
		}
	} else {
		while (precedence(p->current.tok) == prec) {
			op = p->current.tok;
			next(p); // Consume operator.

			y = parse_binary_expr(p, prec);

			tmp = new_expr(BINARY_EXPR);
			tmp->data.binary.x = x;
			tmp->data.binary.op = op;
			tmp->data.binary.y = y;

			x = tmp;
		}
	}

	return x;
}

static struct expr *parse_expr(struct parser *p)
{
	// Start with the lowest-precedence expression
	return parse_binary_expr(p, 0);
}

// ---Statements---

static struct stmt *parse_labeled_stmt(struct parser *p)
{
	struct fulltoken label;
	struct stmt *stmt;
	struct stmt *result;

	expect(p, IDENTIFIER, &label);
	expect(p, COLON, NULL);
	stmt = parse_stmt(p);

	result = new_stmt(LABELED_STMT);
	result->data.labeled.start = label.pos;
	strcpy(result->data.labeled.label, label.lit);
	result->data.labeled.stmt = stmt;
	return result;
}

static struct stmt *parse_expr_stmt(struct parser *p)
{
	struct expr *expr;
	struct stmt *result;

	expr = parse_expr(p);
	expect(p, SEMICOLON, NULL);

	result = new_stmt(EXPR_STMT);
	result->data.expr.x = expr;
	return result;
}

static struct stmt *parse_branch_stmt(struct parser *p)
{
	struct fulltoken keyword = p->current;
	struct fulltoken label;
	struct stmt *result;

	next(p); // Always progress

	switch (keyword.tok) {
	case BREAK:
	case CONTINUE:
		expect(p, SEMICOLON, NULL);

		result = new_stmt(BRANCH_STMT);
		result->data.branch.start = keyword.pos;
		result->data.branch.tok = keyword.tok;
		strcpy(result->data.branch.label, "");
		return result;
	case GOTO:
		expect(p, IDENTIFIER, &label);
		expect(p, SEMICOLON, NULL);

		result = new_stmt(BRANCH_STMT);
		result->data.branch.start = keyword.pos;
		result->data.branch.tok = keyword.tok;
		result->data.branch.label_pos = label.pos;
		strcpy(result->data.branch.label, label.lit);
		return result;
	default:
		send_error(p, "Expected break, continue, or goto, but found %s",
		         strtoken(keyword.tok));

		result = new_stmt(BAD_STMT);
		result->data.bad.start = keyword.pos;
		return result;
	}
}

static struct stmt *parse_block_stmt(struct parser *p)
{
	struct fulltoken start;
	struct stmt *result;

	size_t nstmts = 0;
	size_t cap = 1;
	struct stmt **stmt_list = emalloc(cap * sizeof(*stmt_list));

	expect(p, LBRACE, &start);
	while (p->current.tok != RBRACE && p->current.tok != TOK_EOF) {
		if (nstmts == cap) {
			cap *= 2;
			stmt_list = erealloc(stmt_list, cap * sizeof(*stmt_list));
		}

		stmt_list[nstmts++] = parse_stmt(p);
	}
	expect(p, RBRACE, NULL);

	if (nstmts == 0) {
		free(stmt_list);
		stmt_list = NULL;
	} else {
		stmt_list = erealloc(stmt_list, nstmts * sizeof(*stmt_list));
	}

	result = new_stmt(BLOCK_STMT);
	result->data.block.start = start.pos;
	result->data.block.stmt_list = stmt_list;
	result->data.block.nstmts = nstmts;
	return result;
}

static struct stmt *parse_if_stmt(struct parser *p)
{
	struct fulltoken keyword;
	struct stmt *result = new_stmt(IF_STMT);

	expect(p, IF, &keyword);                      // if
	expect(p, LPAREN, NULL);                      // (
	result->data.if_stmt.cond = parse_expr(p);    // ...
	expect(p, RPAREN, NULL);                      // )
	result->data.if_stmt.body = parse_stmt(p);    // { ... }

	if (p->current.tok == ELSE) {
		next(p);                                        // else
		result->data.if_stmt.otherwise = parse_stmt(p); // { ... }
	} else {
		result->data.if_stmt.otherwise = new_stmt(EMPTY_STMT);
	}

	result->data.if_stmt.start = keyword.pos;
	return result;
}

static struct stmt *parse_case_clause(struct parser *p)
{
	struct stmt *result = new_stmt(CASE_CLAUSE);
	result->data.case_clause.start = p->current.pos;

	switch(p->current.tok) {
	case CASE:
		next(p); // Consume case
		result->data.case_clause.x = parse_number(p);
		expect(p, COLON, NULL);

		result->data.case_clause.is_default = false;
		return result;
	case DEFAULT:
		next(p); // Consume default
		expect(p, COLON, NULL);

		result->data.case_clause.is_default = true;
		return result;
	default:
		send_error(p, "Expected case or default, but found %s",
		           strtoken(p->current.tok));
		next(p); // Always progress
		return result;
	}
}

static struct stmt *parse_switch_stmt(struct parser *p)
{
	struct fulltoken keyword;
	struct stmt *result = new_stmt(SWITCH_STMT);

	expect(p, SWITCH, &keyword); // switch
	expect(p, LPAREN, NULL);     // (
	result->data.switch_stmt.tag = parse_expr(p);
	expect(p, RPAREN, NULL);     // )
	result->data.switch_stmt.body = parse_block_stmt(p);

	result->data.switch_stmt.start = keyword.pos;
	return result;
}

static struct stmt *parse_while_stmt(struct parser *p)
{
	struct fulltoken keyword;
	struct expr *cond;
	struct stmt *body;
	struct stmt *result;

	expect(p, WHILE, &keyword); // while
	expect(p, LPAREN, NULL);    // (
	cond = parse_expr(p);       // ...
	expect(p, RPAREN, NULL);    // )
	body = parse_stmt(p);       // { ... }

	result = new_stmt(LOOP_STMT);
	result->data.loop.start = keyword.pos;
	result->data.loop.exec_body_first = false;
	result->data.loop.init = new_stmt(EMPTY_STMT);
	result->data.loop.cond = cond;
	result->data.loop.post = new_stmt(EMPTY_STMT);
	result->data.loop.body = body;
	return result;
}

static struct stmt *parse_do_stmt(struct parser *p)
{
	struct fulltoken keyword;
	struct stmt *body;
	struct expr *cond;
	struct stmt *result;

	expect(p, DO, &keyword);    // do
	body = parse_stmt(p);       // { ... }
	expect(p, WHILE, NULL);     // while
	expect(p, LPAREN, NULL);    // (
	cond = parse_expr(p);       // ...
	expect(p, RPAREN, NULL);    // )
	expect(p, SEMICOLON, NULL); // ;

	result = new_stmt(LOOP_STMT);
	result->data.loop.start = keyword.pos;
	result->data.loop.exec_body_first = true;
	result->data.loop.init = new_stmt(EMPTY_STMT);
	result->data.loop.cond = cond;
	result->data.loop.post = new_stmt(EMPTY_STMT);
	result->data.loop.body = body;
	return result;
}

static struct stmt *parse_for_stmt(struct parser *p)
{
	struct fulltoken keyword;
	struct stmt *init;
	struct expr *cond;
	struct stmt *post;
	struct stmt *body;
	struct stmt *result;

	expect(p, FOR, &keyword);          // for
	expect(p, LPAREN, NULL);           // (
	init = parse_expr_stmt(p);         // ... ;
	cond = parse_expr(p);              // ...
	expect(p, SEMICOLON, NULL);        // ;
	post = new_stmt(EXPR_STMT);        // ...
	post->data.expr.x = parse_expr(p);
	expect(p, RPAREN, NULL);           // )
	body = parse_stmt(p);              // { ... }

	result = new_stmt(LOOP_STMT);
	result->data.loop.start = keyword.pos;
	result->data.loop.exec_body_first = false;
	result->data.loop.init = init;
	result->data.loop.cond = cond;
	result->data.loop.post = post;
	result->data.loop.body = body;
	return result;
}

static struct stmt *parse_stmt(struct parser *p)
{
	struct stmt *result;

	switch (p->current.tok) {
	case BREAK:
	case CONTINUE:
	case GOTO:
		return parse_branch_stmt(p);
	case LBRACE:
		return parse_block_stmt(p);
	case IF:
		return parse_if_stmt(p);
	case FOR:
		return parse_for_stmt(p);
	case WHILE:
		return parse_while_stmt(p);
	case DO:
		return parse_do_stmt(p);
	case CASE:
	case DEFAULT:
		return parse_case_clause(p);
	case SWITCH:
		return parse_switch_stmt(p);
	case IDENTIFIER:
		return parse_labeled_stmt(p);
	case SEMICOLON:
		next(p);
		result = new_stmt(EMPTY_STMT);
		result->data.empty.start = p->current.pos;
		return result;
	case HALT:
		next(p);
		result = new_stmt(HALT_STMT);
		result->data.halt.start = p->current.pos;
		return result;
	default:
		if (isunary(p->current.tok) || isoperand(p->current.tok)) {
			return parse_expr_stmt(p);
		} else {
			send_error(p, "Expected start of statement, found %s",
			           strtoken(p->current.tok));

			result = new_stmt(BAD_STMT);
			result->data.bad.start = p->current.pos;

			// Zap to semicolon
			while (p->current.tok != SEMICOLON)
				next(p);
			next(p); // Consume the semicolon, too

			return result;
		}
	}
}

// ---Declarations--

static struct decl *parse_proc_node_decl(struct parser *p)
{
	struct fulltoken keyword;
	struct fulltoken name;
	struct fulltoken source;
	struct decl *result;

	expect(p, PROCESSOR, &keyword);
	expect(p, IDENTIFIER, &name);

	switch (p->current.tok) {
	case LBRACE:
		result = new_decl(PROC_DECL);
		result->data.proc.start = keyword.pos;
		result->data.proc.name_pos = name.pos;
		strcpy(result->data.proc.name, name.lit);
		result->data.proc.body = parse_block_stmt(p);
		return result;
	case ASSIGN:
		expect(p, IDENTIFIER, &source);

		result = new_decl(PROC_COPY_DECL);
		result->data.proc_copy.start = keyword.pos;
		result->data.proc_copy.name_pos = name.pos;
		strcpy(result->data.proc_copy.name, name.lit);
		result->data.proc_copy.source_pos = source.pos;
		strcpy(result->data.proc_copy.source, source.lit);
		return result;
	default:
		send_error(p, "Expected { or =, but found %s",
		         strtoken(p->current.tok));
		next(p); // Always progress

		result = new_decl(BAD_DECL);
		result->data.bad.start = keyword.pos;
		return result;
	}
}

static struct decl *parse_buf_node_decl(struct parser *p)
{
	struct fulltoken keyword;
	struct fulltoken identifier;
	struct decl *result;

	expect(p, BUFFER, &keyword);        // buffer
	expect(p, IDENTIFIER, &identifier); // ...
	expect(p, ASSIGN, NULL);            // =

	result = new_decl(BUF_DECL);
	result->data.buf.start = keyword.pos;
	result->data.buf.name_pos = identifier.pos;
	strcpy(result->data.buf.name, identifier.lit);
	result->data.buf.array_start = p->current.pos;

	switch (p->current.tok) {
	case STRING_LITERAL:
		parse_string(p, result->data.buf.data, &result->data.buf.len);
		break;
	case LBRACE:
		parse_array(p, result->data.buf.data, &result->data.buf.len);
		break;
	default:
		send_error(p, "Expected string literal or {, but found %s",
		           strtoken(p->current.tok));
	}

	expect(p, SEMICOLON, NULL); // ;
	return result;
}

static struct decl *parse_stack_node_decl(struct parser *p)
{
	struct fulltoken keyword;
	struct fulltoken identifier;
	struct decl *result;

	expect(p, STACK, &keyword);         // stack
	expect(p, IDENTIFIER, &identifier); // ...
	expect(p, SEMICOLON, NULL);         // ;

	result = new_decl(STACK_DECL);
	result->data.stack.start = keyword.pos;
	result->data.stack.name_pos = identifier.pos;
	strcpy(result->data.stack.name, identifier.lit);
	return result;
}

static struct decl *parse_wire_decl(struct parser *p)
{
	struct decl *result = new_decl(WIRE_DECL);

	parse_port(p, &result->data.wire.source); // node.port
	expect(p, WIRE, NULL);                    // ->
	parse_port(p, &result->data.wire.dest);   // node.port
	expect(p, SEMICOLON, NULL);               // ;

	return result;
}

struct decl *parse_decl(struct parser *parser)
{
	struct decl *result;

	switch (parser->current.tok) {
	case PROCESSOR:
		return parse_proc_node_decl(parser);
	case BUFFER:
		return parse_buf_node_decl(parser);
	case STACK:
		return parse_stack_node_decl(parser);
	case IDENTIFIER:
		return parse_wire_decl(parser);
	default:
		send_error(parser, "Expected processor, buffer, or "
		           "wire declaration, but found %s",
		           strtoken(parser->current.tok));
		next(parser); // Always progress.

		result = new_decl(BAD_DECL);
		result->data.bad.start = parser->current.pos;
		return result;
	}
}
