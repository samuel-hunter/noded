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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "noded.h"

#define RESET "\033[0m"
#define BOLD  "\033[1m"
#define RED   "\033[31m"

void vprint_error(const char *srcname, FILE *f, const Position *pos,
	ErrorType type, const char *fmt, va_list ap)
{
	const char *typestr = NULL;
	char *lineptr = NULL;
	size_t n;
	long offset;

	fflush(stdout); // Flush stdout so that it doesn't mangle with stderr.

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
		        srcname, pos->lineno, pos->colno, typestr);
	} else {
		fprintf(stderr, BOLD "%s:" RESET " %s ",
			srcname, typestr);
	}

	// Print the error
	vfprintf(stderr, fmt, ap);
	fprintf(stderr, ".\n");

	// Skip printing the offending line if we can't seek to it.
	if (fseek(f, 0, SEEK_CUR) != 0) return;

	// Print the offending line and a caret to its column
	offset = ftell(f); // preserve seek pos for later.
	rewind(f);
	for (int curline = 0; curline < pos->lineno; curline++) {
		getline(&lineptr, &n, f);
	}
	printf("%s", lineptr);
	fseek(f, offset, SEEK_SET);


	if (lineptr[pos->colno] == '\n') {
		// Error at end of line; don't post caret
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
}
