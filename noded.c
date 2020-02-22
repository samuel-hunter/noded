#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <err.h>

#include "noded.h"
#include "ast.h"

void handle_error(const struct noded_error *err)
{
	fflush(stdout); // flush stdout so it doesn't mix with stderr.
	fprintf(stderr, "ERR %s:%d:%d: %s.\n",
	        err->filename, err->pos.lineno,
	        err->pos.colno, err->msg);
}

static void print_usage(const char *prog_name)
{
	fprintf(stderr, "Usage: %s [FILE]\n", prog_name);
}

// Read all of file f and return an allocated char pointer and set the
// pointer n to be the buffer's size. The buffer must be `free`d by
// the caller.
static char *read_all(FILE *f, size_t *n)
{
	// Arbitrary number, chosen because it shouldn't reallocate
	// *too* much.
	size_t size = 4096;
	size_t nread = 0;
	char *result = emalloc(size);

	while (!feof(f) && !ferror(f)) {
		if (nread == size) {
			size *= 2;
			result = erealloc(result, size);
		}
		nread += fread(&result[nread], 1, size-nread, f);
	}

	if (ferror(f)) {
		free(result);
		return NULL;
	}

	result = erealloc(result, nread);
	*n = nread;
	return result;
}

static void indent(int depth)
{
	for (int i = 0; i < depth; i++) {
		printf("\t");
	}
}

static void print_expr(struct expr *x, int depth, void *dat)
{
	(void)dat;

	indent(depth);
	switch (x->type) {
	case BAD_EXPR:
		printf("BadExpr\n");
		break;
	case NUM_LIT_EXPR:
		printf("NumLitExpr %d\n", x->data.num_lit.value);
		break;
	case PAREN_EXPR:
		printf("ParenExpr\n");
		break;
	case UNARY_EXPR:
		printf("UnaryExpr (%s) (suffix=%d)\n",
		       strtoken(x->data.unary.op), x->data.unary.is_suffix);
		break;
	case BINARY_EXPR:
		printf("BinaryExpr (%s)\n", strtoken(x->data.binary.op));
		break;
	case STORE_EXPR:
		printf("Store (%s, %s)\n",
		       strtoken(x->data.store.kind), x->data.store.name);
		break;
	}
}

static void print_stmt(struct stmt *x, int depth, void *dat)
{
	(void)dat;

	indent(depth);
	switch (x->type) {
	case BAD_STMT:
		printf("BadStmt\n");
		break;
	case EMPTY_STMT:
		printf("EmptyStmt\n");
		break;
	case LABELED_STMT:
		printf("LabeledStmt: %s\n", x->data.labeled.label);
		break;
	case EXPR_STMT:
		printf("ExprStmt\n");
		walk_expr(&print_expr, x->data.expr.x, depth+1, NULL);
		break;
	case BRANCH_STMT:
		printf("BranchStmt %s %s\n",
		       strtoken(x->data.branch.tok), x->data.branch.label);
		break;
	case BLOCK_STMT:
		printf("BlockStmt\n");
		break;
	case IF_STMT:
		printf("IfStmt\n");
		walk_expr(&print_expr, x->data.if_stmt.cond, depth+1, NULL);
		break;
	case CASE_CLAUSE:
		printf("CaseClause %d (default=%d)\n",
		x->data.case_clause.is_default, x->data.case_clause.x);
		break;
	case SWITCH_STMT:
		printf("SwitchStmt\n");
		walk_expr(&print_expr, x->data.switch_stmt.tag, depth+1, NULL);
		break;
	case LOOP_STMT:
		printf("LoopStmt (do=%d)\n", x->data.loop.exec_body_first);
		walk_expr(&print_expr, x->data.loop.cond, depth+1, NULL);
		break;
	case HALT_STMT:
		printf("HaltStmt\n");
		break;
	}
}

static void print_array(uint8_t data[], size_t len)
{
	printf("{");
	for (size_t i = 0; i < len; i++) {
		char c = data[i];
		if (isgraph(c) || c == ' ') {
			printf("'%c'", c);
		} else {
			printf("0x%02x", data[i]);
		}

		if (i + 1 < len) {
			printf(", ");
		}
	}
	printf("}\n");
}

static void print_decl(struct decl *decl)
{
	switch (decl->type) {
	case BAD_DECL:
		printf("BadDecl\n");
		break;
	case PROC_DECL:
		printf("ProcDecl %s\n", decl->data.proc.name);
		walk_stmt(&print_stmt, decl->data.proc.body, 1, NULL);
		break;
	case PROC_COPY_DECL:
		printf("ProcCopyDecl %s = %s\n", decl->data.proc_copy.name,
		       decl->data.proc_copy.source);
		break;
	case BUF_DECL:
		printf("BufNodeDecl %s\n\t", decl->data.buf.name);
		print_array(decl->data.buf.data, decl->data.buf.len);
		break;
	case STACK_DECL:
		printf("StackNodeDecl %s\n", decl->data.stack.name);
		break;
	case WIRE_DECL:
		printf("WireDecl %s.%s -> %s.%s\n",
		       decl->data.wire.source.node_name, decl->data.wire.source.name,
		       decl->data.wire.dest.node_name, decl->data.wire.dest.name);
		break;
	}
}

static void count_expr_size(struct expr *x, int depth, void *dat)
{
	(void)depth;
	size_t *counted = (size_t*)dat;
	*counted += sizeof(*x);
}

static void count_stmt_size(struct stmt *x, int depth, void *dat)
{
	(void)depth;
	size_t *counted = (size_t*)dat;
	*counted += sizeof(*x);

	switch (x->type) {
	case EXPR_STMT:
		walk_expr(&count_expr_size, x->data.expr.x, 0, dat);
		break;
	case IF_STMT:
		walk_expr(&count_expr_size, x->data.if_stmt.cond, 0, dat);
		break;
	case SWITCH_STMT:
		walk_expr(&count_expr_size, x->data.switch_stmt.tag, 0, dat);
		break;
	case LOOP_STMT:
		walk_expr(&count_expr_size, x->data.loop.cond, 0, dat);
		break;
	default:
		break; // No expr's here.
	}
}

static size_t count_decl_size(struct decl *x)
{
	size_t counted = sizeof(*x);

	if (x->type == PROC_DECL) {
		walk_stmt(&count_stmt_size, x->data.proc.body,
		          0, &counted);
	}

	return counted;
}

int main(int argc, char **argv)
{
	const char *filename;
	FILE *f;

	char *src;
	size_t src_size;
	struct scanner scanner;
	struct parser parser;

	// Choose the file to interpret
	if (argc > 2) {
		print_usage(argv[0]);
		return 1;
	} else if (argc == 2) {
		filename = argv[1];
		f = fopen(filename, "r");
		if (f == NULL)
			err(1, "Cannot open %s", filename);
	} else {
		filename = "<stdin>";
		f = stdin;
	}

	printf("Expr size: %lu\nStmt size: %lu\nDecl size: %lu\n",
	       sizeof(struct expr), sizeof(struct stmt), sizeof(struct decl));

	src = read_all(f, &src_size);
	if (src == NULL)
		errx(1, "%s: I/O Error.", filename);

	// Scan the file token-by-token
	init_scanner(&scanner, filename, src, src_size);
	init_parser(&parser, &scanner);

	size_t tree_size = 0;

	while (!parser_eof(&parser)) {
		struct decl *decl = parse_decl(&parser);
		if (parser.errors) return 1;

		print_decl(decl);
		tree_size += count_decl_size(decl);
		free_decl(decl);
	}

	printf("\nTotal AST size: %lu\n", tree_size);

	return 0;
}
