/*
 * compile - compile a processor from stdin to bytecode.
 *
 * compile reads a single processor declaration from standard input
 * and writes binary bytecode to standard output. I've been pairing
 * this with disasm to spot-check compiled code
 *
 * see also tools/disasm.c.
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <err.h>

#include "noded.h"

static size_t errors;
// Number of errors before the parser panics.
static const size_t max_errors = 10;

void send_error(const struct position *pos, enum error_type type,
	const char *fmt, ...)
{
	va_list ap;

	// Print the error
	va_start(ap, fmt);
	vprint_error("<stdin>", stdin, pos,
		type, fmt, ap);
	va_end(ap);

	// Send an exit depending on fatality.
	switch (type) {
	case WARN:
		break;
	case ERR:
		if (++errors > max_errors)
			errx(1, "Too many errors.");
		break;
	case FATAL:
		exit(1);
		break;
	}
}

bool has_errors(void)
{
	return errors > 0;
}

int main(int argc, char **argv)
{
	struct parser parser;

	// This program doesn't accept any arguments.
	if (argc != 1) {
		fprintf(stderr, "Usage: %s < INPUT\n", argv[0]);
		return 1;
	}

	// Scan the file token-by-token
	init_parser(&parser, stdin);

	struct decl *decl = parse_decl(&parser);
	if (has_errors()) {
		return 1;
	}

	if (decl->type != PROC_DECL) {
		fprintf(stderr, "Expected proc decl\n");
		return 1;
	}

	uint16_t codesize;
	uint8_t *code = compile(&decl->data.proc, &codesize);

	// free the AST and parser, since we no longer need it.
	free_decl(decl);
	clear_parser(&parser);

	if (code == NULL || has_errors()) {
		return 1;
	} else {
		fwrite(code, sizeof(*code), codesize, stdout);
		free(code);
	}

	return 0;
}
