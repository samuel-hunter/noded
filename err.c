/*
 * err - error handling
 *
 * send_error(), the actual error-handling function, is noticeably
 * missing from here. Instead of passing down a function pointer to
 * every nontrivial module (scanning, parsing, compiling), it's much
 * simpler for each module to just call that handler outright.
 *
 * So, packed in noded.c and every tool, is their own implementation
 * of send_error(). Each program is still allowed to handle the error
 * in their own way, e.g. the tests can immediately exit on the first
 * error.
 */
#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "noded.h"

#define RESET "\033[0m"
#define BOLD  "\033[1m"
#define RED   "\033[31m"

static const int ERROR_MAX = 10;

/* Module-global variables */
static struct {
	const char *fname;
	FILE *f;
	int nerrors;
} Globals = {0};

void
init_error(FILE *f, const char *fname)
{
	Globals.f = f;
	Globals.fname = fname;
}

void
send_error(const Position *pos, ErrorType type, const char *fmt, ...)
{
	const char *typestr = NULL;
	char *lineptr = NULL;
	size_t n;
	long offset;
	va_list ap;

	/* Flush stdout so that it doesn't mangle with stderr. */
	fflush(stdout);

	switch (type) {
	case WARN:
		typestr = "warning:";
		break;
	case ERR:
		typestr = RED "error:" RESET;
		break;
	case FATAL:
		typestr = RED "FATAL:" RESET;
		break;
	}

	if (pos) {
		fprintf(stderr, BOLD "%s:%d:%d:" RESET " %s ",
		        Globals.fname, pos->lineno, pos->colno, typestr);
	} else {
		fprintf(stderr, BOLD "%s:" RESET " %s ",
			Globals.fname, typestr);
	}

	/* Print the error */
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	fprintf(stderr, ".\n");
	va_end(ap);

	/* Skip printing the offending line if we can't seek to it. */
	if (fseek(Globals.f, 0, SEEK_CUR) != 0) return;

	/* Print the offending line and a caret to its column */
	offset = ftell(Globals.f); /* preserve seek pos for later. */
	rewind(Globals.f);
	for (int curline = 0; curline < pos->lineno; curline++) {
		getline(&lineptr, &n, Globals.f);
	}
	printf("%s", lineptr);
	fseek(Globals.f, offset, SEEK_SET);


	if (lineptr[pos->colno] == '\n') {
		/* Error at end of line; don't post caret */
		fprintf(stderr, "\n");
	} else {
		for (int i = 0; i < pos->colno; i++) {
			if (lineptr[i] == '\t') {
				fprintf(stderr, "\t");
			} else {
				fprintf(stderr, " ");
			}
		}
		fprintf(stderr, "^\n");
	}
	free(lineptr);

	switch (type) {
	case WARN:
		break;
	case ERR:
		if (++Globals.nerrors > ERROR_MAX)
			errx(1, "too many errors.");
		break;
	case FATAL:
		/* Fatal errors mean the program should die */
		errx(1, "fatal error.");
		break;
	}
}

bool
has_errors(void)
{
	return Globals.nerrors > 0;
}