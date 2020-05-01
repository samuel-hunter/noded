/*
 * test-vm - virtual machine validation
 *
 * test-vm takes bytecode, feeds it numbers for RECV ops, and ensures
 * that it sends back the expected SEND ops.
 */
#include <err.h>
#include <stdio.h>
#include <stdlib.h>

#include "noded.h"
#include "vm-framework.h"

#define LEN(X) (sizeof(X)/sizeof(*(X)))

static void test_vm(struct vm_test *test)
{
	printf("Running %s...\n", test->name);
	test_code(test);
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
		.code_size = LEN(test_basics_c),
		.ports = {
			{
				.type = SEND_PORT,
				.buf = test_basics_1s,
				.buf_len = LEN(test_basics_1s)
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
		.code_size = LEN(test_variables_c),
		.ports = {
			{
				.type = SEND_PORT,
				.buf = test_variables_1s,
				.buf_len = LEN(test_variables_1s)
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
		.code_size = LEN(test_manyports_c),
		.ports = {
			{
				.type = SEND_PORT,
				.buf = test_manyports_1s,
				.buf_len = LEN(test_manyports_1s)
			},
			{
				.type = SEND_PORT,
				.buf = test_manyports_2s,
				.buf_len = LEN(test_manyports_2s)
			},
			{
				.type = SEND_PORT,
				.buf = test_manyports_3s,
				.buf_len = LEN(test_manyports_3s)
			},
			{
				.type = SEND_PORT,
				.buf = test_manyports_4s,
				.buf_len = LEN(test_manyports_4s)
			}
		}
	};
	test_vm(&test_manyports);

	uint8_t test_recv_c[] = {
		OP_RECV0,
		OP_SEND1, // %1 <- %0

		OP_RECV2,
		OP_SEND3, // %3 <- %2

		OP_RECV0,
		OP_SAVE1, // $1 <- %0
		OP_LOAD1,
		OP_SEND1, // %1 <- $1

		OP_RECV2,
		OP_SAVE3, // $3 <- %2
		OP_LOAD3,
		OP_SEND3, // %3 <- $3

		OP_HALT
	};
	uint8_t test_recv_1r[] = {10, 30};
	uint8_t test_recv_2s[] = {10, 30};
	uint8_t test_recv_3r[] = {20, 40};
	uint8_t test_recv_4s[] = {20, 40};
	struct vm_test test_recv = {
		.name = "Recv Test",
		.code = test_recv_c,
		.code_size = LEN(test_recv_c),
		.ports = {
			{
				.type = RECV_PORT,
				.buf = test_recv_1r,
				.buf_len = LEN(test_recv_1r)
			},
			{
				.type = SEND_PORT,
				.buf = test_recv_2s,
				.buf_len = LEN(test_recv_2s)
			},
			{
				.type = RECV_PORT,
				.buf = test_recv_3r,
				.buf_len = LEN(test_recv_3r)
			},
			{
				.type = SEND_PORT,
				.buf = test_recv_4s,
				.buf_len = LEN(test_recv_4s)
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
		.code_size = LEN(test_math_c),
		.ports = {
			{
				.type = SEND_PORT,
				.buf = test_math_1s,
				.buf_len = LEN(test_math_1s)
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
		.code_size = LEN(test_comparison_c),
		.ports = {
			{
				.type = SEND_PORT,
				.buf = test_comparison_1s,
				.buf_len = LEN(test_comparison_1s)
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
		.code_size = LEN(test_bitwise_c),
		.ports = {
			{
				.type = SEND_PORT,
				.buf = test_bitwise_1s,
				.buf_len = LEN(test_bitwise_1s)
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
		.code_size = LEN(test_logical_c),
		.ports = {
			{
				.type = SEND_PORT,
				.buf = test_logical_1s,
				.buf_len = LEN(test_logical_1s)
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
		.code_size = LEN(test_jump_c),
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
		.code_size = LEN(test_tjmp_c),
		.ports = {
			{
				.type = SEND_PORT,
				.buf = test_tjmp_1s,
				.buf_len = LEN(test_tjmp_1s)
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
		OP_LNOT, // In total: ++$0 <= 10

		// If true, go to 0
		OP_TJMP, 0, 0,

		OP_HALT
	};
	uint8_t test_countloop_1s[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
	struct vm_test test_countloop = {
		.name = "Counting Loop Test",
		.code = test_countloop_c,
		.code_size = LEN(test_countloop_c),
		.ports = {
			{
				.type = SEND_PORT,
				.buf = test_countloop_1s,
				.buf_len = LEN(test_countloop_1s)
			}
		}
	};
	test_vm(&test_countloop);

	printf("LGTM!\n");
}
