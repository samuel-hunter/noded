#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <err.h>

#include "noded.h"

static struct {
	const char *filename;
	size_t errors;
	char *src;
} Globals = {0};

// Number of errors before the parser panics.
static const size_t max_errors = 10;

void send_error(const struct position *pos, enum error_type type,
	const char *fmt, ...)
{
	va_list ap;

	// Print the error
	va_start(ap, fmt);
	vprint_error(Globals.filename, Globals.src, pos,
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

// Read all of file f and return an allocated char pointer and set the
// pointer n to be the buffer's size. The buffer must be freed by
// the caller.
static char *read_all(FILE *f, size_t *n)
{
	// Arbitrary number, chosen because it shouldn't reallocate
	// *too* much.
	size_t size = 4096;
	size_t nread = 0;
	char *result = emalloc(size);

	while (!feof(f) && !ferror(f)) {
		if (nread == size) {
			size *= 2;
			result = erealloc(result, size);
		}
		nread += fread(&result[nread], 1, size-nread, f);
	}

	// Add a null terminator.
	if (nread == size) {
		result = erealloc(result, ++size);
	}
	result[size-1] = '\000';

	*n = nread;
	if (ferror(f) || nread == 0) {
		free(result);
		return NULL;
	}

	result = erealloc(result, nread);
	return result;
}

int main(int argc, char **argv)
{
	FILE *f;

	size_t src_size;
	struct parser parser;

	// Choose the file to interpret
	if (argc > 2) {
		print_usage(argv[0]);
		return 1;
	} else if (argc == 2) {
		Globals.filename = argv[1];
		f = fopen(Globals.filename, "r");
		if (f == NULL)
			err(1, "Cannot open %s", Globals.filename);
	} else {
		Globals.filename = "<stdin>";
		f = stdin;
	}

	Globals.src = read_all(f, &src_size);
	if (Globals.src == NULL) {
		if (ferror(f)) {
			errx(1, "%s: I/O Error.", Globals.filename);
		} else {
			fprintf(stderr, "WARN %s: Zero size file.\n", Globals.filename);
		}
	}
	fclose(f);

	// Scan the file token-by-token
	init_parser(&parser, Globals.src, src_size);

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
	free(Globals.src);

	if (code == NULL || has_errors()) {
		return 1;
	} else {
		fwrite(code, sizeof(*code), codesize, stdout);
		free(code);
	}

	return 0;
}
