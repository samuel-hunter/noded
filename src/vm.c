#include <err.h>
#include <string.h>

#include "noded.h"

#define INITIAL_STACK_SIZE 8

struct proc_node *new_proc_node(const uint8_t code[], size_t code_size,
	send_handler send, recv_handler recv)
{
	if (code_size > (size_t)UINT16_MAX + 1)
		errx(1, "Bytecode size too large");

	struct proc_node *result = emalloc(sizeof(*result));
	memset(result, 0, sizeof(*result));
	result->code = code;
	result->code_size = code_size;

	result->send = send;
	result->recv = recv;

	// Eagerly create a new stack buffer.
	result->stack_cap = INITIAL_STACK_SIZE;
	result->stack = emalloc(result->stack_cap * sizeof(*result->stack));

	return result;
}

static void expand_stack(struct proc_node *node)
{
	node->stack_cap *= 2;
	node->stack = erealloc(node->stack,
	                       node->stack_cap * sizeof(*node->stack));
}

static void push(struct proc_node *node, uint8_t byte)
{
	if (node->stack_top == node->stack_cap)
		expand_stack(node);

	node->stack[node->stack_top++] = byte;
}

static uint8_t peek(struct proc_node *node)
{
	// TODO: Should I add protection for peeking an empty stack?
	return node->stack[node->stack_top-1];
}

static uint8_t pop(struct proc_node *node)
{
	// TODO: Should I add protection for popping off an empty stack?
	return node->stack[--node->stack_top];
}

static uint16_t addr_value(const uint8_t *src)
{
	return ((uint16_t)(src[0])<<8) +
	       (uint16_t)(src[1]);
}

void run(struct proc_node *node, void *handler_dat)
{
	while (true) {
		// The length of the instruction, or how many bytes to advance by.
		uint16_t advance = 1;
		uint8_t instr = node->code[node->isp];
		uint8_t val1, val2;

		switch (instr) {
		case OP_PUSH:
			advance = 2;
			val1 = node->code[node->isp+1];
			push(node, val1);
			break;
		case OP_POP:
			pop(node);
			break;
		case OP_DUP:
			push(node, peek(node));
			break;
		case OP_NEGATE:
			push(node, ~pop(node));
			break;
		case OP_MUL:
			push(node, pop(node)*pop(node));
			break;
		case OP_DIV:
			val2 = pop(node); val1 = pop(node);
			push(node, val1/val2);
			break;
		case OP_MOD:
			val2 = pop(node); val1 = pop(node);
			push(node, val1%val2);
			break;
		case OP_ADD:
			push(node, pop(node)+pop(node));
			break;
		case OP_SUB:
			val2 = pop(node); val1 = pop(node);
			push(node, val1-val2);
			break;
		case OP_SHL:
			val2 = pop(node); val1 = pop(node);
			push(node, val1<<val2);
			break;
		case OP_SHR:
			val2 = pop(node); val1 = pop(node);
			push(node, val1>>val2);
			break;
		case OP_AND:
			push(node, pop(node)&pop(node));
			break;
		case OP_XOR:
			push(node, pop(node)^pop(node));
			break;
		case OP_OR:
			push(node, pop(node)|pop(node));
			break;
		case OP_LSS:
			val2 = pop(node); val1 = pop(node);
			push(node, val1 < val2 ? 1 : 0);
			break;
		case OP_LTE:
			val2 = pop(node); val1 = pop(node);
			push(node, val1 <= val2 ? 1 : 0);
			break;
		case OP_GTR:
			val2 = pop(node); val1 = pop(node);
			push(node, val1 > val2 ? 1 : 0);
			break;
		case OP_GTE:
			val2 = pop(node); val1 = pop(node);
			push(node, val1 >= val2 ? 1 : 0);
			break;
		case OP_EQL:
			push(node, pop(node) == pop(node) ? 1 : 0);
			break;
		case OP_NEQ:
			push(node, pop(node) != pop(node) ? 1 : 0);
			break;
		case OP_LAND:
			val2 = pop(node); val1 = pop(node);
			push(node, val1 ? val2 : 0);
			break;
		case OP_LOR:
			val2 = pop(node); val1 = pop(node);
			push(node, val1 ? val1 : val2);
			break;
		case OP_LNOT:
			push(node, !pop(node));
			break;
		case OP_JMP:
			node->isp = addr_value(&node->code[node->isp+1]);
			advance = 0;
			break;
		case OP_TJMP:
			advance = 3;

			if (pop(node)) {
				node->isp = addr_value(&node->code[node->isp+1]);
				advance = 0;
			}
			break;
		case OP_FJMP:
			advance = 3;

			if (!pop(node)) {
				node->isp = addr_value(&node->code[node->isp+1]);
				advance = 0;
			}
			break;
		case OP_SAVE0:
		case OP_SAVE1:
		case OP_SAVE2:
		case OP_SAVE3:
			node->mem[instr - OP_SAVE0] = peek(node);
			break;
		case OP_LOAD0:
		case OP_LOAD1:
		case OP_LOAD2:
		case OP_LOAD3:
			push(node, node->mem[instr - OP_LOAD0]);
			break;
		case OP_INC0:
		case OP_INC1:
		case OP_INC2:
		case OP_INC3:
			push(node, ++node->mem[instr - OP_INC0]);
			break;
		case OP_DEC0:
		case OP_DEC1:
		case OP_DEC2:
		case OP_DEC3:
			push(node, --node->mem[instr - OP_DEC0]);
			break;
		case OP_SEND0:
		case OP_SEND1:
		case OP_SEND2:
		case OP_SEND3:
			node->send(pop(node), instr - OP_SEND0, handler_dat);
			break;
		case OP_RECV0:
		case OP_RECV1:
		case OP_RECV2:
		case OP_RECV3:
			push(node, node->recv(instr - OP_RECV0, handler_dat));
			break;
		case OP_HALT:
			return;
		default:
			errx(1, "Unknown opcode %d.", instr);
		}

		node->isp += advance;
		if (node->isp == node->code_size) {
			// Loop back around forever.
			node->isp = 0;
		}
	}
}
