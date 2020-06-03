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

## Building

The project uses a simple Makefile and assumes a POSIX environment. To
build and run the interpreter, simply:

```
$ git clone https://git.sr.ht/~shunter/noded
$ make
$ sudo make install # why would you do this?
$ ./noded examples/hello.nod
```

## Progress

The implementation should be valid to the specification draft for all
valid prorgrams. There are a few bugs which need to be worked out
(most documented as TODOS in-code). Once I have sorted these out, I
plan to tag the version as v0.1 and work on an online demo through
emscripten (or some variant).

## Problems

While all valid code should be working (except for one flaw), all working
programs should produce valid code. Some invalid programs might not
be checked during compilation, and therefore can cause runtime errors or
undefined behavior. These include:

- The interpreter wants to support UTF-8, but does not recognize the
  Byte Order Mark. The scanner instead interprets it like the
  beginning of an identifier.
- Reads from io.out, io.err or writes to io.in are not checked during
  compile-time and causes the VM to fail without helpful info as a runtime
  error.
- A processor node participating in more than one wire is not checked and
  causes memory leaks and unexpected behavior. This should be checked and
  forbidden during compile-time.
- There is no sort of testing framework, so there's no indicator of whether
  any change I make breaks preexisting code.

## Contributing

If you catch any issues or want to improve the compiler, I greatly
appreciate any feedback in the 
[SourceHut mailing list](https://lists.sr.ht/~shunter/public-inbox).
Thanks!

## Language Specification

A more detailed specification which has the nitty-gritty details can
be found in [SPEC.md](./SPEC.md).

## Examples

More examples can be found in the [corresponding
directory](./examples).

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
