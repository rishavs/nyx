# Nippy semantics

What a program means, and which programs the compiler accepts. The accepted
syntax is in `GRAMMAR.ebnf`; the implementation is in `ARCH.md`.

This file describes the language **as implemented today**. Anything that is
merely intended is listed under [Not yet decided or not yet
implemented](#not-yet-decided-or-not-yet-implemented).

## Programs

A program is a sequence of top-level declarations, read in source order:

- a struct type declaration, `type Name ... end`;
- a value declaration, `var name [: Type] = expression`;
- a function declaration, `fun name(parameters): Type ... end`.

Declarations are processed in order, and a name must be declared before it is
used. There is no forward reference and no preprocessor. A function may refer to
itself, because its own name is bound before its body is checked.

A type, value, or function name may not be declared twice. Re-declaring a name
is an error. `Int32` and `Bool` are built in and cannot be redefined. Type names,
variable names, and function names share one ordinary namespace because they
become C identifiers.

There is no requirement that a program contain a value declaration. A program of
nothing but type declarations or function declarations is legal.

## Whitespace and newlines

Nippy is newline- and whitespace-agnostic, like Lua. Spaces, tabs, and newlines
only separate tokens; indentation is not syntax, there are no semicolons, and no
line continuation rule. A declaration or statement ends where the grammar says
it ends, not at a line boundary. The same program may be written on one line or
spread across many lines with arbitrary spacing between tokens.

## Syntax shape

The surface syntax belongs to the Lua/Luau family. Blocks are delimited by
keywords and closed with `end`; there are no braces or semicolons; `if` uses
`then`, `elseif`, `else`, and `end`; and types are attached to names with `:`.

Nippy is not a Lua dialect: a function declaration is `fun`, a value declaration
is `var`, blocks are not expressions, and the function type is written
`Fun<(Int32):Bool>` rather than Luau's `(Int32) -> Bool`. The shape is Lua-like
so that adding constructs such as loops, blocks, and declarations follows the
same pattern rather than introducing C-style braces.

## Types

Four kinds of type exist:

- **`Int32`** — the built-in signed 32-bit integer type. It maps to `int32_t` in
  generated C.
- **`Bool`** — the built-in boolean type, with the values `true` and `false`. It
  maps to C `bool`.
- **struct types** — declared by the program, with fields.
- **function types** — written `Fun<(Int32, Int32):Int32>`, naming the parameter
  types and the return type. `Fun<():Bool>` is a function type with no
  parameters.

A struct type has one or more fields, each of which is `name : Type`. Field
types may currently be `Int32` or `Bool`; a struct cannot yet contain a struct
or a function value, and there are no pointer or reference types yet.

Struct types are nominal: two structs with identical fields but different names
are different types and do not convert into each other. There is no structural
typing and no subtyping. Function types are structural: two function types with
the same parameter and return types are the same type regardless of where they
were written.

A function declaration's type is the function type built from its parameter and
return types. The type of

```text
fun adder(x:Int32, y:Int32):Int32
```

is `Fun<(Int32, Int32):Int32>`.

## Expressions and values

Expressions are:

- integer literals and boolean literals (`true`, `false`);
- names of variables, parameters, and functions;
- calls, `name(arguments)`;
- struct constructors, written as calls whose name resolves to a struct type;
- arithmetic, comparison, equality, and logical operators;
- grouped expressions, `( expression )`.

Operator precedence, tightest first:

| Level | Operators | Operand types | Result |
|---|---|---|---|
| unary | `!` | `Bool` | `Bool` |
| additive | `+` | `Int32`, `Int32` | `Int32` |
| relational | `<`, `<=`, `>`, `>=` | `Int32`, `Int32` | `Bool` |
| equality | `==`, `!=` | same type: `Int32` or `Bool` | `Bool` |
| logical and | `&&` | `Bool`, `Bool` | `Bool` |
| logical or | `\|\|` | `Bool`, `Bool` | `Bool` |

All binary operators are left-associative. `&&` and `||` short-circuit in
generated C. There is no implicit conversion. Addition wraps: generated C
performs the addition in unsigned arithmetic and converts the result back to
`int32_t`, so the language does not rely on signed overflow.

An integer literal is a decimal digit sequence that must fit in a signed 32-bit
`int`; an out-of-range literal is a lexer error rather than a wrap-around.

A name expression must resolve to a variable, parameter, or function. A type
name is not a value and is rejected when used without a call. A function name is
a first-class value of its function type.

Struct values are built with a constructor:

```text
Point(1, 2)
Point(x = 3, y = 4)
```

Rules, all enforced by the checker:

- The constructor name must name a declared struct type. `Int32(1)` and
  `Bool(true)` are errors, because neither is a struct.
- Arguments are **either** all positional **or** all named. Mixing them is a
  parser error.
- With positional arguments, the count must equal the field count and each
  argument supplies the field at that position.
- With named arguments, every field must be supplied exactly once. Arguments may
  appear in any order; the field's position, not the argument's position,
  decides where the value lands.
- Each argument may be any expression whose type matches its field's type.

Argument values are therefore reordered at compile time: `Point(y = 4, x = 3)`
and `Point(3, 4)` produce the same value, and generated C always writes
arguments in field order.

## Value declarations

```text
var x: Int32 = 13
var n = 13
var ok: Bool = true
var p1: Point = Point(1, 2)
var f : Fun<(Int32, Int32):Int32> = adder
```

The type annotation is optional. Exactly one of the two sides must determine the
type:

- with an annotation, the initialiser's type must be identical to it;
- without an annotation, the variable takes the initialiser's type.

There is no implicit conversion anywhere. `var x: SomeStruct = 13` is an error,
and so is `var x: Int32 = Point(1, 2)`.

A declaration is initialised at its declaration point. There is no
uninitialised variable and no default-initialisation. A program-level variable
may refer to variables and functions declared before it.

Program-level variables are local to the generated `main` (or to
`nippy_program_value` when the program is a single `Int32` variable). They are
therefore not visible inside function bodies yet.

## Function declarations

```text
fun classify(x:Int32):Int32
  if x > 0 then
    return 1
  elseif x == 0 then
    return 2
  else
    return 3
  end
end
```

A function declaration has a name, zero or more parameters, a required return
type, and a body containing zero or more statements. Each parameter is
`name : Type` and is in scope throughout the body. Parameters may have `Int32`,
`Bool`, struct, or function types. Parameter names within one function must be
unique.

The statements are:

- `return expression`;
- `if condition then ... elseif condition then ... else ... end`;
- `while condition ... end`;
- `break`;
- `continue`.

`if` conditions and `while` conditions must be `Bool`. `elseif` and `else` are
optional. `elseif` chains are equivalent to nested `if` statements in the
`else` branch.

`break` and `continue` are only valid inside a `while` body. They apply to the
innermost enclosing loop. `return` may appear anywhere in the body.

Every function must return on every path. A block returns if one of its
statements returns before control can fall out of the block. An `if` returns
only when it has an `else` and both branches return. A `while` never guarantees
a return. A function whose body may reach its end is rejected.

A function body cannot contain local variables, blocks, or assignment yet. It
also cannot refer to program-level variables. It can refer to its parameters, to
functions declared before it, and to itself. Because a function is bound before
its body is checked, direct recursion is accepted; mutual recursion is not,
because the later function is not declared yet.

Function calls are positional. Named arguments are accepted only by struct
constructors. The callee may be a function name or a variable/parameter whose
type is a function type. The call's argument count and argument types must match
the callee's function type.

## Evaluation and lifetime

A program's value declarations are evaluated in source order, once, before
`main`'s body runs in the generated C. Function declarations define code; a
function body is evaluated when it is called. There are no side effects other
than control flow yet, so the observable evaluation order is argument evaluation
from left to right, followed by the call.

An `if` evaluates its condition, then exactly one branch. A `while` evaluates its
condition before each iteration. `&&` and `||` short-circuit. A struct
constructor evaluates its arguments in the order written, then stores them in
field order.

All storage is program-level or parameter storage for now. The stack/heap
distinction, scopes, local storage, and lifetime regions are deliberately
deferred.

## The pointer model

Decided on paper, not implemented. Pointers are bidirectional references: a
variable knows which storage it points to, and that storage knows which variable
points to it. A variable may only point at storage that already exists (no
forward references, no uninitialised targets), and at most one variable may point
at a given storage at a time. When these rules land they belong in the analyser,
not in the parser or generator.

## What the generated C means

The generated C is internal IR, not a stable interface, but its behaviour is the
language's behaviour:

- Each struct type becomes a `typedef struct` with one member per field, in
  declaration order. Fields use `int32_t` or `bool`.
- Each function type used by the program becomes a function-pointer `typedef`
  named `nippy_fun_N`.
- Each function declaration becomes a C function with the same name, parameter
  names, and return type. Its body is the generated statement block.
- `if`, `elseif`, `else`, `while`, `break`, and `continue` map directly to the
  corresponding C statements. Conditions are C boolean expressions.
- Each value declaration becomes a local initialised by its expression. A
  single `Int32` variable declaration additionally emits `nippy_program_value()`,
  and `main` prints that value followed by a newline.
- Every other program's `main` returns 0 after performing the initialisations.
- Unused initialised variables get a `(void)name;` line so that the emitted C
  compiles without warnings. Unused parameters get the same treatment at the
  start of the function.
- Each declaration and statement is preceded by `#line <line> "<source>"`, so a
  C compiler diagnostic points at the Nippy source rather than the generated
  file.

## Diagnostics

Compilation is fail-closed. If any declaration or statement is invalid, the
whole program is rejected and no C is emitted; there is no partial output.

Source-level diagnostics carry `file:line:column (byte offset): ` followed by a
stage-prefixed message, for example:

```text
example.nip:1:9 (byte 8): checker: unknown type 'Float' for 'x'
```

The prefix identifies the source file, the 1-based line and column of the
offending construct, and the 0-based byte offset. Driver-level failures (a file
that cannot be read, an unwritable output path, a failing C compiler) are not
source-positioned but do name the path involved.

If compilation fails with no diagnostic recorded, the message is exactly
`Unknown Error`.

## Not yet decided or not yet implemented

- Local variables, assignment, blocks, scopes, and shadowing.
- Assignment statements and any way to mutate a parameter or variable.
- `for` loops and loop labels.
- Field access.
- Operators beyond the table above: subtraction, multiplication, division,
  modulo, bitwise operators, and unary minus are not implemented.
- A unit/void type and functions that return nothing.
- Closures and capturing program-level variables.
- Mutual recursion, because forward references are not accepted.
- The bidirectional pointer model and ownership checking.
- Concurrency and multithreading.
- Modules and imports.
- Whether a bare expression may be a whole program (today it is rejected: a
  program is a sequence of declarations).
- How identifiers avoid colliding with C keywords and generated names in
  emitted code. They are emitted unchanged, so a Nippy variable or function named
  `int` would produce invalid C, and a top-level name such as `main`,
  `nippy_program_value`, or `nippy_fun_0` is not rejected yet.
- Whether type resolution stays string comparison or becomes a real type table.
- Exact wording and formatting of diagnostics beyond the prefix described above.
