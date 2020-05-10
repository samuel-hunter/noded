/*
 * alloc -- stdlib allocation wrapped in pointer-checking
 *
 * In a C library, best practice says to leave memmory allocation to
 * the library-caller itself, or (second-best) receive a function
 * pointer or a struct of pointers to allocate on the user's
 * behalf. This is from what I've heard of other C programmers who
 * I've talked with while hacking. However, I'm doing it this way
 * because a) this is no a library, and b) once a compiler or
 * interpreter reaches an out-of-memory error, there's no reasonable
 * course of action other than to say, "Hey, we're out of memory!" and
 * to exit.
 *
 * A similar story applies to send_error().
 */
#include <err.h>
#include <stdio.h>
#include <stdlib.h>

#include "noded.h"

static void check_sane_pointer(void *ptr)
{
	if (!ptr)
		err(1, "Out of memory.");
}

void *ecalloc(size_t nmemb, size_t size)
{
	void *result;

	/*
	 * According to malloc(3) from the Linux Programmer's Manual:
	 *
         *   If nmemb or size is 0, then calloc() returns either NULL,
         *   or a unique pointer value that can later be successfully
         *   passed to free().
         *
	 * Sounds like a terrible wart, especially when gracefully
	 * cascading error values. Instead treat nmemb*size==0 as a
	 * no-op.
	 */
	if (!(nmemb || size)) return NULL;

	result = calloc(nmemb, size);
	check_sane_pointer(result);
	return result;
}

void *erealloc(void *ptr, size_t size)
{
	void *result;

	// realloc() has a similar wart to calloc().
	if (!size) {
		free(ptr);
		return NULL;
	}

	result = realloc(ptr, size);
	check_sane_pointer(result);
	return result;
}
