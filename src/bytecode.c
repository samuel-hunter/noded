/*
 * bytecode - bytecode utilities
 */
#include "bytecode.h"

// Return the address stored as src points to a JUMP instruction.
uint16_t addr_value(const uint8_t *src)
{
	return ((uint16_t)(src[1])<<8) +
	       (uint16_t)(src[2]);
}

// Return the RECV instruction's port index and set is_store to
// whether the port type is a store.
uint8_t recv_dest(const uint8_t *src, bool *is_store)
{
	*is_store = (src[1] & RECV_PORT_FLAG) == RECV_PORT_FLAG;
	return src[1] & RECV_STORE_MASK;
}
