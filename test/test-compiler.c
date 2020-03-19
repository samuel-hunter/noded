#include <err.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "noded.h"
#include "vm-framework.h"

struct compiler_test {
	const char *name;
	const char *src;

	struct test_port ports[PROC_PORTS];
};

static struct {
	bool has_error;
	const struct compiler_test *cur;
} Globals = {0};

void send_error(const struct position *pos, enum error_type type,
	const char *fmt, ...)
{
	va_list ap;

	// Print the error
	va_start(ap, fmt);
	vprint_error(Globals.cur->name, Globals.cur->src, pos,
		type, fmt, ap);
	va_end(ap);

	// Send an exit if fatal.
	if (type == FATAL)
		exit(1);
}

bool has_errors(void)
{
	return Globals.has_error;
}

static void test_compiler(struct compiler_test *test)
{
	struct parser parser;
	struct decl *decl;
	struct vm_test vmtest;

	Globals.cur = test;

	printf("Running %s...\n", test->name);

	init_parser(&parser, test->src, strlen(test->src));
	decl = parse_decl(&parser);
	if (has_errors())
		exit(1);

	if (decl->type != PROC_DECL) {
		fprintf(stderr, "Expected proc decl\n");
		exit(1);
	}

	vmtest.code = compile(&decl->data.proc, &vmtest.code_size);
	if (has_errors() || vmtest.code == NULL) {
		exit(1);
	}

	vmtest.name = test->name;
	for (int i = 0; i < PROC_PORTS; i++)
		vmtest.ports[i] = test->ports[i];
	test_code(&vmtest);

	// Free all constructs.
	free(vmtest.code);
	free_decl(decl);

	printf("OK\n\n");
}

int main(void)
{
	uint8_t test_if_1s[] = {20};
	struct compiler_test test_if = {
		.name = "If Test",
		.src = "processor test {"
		"	if (10) {"
		"		%out <- 20;"
		"	}"
		"	halt;"
		"}",
		.ports = {
			{
				.send = test_if_1s,
				.send_len = sizeof(test_if_1s)/
				            sizeof(*test_if_1s)
			}
		}
	};
	test_compiler(&test_if);

	printf("LGTM\n");
	return 0;
}
