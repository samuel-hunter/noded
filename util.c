#include <stdio.h>
#include <stdlib.h>

#include "noded.h"

void *emalloc(size_t size)
{
	void *result = malloc(size);
	if (!result) {
		fflush(stdout);
		fprintf(stderr, "Out of memory\n");
		exit(1);
	}

	return result;
}
