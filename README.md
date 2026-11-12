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
program can be written on one line or many. Names are emitted into C unchanged,
so C11 keywords, names used by the generated code (`main`, `int32_t`, `printf`,
and similar), and names beginning with `nyx_` are reserved and rejected. A
struct type declaration is:

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

`const` declares an immutable binding:

```nippy
const Max: Int32 = 10
const local = "nyx"

Max = 20   // rejected
```

A const binding cannot be reassigned. Values inside a mutable value can still
change:

```nippy
const xs: List<Int32> = arena.list<Int32>(2)
xs.push(1)   // allowed: the binding is unchanged
```

Program-level consts follow the same visibility rule as other program-level
variables: they must be passed into functions explicitly.

Struct values are built with `Name(...)`: arguments may be positional or named,
but not mixed. Named arguments may appear in any order, and every field must be
supplied. When a variable omits its type, the initialiser's type is used; either
the left or right side must supply it. `Int32`, `Bool`, `Byte`, `Nil`,
`Store`, `Handle<T>`, `Text`, `String<N>`, `TextBuilder`, `List<T>`,
`Array<T,N>`, `Slice<T>`, `Record<K,V,N>`, `Table<K,V>`, and `Error` are the
built-in types, and a struct field
may have any non-function value type. Struct and `Error` fields are read with
`.`; struct fields of variables/parameters are writable. Anything invalid is rejected with a diagnostic that names the
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
constructors, arithmetic (`+`, `-`, `*`, `/`, `%`, unary `-`), bitwise (`&`, `|`, `^`, `~`,
`<<`, `>>`), comparisons (`<`, `<=`, `>`, `>=`, `==`, `!=`),
logical operators (`!`, `&&`, `||`), `typeof x is T` tests, `if` expressions,
`match` expressions, and grouped expressions.
`Bool` is the built-in boolean type; `true` and `false` are its literals. `Nil`
is the unit type; `nil` is its only value.

## Local variables and fields

Function bodies and `if`/`while` blocks can declare locals:

```nippy
fun score(): Int32
  var p: Point = Point(1, 2)
  p.x = 3
  var xs: Array<Int32, 2> = [10, 20]
  xs[1] = 30
  return p.x + p.y + xs[0] + xs[1]
end
```

Locals are block-scoped. A local may not shadow a parameter or another visible
local/function/type/store/builtin name. `Store` declarations stay program-level.

Struct fields are accessed directly:

```nippy
var p: Point = Point(1, 2)
var x: Int32 = p.x
p.x = 3
```

`Error` fields are readable (`e.line`, `e.header`, ...) but not writable. Field
writes require a variable/parameter field chain; writes through temporaries or
list elements are not supported yet.

## For loops

`for` iterates Arrays, dynamic arena lists, slices, fixed strings, and
dynamic strings:

```nippy
fun sum(): Int32
  var xs: Array<Int32, 3> = [1, 2, 3]
  var total: Int32 = 0

  for x in xs do
    total = total + x
  end

  return total
end
```

An optional index binding comes first:

```nippy
for i, x in xs do
  ...
end
```

- Over lists and slices, the element type is the element type.
- Over `String<N>` and `Text`, the element type is `Byte`.
- The index is `Int32`.
- The iterable expression is evaluated once.
- The length is snapshotted at loop entry.
- Changing the length of an iterated list inside the loop fails deterministically.
- `break` and `continue` apply to the innermost loop.
- The loop bindings are scoped to the loop body.

Text iteration is by byte:

```nippy
fun count(): Int32
  var s: String<8> = "hello"
  var n: Int32 = 0

  for b in s do
    n = n + 1
  end

  return n
end
```

Arena lists and arena texts iterate through their store checks:

```nippy
global arena: Store = Store(256)

fun name_bytes(): Int32
  var a: String<8> = "ada"
  var b: String<8> = "grace"
  var names: List<Text> = arena.list<Text>(2)
  names.push(arena.text(a))
  names.push(arena.text(b))

  var total: Int32 = 0
  for name in names do
    total = total + name.byte_len()
  end
  return total
end
```

`map`/`filter` pipelines are not implemented.

## Printing

`print` is a builtin statement that takes zero or more comma-separated values:

```nippy
print("hello")
print("n =", n)
print("point =", p)
print([1, 2, 3])
```

- Arguments are evaluated left to right.
- One space separates arguments.
- A newline is written at the end.
- `print()` writes only a newline.
- `print` is not a value and cannot be stored or passed.

Pretty output for values:

```text
Int32       42
Byte        200
Bool        true
Nil         nil
String<N>   ada
Text        ada
Error       Error(E, desc, file, 3, 7, 42)
struct      Point { x: 4, y: 5 }
Array       [1, 2, 3]
List        [10, 20]
Slice       [10, 20]
TextBuilder hi
Handle      <handle store=0 offset=48 generation=1>
Function    <function>
```

Unions print their active payload. Collections and structs print recursively.
The pretty-print depth limit defaults to 16 and is configurable through
`CompilerConfig.max_print_depth`; deeper values print as `<...>`.

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

`typeof x is T` tests the active alternative of a union and returns `Bool`:

```nippy
fun to_int(x: Int32 | Bool | Nil): Int32
  if typeof x is Int32 then
    return x
  elseif typeof x is Bool then
    if x then return 1 else return 0 end
  else
    return 0
  end
end
```

Inside the then-block, `x` is narrowed to `T`. The else-block and the code after
the `if` keep the original union type. The old method form is gone.

`match` must be exhaustive: a union type match must cover every alternative, a
`Bool` value match must cover `true` and `false`, and every other form needs an
`else` arm unless it is already exhaustive.

`match` is a multi-arm form of the same tests:

```nippy
match typeof x with Int32 then
  ...
with Bool then
  ...
else
  ...
end
```

Value checks use normal equality:

```nippy
if x == true then ... end

match x with true then
  ...
with false then
  ...
end
```

`if` and `match` also have expression forms. Every expression form currently
needs an `else`:

```nippy
var z: Int32 = if typeof x is Int32 then 1 else 0 end
var w: Int32 = match x with true then 1 else 0 end
```

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

- `alloc<T>(value)` copies a fixed value `T` into the store and returns an
  opaque `Handle<T>` containing a store id, byte offset, and generation.
  `Handle`, `Text`, `List`, `Slice`, `TextBuilder`, and `Store` are not
  fixed types. `List`, `Text`, and `TextBuilder` use their dedicated
  constructors; `Array<T,N>` is fixed and may be allocated.
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
can assign their own parameters. Inside `if typeof x is T then`, assigning to `x`
invalidates the narrowing for later statements.

## Arrays and Lists

Arrays have an inline capacity and a runtime length up to that capacity:

```nippy
var xs: Array<Int32, 3> = [1, 2]

var n: Int32 = xs.len()       // 2
var first: Int32 = xs[0]

xs.push(3)
xs[0] = 10

var last: Int32 | Nil = xs.pop()
```

- `Array<T,N>` has capacity `N`; a literal may contain `0..N` elements.
- `push(value)` adds while `len() < N`; pushing at capacity is a runtime error.
- `pop()` removes the last element and returns `T | Nil`; an empty list returns
  `nil`.
- `xs[i]` reads an element and `xs[i] = value` writes one. Indices are `Int32`
  and bounds-checked.
- Arrays are inline values. Mutation through `push`, `pop`, and `xs[i] =`
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
  alias the same list state, like `TextBuilder`.
- Dynamic `push` grows the buffer; old buffers remain until reset.
- `reset()` invalidates dynamic lists and their descriptors; access is a
  runtime stale check.

List literals are always `Array<T,N>`; create a dynamic list with
`arena.list<T>()`. A non-empty literal infers its type when every element has one
common type:

```nippy
var c = [1, 2, 3]       // Array<Int32,3>
var d: Array<Int32, 5> = [1, 2]  // capacity 5, length 2
```

Empty `[]` needs an Array context. Mixed element types need an explicit
union annotation, for example `Array<Int32 | Bool, 2> = [1, true]`.

Element types must be single items: primitives, structs, unions, functions,
handles, fixed strings, `Text`, and `Error`. Collections are not elements:
`Array`, `List`, `Slice`, and `TextBuilder` are rejected. Store is not a
value and is not listable. To nest a collection, wrap it in a named struct.

`alloc<T>` and `Handle<T>` require a **fixed type**. Fixed types contain no
allocator-backed fields: primitives, function types, `Error`, `String<N>`,
`Array<T,N>` whose elements are fixed, and structs/unions whose fields or
alternatives are fixed. `Handle`, `Text`, `List`, `Slice`, `TextBuilder`,
`Record`, and `Table` are not fixed. `List`, `Text`, and `TextBuilder` have dedicated constructors;
`Array` is fixed and uses `alloc` when arena storage is required.

Collection elements remain single items, so `List<Text>` and
`List<StructWithText>` are allowed even though `alloc<Text>` and
`alloc<StructWithText>` are rejected. `Record` and `Table` are collections and
cannot be direct elements of another collection. Wrap a collection in a struct only when
it is an element; that does not make the wrapper allocatable.

## Records and Tables

A fixed record is an inline hash map with a maximum entry count:

```nippy
var x: Record<String<8>, Bool, 4> = {
  "a": true,
  "b": false
}

var a: Bool = x["a"]
x["c"] = true
var present: Bool = x.has("a")
var removed: Bool = x.remove("b")
var n: Int32 = x.len()
```

- Keys are fixed `String<N>` values only.
- `N` is the maximum number of entries.
- The literal is required to fit `N`.
- Duplicate literal keys are rejected.
- A Record is an inline value; copies are independent.
- `x[key]` panics if the key is missing. Use `has` first when absence is expected.
- `remove` returns whether the key existed.
- Hashing is FNV-1a with linear probing.

A dynamic table is arena-backed:

```nippy
global arena: Store = Store(1024)

var t: Table<String<8>, Int32> =
  arena.table<String<8>, Int32>(4)

t["a"] = 1
var n: Int32 = t.len()
```

- `arena.table<K,V>(capacity)` creates a Table.
- Tables grow by rehashing.
- Copies alias the same table state.
- `reset()` invalidates a Table.

Both print as `{key: value, ...}`. Iteration yields key/value pairs:

```nippy
for key, value in x do
  ...
end
```

A single binding iterates keys:

```nippy
for key in x do
  ...
end
```

Hash order is unspecified. Mutating a Record or Table structurally during
iteration is not supported.

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
- Slices are only for dynamic `List<T>`; Arrays do not have stable
  addresses for a non-owning view, so `.slice` on `Array<T,N>` is rejected.

## Strings and Error

### Text

A `Text` has a dynamic byte length. It is backed by a Store or by static
literal data:

```nippy
global arena: Store = Store(256)

var greeting: Text = "hello"          // static, no allocation
var joined: Text = arena.concat(greeting, " world")
var copy: Text = arena.copy(joined)
var slice: Text = joined.slice(0, 5)

var n: Int32 = joined.byte_len()
var b: Byte = joined.byte_at(0)
var same: Bool = joined == "hello world"
```

- Static literals need no Store. Arena-backed strings are allocated by
  `store.concat`, `store.copy`, and `store.text`.
- A `Text` descriptor is copied by value and is immutable; slicing is a
  zero-copy view.
- `byte_len()`, `byte_at(i)`, `slice(start, length)`, and content equality work
  on both static and arena-backed strings.
- Arena texts are invalidated by `store.reset()`, like handles. Access is
  runtime-checked for store id, generation, and bounds.
- `Text` and `String<N>` do not convert implicitly. Copy a fixed value with
  `arena.text(fixed)`.

A builder grows a string across many appends:

```nippy
global arena: Store = Store(256)

var b: TextBuilder = arena.builder(16)
b.append("hello")
b.append_byte(44)      // comma
b.append(" world")
var out: Text = b.finish()

var len: Int32 = out.byte_len()   // 12
```

- `append`, `append_byte`, and `byte_len` operate on the builder.
- `finish()` copies the current bytes into a fresh immutable `Text`; the
  builder remains usable.
- Copying a `TextBuilder` copies the descriptor and all copies alias the same
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

- `String<N>` is the fixed string type; bare `String` is rejected because it
  has no capacity. Use `String<N>` for fixed text and `Text` for allocated or
  static dynamic text.
- `String<N>` holds at most `N` bytes; the value carries a length and a NUL
  terminator, so it is directly usable from C.
- The capacity must be an integer literal in `0..4096` by default. It can be
  raised or lowered through `CompilerConfig.max_string_capacity`, up to a hard
  limit of 1 MiB.
- String literals are typed by context. `var s: String<16> = "nyx"` is fixed;
  `var s: Text = "nyx"` is a static dynamic string. `var s = "nyx"` is
  rejected because it has no context.
- A literal longer than a fixed capacity, or a value of a different capacity,
  is rejected. `Text` and `String<N>` do not convert implicitly.
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

var line: Int32 = e.line
var header: String<32> = e.header
var description: String<128> = e.description
```

The compiler can fill the source position:

```nippy
var e: Error = Error.throw("E_PARSE", "unexpected token")
```

`Error.throw(header, description)` uses the configured source filename and the
call-site line, column, and byte offset for the other four fields.

It carries `header: String<32>`, `description: String<128>`,
`filename: String<128>`, and `line`/`col`/`pos: Int32` by default. The three
string capacities are configurable through `CompilerConfig`
(`error_header_capacity`, `error_description_capacity`,
`error_filename_capacity`); setting a field to `0` selects the default,
clamped to `max_string_capacity`. `Error` fields are readable directly
(`e.line`, `e.header`, ...) and the field methods remain available; fields are
not writable. `Error` can be stored, passed, returned, put in a struct field,
and used as a union alternative such as `String<8> | Error`.

Dynamic strings, slicing, `store.concat`/`store.copy`, and `TextBuilder` are
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
see ordinary program-level variables yet; stores remain visible. Function
bodies support local variables, block-scoped `if`/`while` blocks, assignment,
field access, and list indexing.

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
