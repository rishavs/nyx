# Nippy compiler prototype

Nippy is a small, statically typed language in the Lua/Luau syntactic family:
keyword-delimited blocks ending in `end`, no braces or semicolons, and type
annotations written with `:`. It is a typed relative of Lua, not a C-shaped
language with Lua spelling.

This first compiler accepts a sequence of top-level declarations and
statements: struct type declarations, which emit C `typedef struct`s; type
aliases and union types; variable declarations; function declarations,
including generic ones; and assignments. A single integer variable declaration still emits a
program that prints its value; other programs declare their types, values, and
functions. Compiler modules live in `compiler/`, and public headers live in
`include/`. `compiler.c` calls the stages in order: lexer, parser, checker,
analyser, and generator. The compiler API accepts a source string and
configuration, then returns owned C and header source strings. `main.c` is the
command-line driver; it writes those strings and invokes `cc` to build the
requested executable.

A program is a sequence of top-level declarations and statements. Whitespace and newlines only
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
the left or right side must supply it. `Int32`, `Bool`, `Byte`, `Nil`,
`String`, `String<N>`, and `Error` are the built-in types, and a struct field may
have any non-function value type. Field access does
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

Dynamic memory is managed by arenas called **Stores**. A store is declared with
`global` and an initial byte size:

```nippy
global points: Store = Store(4096)

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
- Stores also create builders and dynamic lists: `s.builder(bytes)` and
  `s.list<T>(capacity)`.
- `global` requires the declared type to be `Store`. The older `var` spelling
  with a `Store` annotation is still accepted for compatibility; `global` is the
  canonical form.

Call expressions can be used as statements at program level and inside function
bodies; the value is discarded:

```nippy
fun clear(): Nil
  points.reset()
  return nil
end
```


## Assignment

Variables and parameters are mutable:

```nippy
var x: Int32 = 1
x = 2

fun bump(x: Int32): Int32
  x = x + 1
  return x
end
```

The target is a variable or parameter. The right-hand side is evaluated first,
then copied into the target. The value must match the target's declared type,
with the usual literal and union-injection rules:

```nippy
var b: Byte = 1
b = 200

var u: Int32 | Bool = 1
u = true

var name: String<8> = "nyx"
name = "hi"
```

Assignment is a statement, not an expression. There is no `+=`, no chained
assignment, and no assignment to stores, functions, or type names. Program-level
variables are assignable at program level, but not inside functions; functions
can assign their own parameters. Inside `if x.is<T>() then`, assigning to `x`
invalidates the narrowing for later statements.

## Lists

Fixed lists have an inline capacity and a runtime length up to that capacity:

```nippy
var xs: List<Int32, 3> = [1, 2]

var n: Int32 = xs.len()       // 2
var first: Int32 = xs[0]

xs.push(3)
xs[0] = 10

var last: Int32 | Nil = xs.pop()
```

- `List<T,N>` has capacity `N`; a literal may contain `0..N` elements.
- `push(value)` adds while `len() < N`; pushing at capacity is a runtime error.
- `pop()` removes the last element and returns `T | Nil`; an empty list returns
  `nil`.
- `xs[i]` reads an element and `xs[i] = value` writes one. Indices are `Int32`
  and bounds-checked.
- Fixed lists are inline values. Mutation through `push`, `pop`, and `xs[i] =`
  requires the list to be a variable or parameter, so the compiler can update
  the inline storage.
- `N` defaults to a maximum of 4096 and can be configured through
  `CompilerConfig.max_list_capacity`.

Dynamic lists are store-backed and growable:

```nippy
global arena: Store = Store(256)

var xs: List<Int32> = arena.list<Int32>(2)

xs.push(1)
xs.push(2)
xs[0] = 10

var n: Int32 = xs.len()       // 2
var first: Int32 = xs[0]      // 10
```

- `arena.list<T>(capacity)` allocates a dynamic list in the store.
- `push`, `pop`, `xs[i]`, and `len()` are available.
- Dynamic lists are mutable. Copying one copies the descriptor, and all copies
  alias the same list state, like `StringBuilder`.
- Dynamic `push` grows the buffer; old buffers remain until reset.
- `reset()` invalidates dynamic lists and their descriptors; access is a
  runtime stale check.

List literals are always fixed `List<T,N>`; create a dynamic list with
`arena.list<T>()`. A non-empty literal infers its type when every element has one
common type:

```nippy
var c = [1, 2, 3]       // List<Int32,3>
var d: List<Int32, 5> = [1, 2]  // capacity 5, length 2
```

Empty `[]` needs a fixed-list context. Mixed element types need an explicit
union annotation, for example `List<Int32 | Bool, 2> = [1, true]`.

Element types may be any concrete-sized value type, including structs, unions,
handles, strings, builders, lists, functions, and `Error`. `Store` is not a
value and is not listable. `pop` supports all of these element types, including
unions and functions; a union element produces the flattened `T | Nil` result.

## Slices

A slice is a read-only, non-owning view over part of a dynamic list:

```nippy
global arena: Store = Store(256)

var xs: List<Int32> = arena.list<Int32>(3)
xs.push(10)
xs.push(20)
xs.push(30)

var s: Slice<Int32> = xs.slice(1, 2)

var n: Int32 = s.len()   // 2
var first: Int32 = s[0]  // 20
```

- `xs.slice(start, length)` returns a `Slice<T>` view; it does not allocate.
- `Slice<T>` has `len()` and `[]` reads. It is read-only: no `push`, `pop`,
  `set`, or `[] =`.
- A slice of a slice is another view.
- Parent `xs[i] = value` writes are visible through a slice.
- Parent `push` may allocate a new buffer; the slice keeps reading the old
  buffer, which remains allocated until reset.
- `reset()` invalidates slices through the generation check.
- Slices are only for dynamic `List<T>`; fixed lists do not have stable
  addresses for a non-owning view, so `.slice` on `List<T,N>` is rejected.

## Strings and Error

### Dynamic strings

A `String` has a dynamic byte length. It is backed by a Store or by static
literal data:

```nippy
global arena: Store = Store(256)

var greeting: String = "hello"          // static, no allocation
var joined: String = arena.concat(greeting, " world")
var copy: String = arena.copy(joined)
var slice: String = joined.slice(0, 5)

var n: Int32 = joined.byte_len()
var b: Byte = joined.byte_at(0)
var same: Bool = joined == "hello world"
```

- Static literals need no Store. Arena-backed strings are allocated by
  `store.concat`, `store.copy`, and `store.string`.
- A `String` descriptor is copied by value and is immutable; slicing is a
  zero-copy view.
- `byte_len()`, `byte_at(i)`, `slice(start, length)`, and content equality work
  on both static and arena-backed strings.
- Arena strings are invalidated by `store.reset()`, like handles. Access is
  runtime-checked for store id, generation, and bounds.
- `String` and `String<N>` do not convert implicitly. Copy a fixed value with
  `arena.string(fixed)`.

A builder grows a string across many appends:

```nippy
global arena: Store = Store(256)

var b: StringBuilder = arena.builder(16)
b.append("hello")
b.append_byte(44)      // comma
b.append(" world")
var out: String = b.finish()

var len: Int32 = out.byte_len()   // 12
```

- `append`, `append_byte`, and `byte_len` operate on the builder.
- `finish()` copies the current bytes into a fresh immutable `String`; the
  builder remains usable.
- Copying a `StringBuilder` copies the descriptor and all copies alias the same
  state, so appends through one are visible through another.
- Builder buffers live in the Store and are reset with it.

### Fixed strings

A fixed string has a compile-time byte capacity and stores its bytes inline:

```nippy
var name: String<16> = "nyx"
var n: Int32 = name.byte_len()
var first: Byte = name.byte_at(0)
var same: Bool = name == "nyx"
```

- `String<N>` holds at most `N` bytes; the value carries a length and a NUL
  terminator, so it is directly usable from C.
- The capacity must be an integer literal in `0..4096` by default. It can be
  raised or lowered through `CompilerConfig.max_string_capacity`, up to a hard
  limit of 1 MiB.
- String literals are typed by context. `var s: String<16> = "nyx"` is fixed;
  `var s: String = "nyx"` is a static dynamic string. `var s = "nyx"` is
  rejected because it has no context.
- A literal longer than a fixed capacity, or a value of a different capacity,
  is rejected. `String` and `String<N>` do not convert implicitly.
- `byte_len()` is the byte length; `byte_at(i)` is bounds-checked and returns a
  `Byte`; `==`/`!=` compare content.
- A fixed string is a sized value, so it can be stored in a Store like any
  other value:

```nippy
global arena: Store = Store(64)
var h: Handle<String<16>> = arena.alloc<String<16>>("nyx")
var stored: String<16> = h.read()
```

`Error` is a built-in value with a fixed layout:

```nippy
var e: Error = Error("E_PARSE", "unexpected token", "main.nip", 3, 7, 42)

var line: Int32 = e.line()
var header: String<32> = e.header()
var description: String<128> = e.description()
```

It carries `header: String<32>`, `description: String<128>`,
`filename: String<128>`, and `line`/`col`/`pos: Int32` by default. The three
string capacities are configurable through `CompilerConfig`
(`error_header_capacity`, `error_description_capacity`,
`error_filename_capacity`); setting a field to `0` selects the default,
clamped to `max_string_capacity`. Field access does not
exist yet, so the fields are read through methods. `Error` can be stored,
passed, returned, put in a struct field, and used as a union alternative such as
`String<8> | Error`.

Dynamic strings, slicing, `store.concat`/`store.copy`, and `StringBuilder` are
implemented. C string projection and UTF-8 codepoint APIs are not.

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
