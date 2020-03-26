/*
 * disasm - disassemble bytecode from stdin.
 *
 * disasm reads the VM's bytecode directly from standard input and
 * prints a human-readable format. I'm mainly using this to spot-check
 * how the compiler is handling parsed code.
 *
 * see also tools/compile.c
 */
#include <stdio.h>
#include <err.h>

#include "noded.h"

const char *code_names[] = {
	[OP_INVALID] = "INVALID",
	[OP_NOOP] = "NOOP",

	[OP_PUSH] = "PUSH",
	[OP_POP] = "POP",
	[OP_DUP] = "DUP",

	[OP_NEGATE] = "NEGATE",

	[OP_MUL] = "MUL",
	[OP_DIV] = "DIV",
	[OP_MOD] = "MOD",
	[OP_ADD] = "ADD",
	[OP_SUB] = "SUB",

	[OP_SHL] = "SHL",
	[OP_SHR] = "SHR",

	[OP_AND] = "AND",
	[OP_XOR] = "XOR",
	[OP_OR] = "OR",

	[OP_LSS] = "LSS",
	[OP_LTE] = "LTE",
	[OP_GTR] = "GTR",
	[OP_GTE] = "GTE",

	[OP_EQL] = "EQL",
	[OP_NEQ] = "NEQ",

	[OP_LAND] = "LAND",
	[OP_LOR] = "LOR",
	[OP_LNOT] = "LNOT",

	[OP_JMP] = "JMP",
	[OP_TJMP] = "TJMP",
	[OP_FJMP] = "FJMP",

	[OP_SAVE0] = "SAVE0",
	[OP_SAVE1] = "SAVE1",
	[OP_SAVE2] = "SAVE2",
	[OP_SAVE3] = "SAVE3",

	[OP_LOAD0] = "LOAD0",
	[OP_LOAD1] = "LOAD1",
	[OP_LOAD2] = "LOAD2",
	[OP_LOAD3] = "LOAD3",

	[OP_INC0] = "INC0",
	[OP_INC1] = "INC1",
	[OP_INC2] = "INC2",
	[OP_INC3] = "INC3",

	[OP_DEC0] = "DEC0",
	[OP_DEC1] = "DEC1",
	[OP_DEC2] = "DEC2",
	[OP_DEC3] = "DEC3",

	[OP_SEND0] = "SEND0",
	[OP_SEND1] = "SEND1",
	[OP_SEND2] = "SEND2",
	[OP_SEND3] = "SEND3",

	[OP_RECV0] = "RECV0",
	[OP_RECV1] = "RECV1",
	[OP_RECV2] = "RECV2",
	[OP_RECV3] = "RECV3",

	[OP_HALT] = "HALT"
};

static const char *code_str(uint8_t code)
{
	return code_names[code];
}

// TODO: This is ripped straight from vm.c. Should put this somewhere
// shared instead.
static uint16_t addr_value(const uint8_t *src)
{
	return ((uint16_t)(src[0])<<8) +
	       (uint16_t)(src[1]);
}

int main(int argc, char **argv)
{
	// this program doesn't need any arguments.
	if (argc != 1) {
		fprintf(stderr, "Usage: %s < INPUT\n", argv[0]);
		return 1;
	}

	// TODO have the program accept bytecode of any size up to UINT16_MAX.
#define BUF_SIZE 4096
	uint8_t code[BUF_SIZE];
	uint16_t codesize = fread(code, sizeof(*code), BUF_SIZE, stdin);
	if (!feof(stdin)) {
		errx(1, "Input too large.");
	}
#undef BUF_SIZE

        // Disassemble bytecode
	uint16_t isp = 0;
	while (isp < codesize) {

		size_t advance = 1;
		uint8_t instr = code[isp];
		uint8_t tmp;

		printf("%04x\t%s ", isp, code_str(instr));
		switch (instr) {
		case OP_PUSH:
			advance = 2;
			printf("0x%02x\n", code[isp+1]);
			break;
		case OP_JMP:
		case OP_FJMP:
		case OP_TJMP:
			advance = 3;
			printf("0x%04x\n", addr_value(&code[isp+1]));
			break;
		case OP_RECV0:
		case OP_RECV1:
		case OP_RECV2:
		case OP_RECV3:
			advance = 2;

			tmp = code[isp+1];
			printf("%c%d\n", tmp&RECV_PORT_FLAG ? '%' : '$',
				tmp & RECV_STORE_MASK);
			break;
		default:
			printf("\n");
		}

		isp += advance;
	}

	printf("%04x\tend\n", isp);

	return 0;
}
