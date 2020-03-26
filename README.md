# Noded
Concurrent programming language of little microprocessors
communicating with each other.

## Description

A Noded program consists of a collection of nodes operating at the
same time and communicating to each other through ports wired to
each other.

## Progress

We got a working interpreter! Currently, it interprets only a single
node, and all message-sending is between that node and standard
input/output.

## Language Specification

A more detailed specification which has the nitty-gritty details can
be found in [SPEC.md](./SPEC.md).

## Examples

### Hello World

```c
// Implement a `report` processor node that interacts with a string
// node to send all data within the string once out.
processor report {
	// All code within a processor node executes forever until
	// it reaches a `halt` statement, or the program stops
	// because all nodes stopped running (read: a deadlock)

	// %ports and $variables don't need to be declared, and their
	// scope is the whole processor node. Variables always start out
	// at 0 at the beginning of the program.

	// Send the state of the variable $i to the output-only port
	// %idx.
	%idx <- $i;
	$i++; // Increment $i by 1.

	// Store a message from the input-only port %chr into $mem.
	$mem <- %chr;

	// Halt forever when $mem reads the terminating null character.
	if ($mem == 0)
		halt;

	// Relay the message to the output-only port %out.
	%out <- $mem;
}

// Add a buffer node that stores "Hello, World!\n" at the beginning
// of the program. Buffer nodes have the ports %idx and %chr, which
// respectively store the buffer's active index, and the buffer's
// character at that index.
buffer message = "Hello, World!\n";

// Start wiring the nodes together.

// Messages from the report node's %idx port is sent to
// the buffer node's port.
report.idx -> message.idx;

// Whenever the report node reads from %chr, it takes the buffer's
// character at that index.
message.chr -> report.chr;

// Send the report's output to the console via the port `out` from
// unique node `io`.
report.out -> io.out;
```

Without comments:
```c
processor report {
	%idx <- $i;
	$i++;

	$mem <- %chr;
	if ($mem == 0)
		halt;

	%out <- $mem;
}

buffer message = "Hello, World!\n";

report.idx -> message.idx;
message.chr -> report.chr;
report.out -> io.out;
```

### cat

An explicit implementation:

```c
processor cat {
	// This node will keep churning until the %in port blocks
	// forever
	$chr <- %in;
	%out <- $chr;
}

// The special node `io` has a port `in` which will read from the
// console and block forever when there is no more input.
io.in -> cat.in;
cat.out -> io.out;
```

The `cat` node may be explicit, but is technically unnecessary:

```c
// input and output can be piped directly to each other.
io.in -> io.out;
```

### Truth Machine

```c
processor truth {
	$status <- %in;
	// Character literals can be used like number literals.
	if ($status == '0') {
		halt;
	}

	// There are no `true` or `false` keywords, all non-zero
	// values are truthy.
	while (1) {
		%out <- $status;
		%out <- '\n';
	}
}

io.in -> truth.in;
truth.out -> io.out;
```

More examples can be found in the [corresponding
directory](./examples).
