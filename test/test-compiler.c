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
	FILE *f;
} Globals = {0};

void send_error(const struct position *pos, enum error_type type,
	const char *fmt, ...)
{
	va_list ap;

	// Print the error
	va_start(ap, fmt);
	vprint_error(Globals.cur->name, Globals.f, pos,
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
	FILE *f;

	Globals.cur = test;

	printf("Running %s...\n", test->name);

	// Move test code to a file for the program to read.
	f = tmpfile();
	if (f == NULL)
		errx(1, "tmpfile(): I/O Error");
	if (fwrite(test->src, 1, strlen(test->src), f) < strlen(test->src))
		errx(1, "fwrite(): I/O Error");
	rewind(f);

	Globals.f = f;
	init_parser(&parser, f);
	decl = parse_decl(&parser);
	if (has_errors())
		exit(1);

	if (decl->type != PROC_DECL) {
		errx(1, "Expected proc decl\n");
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
	Globals.f = NULL;
	fclose(f);
	free(vmtest.code);
	free_decl(decl);

	printf("OK\n\n");
}

int main(void)
{
	const char *test_send_src =
		"processor test {"
		"    %one <- 2;"
		"    %two <- 3;"
		"    %three <- 5;"
		"    %four <- 7;"
		"    %three <- 11;"
		"    %two <- 13;"
		"    %one <- 17;"
		"    halt;"
		"}";
	uint8_t test_send_1s[] = {2, 17};
	uint8_t test_send_2s[] = {3, 13};
	uint8_t test_send_3s[] = {5, 11};
	uint8_t test_send_4s[] = {7};
	struct compiler_test test_send = {
		.name = "Send Test",
		.src = test_send_src,
		.ports = {
			{
				.send = test_send_1s,
				.send_len = sizeof(test_send_1s)/
				            sizeof(*test_send_1s)
			},
			{
				.send = test_send_2s,
				.send_len = sizeof(test_send_2s)/
				            sizeof(*test_send_2s)
			},
			{
				.send = test_send_3s,
				.send_len = sizeof(test_send_3s)/
				            sizeof(*test_send_3s)
			},
			{
				.send = test_send_4s,
				.send_len = sizeof(test_send_4s)/
				            sizeof(*test_send_4s)
			},
		}
	};
	test_compiler(&test_send);

	const char *test_vars_src =
		"processor test {"
		"    $a = 2;"
		"    %out <- $a;"
		"    %out <- ($a = 3);"
		"    halt;"
		"}";
	uint8_t test_vars_1s[] = {2, 3};
	struct compiler_test test_vars = {
		.name = "Variable Test",
		.src = test_vars_src,
		.ports = {
			{
				.send = test_vars_1s,
				.send_len = sizeof(test_vars_1s)/
				            sizeof(*test_vars_1s)
			}
		}
	};
	test_compiler(&test_vars);

	const char *test_recv_src =
		"processor test {"
		"    $var <- %one;" // make sure %one is registered as port 0
		"    %two <- $var;"
		"    %two <- %one;"
		"    halt;"
		"}";
	uint8_t test_recv_1s[] = {10, 20};
	struct compiler_test test_recv = {
		.name = "Recv Test",
		.src = test_recv_src,
		.ports = {
			{
				.recv = test_recv_1s,
				.recv_len = sizeof(test_recv_1s)/
				            sizeof(*test_recv_1s)
			},
			{
				.send = test_recv_1s,
				.send_len = sizeof(test_recv_1s)/
				            sizeof(*test_recv_1s)
			}
		}
	};
	test_compiler(&test_recv);

	const char *test_unary_src =
		"processor test {"
		"    %out <- ~0xaa;"
		"    %out <- !1;"
		"    %out <- !0;"

		"    $var1 = 10;"
		"    $var2 = 10;"

		"    %out <- ++$var1;"
		"    %out <- $var1++;"
		"    %out <- $var1;"

		"    %out <- --$var2;"
		"    %out <- $var2--;"
		"    %out <- $var2;"
		"    halt;"
		"}";
	uint8_t test_unary_1s[] = {0x55, 0, 1, 11, 11, 12, 9, 9, 8};
	struct compiler_test test_unary = {
		.name = "Unary Test",
		.src = test_unary_src,
		.ports = {
			{
				.send = test_unary_1s,
				.send_len = sizeof(test_unary_1s)/
				            sizeof(*test_unary_1s)
			}
		}
	};
	test_compiler(&test_unary);

	const char *test_binary_src =
		"processor test {"
		"    %out <- 2 + 3;"
		"    %out <- 2 * 3;"
		"    %out <- 2 << 3;"
		"    %out <- 7 % 3;"
		"    halt;"
		"}";
	uint8_t test_binary_1s[] = {5, 6, 16, 1};
	struct compiler_test test_binary = {
		.name = "Binary Test",
		.src = test_binary_src,
		.ports = {
			{
				.send = test_binary_1s,
				.send_len = sizeof(test_binary_1s)/
				            sizeof(*test_binary_1s)
			}
		}
	};
	test_compiler(&test_binary);

	const char *test_labeled_src =
		"processor test {"
		"    %out <- 2;"
		"    goto ok;"

		"    %out <- 3;"

		"end:"
		"    %out <- 5;"
		"    halt;"

		"ok:"
		"    %out <- 7;"
		"    goto end;"
		"}";
	uint8_t test_labeled_1s[] = {2, 7, 5};
	struct compiler_test test_labeled = {
		.name = "Labeled Test",
		.src = test_labeled_src,
		.ports = {
			{
				.send = test_labeled_1s,
				.send_len = sizeof(test_labeled_1s)/
				            sizeof(*test_labeled_1s)
			}
		}
	};
	test_compiler(&test_labeled);

	const char *test_if_src =
		"processor test {"
		"    if (10) {"
		"        %out <- 20;"
		"    }"
		"    halt;"
		"}";
	uint8_t test_if_1s[] = {20};
	struct compiler_test test_if = {
		.name = "If Test",
		.src = test_if_src,
		.ports = {
			{
				.send = test_if_1s,
				.send_len = sizeof(test_if_1s)/
				            sizeof(*test_if_1s)
			}
		}
	};
	test_compiler(&test_if);

	const char *test_continuous_src =
		"processor test {"
		"    $var <- %in;"
		"    if ($var == 0) halt;"
		"    %out <- $var;"
		"}";
	uint8_t test_continuous_1r[] = {2, 3, 5, 7, 11, 0};
	uint8_t test_continuous_2s[] = {2, 3, 5, 7, 11};
	struct compiler_test test_continuous = {
		.name = "Continuous Test",
		.src = test_continuous_src,
		.ports = {
			{
				.recv = test_continuous_1r,
				.recv_len = sizeof(test_continuous_1r)/
				            sizeof(*test_continuous_1r)
			},
			{
				.send = test_continuous_2s,
				.send_len = sizeof(test_continuous_2s)/
				            sizeof(*test_continuous_2s)
			}
		}
	};
	test_compiler(&test_continuous);

	const char *test_while_src =
		"processor test {"
		"    $var = 10;"
		"    while ($var > 0) {"
		"        %out <- $var;"
		"        --$var;"
		"    }"
		"    halt;"
		"}";
	uint8_t test_while_1s[] = {10, 9, 8, 7, 6, 5, 4, 3, 2, 1};
	struct compiler_test test_while = {
		.name = "While Test",
		.src = test_while_src,
		.ports = {
			{
				.send = test_while_1s,
				.send_len = sizeof(test_while_1s)/
				            sizeof(*test_while_1s)
			}
		}
	};
	test_compiler(&test_while);

	const char *test_do_while_src =
		"processor test {"
		"    $var = 0;"
		"    do {"
		"        %out <- $var;"
		"        ++$var;"
		"    } while ($var < 10);"

		"    do {"
		"        %out <- $var;"
		"        ++$var;"
		"    } while ($var < 10);"

		"    halt;"
		"}";
	uint8_t test_do_while_1s[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
	struct compiler_test test_do_while = {
		.name = "Do While Test",
		.src = test_do_while_src,
		.ports = {
			{
				.send = test_do_while_1s,
				.send_len = sizeof(test_do_while_1s)/
				            sizeof(*test_do_while_1s)
			}
		}
	};
	test_compiler(&test_do_while);

	const char *test_for_src =
		"processor test {"
		"    for ($var = 0; $var < 10; $var++) {"
		"        %out <- $var;"
		"    }"
		"    halt;"
		"}";
	uint8_t test_for_1s[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
	struct compiler_test test_for = {
		.name = "For Test",
		.src = test_for_src,
		.ports = {
			{
				.send = test_for_1s,
				.send_len = sizeof(test_for_1s)/
				            sizeof(*test_for_1s)
			}
		}
	};
	test_compiler(&test_for);

	const char *test_break_src =
		"processor test {"
		"    for ($var = 0; $var < 10; $var++) {"
		"        if ($var == 5) break;"
		"        %one <- $var;"
		"    }"

		"    for ($var = 0; $var < 3; $var++) {"
		"        %two <- $var;"
		"        for ($var2 = 0; $var2 < 3; $var2++) {"
		"            if ($var == 2) break;"
		"            %two <- $var2;"
		"        }"
		"    }"
		"    halt;"
		"}";
	uint8_t test_break_1s[] = {0, 1, 2, 3, 4};
	uint8_t test_break_2s[] = {0, 0, 1, 2, 1, 0, 1, 2, 2};
	struct compiler_test test_break = {
		.name = "Break Test",
		.src = test_break_src,
		.ports = {
			{
				.send = test_break_1s,
				.send_len = sizeof(test_break_1s)/
				            sizeof(*test_break_1s)
			},
			{
				.send = test_break_2s,
				.send_len = sizeof(test_break_2s)/
				            sizeof(*test_break_2s)
			}
		}
	};
	test_compiler(&test_break);

	const char *test_continue_src =
		"processor test {"
		"    for ($var = 0; $var < 5; $var++) {"
		"        if ($var == 3) continue;"
		"        %out <- $var;"
		"    }"
		"    halt;"
		"}";
	uint8_t test_continue_1s[] = {0, 1, 2, 4};
	struct compiler_test test_continue = {
		.name = "Continue Test",
		.src = test_continue_src,
		.ports = {
			{
				.send = test_continue_1s,
				.send_len = sizeof(test_continue_1s)/
				            sizeof(*test_continue_1s)
			}
		}
	};
	test_compiler(&test_continue);

	printf("LGTM!\n");
	return 0;
}
