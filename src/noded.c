/*
 * noded - noded interpreter
 *
 * noded currently assumes the first declaration to be a processor,
 * compiles it, and runs the program. All SEND ops are sent as
 * characters to stdout, and all RECV ops are read from stdin.
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <err.h>
#include <errno.h>

#include "noded.h"
#include "resolve.h"


static struct {
	// Source file
	FILE *f;
	const char *filename;

	size_t errors;
} Globals = {0};

// Number of errors before the parser panics.
static const size_t max_errors = 10;

void send_error(const struct position *pos, enum error_type type,
	const char *fmt, ...)
{
	va_list ap;

	// Print the error
	va_start(ap, fmt);
	vprint_error(Globals.filename, Globals.f, pos,
		type, fmt, ap);
	va_end(ap);

	// Send an exit depending on fatality.
	switch (type) {
	case WARN:
		break;
	case ERR:
		if (++Globals.errors > max_errors)
			errx(1, "Too many errors.");
		break;
	case FATAL:
		exit(1);
		break;
	}
}

static bool has_errors(void)
{
	return Globals.errors > 0;
}

int main(int argc, char **argv)
{
	struct symdict dict = {0}; // a zero-value dict is freshly initialized.
	struct runtime env = {0}; // a zero-value runtime is freshly initialized.
	struct resolve_ctx rctx = {0}; // ...ditto
	struct parser parser;
	struct decl *decl;
	struct node *node;
	size_t port_ids[PROC_PORTS];

	size_t codearr_cap = 0;
	size_t ncode = 0;
	uint8_t **codearr = NULL;
	size_t code_size;

	if (argc != 2) {
		// Noded requires a file argument
		fprintf(stderr, "Usage: %s FILE\n", argv[0]);
		return 1;
	}

	// Add the IO node
	node = add_io_node(&env);

	// HACK: the first element *must* be in, and second
	// *must* be out, to match the order of the enum io_port. See
	// vm.h.
	port_ids[0] = sym_id(&dict, "in");
	port_ids[1] = sym_id(&dict, "out");
	port_ids[2] = sym_id(&dict, "err");
	resolve_add_node(&rctx, node, sym_id(&dict, "io"), port_ids);
	memset(port_ids, 0, sizeof(port_ids));

	// Open the file
	Globals.filename = argv[1];
	Globals.f = fopen(Globals.filename, "r");
	if (Globals.f == NULL)
		err(1, "Cannot open %s", Globals.filename);

	// Grab a declaration (hopefully a processor decl)
	init_parser(&parser, Globals.f, &dict);

	while (true) {
		decl = parse_decl(&parser);
		if (has_errors())
			return 1;

		switch (decl->type) {
		case PROC_DECL:
			if (codearr_cap == ncode) {
				if (codearr_cap == 0) {
					codearr_cap = 8;
				} else {
					codearr_cap *= 2;
				}
				codearr = erealloc(codearr,
					codearr_cap*sizeof(*codearr));
			}

			// Compile the processor from its declaration
			code_size = bytecode_size(&decl->data.proc);
			codearr[ncode] = ecalloc(code_size, sizeof(*codearr[ncode]));
			compile(&decl->data.proc, codearr[ncode], port_ids, NULL);
			if (has_errors())
				return 1;

			// Add the proc and io nodes.
			resolve_add_node(&rctx,
				add_proc_node(&env, codearr[ncode], code_size),
				decl->data.proc.name_id, port_ids);

			memset(port_ids, 0, sizeof(port_ids));
			ncode++;
			break;
		case BUF_DECL:
			// HACK: the symbols for port_ids *must* match
			// the order of enum buf_port found in vm.h.
			port_ids[0] = sym_id(&dict, "idx");
			port_ids[1] = sym_id(&dict, "elm");
			resolve_add_node(&rctx, add_buf_node(&env, decl->data.buf.data),
				decl->data.buf.name_id, port_ids);
			memset(port_ids, 0, sizeof(port_ids));
			break;
		case WIRE_DECL:
			resolve(&rctx, &env, &decl->data.wire);
			break;
		case EOF_DECL:
			free_decl(decl);
			goto stop_parsing;
		default:
			errx(1, "Unexpected declaration type.");
		}

		free_decl(decl);
		if (has_errors())
			return 1;
	}

stop_parsing:

	// free the dict and AST, since we no longer need it.
	clear_dict(&dict);
	fclose(Globals.f);

	// Run, clear, and exit.
	run(&env);
	clear_runtime(&env);

	for (size_t i = 0; i < ncode; i++)
		free(codearr[i]);
	free(codearr);

	return 0;
}
