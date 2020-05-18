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
	[STRING_LITERAL] = "STRING_LITERAL",

	// Operators
	[SEND] = "<-",

	[INC] = "++",
	[DEC] = "--",
	[LNOT] = "!",
	[NOT] = "~",

	[MUL] = "*",
	[DIV] = "/",
	[MOD] = "%",
	[ADD] = "+",
	[SUB] = "-",

	[SHL] = "<<",
	[SHR] = ">>",

	[LSS] = "<",
	[LTE] = "<=",
	[GTR] = ">",
	[GTE] = ">=",

	[EQL] = "==",
	[NEQ] = "!=",

	[AND] = "&",
	[XOR] = "^",
	[OR] = "|",

	[LAND] = "&&",
	[LOR] = "||",

	[COND] = "?",

	[ASSIGN] = "=",

	[MUL_ASSIGN] = "*=",
	[DIV_ASSIGN] = "/=",
	[MOD_ASSIGN] = "%=",

	[ADD_ASSIGN] = "+=",
	[SUB_ASSIGN] = "-=",

	[SHL_ASSIGN] = "<<=",
	[SHR_ASSIGN] = ">>=",

	[AND_ASSIGN] = "&=",
	[XOR_ASSIGN] = "^=",
	[OR_ASSIGN] = "|=",

	[BREAK] = "break",
	[CASE] = "case",
	[CONTINUE] = "continue",
	[DEFAULT] = "default",
	[DO] = "do",
	[ELSE] = "else",
	[FOR] = "for",
	[GOTO] = "goto",
	[HALT] = "halt",
	[IF] = "if",
	[SWITCH] = "switch",
	[WHILE] = "while",

	[BUFFER] = "buffer",
	[PROCESSOR] = "processor"
};

struct {
	const char *literal;
	Token tok;
} keywords[] = {
	{"break", BREAK},
	{"case", CASE},
	{"continue", CONTINUE},
	{"default", DEFAULT},
	{"do", DO},
	{"else", ELSE},
	{"for", FOR},
	{"goto", GOTO},
	{"halt", HALT},
	{"if", IF},
	{"switch", SWITCH},
	{"while", WHILE},
	{"buffer", BUFFER},
	{"processor", PROCESSOR},
	{NULL, ILLEGAL}
};

// map an identifier to its keyword token or IDENTIFIER if not a
// keyword.
Token lookup(char ident[])
{
	for (size_t i = 0; keywords[i].literal != NULL; i++) {
		if (strcmp(keywords[i].literal, ident) == 0)
			return keywords[i].tok;
	}

	return IDENTIFIER;
}

// All information regarding precedence and associativity is found in
// SPEC.md, section `EXPRESSIONS'.

int precedence(Token op)
{
	switch (op) {
	case MUL:
	case DIV:
	case MOD:
		return 13;
	case ADD:
	case SUB:
		return 12;
	case SHL:
	case SHR:
		return 11;
	case LSS:
	case LTE:
	case GTR:
	case GTE:
		return 10;
	case EQL:
	case NEQ:
		return 9;
	case AND:
		return 8;
	case XOR:
		return 7;
	case OR:
		return 6;
	case LAND:
		return 5;
	case LOR:
		return 4;
	case COND:
		return 3;
	case ASSIGN:
	case MUL_ASSIGN:
	case DIV_ASSIGN:
	case MOD_ASSIGN:
	case ADD_ASSIGN:
	case SUB_ASSIGN:
	case SHR_ASSIGN:
	case SHL_ASSIGN:
	case AND_ASSIGN:
	case XOR_ASSIGN:
	case OR_ASSIGN:
		return 2;
	case COMMA:
		return 1;
	case SEND:
		return 0;
	default:
		return NON_OPERATOR;
	}
}

// Return whether the associativity is left-to-right.
bool isltr(Token op)
{
	switch (precedence(op)) {
	case 14: // Prefixes
	case 3:  // COND
	case 2:  // ASSIGN, *_ASSIGN
		return false;
	default:
		return true;
	}
}

// Predicates
bool isliteral(Token tok) { return literal_beg < tok && tok < literal_end; }
bool isoperator(Token tok) { return operator_beg < tok && tok < operator_end; }
bool iskeyword(Token tok) { return keyword_beg < tok && tok < keyword_end; }

bool isoperand(Token tok)
{
	switch (tok) {
	case NUMBER:
	case CHAR:
	case LPAREN:
	case PORT:
	case VARIABLE:
		return true;
	default:
		return false;
	}
}

bool isunary(Token tok)
{
	switch (tok) {
	case INC:
	case DEC:
	case LNOT:
	case NOT:
		return true;
	default:
		return false;
	}
}

bool issuffix(Token tok) {
	return tok == INC || tok == DEC;
}

const char *strtoken(Token tok)
{
	return tokens[tok];
}
