#include <err.h>
#include <stdio.h>
#include <stdlib.h>

#include "noded.h"

struct vm_test {
	const char *name;
	const uint8_t *code;
	size_t code_size;

	struct test_port {
		const uint8_t *send;
		size_t send_len;
		size_t send_idx;

		const uint8_t *recv;
		size_t recv_len;
		size_t recv_idx;
	} ports[PROC_PORTS];
};

static void send(uint8_t val, int porti, void *dat)
{
	struct vm_test *test = (struct vm_test *)dat;
	struct test_port *port = &test->ports[porti];

	printf("Port%d -> %d\n", porti, val);

	if (port->send_idx == port->send_len)
		errx(1, "Too many messages from port %d "
		        "(expected only %lu messages)", porti, port->send_len);

	if (port->send[port->send_idx++] != val)
		errx(1, "Expected %d from port %d, but received %d",
		     port->send[port->send_idx-1], porti, val);
}

static uint8_t recv(int porti, void *dat)
{
	struct vm_test *test = (struct vm_test *)dat;
	struct test_port *port = &test->ports[porti];

	if (port->recv_idx == port->recv_len)
		errx(1, "Too many messages requested from port %d "
		        "(expected only %lu messages)", porti, port->recv_len);

	uint8_t result = port->recv[port->recv_idx++];
	printf("Port%d <- %d\n", porti, result);
	return result;
}

static void test_vm(struct vm_test *test)
{
	printf("Running %s...\n", test->name);
	struct proc_node *node = new_proc_node(test->code, test->code_size, &send, &recv);
	run(node, test);

	// Make sure *all* messages were sent and received
	for (size_t i = 0; i < PROC_PORTS; i++) {
		struct test_port *port = &test->ports[i];
		if (port->send_idx != port->send_len) {
			errx(1, "Expected %lu messages to be sent to port %lu, "
			     "but found only %lu.", port->send_len, i, port->send_idx);
		}

		if (port->recv_idx != port->recv_len) {
			errx(1, "Expected %lu messages to be received to port %lu, "
			     "but found only %lu.", port->recv_len, i, port->recv_idx);
		}
	}

	// Free the node manually, since code[] is statically
	// allocated.
	if (node->stack) free(node->stack);
	free(node);
	printf("OK\n\n");
}

int main(void)
{
	uint8_t test_basics_c[] = {
			OP_PUSH, 10,
			OP_SEND0, // Expected: 10

			OP_PUSH, 20,
			OP_PUSH, 0xff,
			OP_POP,
			OP_SEND0, // Expected: 20

			OP_PUSH, 30,
			OP_DUP,
			OP_SEND0, // Expected: 30
			OP_SEND0, // Expected: 30

			OP_HALT

	};
	uint8_t test_basics_1s[] = {10, 20, 30, 30};
	struct vm_test test_basics = {
		.name = "Basics Test",
		.code = test_basics_c,
		.code_size = sizeof(test_basics_c)/sizeof(*test_basics_c),
		.ports = {
			{
				.send = test_basics_1s,
				.send_len = sizeof(test_basics_1s)/
				            sizeof(*test_basics_1s)
			}
		}
	};
	test_vm(&test_basics);

	uint8_t test_variables_c[] = {
		// All variables should start at 0.
		OP_LOAD0,
		OP_SEND0, // Expected: 0

		OP_LOAD1,
		OP_SEND0, // Expected: 0

		OP_LOAD2,
		OP_SEND0, // Expected: 0

		OP_LOAD3,
		OP_SEND0, // Expected: 0

		OP_PUSH, 10,
		OP_SAVE0,
		OP_POP,

		OP_PUSH, 20,
		OP_SAVE1,
		OP_POP,

		OP_PUSH, 30,
		OP_SAVE2,
		OP_POP,

		OP_PUSH, 40,
		OP_SAVE3,
		OP_POP,

		OP_LOAD0,
		OP_SEND0, // Expected: 10

		OP_LOAD1,
		OP_SEND0, // Expected: 20

		OP_LOAD2,
		OP_SEND0, // Expected: 30

		OP_LOAD3,
		OP_SEND0, // Expected: 40

		OP_HALT
	};
	uint8_t test_variables_1s[] = {
		0, 0, 0, 0, 10, 20, 30, 40
	};
	struct vm_test test_variables = {
		.name = "Variable Test",
		.code = test_variables_c,
		.code_size = sizeof(test_variables_c)/sizeof(*test_variables_c),
		.ports = {
			{
				.send = test_variables_1s,
				.send_len = sizeof(test_variables_1s)/
				            sizeof(*test_variables_1s)
			}
		}
	};
	test_vm(&test_variables);

	uint8_t test_manyports_c[] = {
		OP_PUSH, 10,
		OP_SEND0,

		OP_PUSH, 20,
		OP_SEND1,

		OP_PUSH, 30,
		OP_SEND2,

		OP_PUSH, 40,
		OP_SEND3,

		OP_PUSH, 50,
		OP_SEND2,

		OP_PUSH, 60,
		OP_SEND1,

		OP_PUSH, 70,
		OP_SEND0,

		OP_HALT
	};
	uint8_t test_manyports_1s[] = {10, 70};
	uint8_t test_manyports_2s[] = {20, 60};
	uint8_t test_manyports_3s[] = {30, 50};
	uint8_t test_manyports_4s[] = {40};
	struct vm_test test_manyports = {
		.name = "Many Ports Test",
		.code = test_manyports_c,
		.code_size = sizeof(test_manyports_c)/sizeof(*test_manyports_c),
		.ports = {
			{
				.send = test_manyports_1s,
				.send_len = sizeof(test_manyports_1s)/
				            sizeof(*test_manyports_1s)
			},
			{
				.send = test_manyports_2s,
				.send_len = sizeof(test_manyports_2s)/
				            sizeof(*test_manyports_2s)
			},
			{
				.send = test_manyports_3s,
				.send_len = sizeof(test_manyports_3s)/
				            sizeof(*test_manyports_3s)
			},
			{
				.send = test_manyports_4s,
				.send_len = sizeof(test_manyports_4s)/
				            sizeof(*test_manyports_4s)
			}
		}
	};
	test_vm(&test_manyports);

	uint8_t test_recv_c[] = {
		OP_RECV0,
		OP_SEND1,

		OP_RECV2,
		OP_SEND3,

		OP_HALT
	};
	uint8_t test_recv_1r[] = {10};
	uint8_t test_recv_2s[] = {10};
	uint8_t test_recv_3r[] = {20};
	uint8_t test_recv_4s[] = {20};
	struct vm_test test_recv = {
		.name = "Recv Test",
		.code = test_recv_c,
		.code_size = sizeof(test_recv_c)/sizeof(*test_recv_c),
		.ports = {
			{
				.recv = test_recv_1r,
				.recv_len = sizeof(test_recv_1r)/
				            sizeof(*test_recv_1r)
			},
			{
				.send = test_recv_2s,
				.send_len = sizeof(test_recv_2s)/
				            sizeof(*test_recv_2s)
			},
			{
				.recv = test_recv_3r,
				.recv_len = sizeof(test_recv_3r)/
				            sizeof(*test_recv_3r)
			},
			{
				.send = test_recv_4s,
				.send_len = sizeof(test_recv_4s)/
				            sizeof(*test_recv_4s)
			}
		}
	};
	test_vm(&test_recv);


	uint8_t test_math_c[] = {
		// port0 <- 3 * 5
		OP_PUSH, 3,
		OP_PUSH, 5,
		OP_MUL,
		OP_SEND0, // Expected: 3*5 == 15

		// port0 <- 15 / 3
		OP_PUSH, 15,
		OP_PUSH, 3,
		OP_DIV,
		OP_SEND0, // Expected: 15/3 == 5

		// port0 <- 17 % 5
		OP_PUSH, 17,
		OP_PUSH, 5,
		OP_MOD,
		OP_SEND0, // Expected: 17 % 5 == 2

		// port0 <- 10 + 20
		OP_PUSH, 10,
		OP_PUSH, 20,
		OP_ADD,
		OP_SEND0, // Expected: 10+20 == 30

		// port0 <- 20 - 10
		OP_PUSH, 20,
		OP_PUSH, 10,
		OP_SUB,
		OP_SEND0, // Expected: 20-10 == 10

		// port0 <- 10 << 2
		OP_PUSH, 10,
		OP_PUSH, 2,
		OP_SHL,
		OP_SEND0, // Expected: 10<<2 == 40

		// port0 <- 64 >> 3
		OP_PUSH, 64,
		OP_PUSH, 3,
		OP_SHR,
		OP_SEND0, // Expected: 64>>3 == 8

		// Test {over,under}flow:
		OP_PUSH, 255,
		OP_PUSH, 1,
		OP_ADD,
		OP_SEND0, // Expected: 0

		OP_PUSH, 0,
		OP_PUSH, 1,
		OP_SUB,
		OP_SEND0, // Expected: 255

		OP_HALT
	};
	uint8_t test_math_1s[] = {
		3*5, 15/3, 17%5, 10+20, 20-10, 10<<2, 64>>3, 0, 255
	};
	struct vm_test test_math = {
		.name = "Math Test",
		.code = test_math_c,
		.code_size = sizeof(test_math_c)/sizeof(*test_math_c),
		.ports = {
			{
				.send = test_math_1s,
				.send_len = sizeof(test_math_1s)/
				            sizeof(*test_math_1s)
			}
		}
	};
	test_vm(&test_math);

	uint8_t test_comparison_c[] = {
		// 1 < 2
		OP_PUSH, 1,
		OP_PUSH, 2,
		OP_LSS,
		OP_SEND0, // Expected: 1

		// 2 < 1
		OP_PUSH, 2,
		OP_PUSH, 1,
		OP_LSS,
		OP_SEND0, // Expected: 0

		// 1 > 2
		OP_PUSH, 1,
		OP_PUSH, 2,
		OP_GTR,
		OP_SEND0, // Expected: 0

		// 2 > 1
		OP_PUSH, 2,
		OP_PUSH, 1,
		OP_GTR,
		OP_SEND0, // Expected: 1

		// 1 == 2
		OP_PUSH, 1,
		OP_PUSH, 2,
		OP_EQL,
		OP_SEND0, // Expected: 0

		// 2 == 2
		OP_PUSH, 2,
		OP_PUSH, 2,
		OP_EQL,
		OP_SEND0, // Expected: 1

		OP_HALT
	};
	uint8_t test_comparison_1s[] = {1, 0, 0, 1, 0, 1};
	struct vm_test test_comparison = {
		.name = "Comparison Test",
		.code = test_comparison_c,
		.code_size = sizeof(test_comparison_c)/sizeof(*test_comparison_c),
		.ports = {
			{
				.send = test_comparison_1s,
				.send_len = sizeof(test_comparison_1s)/
				            sizeof(*test_comparison_1s)
			}
		}
	};
	test_vm(&test_comparison);

	uint8_t test_bitwise_c[] = {
		OP_PUSH, 0x0C, // 0b1100
		OP_PUSH, 0x0A, // 0b1010
		OP_AND,
		OP_SEND0, // Expected: 8 (0b1000)

		OP_PUSH, 0x0C,
		OP_PUSH, 0x0A,
		OP_XOR,
		OP_SEND0, // Expected: 6 (0b0110)

		OP_PUSH, 0x0C,
		OP_PUSH, 0x0A,
		OP_OR,
		OP_SEND0, // Expected: 14 (0b1110)

		OP_HALT
	};
	uint8_t test_bitwise_1s[] = {8, 6, 14};
	struct vm_test test_bitwise = {
		.name = "Bitwise Test",
		.code = test_bitwise_c,
		.code_size = sizeof(test_bitwise_c)/sizeof(*test_bitwise_c),
		.ports = {
			{
				.send = test_bitwise_1s,
				.send_len = sizeof(test_bitwise_1s)/
				            sizeof(*test_bitwise_1s)
			}
		}
	};
	test_vm(&test_bitwise);

	uint8_t test_logical_c[] = {
		OP_PUSH, 2,
		OP_PUSH, 3,
		OP_LAND,
		OP_SEND0, // Expected: 3

		OP_PUSH, 0,
		OP_PUSH, 3,
		OP_LAND,
		OP_SEND0, // Expected: 0

		OP_PUSH, 2,
		OP_PUSH, 3,
		OP_LOR,
		OP_SEND0, // Expected: 2

		OP_PUSH, 0,
		OP_PUSH, 3,
		OP_LOR,
		OP_SEND0, // Expected: 3

		OP_PUSH, 0,
		OP_LNOT,
		OP_SEND0, // Expected: 1

		OP_PUSH, 1,
		OP_LNOT,
		OP_SEND0, // Expected: 0

		OP_HALT
	};
	uint8_t test_logical_1s[] = {3, 0, 2, 3, 1, 0};
	struct vm_test test_logical = {
		.name = "Logical Test",
		.code = test_logical_c,
		.code_size = sizeof(test_logical_c)/sizeof(*test_logical_c),
		.ports = {
			{
				.send = test_logical_1s,
				.send_len = sizeof(test_logical_1s)/
				            sizeof(*test_logical_1s)
			}
		}
	};
	test_vm(&test_logical);

	uint8_t test_jump_c[] = {
		OP_JMP, 0, 6, // 6 = address to halt instruction

		// Skip this instruction
		OP_PUSH, 10,
		OP_SEND0, // Expected: skipped

		OP_HALT,
	};
	struct vm_test test_jump = {
		.name = "Unconditional Jump Test",
		.code = test_jump_c,
		.code_size = sizeof(test_jump_c)/sizeof(*test_jump_c)
	};
	test_vm(&test_jump);

	uint8_t test_tjmp_c[] = {
		OP_PUSH, 1,
		OP_TJMP, 0, 8, // 8 = address to PUSH 0

		// This should be skipped because JTMP pops a truthy
		// value.
		OP_PUSH, 10,
		OP_SEND0, // Expected: skipped

		OP_PUSH, 0,
		OP_TJMP, 0, 16, // 16 = address to HALT

		// This should *not* be skipped because TJMP pops a
		// falsey value.
		OP_PUSH, 20,
		OP_SEND0, // Expected: 20

		OP_HALT
	};
	uint8_t test_tjmp_1s[] = {20};
	struct vm_test test_tjmp = {
		.name = "Conditional Jump Test",
		.code = test_tjmp_c,
		.code_size = sizeof(test_tjmp_c)/sizeof(*test_tjmp_c),
		.ports = {
			{
				.send = test_tjmp_1s,
				.send_len = sizeof(test_tjmp_1s)/
				            sizeof(*test_tjmp_1s)
			}
		}
	};
	test_vm(&test_tjmp);

	uint8_t test_countloop_c[] = {
		// %0 <- $0
		OP_LOAD0,
		OP_SEND0,

		// Push ++$0 to stack
		OP_INC0,

		// <= 0
		OP_PUSH, 10,
		OP_GTR,
		OP_LNOT, // In total: ++$0 <= 0

		// If true, go to 0
		OP_TJMP, 0, 0,

		OP_HALT
	};
	uint8_t test_countloop_1s[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
	struct vm_test test_countloop = {
		.name = "Counting Loop Test",
		.code = test_countloop_c,
		.code_size = sizeof(test_countloop_c)/sizeof(*test_countloop_c),
		.ports = {
			{
				.send = test_countloop_1s,
				.send_len = sizeof(test_countloop_1s)/
				            sizeof(*test_countloop_1s)
			}
		}
	};
	test_vm(&test_countloop);

	printf("Looks good!\n");
}
