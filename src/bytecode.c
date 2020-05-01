#include "vm.h"

// Return the address stored as src points to a JUMP instruction.
uint16_t addr_value(const uint8_t *src)
{
	return ((uint16_t)(src[1])<<8) +
	       (uint16_t)(src[2]);
}
