/*
 * token - token utilities
 *
 * token includes token introspection like its name, whether it's a
 * keyword, and some AST-supporting properties such as its precedence
 * or whether it's a specific token type.
 */
#include <string.h>

#include "noded.h"

// Should be in the same exact order as Tokens.
static const char *tokens[] = {
	[ILLEGAL] = "ILLEGAL",
	[TOK_EOF] = "EOF",

	// Grammar
	[LPAREN] = "(",
	[RPAREN] = ")",
	[LBRACE] = "{",
	[RBRACE] = "}",

	[COLON] = ":",
	[COMMA] = ",",
	[PERIOD] = ".",
	[SEMICOLON] = ";",
	[WIRE] = "->",

	// Literals
	[IDENTIFIER] = "IDENTIFIER",
	[VARIABLE] = "VARIABLE",
	[PORT] = "PORT",
	[NUMBER] = "NUMBER",
	[CHAR] = "CHAR",
	[STRING] = "STRING",

	// Operators
	[SEND] = "<-",

	[ASSIGN] = "=",
	[OR_ASSIGN] = "|=",
	[XOR_ASSIGN] = "^=",
	[AND_ASSIGN] = "&=",
	[SHR_ASSIGN] = ">>=",
	[SHL_ASSIGN] = "<<=",
	[ADD_ASSIGN] = "+=",
	[SUB_ASSIGN] = "-=",
	[MUL_ASSIGN] = "*=",
	[DIV_ASSIGN] = "/=",
	[MOD_ASSIGN] = "%=",

	[COND] = "?",

	[LOR] = "||",
	[LAND] = "&&",

	[OR] = "|",
	[XOR] = "^",
	[AND] = "&",

	[EQL] = "==",
	[NEQ] = "!=",

	[GTE] = ">=",
	[GTR] = ">",
	[LTE] = "<=",
	[LSS] = "<",

	[SHR] = ">>",
	[SHL] = "<<",

	[ADD] = "+",
	[SUB] = "-",

	[MUL] = "*",
	[DIV] = "/",
	[MOD] = "%",

	[INC] = "++",
	[DEC] = "--",
	[LNOT] = "!",
	[NOT] = "~",


	[BREAK] = "break",
	[CONTINUE] = "continue",
	[DO] = "do",
	[ELSE] = "else",
	[FOR] = "for",
	[GOTO] = "goto",
	[HALT] = "halt",
	[IF] = "if",
	[WHILE] = "while",

	[BUFFER] = "buffer",
	[PROCESSOR] = "processor",
	[STACK] = "stack",
};

struct {
	const char *literal;
	TokenType type;
} keywords[] = {
	{"break", BREAK},
	{"continue", CONTINUE},
	{"do", DO},
	{"else", ELSE},
	{"for", FOR},
	{"goto", GOTO},
	{"halt", HALT},
	{"if", IF},
	{"while", WHILE},
	{"buffer", BUFFER},
	{"processor", PROCESSOR},
	{"stack", STACK},
	{NULL, ILLEGAL}
};

// map an identifier to its keyword token or IDENTIFIER if not a
// keyword.
TokenType lookup(char ident[])
{
	for (size_t i = 0; keywords[i].literal != NULL; i++) {
		if (strcmp(keywords[i].literal, ident) == 0)
			return keywords[i].type;
	}

	return IDENTIFIER;
}

const char *tokstr(TokenType type)
{
	return tokens[type];
}
