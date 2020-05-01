#ifndef VM_FRAMEWORK_H
#define VM_FRAMEWORK_H

#include "noded.h"
#include "vm.h"

enum port_type { NONE, SEND_PORT, RECV_PORT };

struct vm_test {
	const char *name;
	uint8_t *code;
	uint16_t code_size;

	struct test_port {
		enum port_type type;
		const uint8_t *buf;
		size_t buf_len;
	} ports[PROC_PORTS];
};

void test_code(struct vm_test *vm_test);

#endif /* VM_FRAMEWORK_H */
