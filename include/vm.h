#ifndef BYTECODE_H
#define BYTECODE_H

#include <stdbool.h> // bool
#include <stddef.h>  // size_t
#include <stdint.h>  // uint8_t, UINT8_MAX, uint16_t

#include "noded.h" // position
#include "dict.h"  // symdict

enum {
	RECV_STORE_MASK = 0x03,
	RECV_PORT_FLAG = 0x04
};

enum {
	PROC_VARS  = 4,
	PROC_PORTS = 4
};

enum opcode {
	OP_INVALID,
	OP_NOOP,

	OP_PUSH,
	OP_POP,
	OP_DUP,

	OP_NEGATE,

	// All opcodes from OP_MUL to OP_LNOT *MUST* be aligned with
	// MUL to LNOT.
	OP_MUL,
	OP_DIV,
	OP_MOD,
	OP_ADD,
	OP_SUB,

	OP_SHL,
	OP_SHR,

	OP_AND,
	OP_XOR,
	OP_OR,

	OP_LSS,
	OP_LTE,
	OP_GTR,
	OP_GTE,

	OP_EQL,
	OP_NEQ,

	OP_LAND,
	OP_LOR,
	OP_LNOT,

	OP_JMP,
	OP_TJMP,
	OP_FJMP,

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

struct wire {
	enum { EMPTY, FULL, CONSUMED } status;
	uint8_t buf;
};

enum wire_status {
	BUFFERED,
	BLOCKED,
	PROCESSED
};

struct node_class {
	bool (*tick)(void *this);
	void (*free)(void *this);
	void (*add_wire)(void *this, int porti, struct wire *wire);
};

struct node {
	const struct node_class *class;
	void *dat;
};

struct proc_node {
	uint16_t isp; // instruction pointer
	const uint8_t *code; // machine code.
	uint16_t code_size;

	uint8_t *stack;
	size_t stack_top;
	size_t stack_cap;

	struct wire *wires[PROC_PORTS];
	uint8_t mem[PROC_VARS];
};

struct io_node {
	bool eof_reached;
	bool has_buf;
	uint8_t buf;

	struct wire *in_wire;
	struct wire *out_wire;
};

enum io_port
{
	IO_IN,
	IO_OUT
};

struct runtime {
	struct node *nodes;
	size_t nnodes;
	size_t node_cap;

	struct wire *wires;
	size_t nwires;
	size_t wire_cap;
};


// vm.c

enum wire_status send(struct wire *wire, uint8_t dat);
enum wire_status recv(struct wire *wire, uint8_t *dest);

struct node *add_node(struct runtime *env);
struct node *add_proc_node(struct runtime *env,
	uint8_t code[], size_t code_size);
struct node *add_io_node(struct runtime *env);
void add_wire(struct runtime *env, struct node *src, int src_porti,
	struct node *dest, int dest_porti);
void run(struct runtime *env);
void clear_runtime(struct runtime *env);

#endif // BYTECODE_H
