#ifndef VM_FRAMEWORK_H
#define VM_FRAMEWORK_H

#include "noded.h"
#include "bytecode.h"

struct vm_test {
	const char *name;
	uint8_t *code;
	uint16_t code_size;

	struct test_port {
		const uint8_t *send;
		size_t send_len;
		size_t send_idx;

		const uint8_t *recv;
		size_t recv_len;
		size_t recv_idx;
	} ports[PROC_PORTS];
};

void test_code(struct vm_test *test);

#endif /* VM_FRAMEWORK_H */
