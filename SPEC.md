# Noded Specification

Noded is a concurrent, procedural programming language that consists
of a collection of nodes executing code at the same time and sending
messages to each other.

```c
processor report {
    %idx <- $idx++;
    $chr <- %in;
    if ($chr == 0) {
        halt;
    }
    
    %out <- $chr;
}

buffer message = "Hello, world!\n";

report.idx -> message.idx;
message.elm -> report.in;
report.out -> io.out;
```

## Notation

The formal syntax is written down in Extended Backus-Naur Form:

```
letter = "a" ... "z" | "A" ... "Z" | "_" ;

digit = "0" ... "9" ;
hex_digit = "0" ... "9" | "A" ... "F" | "a" ... "f" ;
octal_digit = "0" ... "7" ;
binary_digit = "0" | "1" ;

alphanumeric = letter | digit ;
identifier = letter {alphanumeric } ;

(* comment *)
```

Within a paragraph, code snippets are within `monospaced text`,
language terminology will be within *italicized text*, and emphasized
phrases will be **bolded**.

An ellipsis may be shown within code examples to express content that
could be there, but is not important to the demonstration. There is no
ellipsis defined within the language itself.

Example code blocks are highlighted as a C program

## Lexical Elements

### Comments

Comments are ignored by the language. Comments can start with a
slash-slash `//` and terminate at the end of the line, or start with a
slash asterisk `/*` and terminate at the nearest asterisk slash
`*/`.

### Keywords

The following keywords are reserved and may not be used as
identifiers:

```c
break       else        switch
case        for         while
continue    goto        processor
default     halt        buffer
go          if
```

### Operators and punctuation

The following sequences represent operators and grammatical
punctuation:

```
+   &   >>  +=  &=  >>= ++ ->  ,
-   |   !   -=  |=  !=  -- (   .
*   ^   <   *=  ^=  <=  && )   ;
/   ~   >   /=  :   >=  || {
%   <<  =   %=  <<= ==  <- }
```

### Integer Literals

An integer literal is a sequence of digits representing a number
constant. By default, this sequence is base 10. The sequence is base 8
if it is prefixed by `0` or `0o`. The sequence is base 16 if prefixed
by `0x`. The sequence is base 2 is prefixed by `0b`.

Digits can be spaced out using an underscore after the base prefix or
a digit.

```
integer_literal = decimal_lit | binary_lit | octal_lit | hex_lit ;
decimal_lit = "0" | ("1" ... "9") [ "_" ] decimal_digits ;
binary_lit = "0" ("b" | "B") [ "_" ] binary_digits ;
octal_lit = "0" ["o" | "O"] [ "_" ] octal_digits ;
hex_lit = "0" ( "x" | "X" ) [ "_" ] hex_digits ;

decimal_digits = decimal_digit { [ "_" ] decimal_digit } ;
binary_digits = binary_digit { [ "_" ] binary_digit } ;
octal_digits = octal_digit { [ "_" ] octal_digit } ;
hex_digits = hex_digit { [ "_" ] hex_digits } ;
```

### Floating Point Literals

There are no floating point literals.

### Character Literals

A character literal is a single 7-bit ASCII character surrounded by
apostrophes, or a character escape sequence surrounded by apostrophes.

```
character_literal = "'" ( ascii_char | escaped_char | byte_value ) "'" ;
byte_value = octal_byte_value | hex_byte_value ;
octal_byte_value = `\` octal_digit octal_digit octal_digit ;
hex_byte_value = `\` "x" hex_digit hex_digit ;
escaped_char = `\` ( "a" | "b" | "f" | "n" | "r" | "t" | "v" ) ;
```

```c
'a'
'b'
'\xff'
'\123'
'\''
'aa' // illegal: too many characters
''   // illegal: no characters
'\1' // illegal: too few octal digits
'\x' // illegal: too few hex digits
```

### String literals

String literals represent a zero-terminated, UTF-8 encoded byte array.

```
string_lit = `"` { ascii_char | escaped_char | byte_value } `"`
```

```c
"abc"
""
"Hello, world!\n"
"\xaa\xbb\xcc"
"\"  // Illegal: unterminated string
```

## Byte

There is only one datatype: the byte, an unsigned 8-bit
integer. Constants outside the range `0 ... 255` are considered a
boundary error.

A byte constant can be expressed either as an integer literal or a
character literal. Since the byte is the sole numeric data type, the
spec may say char, character, byte, integer, or number
interchangeably.

A byte is considered "truthy" if the value is nonzero; otherwise, it
is considered "falsy".

```
constant = char_lit | integer_lit ;
```

## Nodes

Nodes are the fundamental building block of noded. A program consists
of a series of nodes wired to each other between its ports.

### Processor Nodes

A processor node runs user-defined code. The code can use up to four
different *variables* to store state, and up to four different *ports* to
communicate between other nodes. **The code within a processor node
will run continuously** until it reaches a *halt statement*.

A processor node's *port* can only either be read from or written to. It
can never be both. Its *ports* can also only be attached to one
wire. It can also not be wired to another port of the same node. It
must also **always** be connected to another port.

A processor node is declared first by the `processor` keyword, then
the name of the node, and then a block containing its
code. Alternatively, a processor node can "copy" the logic from
another processor node:

```
processor_node_decl = new_processor_decl | processor_copy ;
new_processor_decl = "processor" name body;
processor_copy = "processor" name "=" source ";" ;

name = identifier ;
source = identifier ;
body = block_stmt ;

(* block_stmt will be defined in the Statements section of the spec *)
```

```c
processor a { ... }
processor b = a;

processor c = d; // Processors can copy from later-declared nodes
processor d { ... }

/* Note: an empty processor node, or any other processor node that
 * never halts or gets blocked, will run a busy loop that forces
 * the program to run forever.
 */
processor e {}

// Always send a constant value 1 to a write-only port %out.
processor constant {
    // The program can infer that %out is a write-only port
    // when it reads a send operation where %out is the
    // destination.
    %out <- 1;
}

// Receive a value from read-only port %in and relay it to a
// Write-only port %out.
processor relay {
    // This statement implies the variable $var and
    // the read-only port %in.
    $var <- %in;
    
    // This statement implies the write-only port
    // %out (the variable $var is already inferred from the
    // previous statement).
    %out <- $var;
}
```

A processor node has three execution states:
- Active: the node can continue executing its code.
- Blocked: the node is busy processing a send statement, but the
  port it wants to communicate with is not ready yet.
- Halted: the node reached the *halt statement* and no longer runs.

### Buffer Nodes

A buffer node holds an array of bytes and an index pointing to an
element of the array. Buffer nodes can be defined either with a
string literal, or an array literal:

```
buffer_node_decl = "buffer" name "="
                   ( string_lit | array_lit ) ";" ;
array_lit = "{" [ const_byte { "," const_byte } ]"}" ;

name = identifier ;
```

```c
/// All four declarations below are tautological.
buffer my_buf = "Hello, world!\n";

buffer my_buf = { 'H', 'e', 'l', 'l', 'o', ',', ' ', 'w', 'o', 'r',
                  'l', 'd', '!', '\n', 0 };
                  
buffer my_buf = { 0x48, 0x65, 0x6c, 0x6f, 0x2c, 0x20, 0x77, 0x6f,
                  0x72, 0x6c, 0x64, 0x21, 0x0a, 0 };
                  
buffer my_buf = { 72, 101, 108, 108, 111, 44, 32, 119, 111, 114,
                  108, 100, 33, 10, 0 };
```

A buffer node exposes itself through two read-write *ports*, `%idx`
and `%elm`. `%idx` represents the current index that points to the
array. `%elm` represents the element of the array that the index is
pointing to. In C-like terms, accessing `%idx` and `%elm` is like
accessing the variable `idx` and the expression `buffer[idx]`.

All buffers have a size of `256` elements, the maximum size of a byte
plus one. As such, there is no exceptional case where an element
out-of-bounds is accessed.

### Stack Nodes

A stack nodes holds a stack of bytes. It has a single read-write port,
`%elm`. Writing to `%elm` pushes a byte to the stack, and reading from
`%elm` pops a byte from the stack, and sends the value over.

If `%elm` is read when the stack is empty, then the port is blocked until
another node sends a value to the stack.

A stack's memory should grow dynamically to contain all values pushed to it.

```
stack_node_decl = "stack" name ";" ;
name = identifier ;
```

### Special Nodes

A Noded implementation may have multiple special nodes so that a
program may interact with its environment. The implementation within
this package currently has one node to communicate in a command-line
environment, the IO node.

#### The IO Node

The IO node has three ports, `%in`, `%out`, and `%err`. These are
similar to the Standard Input, Standard Output, and Standard Error
streams found in a POSIX environment.

The `%in` port is a read-only port that reads bytes from a standard
input stream. The input stream may close, in which case the `%in` port
will block.

The `%out` port is write-only that sends bytes to a standard output
stream. `%err` similarly writes to an error output stream.

This example program capitalizes all letters sent from `%in` and
relays the processed result to `%out`:

```c
processor capitalize {
    $chr <- %in;
    if ($chr >= 'a' && $chr <= 'z') {
        $chr -= 0x20;
    }
    %out <- $chr;
}

io.in -> capitalize.in;
capitalize.out -> io.out;
```

## Ports and Wires

Ports are a mechanism that provides inter-node communication via
sending and receiving values. Wires are bidirectional units of
communication that two ports use to send messages to each other.

In a Noded program, node declarations specify the ports that each node
has, and wire declarations connect the ports together. Nodes are the
heartbeat of the program, and as such, wires must be connected to
at least one processor node.

```
(* Within a processor node: *)
port = "%" letter {alphanumeric};

(*  A Wire declaration: *)
wire_decl = qualified_port "->" qualified_port ";" ;
qualified_port = node_name "." port_name ;

node_name = identifier ;
port_name = identifier ;
```

```
nodename.port -> othernode.port;
io.in -> io.out;
report.out -> buf.idx;

// Error: all nodes (and their ports) must exist.
nonexistingnode.a -> ...;
```

Nearly all ports are **blocking** -- that is, if a node writes a
message to a port, and the receiving end isn't ready, then the node
suspends execution -- or *blocks* -- until the message is read. This
can also happen the other way, where a node is reading from a port and
is blocked until that port receives a message.

For example:

```c
processor a {
    $var <- %in;
    if ($var % 2 == 0) {
        // Relay only even numbers
        %out <- $var;
    }
}

processor b {
    // Do nothing but consume messages.
    $var <- %in;
}

... -> a.in; // a's input comes from another node.
a.out -> b.in; // Connect a and b together.
```

The node `a`'s port `%out` sends messages to `b`'s port `%in`, as
declared in `a.out -> b.in;`. When the processor `b` runs its sole
statement, `%out <- %in;`, it will be blocked until processor `a`
receives an even number and sends it to `%out`.

## Variables

A variable is a unit of storage for holding a byte within a processor
node. A variable is prefixed with a dollar sign `$`, followed by an
alphanumeric sequence.

Variables don't have to be declared; they can be used immediately. All
variables are initialized with the value of `0` at the start of the
program, and the scope of all variables are local to the processor
it is used in.

```
variable = "$" letter { alphanumeric }
```

## Expressions

Primary expressions consist of variables, constants, and parenthesized
`(` expressions `)`.

The following table lists the precedence and associativity of all
operators. They are listed from top to bottom, in descending
precedence:

```
15  ++ --     Suffix increment and decrement   Left-to-right

14  ++ --     Prefix increment and decrement   Right-to-left
    + -       Unary positive and negative
    ! ~       Logical and binary not operator

13  * / %     Multiplicative binary operators  Left-to-right

12  + -       Additive binary operators

11  << >>     Bitwise shift operators

10  < <=      Relational operators < and ≤
    > >=      Relational operators > and ≥
 
 9  == !=     Equality operators = and ≠
 
 8  &         Bitwise operator and
 
 7  ^         Bitwise operator xor
 
 6  |         Bitwise operator or
 
 5  &&        Logical operator and
 
 4  ||        Logical operator or
 
 3  ?:        Ternary conditional              Right-to-left
 
 2  =         Simple assignment
    *= /= %=  Assignment by mult, div, or mod
    += -=     Assignment by sum or difference
    <<= >>=   Assignment by bitwise shift
    &= ^= |=  Assignment by bitwise and, xor or
    
 1  ,         Comma                            Left-to-right
    
```

```
assign_op = "=" | "+=" | "-=" | "*=" | "/=" | "%=" |
            ">>=" | "<<=" | "&=" | "^=" | "|=" ;
cmp_op = "<" | "<=" | ">" | ">=" ;
mul_op = "*" | "/" | "%" ;
prefix_expr = "++" | "--" | "+" | "-" | "!" | "~" ;

expr = list_expr ;
list_expr = assign_expr { "," assign_expr } ;
assign_expr = cond_expr { assign_op cond_expr } ;
cond_expr = lor_expr { "?" lor_expr ":" lor_expr } ;
lor_expr = land_expr { "||" land_expr } ;
land_expr = or_expr { "&&" or_expr } ;
or_expr = xor_expr { "|" xor_expr } ;
xor_expr = and_expr { "^" and_expr } ;
and_expr = eql_expr { "&" eql_expr } ;
eql_expr = cmp_expr { ( "=" | "!=" ) cmp_expr } ;
cmp_expr = shift_expr { cmp_op shift_expr } ;
shift_expr = add_expr { ( "<<" | ">>" ) add_expr } ;
add_expr = mul_expr { ( "+" | "-" ) mul_expr } ;
mul_expr = prefix_expr { mul_op prefix_expr } ;
prefix_expr = { prefix_op } suffix_expr ;
suffix_expr = primary_expr { suffix_op } ;
primary_expr = variable | constant | "(" expr ")" ;
```

All operators shown behave as they would in a C program.

*Constant expressions* have the same exact structure as normal
expressions, except that any primary expressions inside must not be
variables.

## Statements

Statements determine execution.

```
stmt = empty_stmt | expr_stmt | send_stmt | block_stmt |
       if_stmt | while_stmt | for_stmt |
       labeled_stmt | branch_stmt | halt_stmt ;
```

### Empty Statement

An empty statement does nothing.

```
empty_stmt = ";" ;
```

### Expression Statements

Expression statements evaluate the expression.

```
expr_stmt = expr ";" ;
```

### Send Statements

A send statement sends a message through a port.

```
send_stmt = ( variable "<-" port ) | ( port "<-" expr ) ;
```

Execution blocks until the send is processed.

### Block Statements

Block statements group together multiple statements to be treated like
one statement.

```
block_stmt = "{" { stmt } "}" ;
```

### If Statements

If statements specify the conditional execution of one of two possible
statements depending on the value of an expression. If the expression
evaluates to a truthy value, it executes the first
statement. Otherwise, if executes the "else" statement if it exists.

```
if_stmt = "if" "(" expr ")" stmt [ "else" stmt ] ;
```

Note that this can create an ambiguity:
```
if (1) then if (2) then %out <- 1; else %out <- 2;
```

In this case, who does the `else` branch belong to? In this case of
ambiguity, the language takes the side of C and assigns the
else-branch to the nearest if statement. So, the previous statement
becomes tautological to:

```
if (1) then {
    if (2) then {
        %out <- 1;
    } else {
        %out <- 2;
    }
}
```

### (Do-)While Statements

A do-while statement executes its body, and continues executing its
body until its conditional expression is falsy.

A while statement behaves like a do-while statement, except if the
conditional expression is falsy before the first iteration, then it
skips execution altogether.

```
do_while_stmt = "do" body "while" "(" cond ")" ";" ;
while_stmt = "while" "(" cond ")" body ;

body = stmt ;
cond = expr ;
```

### For Statements

A for statement specifies repeated execution of a block by augmenting
a while statement with an initial statement and post-iteration
statement.

The for statement executes its initial statement, then checks the
conditional expression. If truthy, it executes its body, executes the
post statement, then returns back to checking the conditional.

```
for_stmt = "for" "(" init_stmt ";" conditional ";" post_stmt ")" 
               body ;
init_stmt = stmt;
conditional = expr;
post_stmt = stmt;
body = stmt;
```

### Labeled Statements

A labeled statement prefixes a statement with a label to be jumped to
within the node.

```
labeled_stmt = label ":" stmt ;
label = identifier ;
```

### Branch Statements

Branch statements change the execution flow of a program.

```
branch_stmt = ( "break" | "continue" | ( "goto" label ) ) ";" ;
label = identifier ;
```

A `break` statement terminates execution of the innermost `for`,
`switch`, or `while` statement.

A `continue` statement skips the current iteration of the innermost
`for` or `while` statement. The post statement of a `for` loop is
still executed.

A `goto` statement redirects the flow of execution to the
corresponding label within the same node.

### Halt Statements

A halt statement stops execution of the node forever.

```
halt_stmt = "halt" ";" ;
```

## Program Execution

When a program starts, all processor nodes begin execution
concurrently. The program ends when all processor nodes are either
blocked or halted, i.e. there are no nodes able to run any more.

A complete program consists of a series of node and wire declarations:

```
program = { node_decl | wire_decl } ;
node_decl = processor_node_decl | buffer_node_decl | stack_node_decl ;
```
