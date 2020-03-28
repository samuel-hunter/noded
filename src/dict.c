// dict.c -- symbol dictionary.
//
// Storing the string literals in the AST itself can be a bad
// idea. Before this, AST nodes ranged from 1KB-4KB, and the 136-line
// adder.nod example program came in at a whopping 348KB of space --
// most of that space was from storing 1KB literal buffers. Right
// after implementing it, the allocation space shrunk to 17KB+409B
// (409B coming from the dict itself).
#include <stdlib.h>
#include <string.h>

#include "noded.h"

static void init(struct symdict *dict)
{
	dict->cap = 8;
	dict->syms = ecalloc(dict->cap, sizeof(*dict->syms));
}

static void expand(struct symdict *dict)
{
	dict->cap *= 2;
	dict->syms = erealloc(dict->syms, dict->cap * sizeof(*dict->syms));
}

size_t sym_id(struct symdict *dict, const char *sym)
{
	// Fresh dict. This allows sym_id to be called even with a
	// dict's zero-value.
	if (dict->cap == 0)
		init(dict);

	for (size_t i = 0; i < dict->len; i++) {
		if (strcmp(dict->syms[i], sym) == 0)
			return i;
	}

	// No strings matched; add a new one to the array.
	if (dict->len == dict->cap)
		expand(dict);

	size_t result = dict->len++;
	dict->syms[result] = strdup(sym);

	return result;
}

const char *id_sym(const struct symdict *dict, size_t id)
{
	if (id >= dict->len) return NULL;
	return dict->syms[id];
}

size_t dict_size(const struct symdict *dict)
{
	size_t result = sizeof(*dict) + dict->cap*sizeof(dict->syms);

	for (size_t i = 0; i < dict->len; i++) {
		result += strlen(dict->syms[i]) + 1;
	}

	return result;
}

void clear_dict(struct symdict *dict)
{
	for (size_t i = 0; i < dict->len; i++) {
		free(dict->syms[i]);
	}

	free(dict->syms);
	memset(dict, 0, sizeof(*dict));
}
