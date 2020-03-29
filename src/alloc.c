/*
 * alloc -- stdlib allocation wrapped in pointer-checking
 *
 * In a C library, memory allocation would (best) be left to the
 * library-caller itself, or (second-best) be given a function pointer
 * or a struct of pointers to allocate on the user's behalf. This is
 * from what I've heard of other C programmers who I've talked with
 * while hacking. However, I'm doing it this way because a) this is
 * a library, and b) once a compiler or interpreter reaches an
 * out-of-memory error, there's no reasonable course of action other
 * than to say, "Hey, we're out of memory!" and to exit.
 *
 * A similar story applies to send_error().
 */
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

void *ecalloc(size_t nmemb, size_t size)
{
	void *result = calloc(nmemb, size);
	check_sane_pointer(result);

	return result;
}

void *erealloc(void *ptr, size_t size)
{
	void *result = realloc(ptr, size);
	check_sane_pointer(result);

	return result;
}
