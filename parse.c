/*
 * parse - literal value parsing
 */
#include <stdlib.h>
#include <string.h>

#include "noded.h"

/* Parse a cstring into a uint8 value. Mark an error on boundary
 * issues and invalid literals. */
uint8_t parse_int(const Token *tok)
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

/* Convert an escape sequence into a byte. Write the number of chars
 * to advance into *advance, and whether the parse is successful into
 * *ok. */
uint8_t parse_escape(const Token *tok, int offset, int *advance, bool *ok)
{
	const char *seq = &tok->lit[offset];
	size_t len = strlen(seq);
	char buf[4] = {0}; /* for sending sequences to strtoul */
	unsigned long val;
	char *endptr;

	/* Initially do some sanity checks */
	*ok = false;
	*advance = 0;

	if (seq[0] != '\\') {
		send_error(&tok->pos, ERR, "compiler buf: invalid start of escape");
		return 0;
	} else if (len < 2) {
		send_error(&tok->pos, ERR,
		           "escape sequence '%s' too short", seq);
		return 0;
	}

	switch (seq[1]) {
	case 'x':
		/* Escape sequence \x##, ## = hexadecimal byte */
		if (len < 4) {
			send_error(&tok->pos, ERR,
			           "escape sequence '%s' too short", seq);
			*ok = false;
			return 0;
		}

		strncpy(buf, &seq[2], 2);
		val = strtoul(buf, &endptr, 16);

		/* Only check for endptr issues -- a 2-digit hex value can't go beyond
		 * a byte value. */
		if (endptr - buf != 2) {
			send_error(&tok->pos, ERR, "invalid 2-digit hex code '%s'", buf);
			return 0;
		}

		*ok = true;
		*advance = 4;
		return (uint8_t) val;
	case '0':
	case '1':
	case '2':
		// Escape sequence \###, ### = octal byte
		if (len < 4) {
			send_error(&tok->pos, ERR,
			           "escape sequence %s too short", seq);
			return 0;
		}

		strncpy(buf, &seq[1], 3);
		val = strtoul(buf, &endptr, 8);

		if (endptr - buf != 3) {
			send_error(&tok->pos, ERR, "invalid 3-digit octal code '%s'", buf);
			return 0;
		}

		*ok = true;
		*advance = 4;
		return (uint8_t)val;
	case 'n':
		*ok = true;
		*advance = 2;
		return '\n';
	case 't':
		*ok = true;
		*advance = 2;
		return '\t';
	case 'r':
		*ok = true;
		*advance = 2;
		return '\r';
	case '\'':
		*ok = true;
		*advance = 2;
		return '\'';
	case '"':
		*ok = true;
		*advance = 2;
		return '"';
	default:
		send_error(&tok->pos, ERR, "Unknown escape sequence at %s", seq);
		return 0;
	}
}

/* Parse a char into a uint8 value. Mark an error on invalid literals. */
uint8_t parse_char(const Token *tok)
{
	size_t len = strlen(tok->lit);
	uint8_t val;

	int advance;
	bool ok;

	if (tok->lit[0] == '\\') {
		/* parse an excape sequence */
		val = parse_escape(tok, 0, &advance, &ok);
		if (!ok) return 0;
		if ((size_t) advance < len) {
			send_error(&tok->pos, ERR, "character literal too long");
			return 0;
		}

		return val;
	} else if (len == 0) {
		send_error(&tok->pos, ERR, "empty character");
		return 0;
	} else if (len == 1) {
		return tok->lit[0];
	} else {
		send_error(&tok->pos, ERR, "character literal too long");
		return 0;
	}
}
