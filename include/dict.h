#ifndef DICT_H
#define DICT_H

#include <stddef.h> // size_t

struct symdict {
	char **syms;
	size_t len;
	size_t cap;
};

// dict.c

size_t sym_id(struct symdict *dict, const char *sym);
const char *id_sym(const struct symdict *dict, size_t id);
size_t dict_size(const struct symdict *dict);
void clear_dict(struct symdict *dict);


#endif // DICT_H
