# Nippy compiler prototype

Nippy is a small, statically typed language in the Lua/Luau syntactic family:
keyword-delimited blocks ending in `end`, no braces or semicolons, and type
annotations written with `:`. It is a typed relative of Lua, not a C-shaped
language with Lua spelling.

This first compiler accepts a sequence of top-level declarations: struct type
declarations, which emit C `typedef struct`s; type aliases and union types;
variable declarations; and function declarations, including generic ones. A single integer variable declaration still emits a
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
the left or right side must supply it. `Int32`, `Bool`, and `Nil` are the
built-in types, and a struct field may have any non-function value type. Field access does
not exist yet. Anything invalid is rejected with a diagnostic that names the
stage that refused it. Source-level diagnostics are prefixed with the source
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

Expressions are integer, boolean, and `nil` literals, names, calls, struct
constructors, arithmetic (`+`), comparisons (`<`, `<=`, `>`, `>=`, `==`, `!=`),
logical operators (`!`, `&&`, `||`), `.is<T>()` tests, and grouped expressions.
`Bool` is the built-in boolean type; `true` and `false` are its literals. `Nil`
is the unit type; `nil` is its only value.

## Sum types and generics

A type is a type expression. `type Name = Type` is a transparent alias; a union
is written with `|`; and generic declarations take type parameters:

```nippy
type MaybeInt = Int32 | Nil
type Option<T> = T | Nil

type Pair<A, B>
  first: A
  second: B
end

fun identity<T>(x: T): T
  return x
end

var a: MaybeInt = 3
var b: Option<Int32> = nil
var c = Pair(1, true)
var d: Int32 = identity(3)
```

Union alternatives are structural: `Int32 | Bool` and `Bool | Int32` are the
same type. A value of an alternative type is injected automatically where a
union is expected — an initializer, a return, a call argument, or a constructor
argument. There is no other implicit conversion.

`.is<T>()` tests the active alternative of a union and returns `Bool`:

```nippy
fun to_int(x: Int32 | Bool | Nil): Int32
  if x.is<Int32>() then
    return x
  elseif x.is<Bool>() then
    if x then return 1 else return 0 end
  else
    return 0
  end
end
```

Inside the then-block, `x` is narrowed to `T`. The else-block and the code after
the `if` keep the original union type.

Generic calls infer their type arguments from the value arguments, including
generic constructors (`Pair(1, true)` is `Pair<Int32, Bool>`). Each concrete
instantiation is monomorphized into generated C. A generic function cannot be
used as a value, and there is no explicit call-site type-argument syntax.

## Memory: Stores and handles

Dynamic memory is managed by arenas called **Stores**. A store is global and
declared with an initial byte size:

```nippy
var points: Store = Store(4096)

var p: Handle<Point> = points.alloc<Point>(Point(1, 2))
var n: Handle<Int32> = points.alloc<Int32>(13)
var b: Handle<Byte> = points.alloc<Byte>(7)

var q: Point = p.read()
p.write(Point(3, 4))

points.reset()
```

- `alloc<T>(value)` copies a single sized value `T` into the store and returns
  an opaque `Handle<T>` containing a store id, byte offset, and generation.
  Unions, function types, and `Store` are not allocation element types.
- `read()` / `write(value)` access the allocated value through the handle. Both
  check the store id, generation, and bounds and fail deterministically.
- `reset()` invalidates every handle from that store while keeping the arena's
  capacity for reuse.
- Stores grow by doubling, so the declared size is an initial reservation.
- `Byte` maps to `uint8_t`; integer literals are typed as `Byte` where a Byte is
  expected, and must be in `0..255`. Byte supports `==` and `!=` only.
- Stores are global and non-first-class: function bodies can name them, but a
  store cannot be passed, returned, copied, or stored in a field.

Call expressions can be used as statements at program level and inside function
bodies; the value is discarded:

```nippy
fun clear(): Nil
  points.reset()
  return nil
end
```

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
parent's run; generic type parameters use a separate `type_parameters` run.
Identifiers are stored inline in a separate symbol list, and nodes refer to them
by index, because a list element may not hold a pointer. The lexer returns a
list of tokens when lexing succeeds or a list of errors when it fails. List
elements are stored inline; pushing copies an element in, and popping copies it
out before removing it and shrinking the allocation. The checker resolves type
expressions (aliases, unions, Nil, generic applications), instantiates generic
types, monomorphizes generic functions, and checks injections and narrowing; the
analyser validates the typed program's links; the generator emits type
definitions, function typedefs, function definitions, and calls.

Five documents describe the compiler in more depth: `ARCH.md` (pipeline and
internals), `API.md` (the C API in `include/`), `SEMANTICS.md` (what a program
means and what is rejected), `GRAMMAR.ebnf` (the accepted grammar), and
`BUILD.md` (build, tests, and tooling).

Build the compiler with `make` and run both suites with `make test`. The core
compiler suite (`bin/compiler-tester`) compiles inline Nippy source through
`compiler_compile` and compares the generated C and header against text the test
owns. The tool suite (`bin/tool-tester`) covers the driver, file I/O, and the
`cc` invocation. `make test-compiler` and `make test-tool` run one suite each.

To compile a program by hand, pass the Nippy source path and output binary path:

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
