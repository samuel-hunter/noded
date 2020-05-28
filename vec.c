/*
 * vec - frequently used vectors
 */
#include <stdlib.h>
#include <string.h>

#include "noded.h"

#define MAX(X, Y) ((X) > (Y) ? (X) : (Y))

enum
{
	VEC_START = 8,
};

/* add val to the end of vec */
void
bytevec_append(ByteVec *vec, uint8_t val)
{
	if (vec->len == vec->cap) {
		vec->cap = vec->cap ? vec->cap*2 : VEC_START;
		vec->buf = erealloc(vec->buf, vec->cap);
	}

	vec->buf[vec->len++] = val;	
}

/* reserve nmemb members in vec and return the reserved memory's index */
size_t
bytevec_reserve(ByteVec *vec, size_t nmemb)
{
	size_t result;

	if (vec->len + nmemb >= vec->cap) {
		vec->cap = vec->cap ? MAX(vec->cap*2, vec->cap+nmemb)
							: MAX(VEC_START, nmemb);
		vec->buf = erealloc(vec->buf, vec->cap);
	}

	result = vec->len;
	vec->len += nmemb;
	return result;
}

/* Shrink vec->buf to vec->len members */
void
bytevec_shrink(ByteVec *vec)
{
	if (vec->len == vec->cap) return;
	vec->cap = vec->len;
	vec->buf = erealloc(vec->buf, vec->cap);
}

/* Add an address to the vector */
void
addrvec_append(AddrVec *vec, uint16_t val)
{
	if (vec->len == vec->cap) {
		vec->cap = vec->cap ? vec->cap*2 : VEC_START;
		vec->buf = erealloc(vec->buf, vec->cap*sizeof(*vec->buf));
	}

	vec->buf[vec->len++] = val;
}

/* Free the buffer and set the vec to its zero value */
void
addrvec_clear(AddrVec *vec)
{
	free(vec->buf);
	memset(vec, 0, sizeof(*vec));
}