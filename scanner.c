/*
 * scanner - stream to tokens
 */
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "noded.h"

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

	/* Refresh the buffer when required. */
	if (s->offset == s->nread && !(feof(s->f) || ferror(s->f))) {
		s->offset = 0;
		s->nread = fread(s->buf, 1, sizeof(s->buf), s->f);
		/*
		 * If an I/O error happens, we can pretend it's the
		 * same as feof and return an EOF character.
		 */
	}

	/* Replace next character. */
	if (s->offset == s->nread) {
		s->chr = EOF;
	} else {
		s->chr = s->buf[s->offset++];
	}
}

void init_scanner(Scanner *scanner, FILE *f)
{
	memset(scanner, 0, sizeof(*scanner));
	scanner->f = f;
	scanner->pos.lineno = 1;

	next(scanner); /* Populate the rune buffer. */
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
	while (isalnum(s->chr)) {
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

/*
 * Scan a string and write the literal to `dest`, excluding the
 * quotes. Assumes `dest` has the length LITERAL_MAX+1. If the literal
 * is greater than LITERAL_MAX, populate s.err.
 */
static void scan_string(Scanner *s, char *dest)
{
	size_t len = 0;
	bool escaped = false;

	next(s); /* Consume starting " */
	while (true) {
		if (s->chr == '\\') {
			escaped = !escaped;
		} else if (s->chr == '"' && !escaped) {
			next(s); /* Advance past ending " */
			*dest = '\0'; /* Terminate string literal */

			return;
		} else {
			escaped = false;
		}

		len++;
		if (len > LITERAL_MAX) {
			send_error(&s->pos, ERR, "String literal too large");
			return;
		}
		*dest++ = s->chr;
		next(s);
	}
}

/*
 * Scan a char and write the literal to `dest`, skipping the
 * ampersands. Assumes `dest` has the length LITERAL_MAX+1. If the
 * literal is greater than LITERAL_MAX, mark the error.
 */
static void scan_char(Scanner *s, char *dest)
{
	size_t len = 0;
	bool escaped = false;

	next(s); /* Consume starting ' */

	while (true) {
		if (s->chr == '\\') {
			escaped = !escaped;
		} else if (s->chr == '\'' && !escaped) {
			next(s); /* Advance past ending ' */
			*dest = '\0'; /* Terminate end of string */

			return;
		} else {
			escaped = false;
		}

		len++;
		if (len > LITERAL_MAX) {
			send_error(&s->pos, ERR, "Character literal too large");
			return;
		}
		*dest++ = s->chr;
		next(s);
	}
}

/* Helper functions for scanning multi-byte tokens such as >> += >>= . */

static Token switch2(Scanner *s, Token tok0, Token tok1)
{
	if (s->chr == '=') {
		next(s);
		return tok1;
	}

	return tok0;
}

static Token switch3(Scanner *s, Token tok0, Token tok1,
	char chr2, Token tok2)
{

	if (s->chr == '=') {
		next(s);
		return tok1;
	} else if (s->chr == chr2) {
		next(s);
		return tok2;
	}

	return tok0;
}

static Token switch4(Scanner *s, Token tok0, Token tok1,
	char chr2, Token tok2, Token tok3)
{
	if (s->chr == '=') {
		next(s);
		return tok1;
	} else if (s->chr == chr2) {
		next(s);
		if (s->chr == '=') {
			next(s);
			return tok3;
		}

		return tok2;
	}

	return tok0;
}

/*
 * Scan the next token and return its result. Write to dest_literal
 * the literal string, and to pos the token position. Assumes
 * dest_literal has a capacity of LITERAL_MAX+1 bytes. A literal
 * greater than LITERAL_MAX will mark an error in the scanner.
 */
void scan(Scanner *s, FullToken *dest) {
	Token tok = ILLEGAL;
	Position start;
	strcpy(dest->lit, ""); 	/* By default, set the literal empty. */
scan_again:
	skip_space(s);
	start = s->pos;
	if (isalpha(s->chr)) {
		scan_identifier(s, dest->lit);
		tok = lookup(dest->lit);
	} else if (isdigit(s->chr)) {
		scan_number(s, dest->lit);
		tok = NUMBER;
	} else {
		/*
		 * The order of the keys in this switch should roughly
		 * be in the same order as how the token constants are
		 * declared.
		 */
		switch (s->chr) {
		case '(':
			next(s); /* consume ( */
			tok = LPAREN;
			break;
		case ')':
			next(s); /* consume ) */
			tok = RPAREN;
			break;
		case '{':
			next(s); /* consume { */
			tok = LBRACE;
			break;
		case '}':
			next(s); /* consume } */
			tok = RBRACE;
			break;
		case ':':
			next(s); /* consume : */
			tok = COLON;
			break;
		case ',':
			next(s); /* consume , */
			tok = COMMA;
			break;
		case '.':
			next(s); /* consume . */
			tok = PERIOD;
			break;
		case ';':
			next(s); /* consume ; */
			tok = SEMICOLON;
			break;
		case '$':
			next(s); /* consume $ */
			scan_identifier(s, dest->lit);
			tok = VARIABLE;
			break;
		case '%':
			next(s); /* consume % */
			if (isalpha(s->chr)) {
				tok = PORT;
				/*
				 * The '%' will be truncated in the
				 * literal value.
				 */
				scan_identifier(s, dest->lit);
			} else {
				tok = switch2(s, MOD, MOD_ASSIGN);
			}
			break;
		case '\'':
			tok = CHAR;
			scan_char(s, dest->lit);
			break;
		case '"':
			tok = STRING_LITERAL;
			scan_string(s, dest->lit);
			break;
		case '+':
			next(s);
			tok = switch3(s, ADD, ADD_ASSIGN, '+', INC);
			break;
		case '-':
			next(s);
			if (s->chr == '>') {
				next(s);
				tok = WIRE;
			} else {
				tok = switch3(s, SUB, SUB_ASSIGN, '-', DEC);
			}
			break;
		case '!':
			next(s);
			tok = switch2(s, LNOT, NEQ);
			break;
		case '~':
			next(s);
			tok = NOT;
			break;
		case '*':
			next(s);
			tok = switch2(s, MUL, MUL_ASSIGN);
			break;
		case '/':
			next(s);
			if (skip_comment(s)) {
				goto scan_again;
			}

			tok = switch2(s, DIV, DIV_ASSIGN);
			break;
		case '<':
			next(s);
			if (s->chr == '-') {
				next(s);
				tok = SEND;
			} else {
				tok = switch4(s, LSS, LTE, '<', SHL, SHL_ASSIGN);
			}
			break;
		case '>':
			next(s);
			tok = switch4(s, GTR, GTE, '>', SHR, SHR_ASSIGN);
			break;
		case '=':
			next(s);
			tok = switch2(s, ASSIGN, EQL);
			break;
		case '&':
			next(s);
			tok = switch3(s, AND, AND_ASSIGN, '&', LAND);
			break;
		case '^':
			next(s);
			tok = switch2(s, XOR, XOR_ASSIGN);
			break;
		case '|':
			next(s);
			tok = switch3(s, OR, OR_ASSIGN, '|', LOR);
			break;
		case '?':
			next(s);
			tok = COND;
			break;
		case EOF:
			next(s);
			tok = TOK_EOF;
			break;
		default:
			tok = ILLEGAL;
			dest->lit[0] = s->chr;
			dest->lit[1] = '\0';
			next(s); /* Always advance. */
		}
	}

	if (tok == ILLEGAL) {
		send_error(&s->pos, ERR, "Illegal token '%s'", dest->lit);
	}

	memcpy(&dest->pos, &start, sizeof(dest->pos));
	dest->tok = tok;
}
