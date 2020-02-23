#include <ctype.h> // isgraph
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
// pointer n to be the buffer's size. The buffer must be freed by
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

	*n = nread;
	if (ferror(f) || nread == 0) {
		free(result);
		return NULL;
	}

	result = erealloc(result, nread);
	return result;
}

static void indent(int depth)
{
	for (int i = 0; i < depth; i++) {
		printf("\t");
	}
}

static void print_expr(struct expr *e, void *dat, int depth)
{
	struct symdict *dict = (struct symdict *)dat;

	indent(depth);
	switch (e->type) {
	case NUM_LIT_EXPR:
		printf("%s %d\n", strexpr(e), e->data.num_lit.value);
		break;
	case UNARY_EXPR:
		printf("%s (%s) (suffix=%d)\n", strexpr(e),
		       strtoken(e->data.unary.op), e->data.unary.is_suffix);
		break;
	case BINARY_EXPR:
		printf("%s (%s)\n", strexpr(e), strtoken(e->data.binary.op));
		break;
	case STORE_EXPR:
		printf("%s (%s, %s)\n", strexpr(e),
		       strtoken(e->data.store.kind), id_sym(dict, e->data.store.name_id));
		break;
	default:
		puts(strexpr(e));
	}
}

static void print_stmt(struct stmt *s, void *dat, int depth)
{
	struct symdict *dict = (struct symdict *)dat;

	indent(depth);
	switch (s->type) {
	case LABELED_STMT:
		printf("%s %s\n", strstmt(s),
		       id_sym(dict, s->data.labeled.label_id));
		break;
	case BRANCH_STMT:
		printf("%s %s %s\n", strstmt(s),
		       strtoken(s->data.branch.tok),
		       id_sym(dict, s->data.branch.label_id));
		break;
	case CASE_CLAUSE:
		printf("%s ", strstmt(s));
		if (s->data.case_clause.is_default) {
			printf("default\n");
		} else {
			printf("case %d\n", s->data.case_clause.x);
		}
		break;
	case LOOP_STMT:
		printf("%s (do=%s)\n", strstmt(s),
		       s->data.loop.exec_body_first ? "yes" : "no");
		break;
	default:
		puts(strstmt(s));
	}
}


static void print_array(uint8_t data[], size_t len)
{
	printf("{");
	for (size_t i = 0; i < len; i++) {
		char c = data[i];

		// Try to show the printable character, but fall back
		// to a hex value.
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

static void print_decl(struct decl *d, void *dat)
{
	struct symdict *dict = (struct symdict *)dat;

	switch (d->type) {
	case PROC_DECL:
		printf("%s %s\n", strdecl(d),
		       id_sym(dict, d->data.proc.name_id));
		break;
	case PROC_COPY_DECL:
		printf("%s %s = %s\n", strdecl(d),
		       id_sym(dict, d->data.proc_copy.name_id),
		       id_sym(dict, d->data.proc_copy.source_id));
		break;
	case BUF_DECL:
		printf("%s %s =\n\t", strdecl(d),
		       id_sym(dict, d->data.buf.name_id));
		print_array(d->data.buf.data, d->data.buf.len);
		break;
	case STACK_DECL:
		printf("%s %s\n", strdecl(d),
		       id_sym(dict, d->data.stack.name_id));
		break;
	case WIRE_DECL:
		printf("%s %s.%s -> %s.%s\n", strdecl(d),
		       id_sym(dict, d->data.wire.source.node_id),
		       id_sym(dict, d->data.wire.source.name_id),
		       id_sym(dict, d->data.wire.dest.node_id),
		       id_sym(dict, d->data.wire.dest.name_id));
		break;
	default:
		puts(strdecl(d));
	}
}

static void count_expr_size(struct expr *e, void *dat, int depth)
{
	(void)depth;
	size_t *counted = (size_t*)dat;
	*counted += sizeof(*e);
}

static void count_stmt_size(struct stmt *s, void *dat, int depth)
{
	(void)depth;
	size_t *counted = (size_t*)dat;
	*counted += sizeof(*s);
}

static void count_decl_size(struct decl *d, void *dat)
{
	size_t *counted = (size_t*)dat;
	*counted += sizeof(*d);
}

int main(int argc, char **argv)
{
	const char *filename;
	FILE *f;

	char *src;
	size_t src_size;
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
	if (src == NULL) {
		if (ferror(f)) {
			errx(1, "%s: I/O Error.", filename);
		} else {
			fprintf(stderr, "WARN %s: Zero size file.\n", filename);
		}
	}

	// Scan the file token-by-token
	init_parser(&parser, filename, src, src_size);

	size_t tree_size = 0;

	while (!parser_eof(&parser)) {
		struct decl *decl = parse_decl(&parser);
		if (parser.errors) return 1;

		walk_decl(&print_decl, &print_stmt, &print_expr,
		          decl, &parser.dict, PARENT_FIRST);
		walk_decl(&count_decl_size, &count_stmt_size, &count_expr_size,
		          decl, &tree_size, PARENT_FIRST);
		free_decl(decl);
	}

	printf("\nTotal AST size: %lu\n", tree_size);
	printf("Dict size: %lu\n", dict_size(&parser.dict));

	return 0;
}
