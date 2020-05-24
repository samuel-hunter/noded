/*
 * scanner - stream to tokens
 */
#include <ctype.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "noded.h"

typedef TokenType (*Scanlet)(Scanner *s, char *literal, char c);

typedef struct TokenRule TokenRule;
struct TokenRule {
	Scanlet scanlet;
	TokenType tok0;
	TokenType tok1;
	char chr2;
	TokenType tok2;
	TokenType tok3;
};

static TokenType simple(Scanner *s, char *lit, char c);
static TokenType switch2(Scanner *s, char *lit, char c);
static TokenType switch3(Scanner *s, char *lit, char c);
static TokenType switch4(Scanner *s, char *lit, char c);
static TokenType var(Scanner *s, char *lit, char c);
static TokenType port(Scanner *s, char *lit, char c);
static TokenType char_(Scanner *s, char *lit, char c);
static TokenType string(Scanner *s, char *lit, char c);
static TokenType wire(Scanner *s, char *lit, char c);
static TokenType comment(Scanner *s, char *lit, char c);
static TokenType send(Scanner *s, char *lit, char c);

static TokenRule token_table[UCHAR_MAX+1] = {
	['('] =  {&simple,  LPAREN, 0, 0, 0, 0},
	[')'] =  {&simple,  RPAREN, 0, 0, 0, 0},
	['{'] =  {&simple,  LBRACE, 0, 0, 0, 0},
	['}'] =  {&simple,  RBRACE, 0, 0, 0, 0},
	[':'] =  {&simple,  COLON, 0, 0, 0, 0},
	[','] =  {&simple,  COMMA, 0, 0, 0, 0},
	['.'] =  {&simple,  PERIOD, 0, 0, 0, 0},
	[';'] =  {&simple,  SEMICOLON, 0, 0, 0, 0},
	['$'] =  {&var,     0, 0, 0, 0, 0},
	['%'] =  {&port,    MOD, MOD_ASSIGN, 0, 0, 0},
	['\''] = {&char_,   0, 0, 0, 0, 0},
	['"'] =  {&string,  0, 0, 0, 0, 0},
	['+'] =  {&switch3, ADD, ADD_ASSIGN, '+', INC, 0},
	['-'] =  {&wire,    SUB, SUB_ASSIGN, '-', DEC, 0},
	['!'] =  {&switch2, LNOT, NEQ, 0, 0, 0},
	['~'] =  {&simple,  NOT, 0, 0, 0, 0},
	['*'] =  {&switch2, MUL, MUL_ASSIGN, 0, 0, 0},
	['/'] =  {&comment, DIV, DIV_ASSIGN, 0, 0, 0},
	['<'] =  {&send,    LSS, LTE, '<', SHL, SHL_ASSIGN},
	['>'] =  {&switch4, GTR, GTE, '>', SHR, SHR_ASSIGN},
	['='] =  {&switch2, ASSIGN, EQL, 0, 0, 0},
	['&'] =  {&switch3, AND, AND_ASSIGN, '&', LAND, 0},
	['^'] =  {&switch2, XOR, XOR_ASSIGN, 0, 0, 0},
	['|'] =  {&switch3, OR, OR_ASSIGN, '|', LOR, 0},
	['?'] =  {&simple,  COND, 0, 0, 0, 0},
	[(unsigned char) EOF] = {&simple, TOK_EOF, 0, 0, 0, 0},
};

/* non-ascii UTF8 code units always have the highest bit set, giving a
 * signed char a negative value. This is useful if we want to allow
 * utf8 characters as identifiers in our code. */
static bool isutf8(char c)
{
	/* Make EOF an exception to make parsing easier. Not all
         * negative values are UTF8 characters, and EOF isn't the only
         * one. For our purposes, though, it should be enough. */
	return c < 0 && c != EOF;
}

/* Return whether the symbol is a valid identifier character */
static bool isident(char c)
{
	return isalnum(c) || isutf8(c) || c == '_';
}

/* Populate the next character in the scanner. */
static void next(Scanner *s)
{
	/* Update linenumber based on current character. */
	if (s->chr == '\n') {
		s->pos.lineno++;
		s->pos.colno = 0;
	} else {
		s->pos.colno++;
	}

	s->chr = getc(s->f);
}

void init_scanner(Scanner *scanner, FILE *f)
{
	memset(scanner, 0, sizeof(*scanner));
	scanner->f = f;
	scanner->pos.lineno = 1;

	next(scanner); /* Prime the rune buffer. */
}

/*
 * Skip through the source code until it reaches a non-space character
 * or EOF.
 */
static void skip_space(Scanner *s)
{
	while (isspace(s->chr) && s->chr != EOF)
		next(s);
}

/*
 * Skip through the source code if it is at a comment. Return
 * whether it skipped a comment.
 */
static bool skip_comment(Scanner *s)
{
	switch (s->chr) {
	case '/':
		/* Single-lined comment */
		while (s->chr != '\n' && s->chr != EOF) {
			next(s);
		}
		next(s); /* Advance past newline */
		return true;
	case '*':
		/* Multi-lined comment */
		next(s); /* skip the '*' */

		while (s->chr != '/' && s->chr != EOF) {
			/* Advance to the next '*' */
			while (s->chr != '*' && s->chr != EOF) {
				next(s);
			}
			next(s);
		}
		/* Consume the ending '/' */
		next(s);

		return true;
	}

	return false;
}

/*
 * Scan an identifier and write to `dest`. Assumes `dest` can hold
 * LITERAL_MAX+1 bytes. If the length of the identifier is beyond
 * LITERAL_MAX, mark s.err.
 */
static void scan_identifier(Scanner *s, char *dest)
{
	size_t len = 0;
	while (isident(s->chr)) {
		len++;
		if (len > LITERAL_MAX) {
			send_error(&s->pos, ERR, "Identifier too large");
			goto exit;
		}

		*dest++ = s->chr;
		next(s);
	}

exit:
	*dest = '\0'; /* Terminate with null character. */
}

/*
 * Scan a number and write to `dest`. Assumes `dest` can hold
 * LITERAL_MAX+1 bytes. If the length of the literal is beyond LITERAL_MAX,
 * mark s.err.
 */
static void scan_number(Scanner *s, char *dest)
{
	size_t len = 0;
	int base = 10;

	/*
	 * Scan the optional `0[bBoOxX]?` header. Don't check for len
	 * here, since LITERAL_MAX > 2.
	 */
	if (s->chr == '0') {
		base = 8; /* O### numbers are octal. */
		len++;
		*dest++ = s->chr;
		next(s);

		switch (s->chr) {
		case 'b': /* 0b#### */
		case 'B':
			base = 2;
			len++;
			*dest++ = s->chr;
			next(s);
			break;
		case 'o': /* 0o#### */
		case 'O':
			base = 8;
			*dest++ = s->chr;
			next(s);
			break;
		case 'x':
		case 'X':
			base = 16;
			*dest++ = s->chr;
			next(s);
			break;
		}
	}

	while (true) {
		if (s->chr >= '0' && s->chr <= '1') {
			/* do nothing, always a valid digit :) */
		} else if (s->chr >= '2' && s->chr <= '7') {
			if (base < 8)
				goto exit;
		} else if (s->chr >= '8' && s->chr <= '9') {
			if (base < 10)
				goto exit;
		} else if ((s->chr >= 'A' && s->chr <= 'F') ||
                           (s->chr >= 'a' && s->chr <= 'f')) {
			if (base < 16)
				goto exit;
		} else {
			goto exit;
		}

		len++;
		if (len > LITERAL_MAX) {
			send_error(&s->pos, ERR, "Number literal too large");
			goto exit;
		}

		*dest++ = s->chr;
		next(s);
	}

exit:
	*dest = '\0';
}

static TokenType simple(Scanner *s, char *lit, char c)
{
	(void)lit;
	(void)s;

	return token_table[(unsigned char) c].tok0;
}

static TokenType switch2(Scanner *s, char *lit, char c)
{
	(void)lit;
	TokenRule *rule = &token_table[(unsigned char) c];

	if (s->chr == '=') {
		next(s);
		return rule->tok1;
	}

	return rule->tok0;
}

static TokenType switch3(Scanner *s, char *lit, char c)
{
	(void)lit;
	TokenRule *rule = &token_table[(unsigned char) c];

	if (s->chr == '=') {
		next(s);
		return rule->tok1;
	} else if (s->chr == rule->chr2) {
		next(s);
		return rule->tok2;
	}

	return rule->tok0;
}

static TokenType switch4(Scanner *s, char *lit, char c)
{
	(void)lit;
	TokenRule *rule = &token_table[(unsigned char) c];

	if (s->chr == '=') {
		next(s);
		return rule->tok1;
	} else if (s->chr == rule->chr2) {
		next(s);

		if (s->chr == '=') {
			next(s);
			return rule->tok3;
		}

		return rule->tok2;
	}

	return rule->tok0;
}

static TokenType var(Scanner *s, char *lit, char c)
{
	(void)c;
	scan_identifier(s, lit);
	return VARIABLE;
}

static TokenType port(Scanner *s, char *lit, char c)
{
	if (isident(s->chr)) {
		scan_identifier(s, lit);
		return PORT;
	}

	return switch2(s, lit, c);
}

/*
 * Scan a char and write the literal to `dest`, skipping the
 * single-quotes. Assumes `dest` has the length LITERAL_MAX+1. If the
 * literal is greater than LITERAL_MAX, mark the error.
 */
static TokenType char_(Scanner *s, char *lit, char c)
{
	(void)c;
	size_t len = 0;
	bool escaped = false;

	while (true) {
		if (s->chr == '\\') {
			escaped = !escaped;
		} else if (s->chr == '\'' && !escaped) {
			next(s); /* Advance past ending ' */
			*lit = '\0'; /* Terminate end of string */

			return CHAR;
		} else {
			escaped = false;
		}

		len++;
		if (len > LITERAL_MAX) {
			send_error(&s->pos, ERR, "Character literal too large");
			return CHAR;
		}
		*lit++ = s->chr;
		next(s);
	}

	return CHAR;
}

/*
 * Scan a string and write the literal to `dest`, excluding the
 * quotes. Assumes `dest` has the length LITERAL_MAX+1. If the literal
 * is greater than LITERAL_MAX, populate s.err.
 */
static TokenType string(Scanner *s, char *lit, char c)
{
	(void)c;
	size_t len = 0;
	bool escaped = false;

	while (true) {
		if (s->chr == '\\') {
			escaped = !escaped;
		} else if (s->chr == '"' && !escaped) {
			next(s); /* Advance past ending " */
			*lit = '\0'; /* Terminate string literal */

			return STRING;
		} else {
			escaped = false;
		}

		len++;
		if (len > LITERAL_MAX) {
			send_error(&s->pos, ERR, "String literal too large");
			return STRING;
		}
		*lit++ = s->chr;
		next(s);
	}

	return STRING;
}

static TokenType wire(Scanner *s, char *lit, char c)
{
	if (s->chr == '>') {
		next(s); /* > */
		return WIRE;
	}

	return switch3(s, lit, c);
}

static TokenType comment(Scanner *s, char *lit, char c)
{
	if (skip_comment(s)) {
		return SCAN_AGAIN;
	}

	return switch2(s, lit, c);
}

static TokenType send(Scanner *s, char *lit, char c)
{
	if (s->chr == '-') {
		next(s);
		return SEND;
	}

	return switch4(s, lit, c);
}

/*
 * Scan the next token and store it in s->current, for the caller to
 * read from directly.
 */
void scan(Scanner *s, Token *dest) {
	TokenType type;

	if (dest == NULL) {
		/* Sometimes, you just want to consume a token without
		 * caring what it has (e.g. consuming after peeking).
		 * In this case, set it to some garbage memory --
		 * &s->peek conveniently provides this! */
		dest = &s->peek;
	}

	if (s->buffered) {
		s->buffered = false;
		*dest = s->peek;
		return;
	}

	if (!DEBUG) {
		/* By default, set the literal empty. */
		strcpy(dest->lit, "");
	} else {
		/* Fill the entire literal with zeroes, so that it's
		 * easier to read in the debugger. */
		memset(dest->lit, 0, sizeof(dest->lit));
	}

	do {
		skip_space(s);
		dest->pos = s->pos;

		int c = s->chr;
		if (isdigit(c)) {
			scan_number(s, dest->lit);
			type = NUMBER;
		} else if (isident(c)) {
			scan_identifier(s, dest->lit);
			type = lookup(dest->lit);
		} else {
			Scanlet scanlet = token_table[(unsigned char)c].scanlet;
			next(s);
			if (scanlet) {
				type = scanlet(s, dest->lit, c);
			} else {
				type = ILLEGAL;
				dest->lit[0] = c;
				dest->lit[1] = '\000';
			}
		}
	} while (type == SCAN_AGAIN);

	if (type == ILLEGAL) {
		send_error(&s->pos, ERR, "Illegal token '%s'", dest->lit);
	}

	dest->type = type;
}

/* Set *dest to the next token without consuming it */
void peek(Scanner *s, Token *dest)
{
	if (!s->buffered) {
		scan(s, &s->peek);
		s->buffered = true;
	}

	if (dest) *dest = s->peek;
}

/* Return the type of the next token without consuming it */
TokenType peektype(Scanner *s)
{
	Token tok;
	peek(s, &tok);
	return tok.type;
}

/* If the next token is expected, write to *dest and consume
 * it. Otherwise, send an error. */
void expect(Scanner *s, TokenType expected, Token *dest)
{
	Token tok;
	peek(s, &tok);

	if (expected == tok.type) {
		scan(s, dest);
	} else {
		send_error(&tok.pos, ERR, "Expected %s, but received %s",
			tokstr(expected), tokstr(tok.type));
		return;
	}
}
