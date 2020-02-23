#ifndef VM_H
#define VM_H

#include <stddef.h> // size_t
#include <stdint.h> // uint8_t

#define PROC_VARS 4
#define PROC_PORTS 4

enum instruction {
	OP_INVALID,
	OP_NOOP,

	OP_PUSH,
	OP_POP,
	OP_DUP,

	OP_NEGATE,
	OP_MUL,
	OP_DIV,
	OP_MOD,
	OP_ADD,
	OP_SUB,
	OP_SHL,
	OP_SHR,

	OP_LSS,
	OP_GTR,
	OP_EQL,
	OP_AND,
	OP_XOR,
	OP_OR,
	OP_LAND,
	OP_LOR,
	OP_LNOT,

	OP_JMP,
	OP_TJMP,

	// Keep ACTION# in order so someone can math the rest via
	// ACTION0 + n (e.g. SAVE2 == SAVE0 + 2)
	OP_SAVE0,
	OP_SAVE1,
	OP_SAVE2,
	OP_SAVE3,

	OP_LOAD0,
	OP_LOAD1,
	OP_LOAD2,
	OP_LOAD3,

	OP_INC0,
	OP_INC1,
	OP_INC2,
	OP_INC3,

	OP_DEC0,
	OP_DEC1,
	OP_DEC2,
	OP_DEC3,

	OP_SEND0,
	OP_SEND1,
	OP_SEND2,
	OP_SEND3,

	OP_RECV0,
	OP_RECV1,
	OP_RECV2,
	OP_RECV3,

	OP_HALT
};

typedef void (*send_handler)(uint8_t val, int port, void *dat);
typedef uint8_t (*recv_handler)(int port, void *dat);

struct proc_node {
	uint16_t isp; // instruction pointer
	const uint8_t *code; // machine code
	uint16_t code_size;

	uint8_t *stack;
	size_t stack_top;
	size_t stack_cap;

	send_handler send;
	recv_handler recv;

	uint8_t mem[PROC_VARS];
};

struct proc_node *new_proc_node(const uint8_t code[], size_t code_size,
                                send_handler send, recv_handler recv);
void run(struct proc_node *node, void *handler_dat);

#endif // VM_H
