#include <stdio.h>
#include <stdlib.h>
#include <err.h>

#include "noded.h"

void print_usage(const char *prog_name)
{
	fprintf(stderr, "Usage: %s [FILE]\n", prog_name);
}

void interpret(FILE *f, const char filename[])
{
	struct scanner scanner;
	char *src;

	char literal[LITERAL_MAX + 1];
	struct position pos;
	enum token tok;

	fseek(f, 0, SEEK_END);
	size_t fsize = ftell(f);
	rewind(f);

	src = emalloc(fsize);
	fread(src, 1, fsize, f);
	fclose(f);

	init_scanner(&scanner, filename, src, fsize);
	do {
		tok = scan(&scanner, literal, &pos);
		if (scanner.has_errored) {
			fflush(stdout);
			fprintf(stderr, "%s:%d:%d: %s.\n",
                                filename, scanner.err.pos.lineno,
                                scanner.err.pos.colno,
                                scanner.err.msg);
			exit(1);
		}
		printf("%s:%d:%d\ttoken(%s) '%s'\n",
                       filename, pos.lineno, pos.colno,
                       strtoken(tok), literal);
	} while (tok != TOK_EOF);
}

int main(int argc, char **argv)
{
	const char *filename;
	FILE *f;

	if (argc != 2) {
		print_usage(argv[0]);
		return 1;
	}

	filename = argv[1];
	f = fopen(filename, "r");
	if (f == NULL)
		err(1, "Cannot open %s", argv[1]);

	interpret(f, filename);
	return 0;
}
