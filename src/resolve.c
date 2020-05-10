/*
 * resolve - wire resolution
 */
#include <string.h>

#include "resolve.h"

void resolve_add_node(struct resolve_ctx *rctx, struct node *node,
	size_t node_id, size_t port_ids[])
{
	struct resolve_node *rnode;

	if (rctx->node_cap == rctx->nnodes) {
		// Expand/allocate when necessary.
		if (rctx->node_cap == 0) {
			rctx->node_cap = 8;
		} else {
			rctx->node_cap *= 2;
		}

		rctx->nodes = erealloc(rctx->nodes,
			rctx->node_cap*sizeof(*rctx->nodes));
	}

	rnode = &rctx->nodes[rctx->nnodes++];

	rnode->node = node;
	rnode->node_id = node_id;
	memcpy(rnode->port_ids, port_ids, sizeof(rnode->port_ids));
}

// Return the pointer to the target resolve node if it exists; else,
// return NULL.
static struct resolve_node *find_node(
	const struct resolve_ctx *rctx, size_t node_id)
{
	for (size_t i = 0; i < rctx->nnodes; i++) {
		if (node_id == rctx->nodes[i].node_id)
			return &rctx->nodes[i];
	}

	return NULL;
}

// Find the index of a port from the given array. If there is none, return -1.
static int port_index(size_t port_ids[], size_t port_id)
{
	for (int i = 0; i < PROC_PORTS; i++) {
		if (port_id == port_ids[i])
			return i;
	}

	return -1;
}

void resolve(struct resolve_ctx *rctx, struct runtime *env,
	struct wire_decl *wire_decl)
{
	struct resolve_node *src, *dest;
	int src_porti, dest_porti;
	bool is_valid = true;

	src = find_node(rctx, wire_decl->source.node_id);
	if (!src) {
		send_error(&wire_decl->source.node_pos, ERR,
			"Undefined node");
		is_valid = false;
	}

	src_porti = port_index(src->port_ids, wire_decl->source.name_id);
	if (src_porti < 0) {
		send_error(&wire_decl->source.name_pos, ERR,
			"Undefined port");
		is_valid = false;
	}

	dest = find_node(rctx, wire_decl->dest.node_id);
	if (!dest) {
		send_error(&wire_decl->dest.node_pos, ERR,
			"Undefined node");
		is_valid = false;
	}

	dest_porti = port_index(dest->port_ids, wire_decl->dest.name_id);
	if (dest_porti < 0) {
		send_error(&wire_decl->dest.name_pos, ERR,
			"Undefined port");
		is_valid = false;
	}

	if (is_valid) {
		add_wire(env, src->node, src_porti, dest->node, dest_porti);
	}
}
