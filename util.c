#include <stdio.h>
#include <stdlib.h>

#include "noded.h"

static void check_sane_pointer(void *ptr)
{
	if (ptr) return;

	fflush(stdout);
	fprintf(stderr, "Out of memory\n");
	exit(1);
}

void *emalloc(size_t size)
{
	void *result = malloc(size);
	check_sane_pointer(result);

	return result;
}

void *erealloc(void *ptr, size_t size)
{
	void *result = realloc(ptr, size);
	check_sane_pointer(result);

	return result;
}
