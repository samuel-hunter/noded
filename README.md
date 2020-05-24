# Noded
Concurrent programming language of little microprocessors
communicating with each other.

## Description

A Noded program consists of a collection of nodes operating at the
same time and communicating to each other through ports wired to each
other.

This project is maid half as a toy implementation, and half as a
thought experiment for a concurrent language. Please don't have any
expectations of it being fast, or accurate, or cope with invalid code.

## Progress

I'm in progress of a rewrite. The working version before the rewrite
is tagged as `v0.0.0`, and it was written very ad-hoc style as I
figured out what the problem really was. Now that I have an idea, this
rewrite should make a much cleaner version with code I'd be more proud
of!

## Language Specification

A more detailed specification which has the nitty-gritty details can
be found in [SPEC.md](./SPEC.md).

## Examples

### Hello World

```c
/* hello.nod
 * Print Hello World and exit.
 */

// Create a processor node `report`.
//  Processor nodes execute predefined code ad infinitum
//  and can only contain four port and four variables.

// Neither ports nor variables need to be declared, because
//  they can be inferred from the code and the scope is the
//  entire machine.
processor report {
	// Push the value of variable $1 to port %idx.
	%idx <- $i; // All variables, like $1 start out at zero.
	$i++;

	// Read from the port %in and store in the variable $mem.
	$mem <- %in;
	if ($mem == 0)
		halt; // Stop this node from running forever.

	// Relay the value from %chr to the port %out.
	%out <- $mem;
}

// String nodes hold an array of characters initialized at its
//  start and can be interacted with through its %idx and %elm
//  ports. Writing to %idx changes where the position it's currently
//  pointing to, and writing to/reading from the port %elm interacts
//  with that current character as you would expect.
buffer message = "Hello, World!\n";


// WIRING

// Send messages from the report node's %idx port
//  to the message node's %idx port.
report.idx -> message.idx;

// Same thing here.
message.elm -> report.in;

// Wire report.out to the special node `io.out`, which sends the number
//  to standard output as a character.
report.out -> io.out;
```

Without comments, and slightly more compacted:
```c
processor report {
	%idx <- $i++;
	$mem <- %in;

	if ($mem == 0)
		halt;

	%out <- $mem;
}

buffer message = "Hello, World!\n";

report.idx -> message.idx;
message.elm -> report.in;
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
    
    // You could also wire them up directly via:
    //   %out <- %in;
}

// The special node `io` has a port `in` which will read from the
// console and block forever when there is no more input.
io.in -> cat.in;
cat.out -> io.out;
```

The preivous `cat` node was explicit, and the processor wasn't
technically unnecessary:

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
