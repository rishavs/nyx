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
- a value declaration, `var name [: Type] = expression`;
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
have any non-function value type: `Int32`, `Bool`, `Nil`, a struct, a union, an
alias, or — inside a generic declaration — a type parameter. A function type is
not accepted as a field type or as a union alternative yet. A type may not
contain itself by value; such a recursive alias or struct is rejected.

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

- integer literals, boolean literals (`true`, `false`), and `nil`;
- names of variables, parameters, and functions;
- calls, `name(arguments)`;
- struct constructors, written as calls whose name resolves to a struct type;
- `.is<T>()` tests;
- store and handle methods: `s.alloc<T>(value)`, `s.reset()`, `h.read()`,
  `h.write(value)`;
- arithmetic, comparison, equality, and logical operators;
- grouped expressions, `( expression )`.

Operator precedence, tightest first:

| Level | Operators | Operand types | Result |
|---|---|---|---|
| is | `.is<T>()` | any value; `T` is a non-union type | `Bool` |
| unary | `!` | `Bool` | `Bool` |
| additive | `+` | `Int32`, `Int32` | `Int32` |
| relational | `<`, `<=`, `>`, `>=` | `Int32`, `Int32` | `Bool` |
| equality | `==`, `!=` | same type: `Int32`, `Bool`, or `Byte` | `Bool` |
| logical and | `&&` | `Bool`, `Bool` | `Bool` |
| logical or | `\|\|` | `Bool`, `Bool` | `Bool` |

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

### `.is<T>()`

`.is<T>()` asks whether a value's current alternative is `T` and returns `Bool`.
`T` must be a single non-union type.

- On a value whose type is exactly `T`, the result is statically `true`.
- On a value whose type is unrelated to `T`, the result is statically `false`.
- On a union value containing `T`, the result is a runtime test of the active
  alternative.

Inside `if name.is<T>() then`, where `name` is a simple identifier whose type is
a union containing `T`, `name` is **narrowed to `T` for the then-block only**:

```text
fun to_int(x: Int32 | Bool | Nil): Int32
  if x.is<Int32>() then
    return x                 // x has type Int32 here
  elseif x.is<Bool>() then
    if x then return 1 else return 0 end   // x has type Bool here
  else
    return 0
  end
end
```

The else-block and code after the `if` keep the original union type. Narrowing
does not flow through `!`, `&&`, `||`, or a non-identifier receiver. `elseif`
chains work because each condition is evaluated against the original union.

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

A declaration is initialised at its declaration point. There is no
uninitialised variable and no default-initialisation. A program-level variable
may refer to variables and functions declared before it.

Program-level variables are local to the generated `main` (or to
`nippy_program_value` when the program is a single `Int32` variable). They are
therefore not visible inside function bodies yet.

## Stores and handles

Dynamic memory is managed only by arenas, called **Stores**. A store is declared
at program level and is global:

```nippy
var points: Store = Store(4096)
```

- `Store(bytes)` takes a constant `Int32` expression, interpreted as bytes
  (literals and `+`). `Store(0)` is allowed.
- Stores are visible inside function bodies. This is the one exception to the
  rule that functions cannot see program-level variables: a store is a
  resource, not ordinary storage.
- Stores are not first-class: they cannot be passed, returned, stored in a
  field, used as a union alternative, or used as a value. Only `s.alloc` and
  `s.reset` take a store.
- The declared size is an initial reservation, not a cap. Stores grow by
  doubling as needed. `reset()` keeps the grown capacity.
- Stores are freed at program exit.

Allocation returns an opaque handle:

```nippy
var p: Handle<Point> = points.alloc<Point>(Point(1, 2))
var n: Handle<Int32> = points.alloc<Int32>(13)
var b: Handle<Byte> = points.alloc<Byte>(7)
```

`alloc<T>(value)` copies `value` into the arena and returns `Handle<T>`. `T`
must be a **single sized type**: `Int32`, `Bool`, `Byte`, `Nil`, a struct or
concrete generic struct, or a `Handle<U>`. Unions, function types, and `Store`
are rejected as allocation element types; a store may still hold values of many
different types, one allocation at a time.

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

A function body cannot contain local variables, blocks, or assignment yet. It
also cannot refer to program-level variables. It can refer to its parameters, to
functions declared before it, and to itself. Because a function is bound before
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
field order. Union injection stores a tag and the payload; `.is<T>()` reads the
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
- Union injection becomes a compound literal with a tag and payload; `.is<T>()`
  becomes a tag comparison or, when the answer is static, `true`/`false`;
  narrowing reads the payload member directly.
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
- Closures and capturing program-level variables.
- Mutual recursion, because forward references are not accepted.
- Explicit type arguments at generic call sites; generic calls infer them.
- `.is<T>()` where `T` is a union, and union-subset widening.
- Function-typed struct fields and union alternatives, because generated type
  definitions are emitted before the function-pointer typedefs they would need.
- Expected-type inference for generic constructors beyond the argument types;
  nested generic construction may need an intermediate annotated variable.
- Using a generic function as a first-class value.
- The bidirectional pointer model and general ownership checking. Stores are
  the implemented dynamic-memory model; there is no per-object free, no
  first-class store value, and no local store declaration yet.
- Exposing handle fields (`index`, `store`, `generation`); handles are opaque.
- Byte arithmetic, ordering, and conversion to/from Int32.
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
