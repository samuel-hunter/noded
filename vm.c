/*
 * vm - virtual machine execution
 */
#include <err.h>
#include <stdlib.h>
#include <string.h>

#include "noded.h"

enum
{
	STACK_SIZE = 512,
};

/* Holds all metadata for sending and receiving data */
typedef struct Port Port;
struct Port {
	Node *recp; /* owned by VM */
	int recp_port;

	Wire *wire; /* owned by VM */
};

/* Processor nodes execute code and send/receive messages
 * between other nodes. */
typedef struct ProcNode ProcNode;
struct ProcNode {
	const uint8_t *code;
	const uint8_t *isp; /* isp = &code[i] */
	const uint8_t *code_end; /* code_end = &code[size] */

	Port ports[PORT_MAX];
	uint8_t vars[VAR_MAX];

	/* TODO dynamically allocate stack */
	uint8_t stack[STACK_SIZE];
	uint8_t *sp; /* sp = &stack[i] */
};

/* Buffer nodes store and recall data for processor nodes to use. */
typedef struct BufNode BufNode;
struct BufNode {
	uint8_t idx;
	uint8_t data[BUFFER_NODE_MAX];
};

/* Stack nodes push and pop data. Similar to buffer nodes, except
 * they have a dynamically allocated amount of space to store, rather
 * than a fixed space constrained by the maximum value of the byte. */
typedef struct StackNode StackNode;
struct StackNode {
	uint8_t *stack;
	size_t len;
	size_t cap;
};

/* The port rule table holds the logic between how processor nodes
 * interact with nodes of various types. */

typedef bool (*Sendlet)(Wire *wire, void *recp, int port, uint8_t dat);
typedef bool (*Recvlet)(Wire *wire, void *recp, int port, uint8_t *dest);

typedef struct PortRule PortRule;
struct PortRule {
	Sendlet send;
	Recvlet recv;
};

static bool send_proc(Wire *wire, void *recp, int port, uint8_t dat);
static bool recv_proc(Wire *wire, void *recp, int port, uint8_t *dest);
static bool send_io(Wire *wire, void *recp, int port, uint8_t dat);
static bool recv_io(Wire *wire, void *recp, int port, uint8_t *dest);
static bool send_buf(Wire *wire, void *recp, int port, uint8_t dat);
static bool recv_buf(Wire *wire, void *recp, int port, uint8_t *dest);
static bool send_stack(Wire *wire, void *recp, int port, uint8_t dat);
static bool recv_stack(Wire *wire, void *recp, int port, uint8_t *dest);

static PortRule port_table[] = {
	[PROC_NODE]   = {&send_proc,  &recv_proc},
	[IO_NODE]     = {&send_io,    &recv_io},
	[BUFFER_NODE] = {&send_buf,   &recv_buf},
	[STACK_NODE]  = {&send_stack, &recv_stack},
};

void
vm_init(VM *vm, size_t nnodes, size_t nwires)
{
	memset(vm, 0, sizeof(*vm));

	vm->nodes = ecalloc(nnodes, sizeof(*vm->nodes));
	vm->nnodes = nnodes;

	vm->wires = ecalloc(nwires, sizeof(*vm->wires));
	vm->nwires = nwires;
}

static Node *
add_node(VM *vm, NodeType type)
{
	Node *node = &vm->nodes[vm->nodes_added++];

	if (vm->nodes_added > vm->nnodes)
		errx(1, "add_node(): too many nodes added");

	node->type = type;
	return node;
}

void
add_io_node(VM *vm)
{
	add_node(vm, IO_NODE);
}

void
add_proc_node(VM *vm, const uint8_t *code, uint16_t code_size)
{
	Node *node = add_node(vm, PROC_NODE);
	ProcNode *proc = ecalloc(1, sizeof(*proc));
	node->dat = proc;

	proc->code = proc->isp = code;
	proc->code_end = &code[code_size];

	/* initialize stack pointer */
	proc->sp = proc->stack;
}

void
copy_proc_node(VM *vm, size_t source_node)
{
	ProcNode *source = vm->nodes[source_node].dat;
	size_t size = (size_t)(source->code_end - source->code);
	add_proc_node(vm, source->code, size);
}

void
add_buf_node(VM *vm, const uint8_t data[])
{
	Node *node = add_node(vm, BUFFER_NODE);
	BufNode *buf = ecalloc(1, sizeof(*buf));
	node->dat = buf;

	memcpy(buf->data, data, sizeof(buf->data));
}

void
add_stack_node(VM *vm)
{
	Node *node = add_node(vm, STACK_NODE);
	StackNode *stack = ecalloc(1, sizeof(*stack));
	node->dat = stack;
}

static void
add_wire_to_port(Port *port, Wire *wire, Node *recp, int recp_port)
{
	port->recp = recp;
	port->recp_port = recp_port;
	port->wire = wire;
}

void
add_wire(VM *vm, size_t node1, int port1, size_t node2, int port2)
{
	Wire *wire = &vm->wires[vm->wires_added++];
	Node *n1, *n2;
	if (vm->wires_added > vm->nwires)
		errx(1, "add_wire(): too many wires added");

	n1 = &vm->nodes[node1];
	n2 = &vm->nodes[node2];

	if (n1->type == PROC_NODE) {
		ProcNode *proc = n1->dat;
		add_wire_to_port(&proc->ports[port1], wire, n2, port2);
	}

	if (n2->type == PROC_NODE) {
		ProcNode *proc = n2->dat;
		add_wire_to_port(&proc->ports[port2], wire, n1, port1);
	}

	if (n1->type != PROC_NODE && n2->type != PROC_NODE) {
		/* TODO push error-checking to noded.c, where they can
		 * use token positions to point people to the problem. */
		errx(1, "add_wire(): neither nodes are processors.");
	}
}

static bool
send_proc(Wire *wire, void *recp, int port, uint8_t dat)
{
	(void)recp;
	(void)port;

	switch (wire->status) {
	case EMPTY:
		wire->status = FULL;
		wire->buf = dat;
		return false;
	case FULL:
		return false;
	case CONSUMED:
		wire->status = EMPTY;
		return true;
	default:
		errx(1, "send_proc(): invalid status");
	}
}

static bool
recv_proc(Wire *wire, void *recp, int port, uint8_t *dest)
{
	(void)recp;
	(void)port;

	switch (wire->status) {
	case EMPTY:
	case CONSUMED:
		return false;
	case FULL:
		*dest = wire->buf;
		wire->status = CONSUMED;
		return true;
	default:
		errx(1, "recv_proc(): invalid status");
	}
}

static bool
send_io(Wire *wire, void *recp, int port, uint8_t dat)
{
	(void)wire;
	(void)recp;

	switch (port) {
	case IO_OUT:
		putc(dat, stdout);
		break;
	case IO_ERR:
		putc(dat, stderr);
		break;
	default:
		errx(1, "send_io(): invalid port %d.", port);
		break;
	}
	return true;
}

static bool
recv_io(Wire *wire, void *recp, int port, uint8_t *dest)
{
	(void)wire;
	(void)recp;
	int chr;

	if (port != IO_IN)
		errx(1, "recv_io(): invalid port %d.", port);

	chr = getchar();
	if (chr == EOF) {
		return false;
	} else {
		*dest = (uint8_t) chr;
		return true;
	}
}

static bool send_buf(Wire *wire, void *recp, int port, uint8_t dat)
{
	(void)wire;
	BufNode *buf = recp;

	switch (port) {
	case BUFFER_IDX:
		buf->idx = dat;
		break;
	case BUFFER_ELM:
		buf->data[buf->idx] = dat;
		break;
	default:
		errx(1, "send_buf(): invalid port %d.", port);
	}

	return true;
}

static bool recv_buf(Wire *wire, void *recp, int port, uint8_t *dest)
{
	(void)wire;
	BufNode *buf = recp;

	switch (port) {
	case BUFFER_IDX:
		*dest = buf->idx;
		break;
	case BUFFER_ELM:
		*dest = buf->data[buf->idx];
		break;
	default:
		errx(1, "send_buf(): invalid port %d.", port);
	}

	return true;
}

static bool send_stack(Wire *wire, void *recp, int port, uint8_t dat)
{
	(void)wire;
	(void)port;
	StackNode *stack = recp;

	if (stack->cap == stack->len) {
		stack->cap = stack->cap ? stack->cap*2 : 8;
		stack->stack = erealloc(stack->stack, stack->cap);
	}

	stack->stack[stack->len++] = dat;
	return true;
}

static bool recv_stack(Wire *wire, void *recp, int port, uint8_t *dest)
{
	(void)wire;
	(void)port;
	StackNode *stack = recp;

	if (stack->len > 0) {
		*dest = stack->stack[--stack->len];
		return true;
	} else {
		return false;
	}
}


static bool
send(Port *port, uint8_t dat)
{
	Sendlet snd = port_table[port->recp->type].send;
	return snd(port->wire, port->recp->dat, port->recp_port, dat);
}

static bool
recv(Port *port, uint8_t *dest)
{
	Recvlet rcv = port_table[port->recp->type].recv;
	return rcv(port->wire, port->recp->dat, port->recp_port, dest);
}

static void
push(ProcNode *proc, uint8_t dat)
{
	if (proc->sp == &proc->stack[STACK_SIZE])
		errx(1, "push(): stack overflow");

	*proc->sp++ = dat;
}

static uint8_t
pop(ProcNode *proc)
{
	if (proc->sp == proc->stack)
		errx(1, "pop(): stack underflow");

	return *(--proc->sp);
}

static uint8_t
peekproc(ProcNode *proc)
{
	if (proc->sp == proc->stack)
		errx(1, "peek(): stack underflow");

	return *(proc->sp - 1);
}

static bool tick(ProcNode *proc)
{
	int advance = 1;
	Opcode op = proc->isp[0];
	uint8_t arg1, arg2;
	uint16_t addr;

	switch (op) {
	case OP_NOOP:
		break;
	case OP_PUSH:
		advance = 2;

		push(proc, proc->isp[1]);
		break;
	case OP_DUP:
		push(proc, peekproc(proc));
		break;
	case OP_POP:
		pop(proc);
		break;
	case OP_NEG:
		push(proc, -pop(proc));
		break;
	case OP_LNOT:
		push(proc, pop(proc) ? 0 : 0xFF);
		break;
	case OP_NOT:
		push(proc, ~pop(proc));
		break;
	case OP_LOR:
		arg2 = pop(proc);
		arg1 = pop(proc);
		push(proc, arg1 ? arg1 : arg2);
		break;
	case OP_LAND:
		arg2 = pop(proc);
		arg1 = pop(proc);
		push(proc, arg1 ? arg2 : 0);
		break;
	case OP_OR:
		push(proc, pop(proc) | pop(proc));
		break;
	case OP_XOR:
		push(proc, pop(proc) ^ pop(proc));
		break;
	case OP_AND:
		push(proc, pop(proc) & pop(proc));
		break;
	case OP_EQL:
		push(proc, pop(proc) == pop(proc) ? 0xFF : 0);
		break;
	case OP_LSS:
		arg2 = pop(proc);
		arg1 = pop(proc);
		push(proc, arg1 < arg2 ? 0xFF : 0);
		break;
	case OP_LTE:
		arg2 = pop(proc);
		arg1 = pop(proc);
		push(proc, arg1 <= arg2 ? 0xFF : 0);
		break;
	case OP_SHL:
		arg2 = pop(proc);
		arg1 = pop(proc);
		push(proc, arg1 << arg2);
		break;
	case OP_SHR:
		arg2 = pop(proc);
		arg1 = pop(proc);
		push(proc, arg1 >> arg2);
		break;
	case OP_ADD:
		push(proc, pop(proc) + pop(proc));
		break;
	case OP_SUB:
		arg2 = pop(proc);
		arg1 = pop(proc);
		push(proc, arg1 - arg2);
		break;
	case OP_MUL:
		push(proc, pop(proc) * pop(proc));
		break;
	case OP_DIV:
		arg2 = pop(proc);
		arg1 = pop(proc);
		push(proc, arg1 / arg2);
		break;
	case OP_MOD:
		arg2 = pop(proc);
		arg1 = pop(proc);
		push(proc, arg1 % arg2);
		break;
	case OP_JMP:
		advance = 0;
		addr = proc->isp[1] + (proc->isp[2]<<8);
		proc->isp = &proc->code[addr];
		break;
	case OP_FJMP:
		advance = 3;
		addr = proc->isp[1] + (proc->isp[2]<<8);

		if (!pop(proc)) {
			advance = 0;
			proc->isp = &proc->code[addr];
		}
		break;
	case OP_LOAD0:
	case OP_LOAD1:
	case OP_LOAD2:
	case OP_LOAD3:
		push(proc, proc->vars[op - OP_LOAD0]);
		break;
	case OP_SAVE0:
	case OP_SAVE1:
	case OP_SAVE2:
	case OP_SAVE3:
		proc->vars[op - OP_SAVE0] = pop(proc);
		break;
	case OP_SEND0:
	case OP_SEND1:
	case OP_SEND2:
	case OP_SEND3:
		if (send(&proc->ports[op - OP_SEND0], peekproc(proc))) {
			pop(proc);
		} else {
			return false;
		}
		break;
	case OP_RECV0:
	case OP_RECV1:
	case OP_RECV2:
	case OP_RECV3:
		if (recv(&proc->ports[op - OP_RECV0], &arg1)) {
			push(proc, arg1);
		} else {
			return false;
		}
		break;
	case OP_HALT:
		return false;
	default:
		errx(1, "Invalid operand %d.", op);
	}

	/* set isp to next instruction and wrap to beginning if necessary */
	proc->isp += advance;
	if (proc->isp == proc->code_end)
		proc->isp = proc->code;
	return true;
}

static bool run_proc(ProcNode *node)
{
	if (!tick(node)) return false;
	while (tick(node));
	return true;
}

void run(VM *vm)
{
	bool progressed ;
	do {
		progressed = false;
		for (size_t i = 0; i < vm->nnodes; i++) {
			if (vm->nodes[i].type == PROC_NODE)
				progressed |= run_proc(vm->nodes[i].dat);
		}
	} while (progressed);
}
