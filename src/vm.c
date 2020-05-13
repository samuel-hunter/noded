/*
 * vm - virtual machine execution
 *
 * vm includes the heartbeat of the program, and runs the executed
 * bytecode.
 */
#include <assert.h>
#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "noded.h"
#include "vm.h"


static bool tick_proc_node(void *this);
static void free_proc_node(void *this);
static void add_wire_to_proc_node(void *this, int porti, struct wire *wire);

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

static const struct node_class proc_node_class = {
	.tick = &tick_proc_node,
	.free = &free_proc_node,
	.add_wire = &add_wire_to_proc_node
};


static bool tick_io_node(void *this);
static void free_io_node(void *this);
static void add_wire_to_io_node(void *this, int porti, struct wire *wire);

struct io_node {
	bool reached_eof;
	bool self_wired;
};

static const struct node_class io_node_class = {
	.tick = &tick_io_node,
	.free = &free_io_node,
	.add_wire = &add_wire_to_io_node
};


static void free_buf_node(void *this);
static void add_wire_to_buf_node(void *this, int porti, struct wire *wire);

struct buf_node {
	uint8_t idx;
	uint8_t elm[BUFFER_NODE_MAX];
};

static const struct node_class buf_node_class = {
	.tick = NULL,
	.free = &free_buf_node,
	.add_wire = &add_wire_to_buf_node
};

static enum wire_status send_to_proc_wire(void *this, uint8_t dat);
static enum wire_status recv_from_proc_wire(void *this, uint8_t *dest);

struct proc_wire {
	enum { EMPTY, FULL, CONSUMED } status;
	uint8_t buf;
};

static const struct wire_class proc_wire_class = {
	.send = &send_to_proc_wire,
	.recv = &recv_from_proc_wire
};

static enum wire_status send_to_out_wire(void *this, uint8_t dat);

struct out_wire {
	FILE *f;
};

static const struct wire_class out_wire_class = {
	.send = &send_to_out_wire,
	.recv = NULL // TODO: Add some error here saying it's invalid?
		     // It's probably wiser to signal an error at
		     // compilation time anyways. Let it segfault for
		     // now, it's easy enough to trace with a
		     // debugger.
};

static enum wire_status recv_from_in_wire(void *this, uint8_t *dest);

struct in_wire {
	bool reached_eof;
};

static const struct wire_class in_wire_class = {
	.send = NULL, // TODO: see above with out_wire_class
	.recv = &recv_from_in_wire
};

static enum wire_status send_to_buf_wire(void *this, uint8_t dat);
static enum wire_status recv_from_buf_wire(void *this, uint8_t *dest);

struct buf_wire {
	bool is_elm; // true = change elm[idx]; false = change idx
	struct buf_node *node;
};

static const struct wire_class buf_wire_class = {
	.send = &send_to_buf_wire,
	.recv = &recv_from_buf_wire
};


// Send a message through the wire.
static enum wire_status send_to_proc_wire(void *this, uint8_t dat)
{
	struct proc_wire *wire = this;

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
	errx(1, "Internal error: invalid wire status");
}

// Request a message from the wire.
static enum wire_status recv_from_proc_wire(void *this, uint8_t *dest)
{
	struct proc_wire *wire = this;
	if (!wire) return BLOCKED;

	switch (wire->status) {
	case FULL:
		*dest = wire->buf;
		wire->status = CONSUMED;
		return PROCESSED;
	case EMPTY:
	case CONSUMED:
		return BLOCKED;
	}

	// Should never be here unless all hell is loose.
	errx(1, "Internal error: invalid wire status");
}

static enum wire_status send_to_out_wire(void *this, uint8_t dat)
{
	struct out_wire *wire = this;
	putc(dat, wire->f);
	return PROCESSED;
}

static enum wire_status recv_from_in_wire(void *this, uint8_t *dest)
{
	struct in_wire *wire = this;
	int c;

	if (wire->reached_eof) return BLOCKED;
	c = getchar();

	if (c == EOF) {
		wire->reached_eof = true;
		return BLOCKED;
	}

	*dest = (uint8_t)c;
	return PROCESSED;
}

static enum wire_status send_to_buf_wire(void *this, uint8_t dat)
{
	struct buf_wire *wire = this;
	struct buf_node *node = wire->node;

	if (wire->is_elm) {
		node->elm[node->idx] = dat;
	} else {
		node->idx = dat;
	}

	return PROCESSED;
}

static enum wire_status recv_from_buf_wire(void *this, uint8_t *dest)
{
	struct buf_wire *wire = this;
	struct buf_node *node = wire->node;

	if (wire->is_elm) {
		*dest = node->elm[node->idx];
	} else {
		*dest = node->idx;
	}

	return PROCESSED;
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

	node->class = &io_node_class;
	node->dat = ecalloc(1, sizeof(struct io_node));

	return node;
}

struct node *add_buf_node(struct runtime *env, const uint8_t data[])
{
	struct node *node = add_node(env);
	struct buf_node *buf = ecalloc(1, sizeof(*buf));

	node->class = &buf_node_class;
	node->dat = buf;

	memcpy(buf->elm, data, sizeof(buf->elm));

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
		switch (wire->class->send(wire->dat, val1)) {
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
		switch (wire->class->recv(wire->dat, &val1)) {
		case BUFFERED:
			return true;
		case BLOCKED:
			return false;
		case PROCESSED:
			break;
		}

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
	int c;

	if (!io->self_wired || io->reached_eof)
		return false;

	c = getchar();
	if (c == EOF) {
		io->reached_eof = true;
		return false;
	}

	putchar(c);
	return true;
}

static bool run_node(struct node *node)
{
	bool (*tick)(void *) = node->class->tick;
	void *dat = node->dat;

	if (!tick) return false;

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

static void free_buf_node(void *this)
{
	struct buf_node *buf = this;

	free(buf);
}

static void add_wire_to_proc_node(void *this, int porti, struct wire *wire)
{
	struct proc_node *proc = this;

	assert(porti >= 0 && porti < PROC_PORTS);
	proc->wires[porti] = wire;
}

static void add_wire_to_io_node(void *this, int porti, struct wire *wire)
{
	(void)this;

	switch (porti) {
	case IO_IN:
		break;
	case IO_OUT:
		((struct out_wire*) wire->dat)->f = stdout;
		break;
	case IO_ERR:
		((struct out_wire*) wire->dat)->f = stderr;
		break;
	default:
		assert(false);
	}
}

static void add_wire_to_buf_node(void *this, int porti, struct wire *wire)
{
	struct buf_node *buf = this;
	struct buf_wire *buf_wire = wire->dat;

	switch (porti) {
	case BUF_IDX:
		buf_wire->is_elm = false;
		break;
	case BUF_ELM:
		buf_wire->is_elm = true;
		break;
	default:
		assert(false);
	}

	buf_wire->node = buf;
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

	// TODO: This code is extremely messy! Find some way to refactor it!.
	if (dest->class == &io_node_class) {
		wire->class = &out_wire_class;
		wire->dat = ecalloc(1, sizeof(struct out_wire*));

		if (src->class == &io_node_class) {
			((struct io_node*)dest->dat)->self_wired = true;
		}
	} else if (src->class == &io_node_class) {
		wire->class = &in_wire_class;
		wire->dat = ecalloc(1, sizeof(struct in_wire*));
	} else if (dest->class == &buf_node_class || src->class == &buf_node_class) {
		wire->class = &buf_wire_class;
		wire->dat = ecalloc(1, sizeof(struct buf_wire*));
	} else {
		wire->class = &proc_wire_class;
		wire->dat = ecalloc(1, sizeof(struct proc_wire*));
	}

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
		if (node->class->free)
			node->class->free(node->dat);
	}
	free(env->nodes);
	free(env->wires);

	memset(env, 0, sizeof(*env));
}
