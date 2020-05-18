#include <err.h>
#include <stdio.h>
#include <stdlib.h>

#include "noded.h"

/* Module-global variables */
static struct {
	char *fname;
	FILE *f;
} Globals = {0};

void send_error(const Position *pos, ErrorType type, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vprint_error(Globals.fname, Globals.f, pos, type, fmt, ap);
	va_end(ap);

	exit(1); /* Abort */
}

int main(int argc, char *argv[])
{
	Scanner s;
	FullToken t;

	if (argc != 2)
		errx(1, "usage: %s file", argv[0]);

	Globals.fname = argv[1];
	Globals.f = fopen(Globals.fname, "r");
	if (Globals.f == NULL)
		err(1, "%s", argv[1]);

	init_scanner(&s, Globals.f);
	do {
		scan(&s, &t);
		printf("%s \"%s\" (%d, %d)\n", strtoken(t.tok),
			t.lit, t.pos.lineno, t.pos.colno);
	} while (t.tok != TOK_EOF);

	fclose(Globals.f);
	return 0;
}
