/*
 * vec - frequently used vectors
 */
#include "noded.h"

#define MAX(X, Y) ((X) > (Y) ? (X) : (Y))

static size_t VEC_START = 8;

void bytevec_append(ByteVec *vec, uint8_t val)
{
	if (vec->len == vec->cap) {
		vec->cap = vec->cap ? vec->cap*2 : VEC_START;
		vec->buf = erealloc(vec->buf, vec->cap);
	}

	vec->buf[vec->len++] = val;	
}

uint8_t *bytevec_reserve(ByteVec *vec, size_t nmemb)
{
	uint8_t *result;

	if (vec->len + nmemb >= vec->cap) {
		vec->cap = vec->cap ? MAX(vec->cap*2, vec->cap+nmemb)
							: MAX(VEC_START, nmemb);
		vec->buf = erealloc(vec->buf, vec->cap);
	}

	result = &vec->buf[vec->len];
	vec->len += nmemb;
	return result;
}

void bytevec_shrink(ByteVec *vec)
{
	if (vec->len == vec->cap) return;
	vec->cap = vec->len;
	vec->buf = erealloc(vec->buf, vec->cap);
}
