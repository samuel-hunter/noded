#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <err.h>

#include "noded.h"

static struct {
	// Source file
	FILE *f;
	const char *filename;

	size_t errors;
} Globals = {0};

// Number of errors before the parser panics.
static const size_t max_errors = 10;

void send_error(const struct position *pos, enum error_type type,
	const char *fmt, ...)
{
	va_list ap;

	// Print the error
	va_start(ap, fmt);
	vprint_error(Globals.filename, Globals.f, pos,
		type, fmt, ap);
	va_end(ap);

	// Send an exit depending on fatality.
	switch (type) {
	case WARN:
		break;
	case ERR:
		if (++Globals.errors > max_errors)
			errx(1, "Too many errors.");
		break;
	case FATAL:
		exit(1);
		break;
	}
}

bool has_errors(void)
{
	return Globals.errors > 0;
}

static void print_usage(const char *prog_name)
{
	fprintf(stderr, "Usage: %s [FILE]\n", prog_name);
}

int main(int argc, char **argv)
{
	struct parser parser;

	// Choose the file to interpret
	if (argc > 2) {
		print_usage(argv[0]);
		return 1;
	} else if (argc == 2) {
		Globals.filename = argv[1];
		Globals.f = fopen(Globals.filename, "r");
		if (Globals.f == NULL)
			err(1, "Cannot open %s", Globals.filename);
	} else {
		Globals.filename = "<stdin>";
		Globals.f = stdin;
	}

	// Scan the file token-by-token
	init_parser(&parser, Globals.f);

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

	// free the AST and code, since we no longer need it.
	free_decl(decl);
	clear_parser(&parser);
	fclose(Globals.f);

	if (code == NULL || has_errors()) {
		return 1;
	} else {
		fwrite(code, sizeof(*code), codesize, stdout);
		free(code);
	}

	return 0;
}
