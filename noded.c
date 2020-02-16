#include <stdio.h>
#include <stdlib.h>
#include <err.h>

#include "noded.h"

void print_usage(const char *prog_name)
{
	fprintf(stderr, "Usage: %s [FILE]\n", prog_name);
}

// Read all of file f and return an allocated char pointer and set the
// pointer n to be the buffer's size. The buffer must be `free`d by
// the caller.
char *read_all(FILE *f, size_t *n)
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

	if (ferror(f)) {
		free(result);
		return NULL;
	}

	result = erealloc(result, nread);
	*n = nread;
	return result;
}

int main(int argc, char **argv)
{
	const char *filename;
	FILE *f;

	char *src;
	size_t src_size;
	struct scanner scanner;

	char literal[LITERAL_MAX + 1];
	struct position pos;
	enum token tok;

	// Choose the file to interpret
	if (argc > 2) {
		print_usage(argv[0]);
		return 1;
	} else if (argc == 2) {
		filename = argv[1];
		f = fopen(filename, "r");
		if (f == NULL)
			err(1, "Cannot open %s", filename);
	} else {
		filename = "<stdin>";
		f = stdin;
	}


	src = read_all(f, &src_size);
	if (src == NULL)
		errx(1, "%s: I/O Error.", filename);

	// Scan the file token-by-token
	init_scanner(&scanner, filename, src, src_size);
	do {
		tok = scan(&scanner, literal, &pos);
		if (scanner.has_errored) {
			fflush(stdout);
			fprintf(stderr, "%s:%d:%d: %s.\n",
                                filename, scanner.err.pos.lineno,
                                scanner.err.pos.colno,
                                scanner.err.msg);
			return 1;
		}
		printf("%s:%d:%d\ttoken(%s) '%s'\n",
                       filename, pos.lineno, pos.colno,
                       strtoken(tok), literal);
	} while (tok != TOK_EOF);

	free(src);
	return 0;
}
