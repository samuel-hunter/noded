/*
 * vm - virtual machine execution
 *
 * vm includes the heartbeat of the program, and runs the executed
 * bytecode.
 */
#include <assert.h>
#include <err.h>
#include <stdlib.h>
#include <string.h>

#include "noded.h"
#include "vm.h"

static bool tick_proc_node(void *this);
static void free_proc_node(void *this);
static void add_wire_to_proc_node(void *this, int porti, struct wire *wire);

static const struct node_class proc_node_class = {
	.tick = &tick_proc_node,
	.free = &free_proc_node,
	.add_wire = &add_wire_to_proc_node
};

static bool tick_io_node(void *this);
static void free_io_node(void *this);
static void add_wire_to_io_node(void *this, int porti, struct wire *wire);

static const struct node_class io_node_class = {
	.tick = &tick_io_node,
	.free = &free_io_node,
	.add_wire = &add_wire_to_io_node
};

// Send a message through the wire.
enum wire_status send(struct wire *wire, uint8_t dat)
{
	if (!wire) return BLOCKED;

	switch (wire->status) {
	case EMPTY:
		wire->buf = dat;
		wire->status = FULL;
		return BUFFERED;
	case FULL:
		return BLOCKED;
	case CONSUMED:
		wire->status = EMPTY;
		return PROCESSED;
	}

	// Should never be here unless all hell is loose.
	assert(false);
}

// Request a message from the wire.
enum wire_status recv(struct wire *wire, uint8_t *dest)
{
	if (!wire) return BLOCKED;

	if (wire->status == FULL) {
		*dest = wire->buf;
		wire->status = CONSUMED;
		return PROCESSED;
	} else {
		return BLOCKED;
	}
}

// Reserve a place for for the node and return its pointer.
struct node *add_node(struct runtime *env)
{
	// Create or expand the node vector when necessary.
	if (env->node_cap == 0) {
		env->node_cap = 8;
		env->nodes = ecalloc(env->node_cap, sizeof(*env->nodes));
	} else if (env->node_cap == env->nnodes) {
		env->node_cap *= 2;
		env->nodes = erealloc(env->nodes,
			env->node_cap * sizeof(*env->nodes));
	}

	return &env->nodes[env->nnodes++];
}

struct node *add_proc_node(struct runtime *env,
	uint8_t code[], size_t code_size)
{
	struct node *node = add_node(env);
	struct proc_node *proc = ecalloc(1, sizeof(*proc));

	node->class = &proc_node_class;
	node->dat = proc;

	proc->code = code;
	proc->code_size = code_size;

	// Eagerly create a new stack buffer.
        // TODO: reckon whether it's possible to analyze the code to
	// find the maximum stack size ahead of time.
	proc->stack_cap = 8; // Arbitrary number.
	proc->stack = ecalloc(proc->stack_cap, sizeof(*proc->stack));

	return node;
}

struct node *add_io_node(struct runtime *env)
{
	struct node *node = add_node(env);
	struct io_node *io = ecalloc(1, sizeof(*io));

	node->class = &io_node_class;
	node->dat = io;

	return node;
}

static void push(struct proc_node *proc, uint8_t byte)
{
	if (proc->stack_top == proc->stack_cap) {
		// Expand the stack when necessary.
		proc->stack_cap *= 2;
		proc->stack = erealloc(proc->stack,
			proc->stack_cap * sizeof(*proc->stack));
	}

	proc->stack[proc->stack_top++] = byte;
}

static uint8_t peek(struct proc_node *node)
{
	assert(node->stack_top > 0); // Stack mustn't be empty.
	return node->stack[node->stack_top-1];
}

static uint8_t pop(struct proc_node *proc)
{
	assert(proc->stack_top > 0); // Stack mustn't be empty.
	return proc->stack[--proc->stack_top];
}

static bool tick_proc_node(void *this)
{
	struct proc_node *proc = this;

	// The length of the instruction, or how many bytes to advance by.
	uint16_t advance = 1;
	uint8_t instr = proc->code[proc->isp];
	uint8_t val1, val2, var;
	struct wire *wire;

	switch (instr) {
	case OP_PUSH:
		advance = 2;
		val1 = proc->code[proc->isp+1];
		push(proc, val1);
		break;
	case OP_POP:
		pop(proc);
		break;
	case OP_DUP:
		push(proc, peek(proc));
		break;
	case OP_NEGATE:
		push(proc, ~pop(proc));
		break;
	case OP_MUL:
		push(proc, pop(proc)*pop(proc));
		break;
	case OP_DIV:
		val2 = pop(proc); val1 = pop(proc);
		push(proc, val1/val2);
		break;
	case OP_MOD:
		val2 = pop(proc); val1 = pop(proc);
		push(proc, val1%val2);
		break;
	case OP_ADD:
		push(proc, pop(proc)+pop(proc));
		break;
	case OP_SUB:
		val2 = pop(proc); val1 = pop(proc);
		push(proc, val1-val2);
		break;
	case OP_SHL:
		val2 = pop(proc); val1 = pop(proc);
		push(proc, val1<<val2);
		break;
	case OP_SHR:
		val2 = pop(proc); val1 = pop(proc);
		push(proc, val1>>val2);
		break;
	case OP_AND:
		push(proc, pop(proc)&pop(proc));
		break;
	case OP_XOR:
		push(proc, pop(proc)^pop(proc));
		break;
	case OP_OR:
		push(proc, pop(proc)|pop(proc));
		break;
	case OP_LSS:
		val2 = pop(proc); val1 = pop(proc);
		push(proc, val1 < val2 ? 1 : 0);
		break;
	case OP_LTE:
		val2 = pop(proc); val1 = pop(proc);
		push(proc, val1 <= val2 ? 1 : 0);
		break;
	case OP_GTR:
		val2 = pop(proc); val1 = pop(proc);
		push(proc, val1 > val2 ? 1 : 0);
		break;
	case OP_GTE:
		val2 = pop(proc); val1 = pop(proc);
		push(proc, val1 >= val2 ? 1 : 0);
		break;
	case OP_EQL:
		push(proc, pop(proc) == pop(proc) ? 1 : 0);
		break;
	case OP_NEQ:
		push(proc, pop(proc) != pop(proc) ? 1 : 0);
		break;
	case OP_LAND:
		val2 = pop(proc); val1 = pop(proc);
		push(proc, val1 ? val2 : 0);
		break;
	case OP_LOR:
		val2 = pop(proc); val1 = pop(proc);
		push(proc, val1 ? val1 : val2);
		break;
	case OP_LNOT:
		push(proc, !pop(proc));
		break;
	case OP_JMP:
		advance = 0;
		proc->isp = *((uint16_t *) &proc->code[proc->isp+1]);
		break;
	case OP_TJMP:
		advance = 3;

		if (pop(proc)) {
			proc->isp = *((uint16_t *) &proc->code[proc->isp+1]);
			advance = 0;
		}
		break;
	case OP_FJMP:
		advance = 3;

		if (!pop(proc)) {
			proc->isp = *((uint16_t *) &proc->code[proc->isp+1]);
			advance = 0;
		}
		break;
	case OP_SAVE0:
	case OP_SAVE1:
	case OP_SAVE2:
	case OP_SAVE3:
		var = instr - OP_SAVE0;
		proc->mem[var] = peek(proc);
		break;
	case OP_LOAD0:
	case OP_LOAD1:
	case OP_LOAD2:
	case OP_LOAD3:
		var = instr - OP_LOAD0;
		push(proc, proc->mem[var]);
		break;
	case OP_INC0:
	case OP_INC1:
	case OP_INC2:
	case OP_INC3:
		var = instr - OP_INC0;
		push(proc, ++proc->mem[var]);
		break;
	case OP_DEC0:
	case OP_DEC1:
	case OP_DEC2:
	case OP_DEC3:
		var = instr - OP_DEC0;
		push(proc, --proc->mem[var]);
		break;
	case OP_SEND0:
	case OP_SEND1:
	case OP_SEND2:
	case OP_SEND3:
		wire = proc->wires[instr - OP_SEND0];
		val1 = pop(proc);
		switch (send(wire, val1)) {
		case BUFFERED:
			push(proc, val1);
			return true;
		case BLOCKED:
			push(proc, val1);
			return false;
		case PROCESSED:
			break;
		}

		break;
	case OP_RECV0:
	case OP_RECV1:
	case OP_RECV2:
	case OP_RECV3:
		wire = proc->wires[instr - OP_RECV0];
		if (recv(wire, &val1) == BLOCKED)
			return false;

		push(proc, val1);
		break;
	case OP_HALT:
		return false;
	default:
		errx(1, "Unknown opcode %d.", instr);
	}

	proc->isp += advance;
	if (proc->isp == proc->code_size) {
		// Loop back around forever.
		proc->isp = 0;
	}

	assert(proc->isp <= proc->code_size);
	return true;
}

static bool tick_io_node(void *this)
{
	struct io_node *io = this;
	bool has_progressed = false;
	uint8_t val;

	if (io->in_wire && !io->has_buf & !io->eof_reached) {
		int chr = getchar();
		if (chr == EOF) {
			io->eof_reached = true;
		} else {
			io->buf = (uint8_t) chr;
			io->has_buf = true;
		}
	}

	if (io->has_buf) {
		switch (send(io->in_wire, io->buf)) {
		case BUFFERED:
			has_progressed = true;
			break;
		case BLOCKED:
			break;
		case PROCESSED:
			has_progressed = true;
			io->has_buf = false;
			break;
		}
	}

	if (recv(io->out_wire, &val) == PROCESSED) {
		has_progressed = true;
		putchar(val);
	}

	return has_progressed;
}

static bool run_node(struct node *node)
{
	bool (*tick)(void *) = node->class->tick;
	void *dat = node->dat;

	// If the first tick made no progress, report accordingly.
	if (!tick(dat)) return false;

	// Keep running until the node stops.
	while (tick(dat));
	return true;
}


static void free_proc_node(void *this)
{
	struct proc_node *proc = this;

	free(proc->stack);
	free(proc);
}

static void free_io_node(void *this)
{
	struct io_node *io = this;

	free(io);
}

static void add_wire_to_proc_node(void *this, int porti, struct wire *wire)
{
	struct proc_node *proc = this;

	assert(porti >= 0 && porti < PROC_PORTS);
	proc->wires[porti] = wire;
}

static void add_wire_to_io_node(void *this, int porti, struct wire *wire)
{
	struct io_node *io = this;

	switch (porti) {
	case IO_IN:
		io->in_wire = wire;
		break;
	case IO_OUT:
		io->out_wire = wire;
		break;
	default:
		assert(false);
	}
}

void add_wire(struct runtime *env, struct node *src, int src_porti,
	struct node *dest, int dest_porti)
{
	struct wire *wire;

	if (env->nwires == env->wire_cap) {
		// Expand or allocate when necessary
		if (env->wire_cap == 0) {
			env->wire_cap = 8;
		} else {
			env->wire_cap *= 2;
		}

		env->wires = erealloc(env->wires,
			env->wire_cap * sizeof(*env->wires));
	}

	wire = &env->wires[env->nwires++];
	memset(wire, 0, sizeof(*wire));

	src->class->add_wire(src->dat, src_porti, wire);
	dest->class->add_wire(dest->dat, dest_porti, wire);
}

void run(struct runtime *env)
{
	bool has_progressed; // Whether a single tick made any progression.

	do {
		has_progressed = false;

		for (size_t i = 0; i < env->nnodes; i++) {
			struct node *node = &env->nodes[i];
			has_progressed |= run_node(node);
		}
	} while (has_progressed);
}

void clear_runtime(struct runtime *env)
{
	for (size_t i = 0; i < env->nnodes; i++) {
		struct node *node = &env->nodes[i];
		node->class->free(node->dat);
	}
	free(env->nodes);
	free(env->wires);

	memset(env, 0, sizeof(*env));
}
