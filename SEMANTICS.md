# Nippy semantics

What a program means, and which programs the compiler accepts. The accepted
syntax is in `GRAMMAR.ebnf`; the implementation is in `ARCH.md`.

This file describes the language **as implemented today**. Anything that is
merely intended is listed under [Not yet decided or not yet
implemented](#not-yet-decided-or-not-yet-implemented).

## Programs

A program is a sequence of items, read in source order:

- a struct type declaration, `type Name ... end`;
- a type alias, `type Name = Type`;
- a value declaration, `var name [: Type] = expression` or
  `const name [: Type] = expression`;
- a global store declaration, `global name: Store = Store(bytes)`;
- a function declaration, `fun name(parameters): Type ... end`;
- an expression statement, which must be a call, such as `points.reset()`.

Either type form may be generic: `type Name<T> = ...` or
`type Name<T> ... end`.

Declarations are processed in order, and a name must be declared before it is
used. There is no forward reference and no preprocessor. A function may refer to
itself, because its own name is bound before its body is checked. A generic
function's body is checked only when an instantiation is needed.

A type, value, or function name may not be declared twice. Re-declaring a name
is an error. `Int32`, `Bool`, and `Nil` are built in and cannot be redefined.
Type names, variable names, and function names share one ordinary namespace
because they become C identifiers.

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

A type is a type expression. The implemented forms are:

- **`Int32`** — the built-in signed 32-bit integer type. It maps to `int32_t` in
  generated C.
- **`Bool`** — the built-in boolean type, with the values `true` and `false`. It
  maps to C `bool`.
- **`Byte`** — the built-in unsigned 8-bit type, mapping to C `uint8_t`.
- **`Nil`** — the built-in unit type, with the single value `nil`.
- **`Store`** — a global byte arena. It is a declaration resource, not an
  ordinary passable value.
- **`Handle<T>`** — an opaque reference to one `T` allocated in a store.
- **`Text`** — a built-in dynamic-length, immutable byte string, backed by
  a Store or by static literal data. It carries a store id, offset, generation,
  and byte length; it does not hold a pointer.
- **`TextBuilder`** — a built-in mutable builder for dynamic strings. It is a
  store-backed value with `append`, `append_byte`, `byte_len`, and `finish`.
- **`List<T>`** — a built-in dynamic-length list backed by a Store. It is
  mutable and created with `store.list<T>(capacity)`.
- **`Array<T,N>`** — a built-in inline list with capacity `N` and a runtime
  length. It is mutable and initialized by a list literal.
- **`Slice<T>`** — a read-only, non-owning view over part of a dynamic
  `List<T>`. It has a store generation, offset, and length.
- **`Record<K,V,N>`** — a fixed inline hash record with fixed-string keys and
  at most `N` entries.
- **`Table<K,V>`** — a dynamic arena-backed hash table with fixed-string keys.
- **`String<N>`** — a built-in fixed-capacity string: at most `N` bytes stored
  inline in the value. `N` is an `Int32` byte capacity in
  `0..max_string_capacity` (default 4096).
- **`Error`** — a built-in fixed-layout error value with a short header, a
  description, a filename, and a source position.
- **struct types** — declared by the program, with fields, and nominal.
- **union types** — written `A | B | C`, structural.
- **function types** — written `Fun<(Int32, Int32):Int32>`, naming the parameter
  types and the return type. `Fun<():Bool>` is a function type with no
  parameters.
- **aliases** — `type Name = Type`, transparent: the alias is interchangeable
  with the type expression it names.
- **generic types** — a declaration with type parameters, applied as
  `Name<Int32, Bool>`.

A union's alternatives are flattened, deduplicated, and sorted into a canonical
order, so `Int32 | Bool` and `Bool | Int32` are the same type. A union with one
alternative is that type. Unions and function types are structural; structs are
nominal: two structs with identical fields but different names are different
types and do not convert into each other. Aliases are transparent, so they do
not create a new type.

A struct type has one or more fields, each of which is `name : Type`. A field may
have any non-function storable value type: `Int32`, `Bool`, `Nil`, a struct, a
union, an alias, or — inside a generic declaration — a type parameter. Function
types are not accepted as struct field types yet, but they are accepted as union
alternatives and collection elements.

Collections are `Array<T,N>`, `List<T>`, `Slice<T>`, `TextBuilder`,
`Record<K,V,N>`, and `Table<K,V>`.
`alloc<...>` and `Handle<...>` require a fixed type; they reject allocator-backed
values directly and transitively through structs/unions. `List`, `Text`, and
`TextBuilder` use their dedicated constructors. A collection cannot be an
`Array`/`List`/`Slice` element; wrap it in a named struct if it must be nested.
`Text` and `String<N>` are items: `List<Text>` is valid even though `Text` is
not fixed. A type may not contain itself by value;
such a recursive alias or struct is rejected.

Generic type parameters are written after the declaration name:

```text
type Option<T> = T | Nil

type Pair<A, B>
  first: A
  second: B
end

var p: Pair<Int32, Bool> = Pair(1, true)
```

A generic type must be instantiated where it is used; `Box` without arguments is
an error. Duplicate or unknown type parameters are errors. Each concrete
application is a distinct type and is monomorphized in generated C.

A function declaration's type is the function type built from its parameter and
return types. The type of

```text
fun adder(x:Int32, y:Int32):Int32
```

is `Fun<(Int32, Int32):Int32>`.

## Expressions and values

Expressions are:

- integer literals, string literals, list literals (`[a, b]`), boolean
  literals (`true`, `false`), and
  `nil`;
- names of variables, parameters, and functions;
- calls, `name(arguments)`;
- struct constructors, written as calls whose name resolves to a struct type;
- `typeof expression is Type` tests;
- `if` expressions and `match` expressions;
- store and handle methods: `s.alloc<T>(value)`, `s.reset()`, `h.read()`,
  `h.write(value)`;
- fixed-string methods: `s.byte_len()`, `s.byte_at(i)`;
- Error methods: `e.header()`, `e.description()`, `e.filename()`, `e.line()`,
  `e.col()`, `e.pos()`;
- arithmetic, comparison, equality, and logical operators;
- grouped expressions, `( expression )`.

Operator precedence, tightest first:

| Level | Operators | Operand types | Result |
|---|---|---|---|
| unary | `!` | `Bool` | `Bool` |
| unary | `-`, `~` | `Int32` | `Int32` |
| multiplicative | `*`, `/`, `%` | `Int32`, `Int32` | `Int32` |
| additive | `+`, `-` | `Int32`, `Int32` | `Int32` |
| shift | `<<`, `>>` | `Int32`, `Int32` | `Int32` |
| relational | `<`, `<=`, `>`, `>=` | `Int32`, `Int32` | `Bool` |
| equality | `==`, `!=` | same type: `Int32`, `Bool`, `Byte`, `Text`, or `String<N>` | `Bool` |
| bitwise and | `&` | `Int32`, `Int32` | `Int32` |
| bitwise xor | `^` | `Int32`, `Int32` | `Int32` |
| bitwise or | `\|` | `Int32`, `Int32` | `Int32` |
| logical and | `&&` | `Bool`, `Bool` | `Bool` |
| logical or | `\|\|` | `Bool`, `Bool` | `Bool` |

Integer arithmetic wraps on `+`, `-`, `*`, and unary `-`. Division and modulo
by a literal `0` is a compile-time error. A non-literal divisor is checked at
runtime and traps if it is zero. `INT32_MIN / -1` is a runtime error and
`INT32_MIN % -1` is `0`. `<<` and `>>` mask the shift count to 31.
All arithmetic is `Int32` only. `&`, `|`, `^`, and `~` are bitwise `Int32`.

All binary operators are left-associative. `&&` and `||` short-circuit in
generated C. Addition wraps: generated C performs the addition in unsigned
arithmetic and converts the result back to `int32_t`, so the language does not
rely on signed overflow.

An integer literal is a decimal digit sequence that must fit in a signed 32-bit
`int`; an out-of-range literal is a lexer error rather than a wrap-around.

An integer literal is typed as `Byte` when the expected type is `Byte`, or when
it is injected into a union that has `Byte` but no `Int32`. The literal must be
in `0..255`. This is contextual literal typing, not an implicit conversion;
`Byte` and `Int32` values do not convert into each other. `Byte` supports `==`
and `!=` (including against integer literals) but no arithmetic yet.

A string literal is a sequence of bytes between double quotes. The escapes are
`\n`, `\t`, `\r`, `\\`, and `\"`; a raw newline, a NUL byte, or any other
escape is rejected. Source bytes are UTF-8 by convention and are not validated
or normalized; literals pass through as bytes.

A string literal is typed by context. There is no standalone string literal
type: `var s = "x"` is rejected.

- In a `Text` context, the literal is a static dynamic string: its bytes live
  in generated read-only data and it needs no Store.
- In a `String<N>` context, the literal is a fixed inline value; it must fit `N`
  and the remaining capacity is padded.
- A literal longer than a fixed capacity is rejected.
- `Text` and `String<N>` do not convert implicitly. Use `store.text` to copy
  a fixed value into a dynamic string.
- A literal in a union is injected into the unique alternative that can hold it.
  Two fitting alternatives, such as `Text | String<8>`, are ambiguous and
  rejected.

A name expression must resolve to a variable, parameter, or function. A type
name is not a value and is rejected when used without a call. A non-generic
function name is a first-class value of its function type. A generic function
name is not a value; it must be called.

### Union injection

A value of an alternative type becomes a union value automatically where a
union is expected:

```text
var s: Int32 | Bool | Nil = 3
var t: Int32 | Bool | Nil = true
var u: Int32 | Bool | Nil = nil
```

Injection applies at:

- a variable initializer whose annotation is a union;
- a `return` expression whose function returns a union;
- a function call argument whose parameter is a union;
- a struct constructor argument whose field is a union.

This is widening, not a general conversion. There is no other implicit
conversion: `Int32` never becomes `Bool`, a union is not assignable to a
different union, and a value outside the union's alternatives is rejected.

### `typeof ... is ...`

`typeof value is T` asks whether a value's current alternative is `T` and
returns `Bool`. `T` must be a single non-union type. The old method form is removed.

- On a value whose type is exactly `T`, the result is statically `true`.
- On a value whose type is unrelated to `T`, the result is statically `false`.
- On a union value containing `T`, the result is a runtime test of the active
  alternative.

Inside `if typeof name is T then`, where `name` is a simple identifier whose type is
a union containing `T`, `name` is **narrowed to `T` for the then-block only**:

```text
fun to_int(x: Int32 | Bool | Nil): Int32
  if typeof x is Int32 then
    return x                 // x has type Int32 here
  elseif typeof x is Bool then
    if x then return 1 else return 0 end   // x has type Bool here
  else
    return 0
  end
end
```

The else-block and code after the `if` keep the original union type. Narrowing
does not flow through `!`, `&&`, `||`, or a non-identifier receiver. `elseif`
chains work because each condition is evaluated against the original union.

### `match`

`match` is a statement or expression with repeated `with` arms:

```nippy
match typeof x with Int32 then
  ...
with Bool then
  ...
else
  ...
end
```

Value patterns use equality:

```nippy
match x with true then
  ...
with false then
  ...
end
```

The scrutinee must be a simple identifier. Type arms narrow that identifier.
There is no fallthrough. Structural mutation during an arm is not a concern for
the scrutinee because the head is a variable.

Expression form:

```nippy
var z: Int32 = match typeof x with Int32 then 1 with Bool then 2 else 0 end
```

All expression arms must have the same type. `else` is required.

### Exhaustiveness

A statement match with no `else` is accepted only when it is exhaustive:

- a type match over a union must cover every alternative;
- a value match over `Bool` must cover `true` and `false`;
- a value match over `Nil` needs `Nil`;
- every other match needs `else`.

Duplicate type patterns are rejected. Expression matches still require `else`.

### `if` expression

```nippy
var z: Int32 = if typeof x is Int32 then 1 else 0 end
```

Each branch is one expression, all branches must have the same type, and `else`
is required.

### Value checks

Value conditions use ordinary equality:

```nippy
if x == true then ... end
match x with true then ... else ... end
```

### Struct constructors

Struct values are built with a constructor:

```text
Point(1, 2)
Point(x = 3, y = 4)
```

Rules, all enforced by the checker:

- The constructor name must name a declared struct type, including a concrete
  instantiation of a generic struct. `Int32(1)`, `Bool(true)`, and `Nil()` are
  errors, because none is a struct.
- Arguments are **either** all positional **or** all named. Mixing them is a
  parser error.
- With positional arguments, the count must equal the field count and each
  argument supplies the field at that position.
- With named arguments, every field must be supplied exactly once. Arguments may
  appear in any order; the field's position, not the argument's position,
  decides where the value lands.
- Each argument may be any expression that is assignable to its field's type,
  including an injected union alternative.

For a generic struct, type arguments are inferred from the constructor
arguments. `Pair(1, true)` infers `Pair<Int32, Bool>`. A type parameter that
cannot be inferred is an error. There is no explicit type-argument syntax on a
constructor call.

Argument values are reordered at compile time: `Point(y = 4, x = 3)` and
`Point(3, 4)` produce the same value, and generated C always writes arguments in
field order.

## Value declarations

```text
var x: Int32 = 13
var n = 13
var ok: Bool = true
var p1: Point = Point(1, 2)
var s: Int32 | Nil = 3
var f : Fun<(Int32, Int32):Int32> = adder
```

The type annotation is optional. Exactly one of the two sides must determine the
type:

- with an annotation, the initializer must be assignable to it (identical, or an
  injected union alternative);
- without an annotation, the variable takes the initializer's type.

There is no other implicit conversion. `var x: SomeStruct = 13` is an error, and
so is `var x: Int32 = Point(1, 2)`.

A declaration is initialised at its declaration point. `var` bindings are
mutable; `const` bindings cannot be reassigned. There is no uninitialised
variable and no default-initialisation.

`const` is binding immutability, not deep immutability:

```nippy
const Max: Int32 = 10
Max = 20              // rejected

const xs: List<Int32> = arena.list<Int32>(2)
xs.push(1)            // allowed: xs is not reassigned
```

Const declarations use the same type inference and scope rules as `var`.
Program-level consts are not visible inside functions unless passed as
arguments. `Store` still uses `global`, not `const`. A program-level variable
may refer to variables and functions declared before it.

Program-level variables are local to the generated `main` (or to
`nippy_program_value` when the program is a single `Int32` variable). They are
therefore not visible inside function bodies yet.


## Printing

`print` is a builtin statement, not a user-defined function:

```nippy
print("n =", n, "ok =", flag)
```

- Zero or more comma-separated arguments.
- Arguments are evaluated left to right.
- One space is written between arguments; a newline follows.
- `print()` writes one newline.
- `print` returns `Nil`.
- `print` is reserved and cannot be declared, stored, passed, or used as a
  value.

The builtin formats by the static argument type:

```text
Int32        decimal
Byte         0..255 decimal
Bool         true / false
Nil          nil
String<N>    raw bytes
Text         raw bytes
Error        Error(header, description, filename, line, col, pos)
struct       Name { field: value, ... }
union        active payload only
Array        [element, ...]
List         [element, ...]
Slice        [element, ...]
TextBuilder  current bytes
Handle       <handle store=N offset=N generation=N>
Function     <function>
```

Collections, struct fields, and union payloads are printed recursively.  A
pretty-print depth limit defaults to 16 and is configurable through
`CompilerConfig.max_print_depth`; deeper values print as `<...>`.

`print` does not interpret format strings. `print("%d", 3)` prints `%d 3`.

## For loops

A `for` statement iterates a list, slice, or string:

```nippy
for value in iterable do
  ...
end

for index, value in iterable do
  ...
end
```

- `List<T>` and `Array<T,N>` yield `T`.
- `Slice<T>` yields `T`.
- `String<N>` and `Text` yield `Byte`.
- The index, when present, is `Int32` and starts at `0`.
- The iterable expression is evaluated exactly once.
- The length is captured before the first iteration.
- The loop bindings are scoped to the loop body and may not shadow an existing
  visible name.
- `break` and `continue` apply to the innermost loop.
- `return` inside the loop returns from the enclosing function.

Iterating a list checks its length before each read and again after each body
execution. If `push` or `pop` changes the length, the program fails with a
deterministic runtime error instead of visiting a shifted range. Dynamic list
reads still check store id, generation, and bounds; string byte reads do the
same.

Iteration is by byte for strings; UTF-8 codepoint iteration is not implemented.
`map` and `filter` are not implemented.

## Local variables and blocks

Function bodies and `if`/`while` blocks may declare locals:

```nippy
fun sum_three(): Int32
  var a: Int32 = 1
  var b = 2
  if a < b then
    var c: Int32 = 3
    a = a + c
  end
  return a + b
end
```

- A local declaration has the same `var name [: Type] = expression` shape as a
  program-level declaration.
- Locals are scoped to the block that declares them, including `if`/`elseif`/
  `else` and `while` bodies.
- A local may not reuse a name already visible in the current function,
  including parameters, or any function/type/store/builtin name. Shadowing is
  currently rejected.
- A local's initializer is checked before the name is brought into scope, so it
  cannot refer to itself.
- `Store` declarations remain program-level only.
- Locals are assigned and passed by value like parameters.

## Field access

Struct fields are read and written with `.`:

```nippy
type Point
  x: Int32
  y: Int32
end

var p: Point = Point(1, 2)
var x: Int32 = p.x
p.x = 3
```

- Field reads work on struct values and on `Error` values:
  `e.header`, `e.description`, `e.filename`, `e.line`, `e.col`, `e.pos`.
- Field writes work on struct fields whose base is a variable/parameter or a
  nested field of one. `Point(1, 2).x = 3` and writes through temporary values
  are rejected.
- `Error` fields are read-only; construct a new Error to change it.
- Field access is not an lvalue for list elements: `xs[0].x` reads, but
  `xs[0].x = ...` is not supported yet.
- Field access is not a method call; `p.x` reads, `p.x()` is a method lookup.

## Assignment

A variable or parameter is mutable. Assignment is a statement:

```nippy
var x: Int32 = 1
x = 2

fun bump(x: Int32): Int32
  x = x + 1
  return x
end
```

- The target must be a variable, parameter, list index, or writable struct
  field in scope. Stores, functions, type names, `Error` fields, and arbitrary
  expressions are not assignable.
- Program-level variables are assignable at program level. Function bodies can
  assign their parameters, but cannot assign program-level variables because
  those names are not visible inside functions.
- The right-hand side is checked against the target's **declared** type. It may
  be identical, a union alternative injected into a union target, or a literal
  typed by context (`Byte`, `String<N>`). There is no general implicit
  conversion.
- A variable's type never changes. Assigning a different union alternative
  requires the variable to have a union type.
- The right-hand side is evaluated first, then copied into the target. Struct,
  `Text`, `TextBuilder`, `List<T>`, `Array<T,N>`, `String<N>`, `Error`,
  union, and `Handle<T>` values are assigned by value.
- Assignment is not an expression: `var y: Int32 = (x = 1)` is rejected. There
  is no chained assignment and no compound assignment such as `+=`.
- A variable must already be declared; assignment does not declare or infer a
  new variable.

### Assignment and narrowing

Inside `if typeof x is T then`, `x` is narrowed to `T`. An assignment to `x` is
allowed, but it invalidates that narrowing:

```nippy
if typeof x is Int32 then
  x = true
  if typeof x is Bool then
    ...
  end
end
```

The right-hand side of the assignment is checked under the old narrowing; after
the assignment, later uses see `x`'s declared union type again. This keeps
narrowing sound. Assigning to a variable that is not narrowed leaves other
narrowings intact.

## Stores and handles

Dynamic memory is managed only by arenas, called **Stores**. A store is declared
at program level with `global` and is visible everywhere:

```nippy
global points: Store = Store(4096)
```

- `global name: Store = Store(bytes)`. The annotation must be `Store`; a
  `global` declaration of any other type is rejected. The older `var ...: Store`
  spelling is still accepted for compatibility, but `global` is canonical.
- `Store(bytes)` takes a constant `Int32` expression, interpreted as bytes
  (literals and `+`). `Store(0)` is allowed.
- Stores are visible inside function bodies. This is the one exception to the
  rule that functions cannot see program-level variables: a store is a
  resource, not ordinary storage.
- Stores are not first-class: they cannot be passed, returned, stored in a
  field, used as a union alternative, or used as a value. Only `s.alloc`,
  `s.reset`, `s.concat`, `s.copy`, `s.text`, and `s.builder` take a store.
- The declared size is an initial reservation, not a cap. Stores grow by
  doubling as needed. `reset()` keeps the grown capacity.
- Stores are freed at program exit.

Allocation returns an opaque handle:

```nippy
var p: Handle<Point> = points.alloc<Point>(Point(1, 2))
var n: Handle<Int32> = points.alloc<Int32>(13)
var b: Handle<Byte> = points.alloc<Byte>(7)
```

`alloc<T>(value)` copies one fixed value into the arena and returns
`Handle<T>`. A fixed type contains no allocator-backed field:

```text
fixed: primitives, functions, Error, String<N>,
       Array<T,N> with fixed elements,
       structs/unions whose fields/alternatives are fixed
not fixed: Handle, Text, List, Slice, TextBuilder, Record, Table, Store
```

`Text`, `List`, and `TextBuilder` use their dedicated constructors.
`Array<T,N>` is fixed and can be allocated directly. A store may hold fixed
values of many different types, one allocation at a time.

`Handle<T>` records the store id, a byte offset, and the store generation. It is
opaque: there is no field access. Its operations are:

```nippy
var x: Point = p.read()
p.write(Point(3, 4))
```

- `p.read()` copies the current `T` out.
- `p.write(value)` copies a new `T` in; `value` must be assignable to `T`.
- Both check the store id, the store generation, and the offset bounds. A
  failure is a deterministic runtime diagnostic, not undefined behaviour.
- Handles survive growth because they hold offsets, not pointers.

`points.reset()` sets the used size to zero, increments the store generation,
and invalidates every handle from that store. The memory is retained and reused;
there is no per-object free. Using an invalidated handle is a runtime error
(`nippy: stale handle`).

Allocation failure (running out of memory while growing) aborts with
`nippy: out of memory`. Allocation never returns uninitialized memory.


## Lists

Collections split into fixed inline Arrays and mutable store-backed Lists.

### Fixed `Array<T,N>`

An Array has inline capacity `N` and a runtime length:

```nippy
var xs: Array<Int32, 3> = [1, 2]

var n: Int32 = xs.len()       // 2
var first: Int32 = xs[0]

xs.push(3)
xs[0] = 10

var last: Int32 | Nil = xs.pop()
```

- A list literal may supply `0..N` elements. The remaining capacity is
  zero-initialized and hidden by `len()`.
- `push(value)` requires `len() < N`; pushing at capacity is a runtime error.
- `pop()` returns `T | Nil`; popping an empty list returns `nil`. It decrements
  the length and leaves the removed slot allocated inline.
- `xs[i]` reads; `xs[i] = value` writes. The index is `Int32` and checked
  against the current length.
- `len()` returns the current length, which may be less than `N`.
- `Array<T,0>` is represented with a one-element dummy C array; `len()` is `0`;
  `push` and every index are out of bounds.
- Mutation (`push`, `pop`, `xs[i] =`) requires the Array expression to be a
  variable or parameter, because the inline length/data must be updated.
- `N` is in `0..max_list_capacity`. The default maximum is 4096;
  `CompilerConfig.max_list_capacity` can change it up to `1 MiB`.
- Arrays are mutable inline values; copies are independent.

### Dynamic `List<T>`

A dynamic list grows in a Store:

```nippy
global arena: Store = Store(256)

var xs: List<Int32> = arena.list<Int32>(2)

xs.push(1)
xs.push(2)
xs[0] = 10

var n: Int32 = xs.len()       // 2
var first: Int32 = xs[0]      // 10
var last: Int32 | Nil = xs.pop()
```

- `store.list<T>(capacity)` allocates the list state and initial buffer.
  Capacity may be `0`; negative capacity is a runtime error.
- `push(value)` adds one element and grows the buffer by doubling when needed.
  Old buffers remain in the store until reset.
- `pop()` removes the last element and returns `T | Nil`.
- `xs[i]` reads and `xs[i] = value` writes; both are bounds-checked.
- `len()` returns the current length.
- Dynamic lists are mutable. Copying a `List<T>` copies the descriptor and all
  copies alias the same list state, like `TextBuilder`.
- `reset()` invalidates the list descriptor and its elements; access is a
  runtime stale check.
- There is no per-list free.
- List literals are only valid for fixed `Array<T,N>`. A dynamic list must be
  created with `store.list<T>(capacity)`.

### Element types

Collection elements must be single items: `Int32`, `Bool`, `Byte`, `Nil`,
structs, unions, handles, `Text`, `String<N>`, function types, and `Error`.

- `Store` is not a value and is not listable.
- `Array`, `List`, `Slice`, and `TextBuilder` are collections and cannot be
  elements directly. Wrap a collection in a named struct to nest it.
- Arrays and Lists may hold function types; Array function elements get an
  inline element typedef in the generated header.
- `pop()` supports union and function elements. A union element is flattened
  into the result union, with payload tags remapped by the generated helper.

### Records and Tables

`Record<K,V,N>` is a fixed inline hash map:

```nippy
var x: Record<String<8>, Bool, 4> = {
  "a": true,
  "b": false
}
```

- `K` must be a fixed `String<N>` type.
- `N` is the maximum entry count.
- The literal is contextual: it needs a Record annotation.
- Duplicate literal keys are rejected.
- The literal may supply `0..N` entries.
- Keys are copied by value and compared by content.

Operations:

```text
x[key]        -> V; missing key is a runtime error
x[key] = value
x.has(key)    -> Bool
x.remove(key) -> Bool
x.len()       -> Int32
```

`Table<K,V>` is dynamic and arena-backed:

```nippy
var t: Table<String<8>, Int32> =
  arena.table<String<8>, Int32>(4)
```

- Created with `arena.table<K,V>(capacity)`.
- Grows by rehashing.
- Copies alias the same arena state.
- `reset()` invalidates it.
- Same operations as Record.

Both use FNV-1a hashing and linear probing. They print as
`{key: value, ...}`; hash order is unspecified. Iteration:

```nippy
for key, value in map do
  ...
end

for key in map do
  ...
end
```

The first form yields keys and values; the single-binding form yields keys.
Structural mutation of the map during iteration is not supported.

Records and Tables are collections: they cannot be direct elements of another
collection and cannot be `alloc`/`Handle` element types. Values and keys follow
the item rules.

### List literals

`[a, b, c]` is always an Array. It can be typed by an expected Array
context or inferred when every element has one common type:

```nippy
var xs: Array<Int32, 3> = [1, 2, 3]       // explicit context
var zs = [1, 2, 3]                       // inferred Array<Int32,3>
var strings = ["a", "b"]                 // inferred Array<Text,2>
var empty: Array<Int32, 0> = []           // empty needs a context
var mixed: Array<Int32 | Bool, 2> = [1, true]  // explicit union context
```

Elements are checked against the list's element type, including Byte, fixed
string, and union injection. The literal may be shorter than the capacity.
Inference fails for an empty literal, mixed element types without a union
context, or elements with no common type.

## Slices

A slice is a read-only, non-owning view over a dynamic list:

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

- `list.slice(start, length)` checks bounds and returns a `Slice<T>`.
- `slice.slice(start, length)` returns a subview.
- `Slice<T>` has `len()` and `[]` reads. It has no mutation operations.
- The view stores `{store, offset, length, generation}`. It does not allocate
  or own storage.
- Parent `set`/`xs[i] =` writes are visible through the view.
- Parent `push` may allocate a new buffer; the old buffer stays alive until
  reset, so the view still reads the old element range safely.
- `reset()` invalidates the view through the generation check.
- Arrays do not support `.slice`: their inline data has no stable
  address for a non-owning view. Copy the Array instead if a detached
  sequence is needed.

## Strings and Error

### `Text`

A dynamic string is an immutable byte sequence whose length is not part of its
type. It is backed by a Store or by static literal data:

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

- A `Text` value is a descriptor `{ store id, offset, byte length,
  generation, static data }`. It does not store a pointer in the source model.
- Static literals have a static data pointer and no store; they never become
  stale.
- Arena-backed strings are allocated with `store.copy`, `store.concat`, or
  `store.text`. They are invalidated by `store.reset()`, like handles.
- The descriptor is copied by value. Strings are immutable; slicing and copying
  cannot mutate an existing string.
- `byte_len()` is the byte length, not a codepoint count.
- `byte_at(i)` is bounds-checked and returns a `Byte`.
- `slice(start, length)` is a zero-copy view with the same backing store and
  generation. A slice is not independently NUL-terminated; only the original
  static/arena text has a terminator at its own end. C projection is not
  implemented yet.
- `==`/`!=` compare content. There is no implicit conversion between `Text`
  and `String<N>`.
- `Text` is a single item: it can be a field, union alternative, parameter,
  return type, or `List<Text>` element. It is not a fixed type, so
  `alloc<Text>` and `Handle<Text>` are rejected; use `store.text`.

Store methods:

```nippy
var a: Text = arena.copy("hello")       // allocate a copy
var b: Text = arena.concat(a, " world") // allocate concatenation
var f: String<8> = "hi"
var c: Text = arena.text(f)           // fixed -> dynamic, allocating
```

`concat` and `copy` require dynamic strings; a fixed string must be converted
explicitly with `store.text`. Allocation failure is fatal.

`Text` values are runtime-checked in the current model. Region/escape
checking, C string projection, and the `build ... do` block syntax are not
implemented yet.

### `TextBuilder`

A builder grows a byte sequence with amortized capacity and finishes with an
immutable `Text` copy:

```nippy
global arena: Store = Store(256)

var b: TextBuilder = arena.builder(16)
b.append("hello")
b.append_byte(44)      // comma
b.append(" world")
b.append_byte(33)      // exclamation mark

var n: Int32 = b.byte_len()
var s: Text = b.finish()
```

- `arena.builder(capacity)` allocates a builder state and an initial buffer in
  the store. Capacity may be `0`.
- `append(s)` appends a dynamic `Text` (or a literal typed as one).
- `append_byte(b)` appends one `Byte`.
- `byte_len()` returns the current length.
- `finish()` allocates a fresh immutable `Text` containing a copy of the
  current bytes. The builder remains usable afterward.
- `TextBuilder` is a value. Copying it copies the descriptor, and all copies
  alias the same builder state; appends through one copy are visible through
  another. This is memory-safe but deliberately shared, not unique ownership.
- Builder state and buffers live in the store. `reset()` invalidates the
  builder and every `Text` it produced; later use is a runtime stale check.
- Growth allocates a new buffer in the same store, copies the old bytes, and
  abandons the old buffer until reset. There is no per-builder free.
- Builder length is `Int32` on the surface; allocation failure is fatal.
- Strings are byte sequences. `append_byte(0)` can embed a NUL; methods use the
  stored length, so this is well-defined. C projection is not implemented and
  must not assume the first NUL is the end.

### `String<N>`

`String<N>` is the fixed string type. Bare `String` is rejected: it has no
capacity. Use `String<N>` for inline fixed text and `Text` for allocated or
static dynamic text.

A fixed string is an immutable value with capacity `N` bytes, stored inline:

```nippy
var name: String<16> = "nyx"
var n: Int32 = name.byte_len()
var b: Byte = name.byte_at(1)
var same: Bool = name == "nyx"
```

- `N` is a byte capacity in `0..max_string_capacity`. The default maximum is
  4096; `CompilerConfig.max_string_capacity` can raise or lower it up to the
  hard limit `NIPPY_MAX_STRING_CAPACITY` (1 MiB). `String<0>` can hold only the
  empty string.
- The value carries `N + 1` inline bytes and a length. The byte at
  `data[length]` is NUL, so the layout is directly usable from C; capacity bytes
  after the terminator are zero padding.
- `String<N>` values are copied by value. They do not allocate and are not
  arena-backed.
- `byte_len()` returns the byte length, not the capacity.
- `byte_at(i)` returns a `Byte` and checks `0 <= i < byte_len()`. An
  out-of-bounds index is a deterministic runtime error
  (`nippy: string index out of bounds`).
- `==` and `!=` compare content, but only between the same `String<N>` type.
  Different capacities are different types.
- `Text` and `String<N>` are distinct types; the only implemented conversion
  is `store.text(fixed)`.
- String literals are contextual, as described above.

A fixed string can be stored in a Store like any other sized value:

```nippy
global arena: Store = Store(64)
var h: Handle<String<16>> = arena.alloc<String<16>>("nyx")
var s: String<16> = h.read()
```

The arena slot contains the fixed string value, including its inline bytes.
`Handle<String<16>>` is an ordinary handle.

### `Error`

`Error` is a built-in fixed-layout value:

```text
type Error
  header: String<32>
  description: String<128>
  filename: String<128>
  line: Int32
  col: Int32
  pos: Int32
end
```

It is constructed positionally and its fields are read directly. The field
methods remain available:

```nippy
var e: Error = Error("E_CONCAT", "concat failed", "main.nip", 3, 7, 42)

var line: Int32 = e.line
var header: String<32> = e.header
var description: String<128> = e.description
```

The compiler can fill in the source position:

```nippy
var e: Error = Error.throw("E_CONCAT", "concat failed")
```

`Error.throw(header, description)` takes the two string fields and fills
`filename`, `line`, `col`, and `pos` from the source file and the call site.
The filename is the compiler's configured source name, truncated to the
configured filename capacity.

- `Error` is a sized value; it does not allocate.
- It can be passed, returned, stored in a Store, stored in a struct field, and
  used as a union alternative, for example `String<8> | Error`.
- `typeof e is Error` narrows a union alternative normally.
- `Error(...)` takes exactly `header`, `description`, `filename`, `line`,
  `col`, `pos`. `Error.throw(header, description)` fills the last four from the
  source position. Named arguments are not accepted.
- The field capacities default to `32` (header), `128` (description), and `128`
  (filename) bytes. `CompilerConfig.error_header_capacity`,
  `error_description_capacity`, and `error_filename_capacity` override them;
  `0` selects the default, clamped to `max_string_capacity`, and explicit values
  must be in `1..max_string_capacity`.
- There is no implicit conversion from a smaller fixed string, so pass a
  matching value or a literal.
- `Error` is a fixed-size inline value (about 300 bytes at the defaults) and is
  copied by value.

Dynamic strings and `TextBuilder` are described under
[Strings and Error](#strings-and-error). C string projection and the
`build ... as ... do` block are not implemented yet.

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
`name : Type` and is in scope throughout the body. Parameters may have any value
type. Parameter names within one function must be unique.

A function returning `Nil` returns `nil`:

```text
fun nothing(): Nil
  return nil
end
```

The statements are:

- `return expression`;
- `if condition then ... elseif condition then ... else ... end`;
- `while condition ... end`;
- `break`;
- `continue`;
- a call expression used as a statement, whose value is discarded.

An expression statement is accepted only when its expression is a call or a
member call; a bare value is rejected. The value is discarded. Call statements
are allowed at program level and inside function bodies.

`if` conditions and `while` conditions must be `Bool`. `elseif` and `else` are
optional. `elseif` chains are equivalent to nested `if` statements in the
`else` branch.

`break` and `continue` are only valid inside a `while` body. They apply to the
innermost enclosing loop. `return` may appear anywhere in the body.

Every function must return on every path. A block returns if one of its
statements returns before control can fall out of the block. An `if` returns
only when it has an `else` and both branches return. A `while` never guarantees
a return. A function whose body may reach its end is rejected.

A function body cannot declare local variables or contain bare nested blocks.
It can assign its parameters, but it cannot refer to or assign program-level
variables. It can
refer to its parameters, to functions declared before it, and to itself. Because a function is bound before
its body is checked, direct recursion is accepted; mutual recursion is not,
because the later function is not declared yet.

### Generic functions

A function declaration may declare type parameters:

```text
fun identity<T>(x: T): T
  return x
end

fun apply<T>(f: Fun<(T):T>, x: T): T
  return f(x)
end
```

A call infers the type arguments from its value arguments:

```text
var n: Int32 = identity(3)       // T = Int32
var m: Bool = identity(true)     // T = Bool
```

Rules:

- Every type parameter must be inferable from the argument types; otherwise the
  call is rejected. There is no explicit call-site type-argument syntax.
- Inference handles type parameters in plain type positions, struct
  applications, `Handle<T>`, and function types. A type parameter inside a union or alias
  whose remaining parameters are unbound is not an inference site.
- A generic function name cannot be used as a value.
- The body is checked once per concrete binding. A generic function that is
  never called produces no code and is not body-checked. Operations in the body
  are checked against the concrete types of each instantiation.
- Each binding is monomorphized into its own C function. Direct recursion works;
  mutual recursion is still unavailable because of the forward-reference rule.
- A generic function can call other generic functions and non-generic functions.

Function calls are positional. Named arguments are accepted only by struct
constructors. The callee may be a function name or a variable/parameter whose
type is a function type. The call's argument count and argument types must match
the callee's function type, with union injection allowed at each argument.

## Evaluation and lifetime

A program's value declarations are evaluated in source order, once, before
`main`'s body runs in the generated C. Function declarations define code; a
function body is evaluated when it is called. There are no side effects other
than control flow yet, so the observable evaluation order is argument evaluation
from left to right, followed by the call.

An `if` evaluates its condition, then exactly one branch. A `while` evaluates its
condition before each iteration. `&&` and `||` short-circuit. A struct
constructor evaluates its arguments in the order written, then stores them in
field order. Union injection stores a tag and the payload; `typeof x is T` reads the
tag and has no other side effect. A store allocation bumps the arena offset; a
reset discards every allocation in that store and bumps its generation.

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

- `Int32` becomes `int32_t`, `Bool` becomes `bool`, `Byte` becomes `uint8_t`,
  and `Nil` becomes a one-byte `nippy_nil` typedef.
- `Text` becomes a `nippy_string` descriptor with a static-data pointer, store
  id, byte offset, byte length, and generation. A static literal points at a
  generated read-only byte array. Arena-backed strings are allocated by the
  store runtime and checked for store id, generation, and bounds on access.
- `TextBuilder` becomes a `nippy_builder` descriptor pointing at an aligned
  builder-state record in the store. The state holds the buffer offset, current
  length, and capacity. Growth and finishing are runtime helpers.
- `Array<T,N>` becomes a `nippy_list_<index>` struct with an inline
  `T data[N]` array (`data[1]` for `N == 0`) and a `uint32_t length`.
- `List<T>` becomes a `nippy_list` descriptor pointing at store-backed state.
  The state holds the element-buffer offset, length, and capacity; push, pop,
  grow, index, and set are runtime helpers.
- `Slice<T>` becomes a `nippy_slice` descriptor with store id, offset, length,
  and generation. List/sub-slice creation and indexed reads are runtime
  helpers; no allocation occurs.
- `String<N>` becomes a `nippy_string_N` struct with a `uint8_t data[N+1]` array
  and a `uint32_t length`. A literal becomes a compound literal with its byte
  values, a NUL, zero padding, and the literal length.
- `Error` becomes a `nippy_error` struct containing the two fixed-string fields
  and the three `Int32` fields, in declaration order.
- Each struct type, and each concrete instantiation of a generic struct, becomes
  a `typedef struct` with one member per field, in declaration order.
- Each distinct union type becomes a tagged struct: a `uint32_t tag` and a union
  of the alternatives that carry payloads. Nil alternatives carry only a tag.
- Each function type used by the program becomes a function-pointer `typedef`
  named `nippy_fun_N`.
- Each non-generic function declaration becomes a C function with its source
  name. Each concrete instantiation of a generic function becomes a separate C
  function with a generated name.
- `if`, `elseif`, `else`, `while`, `break`, and `continue` map directly to the
  corresponding C statements. Conditions are C boolean expressions.
- Union injection becomes a compound literal with a tag and payload;
  `typeof x is T` becomes a tag comparison or, when the answer is static,
  `true`/`false`; narrowing reads the payload member directly.
- `match` and `if` expressions lower to nested conditional expressions; match
  statements lower to nested `if` statements.
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
- A store becomes an entry in a global `nippy_store` table. The generated source
  carries the arena helpers: initialize, grow, allocate, bounds/generation
  checks, write, reset, and free. `Handle<T>` becomes the opaque
  `nippy_handle` struct. Call statements become `(void)(call);`.
- Text helpers check `byte_at` bounds and compare bytes for equality. Error
  field methods become direct struct-member reads. The helper runtime is emitted
  only when the program uses a string or Error type.

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

- Compound assignments (`+=`, `-=`, etc.), chained assignment, and assignment
  as an expression.
- Loop labels.
- Operators beyond the table above: subtraction, multiplication, division,
  modulo, bitwise operators, and unary minus are not implemented.
- Closures and capturing program-level variables.
- Mutual recursion, because forward references are not accepted.
- Explicit type arguments at generic call sites; generic calls infer them.
- Union type patterns in `typeof`/`match`, and union-subset widening.
- Function-typed struct fields, because generated type definitions are emitted
  before the function-pointer typedefs they would need. Function-typed union
  alternatives use inline function-pointer payloads. Function types with
  function-typed parameters/returns are rejected by the inline C-type path;
  methods are the intended way to pass objects instead.
- Expected-type inference for generic constructors beyond the argument types;
  nested generic construction may need an intermediate annotated variable.
- Using a generic function as a first-class value.
- The bidirectional pointer model and general ownership checking. Stores are
  the implemented dynamic-memory model; there is no per-object free, no
  first-class store value, and no local store declaration yet.
- Exposing handle fields (`index`, `store`, `generation`); handles are opaque.
- Byte arithmetic, ordering, and conversion to/from Int32.
- UTF-8 validation, codepoint indexing, and string ordering/search.
- The `build ... as ... do` block syntax; `TextBuilder` is first-class now.
- C string projection (`cstr`), foreign string adoption, and explicit
  dynamic-to-fixed conversion.
- Region/escape checking for dynamic strings and lists; they are runtime-checked
  until scoped arenas exist.
- Array slices, dynamic list literals, and list equality.
- Scoped `access` blocks for reading store data; `read` currently copies.
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
