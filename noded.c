#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <err.h>

#include "noded.h"

static struct globals {
	const char *filename;
	size_t errors;
	char *src;
} Globals = {0};

// Number of errors before the parser panics.
static const size_t max_errors = 10;

// Return the start of the line.
static const char *strline(int lineno)
{
	int cur = 1; // current line
	char *src = Globals.src;
	while (cur < lineno) {
		if (*src == '\0') {
			// This shouldn't happen unless the program
			// has a bug. While I would normally crash and
			// exit, this is only for `send_error`, so
			// printing a NULL is fine enough.
			return NULL;
		}

		if (*src++ == '\n')
			cur++;
	}

	return src;
}

void send_error(const struct position *pos, enum error_type type,
	const char *fmt, ...)
{
	// 512 should be reasonable enough.
#define LINESIZE 512
#define S_LINESIZE "509"
	va_list ap;
	const char *typestr;

	const char *line;
	char linebuf[LINESIZE + 1];
	size_t line_length;

	switch (type) {
	case WARN:
		typestr = "WARN";
		break;
	case ERR:
		typestr = "ERR";
		break;
	case FATAL:
		typestr = "FATAL";
		break;
	}

	fflush(stdout); // Flush stdout so that it doesn't mangle with stderr.
	fprintf(stderr, "%s %s:%d:%d: ", typestr,
	        Globals.filename, pos->lineno, pos->colno);

	// Copy the line (up to but excluding the newline).
	line = strline(pos->lineno);
	line_length = (size_t)(
		(strchr(line, '\n') ? strchr(line, '\n') : strchr(line, '\000')) - line);
	if (line_length > LINESIZE) {
		sprintf(linebuf, "%"S_LINESIZE"s...", line);
	} else {
		strncpy(linebuf, line, line_length);
		linebuf[line_length] = '\000';
	}

	// Print the error
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fprintf(stderr, ".\n");

	// Print the offending line and a caret to its column
	printf("%s\n", linebuf);

	if (line[pos->colno] == '\n') {
		// Error at end of line; don't post caret
		printf("\n");
	} else {
		for (int i = 0; i < pos->colno; i++) {
			if (line[i] == '\t') {
				printf("\t");
			} else {
				printf(" ");
			}
		}
		printf("^\n\n");
	}

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
#undef S_LINESIZE
#undef LINESIZE
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
