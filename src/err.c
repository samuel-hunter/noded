#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "noded.h"

#define RESET "\033[0m"
#define BOLD "\033[1m"
#define RED "\033[31m"

// Return the start of the given line.
static const char *strline(const char *src, int lineno)
{
	int cur = 1; // current line. The first line starts at 1.

	while (cur < lineno) {
		switch (*src) {
		case '\000':
			// This shouldn't happen unless the program
			// has a bug. While I would normally crash and
			// exit, this is only for `send_error`, so
			// printing a NULL is fine enough.
			return NULL;
		case '\n':
			cur++;
			break;
		}

		src++;
	}

	return src;
}

void vprint_error(const char *srcname, const char *src, const struct position *pos,
	enum error_type type, const char *fmt, va_list ap)
{
	const char *typestr;

	const char *line;
	size_t line_length;

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

	fflush(stdout); // Flush stdout so that it doesn't mangle with stderr.
	fprintf(stderr, BOLD "%s:%d:%d:" RESET " %s ",
	        srcname, pos->lineno, pos->colno, typestr);

	// Print the error
	vfprintf(stderr, fmt, ap);
	fprintf(stderr, ".\n");

	// Print the offending line and a caret to its column
	line = strline(src, pos->lineno);
	line_length = (size_t)(
		(strchr(line, '\n') ? strchr(line, '\n') : strchr(line, '\000')) - line);
	fwrite(line, sizeof(*line), line_length, stderr);
	fprintf(stderr, "\n");

	if (line[pos->colno] == '\n') {
		// Error at end of line; don't post caret
		fprintf(stderr, "\n");
	} else {
		for (int i = 0; i < pos->colno; i++) {
			if (line[i] == '\t') {
				fprintf(stderr, "\t");
			} else {
				fprintf(stderr, " ");
			}
		}
		fprintf(stderr, "^\n");
	}
}
