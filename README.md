# Nippy compiler prototype

Nippy is a small, statically typed language in the Lua/Luau syntactic family:
keyword-delimited blocks ending in `end`, no braces or semicolons, and type
annotations written with `:`. It is a typed relative of Lua, not a C-shaped
language with Lua spelling.

This first compiler accepts a sequence of top-level declarations: struct type
declarations, which emit C `typedef struct`s; variable declarations; and
function declarations. A single integer variable declaration still emits a
program that prints its value; other programs declare their types, values, and
functions. Compiler modules live in `compiler/`, and public headers live in
`include/`. `compiler.c` calls the stages in order: lexer, parser, checker,
analyser, and generator. The compiler API accepts a source string and
configuration, then returns owned C and header source strings. `main.c` is the
command-line driver; it writes those strings and invokes `cc` to build the
requested executable.

A program is a sequence of top-level declarations. Whitespace and newlines only
separate tokens: indentation is not syntax, there are no semicolons, and the same
program can be written on one line or many. A struct type declaration is:

```
type Point
  x: Int32
  y: Int32
end
```

A variable declaration may name its type or infer it from the initialiser:

```
var x : Int32 = 13
var n = 13
var p1: Point = Point(1, 2)
var p2: Point = Point(x = 3, y = 4)
var p3 = Point(6, 7)
```

Struct values are built with `Name(...)`: arguments may be positional or named,
but not mixed. Named arguments may appear in any order, and every field must be
supplied. When a variable omits its type, the initialiser's type is used; either
the left or right side must supply it. `Int32` and `Bool` are the built-in
scalar types, and for now struct fields must be `Int32` or `Bool`; field access
and struct-typed fields do not exist yet. Anything else is rejected with a diagnostic that names
the stage that refused it. Source-level diagnostics are prefixed with the source
file, line, column, and byte offset (`file:line:column (byte offset): `).

A function declaration names its parameters and return type, then returns one
expression:

```
fun adder(x:Int32, y:Int32):Int32
  return x + y
end
```

Calls are expressions, so a function can be used in a variable initialiser:

```
var sum : Int32 = adder(1, 2)
```

Functions are first-class values. The type of `adder` is
`Fun<(Int32, Int32):Int32>`, and it can be written wherever a type is expected:

```
fun apply(f:Fun<(Int32, Int32):Int32>, x:Int32, y:Int32):Int32
  return f(x, y)
end

var add : Fun<(Int32, Int32):Int32> = adder
var sum2 : Int32 = apply(add, 1, 2)
```

Expressions are integer and boolean literals, names, calls, struct constructors,
arithmetic (`+`), comparisons (`<`, `<=`, `>`, `>=`, `==`, `!=`), logical
operators (`!`, `&&`, `||`), and grouped expressions. `Bool` is the built-in
boolean type; `true` and `false` are its literals.

Function bodies contain statements:

```nippy
fun classify(x:Int32):Int32
  if x > 0 then
    return 1
  elseif x == 0 then
    return 2
  else
    return 3
  end
end

fun firstPositive(x:Int32):Int32
  while true
    if x > 0 then
      break
    end
    continue
  end
  return 0
end
```

`if` conditions and `while` conditions must be `Bool`. `break` and `continue`
apply to the innermost `while`. Every function must return on every path.
Functions may call earlier functions and themselves, but a function body cannot
see program-level variables yet. There are no local variables, assignment,
blocks, or scopes inside a function yet.

The parser returns a flattened AST: nodes are stored inline in a list, and
child links are node indices. Variable-arity children are indices in a separate
`children` list so an expression can push its own nodes without breaking a
parent's run. Identifiers are stored inline in a separate symbol list, and nodes
refer to them by index, because a list element may not hold a pointer. The lexer
returns a list of tokens when lexing succeeds or a list of errors when it fails.
List elements are stored inline; pushing copies an element in, and popping
copies it out before removing it and shrinking the allocation. The checker
resolves type names, function signatures, and names; the analyser validates the
typed program's links; the generator emits function typedefs, function
definitions, and calls.

Five documents describe the compiler in more depth: `ARCH.md` (pipeline and
internals), `API.md` (the C API in `include/`), `SEMANTICS.md` (what a program
means and what is rejected), `GRAMMAR.ebnf` (the accepted grammar), and
`BUILD.md` (build, tests, and tooling).

Build the compiler with `make`, then compile the original example with
`make example`. The compiler and generated program go in `bin/`; run the example
with `./bin/13`. `examples/adder.nip` is a runnable function example,
`examples/control.nip` exercises `if`/`elseif`/`else`, and
`examples/loop.nip` exercises `while`, `break`, and `continue`:

```sh
./bin/nippy examples/adder.nip bin/adder
./bin/adder
./bin/nippy examples/control.nip bin/control
./bin/control
./bin/nippy examples/loop.nip bin/loop
./bin/loop
```

To compile another literal, pass the Nippy source path and output binary path:

```sh
./bin/nippy path/to/source.nip bin/program
```

The compiler writes the generated C source beside the binary as
`bin/program.c`, then invokes `cc -std=c11` to build it. The generated C is
internal IR, not a product: it is not required to be human-readable or
formatted. It carries a `#line` directive pointing back at the Nippy
declaration, so the C compiler reports the original source location. If
compilation fails without a diagnostic having been reported, the fallback
message is `Unknown Error`.
