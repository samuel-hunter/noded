#ifndef RESOLVE_H
#define RESOLVE_H

#include "ast.h"
#include "vm.h"

struct resolve_ctx {
	size_t node_cap;
	size_t nnodes;

	struct resolve_node {
		struct node *node;
		size_t node_id;
		size_t port_ids[PROC_PORTS];
	} *nodes;
};


void resolve_add_node(struct resolve_ctx *rctx, struct node *node,
	size_t node_id, size_t port_ids[]);
void resolve(struct resolve_ctx *rctx, struct runtime *ctx,
	struct wire_decl *wire_decl);

#endif /* RESOLVE_H */
