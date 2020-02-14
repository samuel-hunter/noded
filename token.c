#include <string.h>

#include "noded.h"

// Should be in the same exact order as enum tokens.
static const char *tokens[] = {
	"ILLEGAL",
	"EOF",

	// Grammar
	"(",
	")",
	"{",
	"}",

	":",
	",",
	".",
	";",
	"->",

	// Literals
	NULL, // literal_beg
	"IDENTIFIER",
	"VARIABLE",
	"PORT",
	"NUMBER",
	"STRING_LITERAL",
	NULL, // literal_end

	// Operators
	NULL, // operator_beg
	"<-",

	"++",
	"--",
	"!",
	"~",

	"*",
	"/",
	"%",
	"+",
	"-",

	"<<",
	">>",

	"<",
	"<=",
	">",
	">=",

	"==",
	"!=",

	"&",
	"^",
	"|",

	"&&",
	"||",

	"?",

	"=",

	"*=",
	"/=",
	"%=",

	"+=",
	"-=",

	"<<=",
	">>=",

	"&=",
	"^=",
	"|=",
	NULL, // operator_end

	// Keywords
	NULL, // keyword_end
	"break",
	"case",
	"continue",
	"default",
	"do",
	"else",
	"for",
	"goto",
	"halt",
	"if",
	"switch",
	"while",

	"buffer",
	"processor",
	"stack",
	NULL // keyword_end
};

struct keyword_arg {
	const char *literal;
	enum token tok;
};

struct keyword_arg keywords[] = {
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
	{"stack", STACK},
	{NULL, ILLEGAL}
};

// map an identifier to its keyword token or IDENTIFIER if not a
// keyword.
enum token lookup(char ident[])
{
	for (size_t i = 0; keywords[i].literal != NULL; i++) {
		if (strcmp(keywords[i].literal, ident) == 0)
			return keywords[i].tok;
	}

	return IDENTIFIER;
}

int precedence(enum token op)
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
bool isltr(enum token op)
{
	switch (precedence(op)) {
	case 14:
	case 3:
	case 2:
		return false;
	default:
		return true;
	}
}

// Predicates
bool isliteral(enum token tok) { return literal_beg < tok && tok < literal_end; }
bool isoperator(enum token tok) { return operator_beg < tok && tok < operator_end; }
bool iskeyword(enum token tok) { return keyword_beg < tok && tok < keyword_end; }

bool isoperand(enum token tok)
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

bool isunary(enum token tok)
{
	switch (tok) {
	case ADD:
	case SUB:
	case INC:
	case DEC:
	case LNOT:
	case NOT:
		return true;
	default:
		return false;
	}
}

bool issuffix(enum token tok) {
	return tok == INC || tok == DEC;
}

const char *strtoken(enum token tok)
{
	return tokens[tok];
}
