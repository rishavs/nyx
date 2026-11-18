# Nyx semantics

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
- a scoped store, `scope name = Store(capacity) do ... end`;
- a function declaration, `fun name(parameters): Type ... end`;
- an `if`, `match`, `for`, or `while` statement;
- an assignment, including indexed and field assignments;
- an expression statement, which must be a call, such as `print(x)`.

`while` and the other block statements are valid at program level, inside a
function body, or inside another block.

Recursive syntax nesting is limited to 256 levels, and the parsed syntax tree
is limited to a depth of 1024. Exceeding either limit is a compile-time error.

Either type form may be generic: `type Name<T> = ...` or
`type Name<T> ... end`.

Comments are ignored between tokens. `--` starts a line comment, and `--[` …
`]--` delimits a multiline comment.

Type names are available throughout the program, including before their source
declarations. Values and functions are otherwise processed in declaration
order. There is no preprocessor. A function may refer to itself, because its own
name is bound before its body is checked; mutual recursion between functions is
not supported. Definitely unknown names in a generic body are checked at its
declaration; full scope and type checking occur when an instantiation is needed.

Struct types can refer to themselves or each other through `Handle<T>`. Every
recursive cycle must pass through a handle; cycles that contain values by value
are rejected when the concrete struct type is instantiated. Generic struct
templates are instantiated on use. Transparent aliases can refer to recursive
structs but cannot themselves form recursive alias cycles. Type instantiation is
limited to 64 simultaneously nested struct instantiations.

```nyx
type Node
  value: Int64
  next: Handle<Node> | Nil = nil
end

global arena: Store = Store(1024)
var tail = arena.alloc<Node>(Node(2))
var head = arena.alloc<Node>(Node(1, tail))
print(head.read().value)  -- 1
```

A type, value, or function name may not be declared twice. Re-declaring a name
is an error. Built-in type names (`Int8`, `Int16`, `Int32`, `Int64`, `Uint8`,
`Uint16`, `Uint32`, `Uint64`, `Float32`, `Float64`, `Bool`, `Byte`, `Nil`, `Store`,
`Handle`, `String`, `Text`, `TextBuilder`, `List`, `Array`, `Slice`, `Record`,
`Table`, and `Error`) cannot be redefined. Type names, variable names,
function names, and store names share one ordinary namespace because they become
C identifiers.

A declared name must also survive emission into C. The checker rejects a name
that is a C11 keyword, a C identifier the generated code uses (such as `main`,
`int32_t`, `bool`, `size_t`, `printf`, or `malloc`), or begins with `nyx_`.
Every name a program chooses (type, field, variable, parameter, function) is
emitted into the generated C with the prefix `nyx_u_`, so no program name can
collide with libc, a C keyword, a macro such as `EOF`, or a generator name. The
generated names are internal; a C driver that calls a Nyx function must spell
it `nyx_u_<name>`. The `nyx_` prefix is reserved for the generator's own types, helpers, and
instantiation names, so source names cannot collide with them.

There is no requirement that a program contain a value declaration. A program of
nothing but type declarations or function declarations is legal.

## Whitespace and newlines

Nyx is newline- and whitespace-agnostic, like Lua. Spaces, tabs, and newlines
only separate tokens; indentation is not syntax, there are no semicolons, and no
line continuation rule. A declaration or statement ends where the grammar says
it ends, not at a line boundary. The same program may be written on one line or
spread across many lines with arbitrary spacing between tokens.

## Syntax shape

The surface syntax belongs to the Lua/Luau family. Blocks are delimited by
keywords and closed with `end`; there are no braces for blocks and no
semicolons; `if` uses `then`, `elseif`, `else`, and `end`; and types are
attached to names with `:`. Braces are used only for record literals.

Nyx is not a Lua dialect: a function declaration is `fun`, a value declaration
is `var`, blocks are not expressions, and the function type is written
`Fun<(Int32):Bool>` rather than Luau's `(Int32) -> Bool`. The shape is Lua-like
so that adding constructs such as loops, blocks, and declarations follows the
same pattern rather than introducing C-style block braces.

## Types

A type is a type expression. The implemented forms are:

- **`Int8`, `Int16`, `Int32`, `Int64`** — signed integer types mapping to
  `int8_t`, `int16_t`, `int32_t`, and `int64_t`.
- **`Uint8`, `Uint16`, `Uint32`, `Uint64`** — unsigned integer types mapping to
  `uint8_t`, `uint16_t`, `uint32_t`, and `uint64_t`. `Byte` is an alias for
  `Uint8`.
- **`Float32`, `Float64`** — IEEE floating-point types mapping to `float` and
  `double`.
- **`Bool`** — the built-in boolean type, with the values `true` and `false`. It
  maps to C `bool`.
- **`Nil`** — the union alternative whose single value is `nil`. It cannot be
  used alone as a source type or generic argument.
- **`Store`** — a global byte arena. It is a declaration resource, not an
  ordinary passable value.
- **`Handle<T>`** — an opaque reference to one `T` allocated in a store.
- **`Text`** — a built-in dynamic-length, immutable byte string, backed by
  a Store or by static literal data. It carries a store reference, offset, generation,
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
  parameters. Omit the return annotation for a no-result function type, for
  example `Fun<(Int32)>`.
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

A struct type has one or more fields, each of which is `name : Type`, and the
field names within one struct are unique; a repeated name is an error. A field may
have any non-function storable value type: `Int32`, `Bool`, a struct, a
union, an alias, or — inside a generic declaration — a type parameter. Function
types are not accepted as struct field types yet, but they are accepted as union
alternatives and collection elements.

Collections are `Array<T,N>`, `List<T>`, `Slice<T>`, `TextBuilder`,
`Record<K,V,N>`, and `Table<K,V>`.
A by-value type is capped at 64 KiB by default. The cap is a compiler setting,
`CompilerConfig.max_value_bytes`, which may be raised to 16 MiB or lowered. It
covers structs, fixed `Array<T,N>`, `Record<K,V,N>`, `String<N>`, `Error`, and
unions, and so also a message on a channel and an argument or result of a
`parallel` arm. A larger type is rejected; put the data in a Store instead, or
raise the cap if the stack can hold it (every copy of a value lives on the C
stack). `List`, `Table`, `Text`, `TextBuilder`, `Slice`,
and `Handle` are descriptors and their payload does not count.

`alloc<...>` and `Handle<...>` accept `Text` and handles in otherwise
eligible payloads. Lists, slices, tables, records, builders, and Stores are
rejected throughout the payload graph. A collection cannot be an
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

- integer literals, floating-point literals, string literals, list literals
  (`[a, b]`), boolean literals (`true`, `false`), and `nil`;
- names of variables, parameters, and functions;
- calls, `name(arguments)`;
- user-defined instance calls, `value.method(arguments)`, and static calls,
  `Type::function(arguments)`;
- struct constructors, written as calls whose name resolves to a struct type;
- `typeof expression is Type` tests;
- `if` expressions and `match` expressions;
- store and handle methods: `s.alloc<T>(value)`, `h.read()`,
  `h.write(value)`;
- fixed-string methods: `s.byte_len()`, `s.byte_at(i)`;
- Error methods: `e.header()`, `e.description()`, `e.filename()`, `e.line()`,
  `e.col()`, `e.pos()`;
- arithmetic, bitwise, comparison, equality, and logical operators;
- grouped expressions, `( expression )`.

Operator precedence, tightest first:

| Level | Operators | Operand types | Result |
|---|---|---|---|
| unary | `!` | `Bool` | `Bool` |
| unary | `-` | signed integer or float | same type |
| unary | `~` | integer | same type |
| multiplicative | `*`, `/` | same numeric type | same type |
| multiplicative | `%` | same integer type | same type |
| additive | `+`, `-` | same numeric type | same type |
| shift | `<<`, `>>` | same integer type | same type |
| bitwise AND | `&` | same integer type | same type |
| bitwise XOR | `^` | same integer type | same type |
| bitwise OR | `\|` | same integer type | same type |
| relational | `<`, `<=`, `>`, `>=` | same numeric type | `Bool` |
| equality | `==`, `!=` | numeric values; strings and text; or matching comparable types | `Bool` |
| strict equality | `===` | comparable values | `Bool` |
| logical | `&&`, `\|\|` | `Bool`, `Bool` | `Bool` |

Union values cannot be compared with `==`, `!=`, or `===`, including comparisons
with `nil`. This is by design. Test the active alternative with `typeof`, then
compare its narrowed value where necessary.

Numeric equality compares mathematical values across integer and floating-point
types without rounding an integer to a floating-point type. Negative integers
never equal unsigned values. Floating-point NaN is unequal to every value,
including itself; positive and negative zero compare equal. Same-sign infinities
compare equal to each other but never to integers. `!=` negates `==`.

```nyx
print(Uint16(3) == Int64(3))          -- true
print(Int64(-1) == Uint64(1))        -- false
var n: Int64 = 9007199254740993
var f: Float64 = 9007199254740992.0
print(n == f)                        -- false, with no lossy conversion
```

`===` requires the same type and equal values; values of different supported
types compare false. Numeric literals take the other operand's numeric type
when their value fits, as in other contextual literal expressions. Both
operands are evaluated even when their types differ. Floating-point equality
retains its usual rules, including NaN and signed zero. Use `!(a === b)` for
strict inequality.

```nyx
var a: Int32 = 3
var b: Int64 = 3
print(a == b, a === b)  -- true false
print(a === 3)          -- true: the literal takes Int32 context
```

`String<N>` and `Text` compare their bytes by content, even across capacities
or between fixed and dynamic strings. Two uncontextual string literals in an
equality expression are treated as `Text`; a literal too long for the other
operand's fixed string type is also treated as `Text`.

Structs, arrays, lists, records, and tables compare by value when their types
match and all their fields or elements support equality. Structs compare each
field; arrays and lists compare length and elements in order. Records and
tables compare key-value mappings, regardless of insertion or bucket order.
Comparing a stale list or table reports the usual stale-store error. Unions and
composites containing a union cannot be compared. Handles are comparable by
identity, including when they occur inside a comparable composite. `===` applies
these value rules only when the operand types match.

Integer arithmetic wraps on `+`, `-`, `*`, and unary `-` for signed types;
unsigned types wrap modulo their width. The generated C does the arithmetic in
unsigned types of at least 32 bits, so narrow operands never overflow C's
promoted `int`. Unary `-` is not defined on unsigned types. Division and modulo
trap on zero, and signed minimum divided by `-1` traps except that signed
minimum modulo `-1` is `0`. Shifts mask a non-constant count to the operand
width, and `>>` is a logical shift for every integer type. `~` yields a value of
the operand's own type.

Compile-time constant expressions are folded in the type of the expression, so
a constant is never rejected for being out of range of a type the expression
does not have. `Int32` overflow is rejected at compile time. In every integer
type, constant shift counts outside the operand width and constant zero divisors
are rejected. For the other widths an out-of-range constant is not an error at
compile time; the expression simply is not known to be constant, and the
run-time overflow check applies. `const` integer bindings are `Int32` and
participate there, and the same evaluator handles `Store(bytes)` and constructor
capacities.

Float arithmetic follows C `float`/`double` arithmetic; `%`, shifts, and bitwise
operators require integers.

All binary operators are left-associative. `&&` and `||` short-circuit in
generated C. Addition wraps: generated C performs the addition in unsigned
arithmetic and converts the result back to `int32_t`, so the language does not
rely on signed overflow.

Integer literals are decimal, or written in another base with a lowercase
prefix: `0x` hexadecimal, `0b` binary, `0o` octal (`0xFF`, `0b1010`, `0o755`).
A leading zero alone is not a prefix, so `010` is ten. Digits of any literal,
including floats and exponents, may be grouped with single underscores between
two digits: `1_000_000`, `0xFF_FF`, `0b1111_0000`, `1_000.5`, `1e1_0`. A radix
literal is an integer only; `0x1.5` is an error, and so are an uppercase prefix
(`0XFF`), a digit outside the base (`0b102`), a prefix with no digits (`0x`), and
a misplaced underscore. A radix literal is typed and range-checked exactly like a
decimal one, so `var x: Int8 = 0x80` is rejected just as `128` is; write `-0x80`
or use an unsigned type.

Integer literals may be up to 64 bits. A literal, or a negated literal such as
`-5`, is typed by context and must fit the type it is given; a literal that does
not fit is a compile-time error:

- beside a numeric operand, it takes that operand's type, so `x + -1` and
  `x < 200` type the literal from `x`;
- where a type is expected (an annotation, an assignment, an argument, a
  `return`, a field, a collection element), it takes that type;
- in a union, it goes to the one numeric alternative of the right kind that can
  hold it. An alternative of the literal's own default type wins. Two other
  fitting alternatives, such as `Int8 | Int64`, are ambiguous and rejected, as
  for strings, and a literal that fits none reports its range;
- with no context it is `Int64` and must fit that type. An explicit `Uint64`
  context can accommodate larger nonnegative literals.

Floating-point literals use decimal digits with a fractional part and/or an
exponent (`1.5`, `2e3`, `1.5E-3`) and default to `Float64`. A float literal
beyond `Float64`'s range is a lexer error; one beyond `Float32`'s range in a
`Float32` context is a checker error. Underflow to a denormal or zero is
accepted. An integer literal never becomes a float and a float literal never
becomes an integer: `var f: Float64 = 1` is rejected, so write `1.0` or
`Float64(1)`. `Byte` is an alias for `Uint8`; a literal typed as `Byte` must be
in `0..255`. This is contextual literal typing, not an implicit conversion:
different numeric types never convert into each other.

Explicit numeric conversion uses type-call syntax with exactly one numeric
argument: `Int8(expr)`, `Uint32(expr)`, `Float64(expr)`, and so on. The result
has exactly the named type; context never retypes it, so `var x: Int64 =
Int32(5)` is rejected.

- A constant (a literal, a negated literal, or a conversion of one) converts at
  compile time and must fit the target: `Int8(300)`, `Uint8(-1)`, and
  `Float32(1e39)` are compile-time errors. A float constant truncates toward
  zero first, so `Int32(2.9)` is `2`.
- Any other value converts at run time. A conversion between integer types that
  could lose a value is range-checked and traps with `nyx: numeric conversion
  out of range`; one that cannot (such as `Int8` to `Int32`) is a plain C
  conversion.
- Float to integer truncates toward zero and traps when the truncated value is
  out of range or the input is NaN or infinite, so `-128.9` converts to `Int8`
  as `-128` and `-129.0` traps.
- Integer to float rounds to nearest. `Float32` to `Float64` is exact.
  `Float64` to `Float32` traps when a finite value is beyond `Float32`'s range;
  infinities and NaN pass through.

A UTF-8 byte order mark is accepted at the beginning of a source file and does
not count as a source column; byte offsets still include its three bytes.

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

### `base64`

`base64("...")` decodes a string literal at compile time and yields a static
`Text` holding the decoded bytes. Its bytes live in read-only data and it needs
no Store, so it is usable anywhere a `Text` is, including `store.copy`,
`store.concat`, equality, `slice`, `for`, and unions with `Text`:

```nyx
var hello: Text = base64("SGVsbG8sIFdvcmxkIQ==")   // "Hello, World!"
var raw: Text = base64("AAECgP8=")                  // bytes 0 1 2 128 255
```

- It takes exactly one argument, which must be a string literal.
- The decoder is strict, so each byte string has one spelling: the standard
  RFC 4648 alphabet (`A-Z a-z 0-9 + /`), `=` padding that completes the final
  four-character group, and zero bits in the unused tail. The URL-safe alphabet
  (`-` `_`), whitespace, unpadded input, and non-canonical padding such as
  `"QR=="` are rejected, each with the byte offset of the problem.
- The empty string decodes to an empty `Text`.
- The result is always a `Text` and is never retyped by context: it does not
  become a `String<N>`. Equality with a fixed string compares the bytes.
- Decoded bytes are arbitrary. They need not be UTF-8 and may contain NUL, which
  `Text` allows because it carries a length.

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
var u: Int32 | Bool | Nil = 1
u = nil
```

Injection applies at:

- a variable initializer whose annotation is a union;
- a `return` expression whose function returns a union;
- a function call argument whose parameter is a union;
- a struct constructor argument whose field is a union.

An explicit `nil` literal cannot initialize a `var` or `const` with a union
type. Assignment, argument passing and returns may inject `nil` into a union.
An initializer expression whose union result may be `nil` is allowed.

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
a union containing `T`, `name` is **narrowed to `T` in the then-block**:

```text
fun to_int(x: Int32 | Bool | Nil): Int32
  if typeof x is Int32 then
    return x                 -- x has type Int32 here
  elseif typeof x is Bool then
    if x then return 1 else return 0 end   -- x has type Bool here
  else
    return 0
  end
end
```

A positive type test narrows the right operand and body of `&&`. A false type
test narrows the right operand of `||`; its false branch and a negated `||`
also combine facts from both operands. Negating a type test removes that
alternative from a union; the same complement applies in an `else` block and
after a branch that returns. A disjunction of type tests on the same value can
narrow it to the union of the tested alternatives; common facts from both
alternatives are retained for compound conditions. Narrowing
also works for struct fields and Array or List elements indexed by a constant,
variable, or arithmetic expression of those values. Assigning to the containing
value or a variable used by the index, or invoking a method on the containing
value, invalidates those facts. Because Lists can alias, a List mutation or a
function or method call also invalidates List element facts, except for `print`.
An expression containing a potentially mutating call invalidates affected facts
before its operands are checked because operand evaluation order may vary.
Field and element facts are not propagated from an earlier condition when a
later part may call a function.

### `match`

`match` is a statement or expression with repeated `with` arms:

```nyx
match typeof x with Int32 then
  ...
with Bool then
  ...
else
  ...
end
```

Value patterns use equality:

```nyx
match x with true then
  ...
with false then
  ...
end
```

The scrutinee may be any expression. It is evaluated once before the arms are
tested. Type arms narrow a scrutinee that is a simple identifier or a stable
field or indexed access, subject to the same invalidation rules as type tests.
There is no fallthrough.

Expression form:

```nyx
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

```nyx
var z: Int32 = if typeof x is Int32 then 1 else 0 end
```

Each branch is one expression, all branches must have the same type, and `else`
is required.

### Value checks

Value conditions use ordinary equality:

```nyx
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
- With positional arguments, each argument supplies the field at that position;
  omitted trailing fields must have defaults.
- With named arguments, each field without a default must be supplied exactly
  once. Arguments may appear in any order; the field's position, not the
  argument's position, decides where the value lands.
- Each argument may be any expression that is assignable to its field's type,
  including an injected union alternative.

A struct field may declare a literal default after its type. Integer, float,
Boolean, string and `nil` literals are supported, including a minus sign on a
numeric literal. A default must be assignable to the field type; expressions
and references to other fields are not defaults. A non-generic struct's defaults
are checked at its declaration; generic defaults are checked for each concrete
instantiation.

```nyx
type Rectangle
  width: Int64
  height: Int64 = 10
end

var a = Rectangle(5)          -- height is 10
var b = Rectangle(width = 3)  -- height is 10
var c = Rectangle(5, 4)       -- height is 4
```

For a generic struct, type arguments are inferred from the constructor
arguments, including defaults used for omitted fields. `Pair(1, true)` infers
`Pair<Int64, Bool>`. A type parameter that
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

```nyx
const Max: Int32 = 10
Max = 20              // rejected

const xs: List<Int32> = arena.list<Int32>(2)
xs.push(1)            // allowed: xs is not reassigned
```

Const declarations use the same type inference and scope rules as `var`.
Program-level consts are not visible inside functions unless passed as
arguments. `Store` still uses `global`, not `const`. A program-level variable
may refer to variables and functions declared before it.

Constness is deep for inline storage. A `const` struct, `Array`, `Record`, or
`String<N>` cannot be mutated through a field, index, `push`, or `pop`. `List`,
`Table`, `Text`, `TextBuilder`, and `Slice` are descriptors; a `const` binding to
one still permits mutation of the shared store-backed value.

Program-level variables are local to the generated `main` (or to
`nyx_program_value` when the program is a single `Int32` variable). They are
therefore not visible inside function bodies yet.


## Printing

`print` is a builtin statement, not a user-defined function:

```nyx
print("n =", n, "ok =", flag)
```

- Zero or more comma-separated arguments.
- Arguments are evaluated left to right.
- One space is written between arguments; a newline follows.
- `print()` writes one newline.
- `print` returns `Nil`.
- `print` is reserved and cannot be declared, stored, passed, or used as a
  value. `base64`, `range`, `now`, `sleep`, and `cores` are reserved the same way.

The builtin formats by the static argument type. Floating-point output uses
ordinary notation for decimal exponents from -6 through 15 and scientific
notation outside that range. An integral float in ordinary notation has a
`.0` suffix. Negative zero is `-0.0`; infinities are `inf` and `-inf`, and
NaN is `nan`.

```text
Int8/Int16   decimal
Int32        decimal
Int64        decimal
Uint8/16     decimal
Uint32       decimal
Uint64       decimal
Float32/64   shortest decimal that round-trips to the same float
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
Record       {key: value, ...}
Table        {key: value, ...}
TextBuilder  current bytes
Handle       <handle store=N offset=N generation=N>
Function     <function>
```

Collections, struct fields, and union payloads are printed recursively.  A
pretty-print depth limit defaults to 16 and is configurable through
`CompilerConfig.max_print_depth`; deeper values print as `<...>`.

`print` does not interpret format strings. `print("%d", 3)` prints `%d 3`.

## For loops

A `for` statement iterates a list, slice, string, Range, Record, or Table:

```nyx
for value in iterable do
  ...
end

for index, value in iterable do
  ...
end
```

- `List<T>` and `Array<T,N>` yield `T`.
- `Slice<T>` yields `T`.
- `Range<T>` yields integer values of type `T`.
- `String<N>` and `Text` yield `Byte`.
- A Record or Table with two bindings yields its key and value; with one
  binding it yields the key.
- The index binding, when present for a sequence, is `Int32` and starts at `0`.
- The iterable expression is evaluated exactly once.
- For collections, the length is captured before the first iteration.
- The loop bindings are scoped to the loop body and may not shadow an existing
  visible name.
- `break` and `continue` apply to the innermost loop.
- `return` inside the loop returns from the enclosing function.

`range(start, end)` creates a lazy `Range<T>` with step 1;
`range(start, end, step)` supplies an `Int64` step. The integer bounds have the
same type `T`, with untyped integer literals using their usual contextual type
or defaulting to `Int64`. Arguments are evaluated once, in source order, when
the range is constructed. A range stores its bounds and step and allocates no
collection. It can be stored, passed to functions, and iterated repeatedly.

The start is inclusive and the end is exclusive. A positive step increases the
value; a negative step decreases it, including for unsigned bounds. A step that
points away from the end produces an empty range. A literal or folded zero step
is rejected at compile time; another zero step traps when the range is created.
Iteration stops before the end without overflowing the element type.

```nyx
var numbers: Range<Int64> = range(0, 5)
for n in numbers do print(n) end       -- 0 through 4
for n in range(5, 0, -2) do print(n) end  -- 5, 3, 1
```

A direct `xs.push` or `xs.pop` on the iterated list inside the loop body is
rejected at compile time. The checker only rejects the locally decidable direct
case; indirect mutation through another function or alias remains.

Iterating a list checks its length before each read and again after each body
execution. If `push` or `pop` changes the length indirectly, the program fails
with a deterministic runtime error instead of visiting a shifted range. Dynamic
list reads still check the store reference, generation, and bounds; string byte reads do the
same.

Iteration is by byte for strings; UTF-8 codepoint iteration is not implemented.
`map` and `filter` are not implemented.

## Defer

`defer do ... end` runs a block later, when control leaves the block that
contains the `defer`. It is for work that must happen however the block ends.

```nyx
fun f(n: Int32): Int32
  defer do print("a") end
  defer do print("b") end
  if n > 0 then
    defer do print("c") end
    return n * 2
  end
  print("body")
  return 0
end

print(f(1))   -- c, b, a, then 2
print(f(0))   -- body, b, a, then 0
```

- A block is left by reaching its end, or by `return`, `break`, or `continue`.
  Each exit runs the defers of every block it leaves, innermost block first.
  Within one block they run in reverse order of writing.
- A defer runs only if control reached it. A `defer` after an early `return`, or
  in a branch that was not taken, never runs.
- A `defer` in a loop body runs at the end of each iteration, and on `break` and
  `continue`.
- `return expression` evaluates the expression first. The defers then run, and
  the function returns the value computed before they ran. A defer can change a
  variable but cannot change a value already being returned.
- The deferred block runs where the exit happens, so it reads variables as they
  are then, not as they were when the `defer` was written. It can use any name
  declared before the `defer` and in scope there. A name declared after it is not
  visible.
- What a `typeof` test or `match` proved before the `defer` does not carry into
  its block, because the variable may change before the block runs. The block
  may test and narrow its own values.
- A deferred block cannot `return`, and cannot `break` or `continue` out of
  itself. A loop that is entirely inside the block may use them. Defers may be
  nested: an inner one runs when the outer block ends.
- A defer in the body of a `scope` runs before that scope's Store is released
  and may allocate in it. A defer written outside the `scope` runs after it.
- A trap ends the program without running pending defers, as it does for
  Stores.
- A `defer` is a statement of a function body or of a block inside one, or of a
  block at program level such as the body of a `while` or `if`. A `defer`
  directly at program level is an error. `defer` is a reserved word.

## Time

Two builtins read and wait on a monotonic clock. They work in functions as
well as at program level.

```nyx
var start: Int64 = now()
sleep(250)
print(now() - start >= 250)   -- true
```

- `now()` takes no arguments and returns an `Int64`: the number of whole
  milliseconds on the system's monotonic clock. The origin is unspecified, so
  only differences between two readings are meaningful. The value never goes
  backwards and does not change when the wall clock is adjusted.
- `sleep(ms)` takes one `Int64` count of milliseconds, waits at least that long,
  and returns `Nil`. It is a statement-like call and cannot be used as a value.
  An integer literal takes the `Int64` type; an `Int32` or a float argument is
  rejected, so `sleep(n)` for `n: Int32` needs `Int64(n)`.
- `sleep(0)` returns immediately. A negative literal is a compile-time error,
  and a negative value computed at run time traps with
  `nyx: FILE:LINE:COL (byte N): sleep duration cannot be negative`.
- `sleep` blocks the calling thread, which is the whole program because there
  are no other threads yet. A signal that interrupts the wait does not shorten
  it.
- `now` and `sleep` are reserved and cannot be declared, like `print`.
- If the system's monotonic clock cannot be read, `now()` traps with
  `monotonic clock is unavailable`.
- Using either builtin makes the generated C depend on POSIX.1-2008
  (`clock_gettime`, `nanosleep`): the emitted source defines
  `_POSIX_C_SOURCE` before its first include. Programs that use neither are
  plain C11.

## Atomic cells

An `Atomic` is a global cell that any thread may read and update without a data
race. It is a resource like a Store, not a value.

```nyx
global stop: Atomic<Bool> = Atomic(false)
global served: Atomic<Int64> = Atomic(0)

print(served.add(1))     -- 0: add returns the value the cell held before
print(served.load())     -- 1
stop.store(true)
```

- `global name: Atomic<T> = Atomic(literal)`. `T` is `Bool` or an integer type
  (`Int8` to `Int64`, `Uint8` to `Uint64`, `Byte`). The initial value is a
  literal of that type, negated if it is a signed integer. An Atomic must be
  `global`; it cannot be a local, a parameter, a field, or a `const`.
- `load()` returns the value, `store(v)` replaces it and returns `Nil`, and
  `add(n)` adds `n` and returns the **previous** value, so it can hand out unique
  tickets. `add` needs an integer cell. Integer addition wraps on overflow, as C11
  defines it for atomics. Every operation is sequentially consistent.
- An Atomic cannot be copied, passed, returned, stored, or assigned; its name
  can only start a `load`, `store`, or `add` call. Like a global Store, its name
  is visible in every function, including a function an arm calls.
- A function that uses an Atomic is safe to run in a `parallel` arm; a function
  that uses a global Store is not.
- The generated C declares a static `_Atomic` cell and uses `<stdatomic.h>`
  through three wrapper macros, so the memory order is written in one place.
  Programs that use no Atomic and no `parallel` block do not include it.

## Parallel blocks

A `parallel` block runs several function calls at the same time and waits for
all of them. Each call is an **arm**.

```nyx
fun square(n: Int32): Int32 return n * n end

var a: Int32 = 0
var b: Int32 = 0
parallel do
  with a = square(6)
  with b = square(7)
end
print(a, b)   -- 36 49
```

The block ends when every arm has returned. No arm outlives it.

```text
parallel [ name = Store(capacity) ] do
  [ var or const declarations ]
  with [ var ] [ target = ] call(arguments) [ for lane in range(first, last) ]
  ...
[ then
  statements ]
end
```

- **Setup.** `var` and `const` declarations may stand between `do` and the first
  `with`. They run on the calling thread after the pool exists and before any
  arm starts, and their names are visible to the arms' arguments and to the
  `then` section. They are how a block makes the pool channels its arms share
  (see Pool channels). Any other statement there is a syntax error.

- **Arms.** An arm is a call to a named, non-generic function declared earlier,
  not to a method, a function value, or the function that contains the block.
  The arguments are evaluated first, in the order written, in the thread that
  runs the block, and **copied** into the arm. The first arm runs on the calling
  thread, the others on worker threads, so a UI loop can be the first arm and
  stay on the main thread. Workers are reused: when an arm ends its thread waits
  for the next block instead of exiting, and a new thread is started only when
  every worker is busy, so an arm that blocks never delays another. A program
  that opens a block every frame pays thread start-up only for its first.
- **Plain values.** An argument must be a plain value: an integer, float, `Bool`,
  `Byte`, `Nil`, `Error`, fixed `String<N>`, or a struct, union, fixed `Array`,
  or `Range` made only of those. `Text`, `TextBuilder`, `Handle`, `List`,
  `Slice`, `Table`, `Record`, a function, and a Store (other than the block's own
  pool) are rejected, because they would let two threads reach the same memory.
  A `Channel<T>` value (see Pool channels) is the one other thing an arm may
  receive, because every operation on a channel is synchronised.
- **Results.** `with target = call` assigns the result to an existing variable
  when the block ends; the variable must have exactly the result's type, must
  not be a `const`, no other arm may assign it, and the result must be a plain
  value. `with var name = call` declares `name` in the `then` section, where it
  can be any type the pool holds. An arm with no binding discards its result, and
  may be a function that returns nothing.
- **The pool.** `parallel pool = Store(n) do` creates a Store, visible to the arms
  and the `then` section, that every arm may allocate in. Its capacity is a hard
  limit, not an initial reservation, and it never moves; allocation takes the next
  bytes with one atomic operation, so arms never wait for one another. A full
  pool traps with `nyx: parallel store is full`. A negative capacity traps. The
  pool is released when the block ends, including by `return`, `break`, or
  `continue` in the `then` section.
- **Objects stay private until the arms return.** An arm gets only plain values
  and the pool, and cannot read a global Store, so it has no way to hold a
  pointer to another arm's object. Nothing races on what the arms build. The
  `then` section runs after every arm has returned, on one thread, and may read
  and change anything they produced.
- **Escape.** A value from the pool is checked like a value from a `scope`
  Store: it cannot be stored in a variable, or returned, where it outlives the
  block. Copy what must persist into a longer-lived Store inside the `then`
  section.
- **Repeated arms.** `with f(lane) for lane in range(first, last)` runs the call
  once for each `lane` in `first` up to but not including `last`, each on its
  own thread, with `lane` an `Int32` that the arguments may use. The bounds are
  `Int32` expressions evaluated once. An empty range runs nothing. `with target =
  call for lane in range(...)` collects the results: `target` must be an existing
  fixed `Array<R,N>` variable, where `R` is the call's plain result type. When
  the block ends the Array holds one element per lane, in lane order, and its
  length is the number of lanes; more lanes than `N` traps with `a repeated arm
  produced more results than its Array holds`. `with var` cannot be repeated.
  `cores()` gives a sensible count.
- **The audit.** Every function an arm can reach, through named calls, must avoid
  global Stores and calls through function values. Atomics, channels, `print`,
  `now`, `sleep`, scopes, and the arm's own `parallel` blocks are fine. The compiler
  rejects a program that breaks this and names the function.
- **Failure.** A trap in any arm ends the program. If two arms trap together, the
  message is printed once. A thread that cannot start traps with
  `nyx: cannot start a thread`.
- **Printing.** Each `print` statement writes as one unit, so lines from different
  arms never interleave inside a line. The order between arms is unspecified.
- **Per thread.** The recursion guard counts each thread separately, so several
  arms may each recurse nearly as deep as one thread may.

`cores()` takes no arguments and returns the number of processors online, as an
`Int32` of at least 1. It is reserved.

A `parallel` block is a statement of a function body or of a block at program
level. An arm cannot `return`, `break`, or `continue`; the `then` section can,
and its `defer` blocks run before the pool is released.

The generated C uses POSIX threads and `<stdatomic.h>`, defines
`_POSIX_C_SOURCE`, and is built with `-pthread`. Programs without `parallel`
generate the same C as before.

## Channels

A channel carries plain values from one thread to another. It is a global
resource like a Store or an `Atomic`: declared once at program level, visible in
every function and every `parallel` arm, and used only through its operations.

```nyx
type Input
  key: Int32
end

global input: Channel<Input> = Channel(64)

input.send(Input(3))
var ev = input.recv()          -- Input | Nil
if typeof ev is Input then print(ev.key) end
input.close()
```

- `global name: Channel<T> = Channel(capacity)`. The capacity is a literal from 0
  to 65536 and the whole buffer, which is static, is at most 64 MiB. `T` is a
  plain value (the same rule as an arm's arguments), not `Nil` and not a union; a
  message that must be optional goes in a one-field struct. A message is also
  bounded by the by-value limit (64 KiB unless `max_value_bytes` says otherwise).
- Capacity `0` is a **rendezvous** channel: `send(v)` waits until a receiver has
  taken the value, so a send that returns `true` means the value was received.
  If the channel is closed first, the parked value is withdrawn and `send`
  returns `false`. A rendezvous channel has no room to try, so `try_send` and a
  `when` send arm on it are errors; `recv`, `try_recv`, a `when` receive arm,
  `close`, and `closed` work as on any channel.
- `send(v)` waits while the channel is full, copies `v` in, and returns `true`;
  it returns `false`, without waiting, if the channel is closed.
- `recv()` waits until a value is queued and returns it as `T | Nil`. It returns
  `nil` only when the channel is closed and empty.
- `try_send(v)` and `try_recv()` never wait. `try_send` returns `false` if the
  channel is full or closed, and `try_recv` returns `nil` if nothing is queued
  right now.
- `close()` returns `Nil`. After it no value can be sent, but queued values can
  still be received. A closed channel stays closed, and `close` may be repeated.
- `closed()` is `true` once the channel is closed **and** every queued value has
  been received. A loop that polls with `try_recv` and stops when `closed()` is
  true cannot lose a message.
- Values leave in the order they entered. Any number of threads may send or
  receive on one channel.
- A global channel cannot be copied, passed, returned, stored, or assigned; its
  name can only start an operation. (A pool channel is a value; see Pool
  channels.) A channel works in a program with no `parallel` block, as a queue for
  one thread.
- All channel state is guarded by one process-wide lock, so every operation is
  safe from any thread. The generated C uses POSIX threads.

### Pool channels

A channel can also be made at run time, in a `parallel` block's pool, so each
run of a block gets its own, with its own closed state, and a program can have
as many as it has lanes:

```nyx
fun producer(jobs: Channel<Int64>)
  for i in range(1, 6) do jobs.send(Int64(i)) end
  jobs.close()
end

fun worker(jobs: Channel<Int64>, results: Channel<Int64>)
  while true do
    var j = jobs.recv()
    if typeof j is Int64 then results.send(j * j) else break end
  end
  results.close()
end

parallel pool = Store(4096) do
  var jobs = pool.channel<Int64>(2)
  var results = pool.channel<Int64>(2)
  with producer(jobs)
  with worker(jobs, results)
  with report(results)
end
```

- `pool.channel<T>(capacity)` returns a `Channel<T>`. The element follows the same
  rules as a global channel's. The capacity is an `Int32` expression from 1 to
  65536, with a buffer of at most 64 MiB; there are no pool rendezvous channels.
  The state and buffer come from the pool, and a full pool traps as usual.
- It is allowed only on a parallel block's pool. The pool never moves, so a
  thread waiting on a channel never finds it gone. On any other Store the
  program traps with `a channel can be allocated only in a parallel block's pool`.
- A `Channel<T>` value has all six operations, and `when` arms may use one. It
  can be a local variable or `const`, a parameter, or an argument to a function
  or to an arm. It cannot be a field, a collection element, a union
  alternative, a type argument, or a return type, cannot be compared or
  printed, and cannot be a channel's own element.
- A pool channel is checked like any value from a pool: it cannot be stored where
  it outlives the block (the analyser rejects that), and using one after the
  block has ended traps with `stale handle`.
- Deadlock detection and `when` treat pool channels exactly like global ones.

### Timers

`Every(ms)` declares a timer, a channel with no queue that receives a value every
`ms` milliseconds:

```nyx
global tick: Channel<Int64> = Every(16)

var t = tick.recv()      -- waits for the next tick: the time it was due
```

- The interval is a positive literal, in milliseconds, and the type is
  `Channel<Int64>`. The value of a tick is the `now()` reading at which it was
  due.
- The first tick arrives one interval after the timer is first used.
- A tick nobody has taken is replaced by later ones, so a slow receiver sees the
  latest tick and no backlog builds up.
- `send` and `try_send` on a timer return `false`. `close()` stops it, and a
  closed timer receives `nil`.
- A wait on a timer wakes by itself when the tick is due. It is not a deadlock.

### Waiting on several channels

`when` waits until one of several channel operations can proceed and runs that
arm. Its arms have the shape of a `match` arm.

```nyx
when
  with t = tick.recv() then
    step()
  with cmd = commands.recv() then
    if typeof cmd is Command then apply(cmd) else break end
  with ok = frames.send(world) then
    print("frame accepted:", ok)
else
  idle()
end
```

- An arm is `with [name =] channel.recv() then block` or
  `with [name =] channel.send(value) then block`. A `recv` arm binds the same
  `T | Nil` that `recv()` returns, and a `send` arm binds the `Bool` that `send`
  returns. The name is declared at the start of the arm's block.
- The value of every send arm is evaluated once, from the first arm down, before
  anything waits.
- A receive arm is ready when a value is queued, when the channel is closed and
  empty (it then gives `nil`), or when a timer's tick is due. A send arm is ready
  when there is room or the channel is closed (it then gives `false`).
- The **first ready arm in source order** runs, so the order of the arms is their
  priority. Put the arm that must never starve first. The runtime does not
  rotate or randomise the choice.
- With an `else` block, `when` never waits: if nothing is ready the else block
  runs. Without one it waits until an arm is ready.
- The operation is performed while the lock is held, and the arm's block runs
  after it is released.
- `break` and `continue` in an arm apply to the enclosing loop; `when` is not a
  loop. `return` and `defer` work as in any block. A `when` whose arms (and else)
  all return does not fall through.

### Deadlock

A program that can never make progress is reported, not left to hang. The
runtime counts the threads that are alive, the threads waiting on a channel, and
the threads waiting for the arms of a `parallel` block to end. When a thread is
about to wait on a channel, or a thread ends, and every live thread is waiting,
the program traps:

```text
nyx: FILE:LINE:COL (byte N): deadlock: every thread is waiting on a channel
```

The position is the wait that completed the deadlock. A trap found when a thread
ends has no position. A thread that is running, sleeping, or waiting on a timer
is never counted as waiting, because it will wake without help; a thread that is
about to be woken by a send is not counted either. A thread blocked in something
other than a channel is counted as running, so it can never cause a false
report. Nyx has no file or socket input yet; when it does, a program whose
threads are all stuck in such waits is outside what the report can see, and
the report will need to learn about those waits.

## Local variables and blocks

Function bodies and every block (`if`/`elseif`/`else`, `while`, `for`, and
`match` arms) may declare locals:

```nyx
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
  `else`, `while`, `for`, and `match` bodies.
- A local may not reuse a name already visible in the current function,
  including parameters, or any function/type/store/builtin name. Shadowing is
  currently rejected.
- A local's initializer is checked before the name is brought into scope, so it
  cannot refer to itself.
- `Store` declarations remain program-level only.
- Locals are assigned and passed by value like parameters.

## Field access

Struct fields are read and written with `.`:

```nyx
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

```nyx
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
- Variables, list elements, and writable struct fields are assignable at
  program level. Function bodies can assign their parameters, but cannot assign
  program-level variables because those names are not visible inside functions.
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

```nyx
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

Dynamic memory is managed only by arenas, called **Stores**. A Store declared
with `global` lasts for the program. A Store introduced by `scope` lasts through
its body. Either can be passed to a function with a `Store` parameter.

```nyx
global points: Store = Store(4096)

scope scratch = Store(256) do
  print(scratch.copy("temporary"))
end
```

- `global name: Store = Store(bytes)`. The annotation must be `Store`; a
  `global` declaration of any other type is rejected, and a `Store` declared
  with `var` or `const` is rejected.
- `Store(bytes)` in a `global` takes a constant `Int32` expression, interpreted
  as bytes (literals and `+`). `Store(0)` is allowed; a literal negative byte
  size is a compile-time error.
- Global Stores are visible inside function bodies. This is the one exception
  to the rule that functions cannot see program-level variables: a store is a
  resource, not ordinary storage.
- `scope name = Store(capacity) do block end` is a statement wherever
  statements are allowed. Its name follows the no-shadowing rule and is not
  visible in its own capacity. The capacity is an `Int32` expression evaluated
  once on entry; zero is valid, a constant negative capacity is a compile-time
  error, and a dynamic negative capacity traps before initialisation. It is an
  initial reservation in bytes. `scope` is a reserved word.
- Each entered scope starts with a fresh empty Store. On fallthrough, `return`,
  `break`, or `continue`, every Store exited by that edge is released in reverse
  order of entry. A `return` evaluates its value before the cleanup. A trap
  terminates the program without running cleanup. A scope that is never entered
  allocates nothing.
- A scope inside a loop keeps its buffer for the loop's activation: the first
  entry allocates, later iterations start it empty again, and the buffer is
  released when the loop ends or a `return` leaves it. This is invisible to the
  program: each entry is a fresh Store and handles from an earlier entry are
  stale. A scope in a function called from a loop allocates on every call.
- A Store may be passed directly to an immutable `Store` parameter, including
  through generic functions, methods, and `Fun` values. The parameter may be
  forwarded or used as a receiver for `alloc`, `concat`, `copy`, `text`,
  `builder`, `list`, and `table`. A Store capability cannot be assigned, copied
  to a binding, returned, stored in a field, used as a union alternative, or
  inferred as a generic type argument. A `Fun` type may name `Store` only as a
  direct parameter.
- The declared size is an initial reservation, not a cap. Stores grow by
  doubling as needed.
- There is no `reset()`. Global Stores are freed at program exit.

### Lifetimes and escapes

A value allocated in a Store must not outlive it. The compiler rejects a program
in which a value from a `scope` Store could be reached after the scope ends.
Every Store has a depth: global Stores are zero, a function's parameters are one
level inside the activation, and each nested `scope` is one deeper. A value
carries the Stores it may point into: handles, `Text`, `TextBuilder`, lists,
slices, and tables, and any struct, union, or array holding one. While a struct
or union stays inline, its fields and alternatives are tracked separately, so
`b.names` has the facts of that field alone. Primitives,
fixed strings, `Error`, and functions carry none. `a.concat`, `a.copy`, and
`a.text` copy bytes, so their result belongs to `a` regardless of the input.

A value may be written only to a destination that outlives it:

- an assignment, `var` initialiser, field write, or element write of an inline
  value (struct, union, `Array`, `Record`) needs the value to outlive the
  variable, whose depth is the innermost `scope` enclosing its declaration;
- `push`, `set`, indexed write, table or record insertion, `alloc`, and handle
  `write` need the value to outlive the Store that holds the destination;
- `return` needs the value to outlive the activation, so a value from a `scope`
  Store opened inside the function cannot be returned.

Reading from a list, table, slice, or handle yields a value no deeper than its
container. The shallowest Store a container could be in is unknown after such a
read, so a container reached by reading another container cannot receive
deeper values. A call result is derived only from the arguments the callee's body
returns (or global memory). Variables are tracked flow-insensitively and loops
are analysed to a fixed point.

A direct call is checked against a summary of the callee's body: for each
parameter, which other parameters' storage it may write that parameter into,
whether it may write it into global memory, and which parameters its result may
come from. The call is rejected when an argument's Store is deeper than a
destination it may be written into. Summaries are computed to a fixed point
over the call graph, so recursion is covered. A call through a function value
assumes the callee may write any argument into any other, but not into global
memory; a function that may write a parameter into a global Store cannot be
used as a function value. The analysis is conservative: it never accepts an
escaping program, but it rejects some correct ones. Diagnostics name the
originating Store and the destination:
`a value from Store 'scratch' cannot be stored in 'result', which outlives it`.

The analysis runs only when a program contains a `scope`; without one every
Store is global. Runtime generation and bounds checks remain in the generated
program as a second line of defence, except that a program with no `scope` and
no parallel pool omits the generation comparison: no Store's generation can
change while it runs, so the comparison could never fail.

Allocation returns an opaque handle:

```nyx
var p: Handle<Point> = points.alloc<Point>(Point(1, 2))
var n: Handle<Int32> = points.alloc<Int32>(13)
var b: Handle<Byte> = points.alloc<Byte>(7)
```

`alloc<T>(value)` copies an eligible value into the arena and returns
`Handle<T>`. Eligible values include primitives, functions, Error, fixed
strings, Text, ranges, handles, arrays, and structs or unions composed of
eligible payloads. The checker follows handle edges and rejects List, Slice,
TextBuilder, Record, Table, and Store anywhere in the payload graph. Collection
element restrictions still apply. Recursive handle graphs are checked with a
visited set. Generation and bounds checks remain in force when handles or Text
are read.

`Handle<T>` records a store reference, a byte offset, and the store generation. It is
opaque: there is no field access. Its operations are:

```nyx
var x: Point = p.read()
p.write(Point(3, 4))
```

- `p.read()` copies the current `T` out.
- `p.write(value)` copies a new `T` in; `value` must be assignable to `T`.
- `==` and `!=` compare allocation identity for handles of the same type:
  store, offset, and generation must all match. They do not read the value.
  `===` additionally requires matching handle types, returning false for
  different types. Both operators validate every handle operand first, so
  comparing a stale handle traps even when the types differ.
- Both check the store reference, the store generation, and the offset bounds. A
  failure is a deterministic runtime diagnostic, not undefined behaviour. (In a
  program with no `scope` and no parallel pool the generation never changes, so
  that comparison is not emitted.)
- Handles survive growth because they use a stable Store reference and an offset
  into the movable buffer.

A Store's generation changes only when its storage is released, which the
compiler's escape check keeps unreachable from safe programs. There is no
per-object free. A stale handle would trap (`nyx: stale handle`).

Allocation failure (running out of memory while growing) aborts with
`nyx: out of memory`. Allocation never returns uninitialized memory.


## Lists

Collections split into fixed inline Arrays and mutable store-backed Lists.

### Fixed `Array<T,N>`

An Array has inline capacity `N` and a runtime length:

```nyx
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
- A literal index that is negative or at least `N` is a compile-time error,
  because an Array's length can never exceed `N`.
- `xs.get(i)` returns `T | Nil`; an out-of-range index returns `nil` instead of
  trapping.
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

```nyx
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
  Capacity may be `0`; a literal negative capacity is a compile-time error.
- `push(value)` adds one element and grows the buffer by doubling when needed.
  Old buffers remain in the store until it ends.
- `pop()` removes the last element and returns `T | Nil`.
- `xs[i]` reads and `xs[i] = value` writes; both are bounds-checked.
- `xs.get(i)` returns `T | Nil`; an out-of-range index returns `nil` instead of
  trapping.
- `len()` returns the current length.
- Dynamic lists are mutable. Copying a `List<T>` copies the descriptor and all
  copies alias the same list state, like `TextBuilder`.
- A list must not outlive its Store (see Lifetimes and escapes).
- There is no per-list free.
- List literals are only valid for fixed `Array<T,N>`. A dynamic list must be
  created with `store.list<T>(capacity)`.

### Element types

Collection elements must be single items: the numeric types, `Bool`,
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

```nyx
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

```nyx
var t: Table<String<8>, Int32> =
  arena.table<String<8>, Int32>(4)
```

- Created with `arena.table<K,V>(capacity)`. A literal negative capacity is a
  compile-time error.
- Grows by rehashing.
- Copies alias the same arena state.
- It must not outlive its Store.
- Same operations as Record, plus `t.get(key): V | Nil`, which returns `nil`
  for a missing key instead of trapping.

Both use FNV-1a hashing and linear probing. They print as
`{key: value, ...}`; hash order is unspecified. Iteration:

```nyx
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

```nyx
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

```nyx
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
- `Slice<T>` has `len()`, `[]` reads, and `s.get(i): T | Nil`. It has no
  mutation operations.
- A slice is a **live window** onto its list. It stores the list's identity, the
  index of its first element, and its length, and finds the list's buffer
  afresh on every access. It does not allocate or own storage.
- Parent writes (`xs[i] =`, `set`) are visible through the view, and so are
  writes made after the parent list has grown: growing moves the list's buffer,
  and the window follows it. A slice never reads a stale copy.
- If the list shrinks (`pop`) until the window no longer fits inside it, any use
  of the slice (`len`, `[]`, `get`, iteration) traps with a list-bounds error.
- A slice must not outlive its list's Store.
- Arrays do not support `.slice`: their inline data has no stable
  address for a non-owning view. Copy the Array instead if a detached
  sequence is needed.

## Strings and Error

### `Text`

A dynamic string is an immutable byte sequence whose length is not part of its
type. It is backed by a Store or by static literal data:

```nyx
global arena: Store = Store(256)

var greeting: Text = "hello"          // static, no allocation
var joined: Text = arena.concat(greeting, " world")
var copy: Text = arena.copy(joined)
var slice: Text = joined.slice(0, 5)

var n: Int32 = joined.byte_len()
var b: Byte = joined.byte_at(0)
var same: Bool = joined == "hello world"
```

- A `Text` value is a descriptor `{ store reference, offset, byte length,
  generation, static data }`. It does not store a pointer in the source model.
- Static literals have a static data pointer and no store; they never become
  stale.
- Arena-backed strings are allocated with `store.copy`, `store.concat`, or
  `store.text`. They follow their Store's lifetime, like handles.
- The descriptor is copied by value. Strings are immutable; slicing and copying
  cannot mutate an existing string.
- `byte_len()` is the byte length, not a codepoint count.
- `byte_at(i)` is bounds-checked and returns a `Byte`. A literal index that is
  negative or at least `N` is a compile-time error.
- `slice(start, length)` is a zero-copy view with the same backing store and
  generation. A slice is not independently NUL-terminated; only the original
  static/arena text has a terminator at its own end. C projection is not
  implemented yet.
- `==`/`!=` compare content, including with `String<N>`. There is no implicit
  conversion between `Text` and `String<N>`.
- `Text` is a single item: it can be a field, union alternative, parameter,
  return type, or `List<Text>` element. It is not a fixed type, so
  `alloc<Text>` and `Handle<Text>` are rejected; use `store.text`.

Store methods:

```nyx
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

```nyx
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
  the store. Capacity may be `0`; a literal negative capacity is a compile-time
  error.
- `append(s)` appends a dynamic `Text` (or a literal typed as one).
- `append_byte(b)` appends one `Byte`.
- `byte_len()` returns the current length.
- `finish()` allocates a fresh immutable `Text` containing a copy of the
  current bytes. The builder remains usable afterward.
- `TextBuilder` is a value. Copying it copies the descriptor, and all copies
  alias the same builder state; appends through one copy are visible through
  another. This is memory-safe but deliberately shared, not unique ownership.
- Builder state and buffers live in the store and end with it.
- Growth allocates a new buffer in the same store, copies the old bytes, and
  abandons the old buffer until the store ends. There is no per-builder free.
- Builder length is `Int32` on the surface; allocation failure is fatal.
- Strings are byte sequences. `append_byte(0)` can embed a NUL; methods use the
  stored length, so this is well-defined. C projection is not implemented and
  must not assume the first NUL is the end.

### `String<N>`

`String<N>` is the fixed string type. Bare `String` is rejected: it has no
capacity. Use `String<N>` for inline fixed text and `Text` for allocated or
static dynamic text.

A fixed string is an immutable value with capacity `N` bytes, stored inline:

```nyx
var name: String<16> = "nyx"
var n: Int32 = name.byte_len()
var b: Byte = name.byte_at(1)
var same: Bool = name == "nyx"
```

- `N` is a byte capacity in `0..max_string_capacity`. The default maximum is
  4096; `CompilerConfig.max_string_capacity` can raise or lower it up to the
  hard limit `NYX_MAX_STRING_CAPACITY` (1 MiB). The by-value size cap still
  applies: a `String<N>` over 64 KiB must live behind a `Handle`, not in a
  local, field, parameter, or return value. `String<0>` can hold only the empty
  string.
- The value carries `N + 1` inline bytes and a length. The byte at
  `data[length]` is NUL, so the layout is directly usable from C; capacity bytes
  after the terminator are zero padding.
- `String<N>` values are copied by value. They do not allocate and are not
  arena-backed.
- `byte_len()` returns the byte length, not the capacity.
- `byte_at(i)` returns a `Byte` and checks `0 <= i < byte_len()`. An
  out-of-bounds index is a deterministic runtime error
  (`nyx: string index out of bounds`).
- `==` and `!=` compare content with `String<N>` of any capacity or `Text`.
  Different capacities are different types for `===`.
- `Text` and `String<N>` are distinct types; the only implemented conversion
  is `store.text(fixed)`.
- String literals are contextual, as described above.

A fixed string can be stored in a Store like any other sized value:

```nyx
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

```nyx
var e: Error = Error("E_CONCAT", "concat failed", "main.nip", 3, 7, 42)

var line: Int32 = e.line
var header: String<32> = e.header
var description: String<128> = e.description
```

The compiler can fill in the source position:

```nyx
var e: Error = Error::throw("E_CONCAT", "concat failed")
```

A call statement whose result type contains `Error` is rejected: the error would
otherwise disappear. Bind the result, inspect it, or use `match` with an `else`
to handle it. A result type contains `Error` recursively through unions,
structs, handles, and collections.

`Error::throw(header, description)` takes the two string fields and fills
`filename`, `line`, `col`, and `pos` from the source file and the call site.
The filename is the compiler's configured source name, truncated to the
configured filename capacity.

- `Error` is a sized value; it does not allocate.
- It can be passed, returned, stored in a Store, stored in a struct field, and
  used as a union alternative, for example `String<8> | Error`.
- `typeof e is Error` narrows a union alternative normally.
- `Error(...)` takes exactly `header`, `description`, `filename`, `line`,
  `col`, `pos`. `Error::throw(header, description)` fills the last four from the
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

A function declaration has a name, zero or more parameters, an optional return
type, and a body containing zero or more statements. Without a return annotation,
the function can fall through, and calls to it are statements rather than values.
The compiler represents its result internally as `Nil`. Each parameter is
`name : Type` and is in scope throughout the body. Parameters may have any value
type. Parameter names within one function must be unique.

Function value scope is closed by design. A function can access its parameters
and its own locals, but cannot capture or read program-level `var` or `const`
bindings. Values a function needs must be passed as arguments. Program-level
stores are the sole exception and remain visible so functions can share
explicitly allocated state. This keeps ordinary dependencies visible at each
call site and makes stores the only deliberate shared mutable state.

A function that returns no value omits its return annotation. It may fall
through or exit with a bare `return`:

```text
fun nothing()
  return
end
```

### Methods and static functions

Associated functions are declared after their concrete struct type:

```text
type Point
  x: Int32,
  y: Int32
end

method Point.move(dx: Int32, dy: Int32): self
  self.x = self.x + dx
  self.y = self.y + dy
  return self
end

static Point::midpoint(a: Point, b: Point): Array<Int32, 2>
  return [(a.x + b.x) / 2, (a.y + b.y) / 2]
end
```

An instance method has an implicit first parameter named `self`. It is a
pointer-like mutable reference to the receiver. The receiver must be a mutable
lvalue formed from a variable/parameter and optional field accesses; temporaries
and deeply const values are rejected. `self` can be used as a value, which
copies the struct, but cannot be reassigned. Thus `return self` returns a value
copy after any field mutations have affected the original receiver.

A static function has no implicit receiver and is called with `Type::name(...)`.
Instance and static functions share one name space within their owner type, so a
type cannot declare both forms with the same name. User-defined associated
functions require a struct type declared earlier in the source. The built-in
`Error::throw` is also a static call.
User-defined instance methods resolve by their receiver type, including when
their names match built-in members on other types.
For a generic struct, the declaration repeats the owner's declared type
parameter names in order: `method Box<T>.replace(value: T): self` or
`static Box<T>::make(value: T): Box<T>`. Those names are in scope throughout
the signature and body. An instance call infers them from its receiver. A
static call specifies the specialized owner, as in `Box<Int32>::make(3)`;
inside a generic body it may use an in-scope type parameter, as in
`Box<T>::make(value)`.

Methods and static functions may also declare their own type parameters after
the function name, as in `method Box<T>.choose<U>(value: U): U`. Calls infer
those parameters from value arguments as for generic functions. A function
parameter cannot duplicate an owner parameter. The combined owner and function
parameter count is limited to eight. Generic associated function bodies are
checked and generated for each concrete combination of arguments when called.
When an untyped string literal is passed directly to a type parameter that is
also the function's return type, an expected `String<N>` or `Text` result type
provides the literal's context. For example:

```text
var label: String<8> = box.choose("ok")
```

Another argument can also supply the string type. A return annotation alone
does not infer a type parameter when no argument supplies that parameter.
Associated functions obey the same closed function scope as other
functions: arguments, locals, and program-level stores are visible, while
program-level `var` and `const` bindings are not.

The statements are:

- `return expression`, or bare `return` in a function without a return type;
- `if condition then ... elseif condition then ... else ... end`;
- `while condition do ... end`;
- `defer do ... end`;
- `parallel ... do ... end`;
- `when ... end`;
- `for bindings in iterable do ... end`;
- `match ... with ... end`;
- a local `var` or `const` declaration;
- an assignment;
- `break`;
- `continue`;
- a call expression used as a statement, whose value is discarded.

An expression statement is accepted only when its expression is a call or a
member call; a bare value is rejected. The value is discarded. Call statements
are allowed at program level and inside function bodies.

`if` conditions and `while` conditions must be `Bool`. `elseif` and `else` are
optional. `elseif` chains are equivalent to nested `if` statements in the
`else` branch.

`break` and `continue` are only valid inside a loop body (`while` or `for`).
They apply to the innermost enclosing loop. `return` may appear anywhere in the
body. A statement after `return`, `break`, or `continue` in the same block is
unreachable and rejected.

Every function with an explicit return type must return on every path. A block returns if one of its
statements returns before control can fall out of the block. An `if` returns
only when it has an `else` and both branches return. A `while true` with no
`break` never falls through, so a function ending in one is accepted. Any other
`while` or `for` never guarantees a return. A function with a return annotation
whose body may reach its end is rejected.

A function whose every return expression calls itself directly is rejected.
Other direct self-recursion is guarded at runtime: exceeding a fixed depth traps
with a deterministic `nyx: recursion depth exceeded` diagnostic instead of
overflowing the C stack.

A function body may declare local variables, but cannot contain a bare nested
block. It can assign its parameters, but it cannot refer to or assign
program-level variables. It can refer to its parameters, to functions declared
before it, and to itself. Because a function is bound before
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
var n: Int32 = identity(Int32(3)) // T = Int32
var m: Bool = identity(true)     // T = Bool
```

Rules:

- Every type parameter must be inferable from the argument types; otherwise the
  call is rejected. Function-specific type arguments have no explicit
  call-site syntax; a generic static owner is specified separately.
- A string literal passed directly to a type parameter can use an expected
  `String<N>` or `Text` result type when that same parameter is the direct
  return type. A typed argument can instead provide the string type.
- Inference handles type parameters in plain type positions, struct
  applications, `Handle<T>`, and function types. In a union such as `T | Nil`,
  one unbound direct type parameter is inferred from the argument alternatives
  not already covered by the other union alternatives. A bare `nil` cannot
  infer `T`. Aliases with unbound parameters are not inference sites.
- A generic function name cannot be used as a value.
- Names with no possible local, function, type, store, or builtin declaration
  are rejected even in an unused generic body. Full scope and type checks run
  once per concrete binding; an unused generic function produces no code.
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
function body is evaluated when it is called. Function-call arguments are
evaluated from left to right, followed by the call. Dynamic-list index
assignment evaluates the receiver and index, checks the index, evaluates the
new value, and checks the index again before writing.

An `if` evaluates its condition, then exactly one branch. A `while` evaluates its
condition before each iteration. `&&` and `||` short-circuit. A struct
constructor evaluates its arguments in the order written, then stores them in
field order. Union injection stores a tag and the payload; `typeof x is T` reads the
tag and has no other side effect. A store allocation bumps the arena offset. A `scope`
evaluates its capacity, enters a fresh Store, runs its body, and releases the
Store on every exit.

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

- The integer types become `int8_t`, `int16_t`, `int32_t`, `int64_t`,
  `uint8_t` (also `Byte`), `uint16_t`, `uint32_t`, and `uint64_t`; `Float32`
  and `Float64` become `float` and `double`; `Bool` becomes `bool`; and `Nil`
  becomes a one-byte `nyx_nil` typedef.
- `Text` becomes a `nyx_string` descriptor with a static-data pointer, store
  reference, byte offset, byte length, and generation. A static literal points at a
  generated read-only byte array. Arena-backed strings are allocated by the
  store runtime and checked for store reference, generation, and bounds on access.
- `TextBuilder` becomes a `nyx_builder` descriptor pointing at an aligned
  builder-state record in the store. The state holds the buffer offset, current
  length, and capacity. Growth and finishing are runtime helpers.
- `Array<T,N>` becomes a `nyx_list_<index>` struct with an inline
  `T data[N]` array (`data[1]` for `N == 0`) and a `uint32_t length`.
- `List<T>` becomes a `nyx_list` descriptor pointing at store-backed state.
  The state holds the element-buffer offset, length, and capacity; push, pop,
  grow, index, and set are runtime helpers.
- `Slice<T>` becomes a `nyx_slice` descriptor with store reference, the
  parent list's state offset, the first element's index, length, and generation. List/sub-slice creation and indexed reads are runtime
  helpers; no allocation occurs.
- `String<N>` becomes a `nyx_string_N` struct with a `uint8_t data[N+1]` array
  and a `uint32_t length`. A literal becomes a compound literal with its byte
  values, a NUL, zero padding, and the literal length.
- `Error` becomes a `nyx_error` struct containing the two fixed-string fields
  and the three `Int32` fields, in declaration order.
- Each struct type, and each concrete instantiation of a generic struct, becomes
  a `typedef struct` with one member per field, in declaration order.
- Each distinct union type becomes a tagged struct: a `uint32_t tag` and a union
  of the alternatives that carry payloads. Nil alternatives carry only a tag.
- Each function type used by the program becomes a function-pointer `typedef`
  named `nyx_fun_N`. The `nyx_` prefix is reserved for all generated names;
  source names that use it are rejected.
- Each non-generic function declaration becomes a C function with a mangled
  name. Each concrete instantiation of a generic function becomes a separate C
  function with a generated name.
- `if`, `elseif`, `else`, `while`, `for`, `break`, and `continue` map directly
  to the corresponding C statements. Conditions are C boolean expressions.
- Union injection becomes a compound literal with a tag and payload;
  `typeof x is T` becomes a tag comparison or, when the answer is static,
  `true`/`false`; narrowing reads the payload member directly.
- `if` expressions lower to conditional expressions. `match` saves its
  scrutinee once, then lowers its arms to conditionals or `if` statements.
- Each value declaration becomes a local initialised by its expression. A
  single `Int32` variable declaration additionally emits `nyx_program_value()`,
  and `main` prints that value followed by a newline.
- Every other program's `main` returns 0 after performing the top-level
  initialisations and statements.
- Unused initialised variables get a `(void)name;` line so that the emitted C
  compiles without warnings. Unused parameters get the same treatment at the
  start of the function.
- Each declaration and statement is preceded by `#line <line> "<source>"`, so a
  C compiler diagnostic points at the Nyx source rather than the generated
  file.
- A store becomes an entry in a global `nyx_store` table. The generated source
  carries the arena helpers: initialize, grow, allocate, bounds/generation
  checks, write, and free. A `scope` is a C block holding the Store header, with a per-activation cleanup chain. `Handle<T>` becomes the opaque
  `nyx_handle` struct. Call statements become `(void)(call);`.
- Text helpers check `byte_at` bounds and compare bytes for equality. Error
  field methods become direct struct-member reads. The helper runtime is emitted
  only when the program uses a string or Error type.

## Diagnostics

Compilation is fail-closed. If any declaration or statement is invalid, the
whole program is rejected and no C is emitted; there is no partial output.
The parser resumes at a later declaration, statement, or match arm when it can
find a safe boundary. The checker reports independent errors in declarations,
blocks, and match arms. Recovery is conservative where the next boundary is
ambiguous, so a compilation does not necessarily report every error. After a
syntax error, recovery may use a later line as a hint for an unmatched
delimiter; line breaks do not otherwise delimit statements.

Source-level diagnostics carry `file:line:column (byte offset): ` followed by a
stage-prefixed message, for example:

```text
example.nip:1:9 (byte 8): checker: unknown type 'Float' for 'x'
```

The prefix identifies the source file, the 1-based line and column of the
offending construct, and the 0-based byte offset. Driver-level failures (a file
that cannot be read, an unwritable output path, a failing C compiler) are not
source-positioned but do name the path involved.

Runtime failures identify the source file, line, column, and byte offset after
the `nyx:` prefix. The recursion-depth guard identifies the recursive
function's declaration; other failures identify the operation that failed.
Runtime failures flush standard output before writing their diagnostic to
standard error.

If compilation fails with no diagnostic recorded, the message is exactly
`Unknown Error`.

## Not yet decided or not yet implemented

- Compound assignments (`+=`, `-=`, etc.), chained assignment, and assignment
  as an expression.
- Loop labels.
- Closures and capturing program-level variables.
- Mutual recursion between functions, because function forward references are not accepted.
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
  the implemented dynamic-memory model; there is no per-object free and no
  first-class store value.
- Exposing handle fields (`index`, `store`, `generation`); handles are opaque.
- Compile-time overflow errors for numeric widths other than `Int32`, and
  `const` bindings of other numeric types inside constant expressions.
- Runtime base64 encoding and decoding (`base64` is a compile-time literal
  form only), hexadecimal byte-string literals, and long base64 blobs, since a
  string literal cannot span lines.
- `Float16`, and an implicit or literal conversion from an integer literal to a
  float type; `Float64(1)` is the spelling today.
- UTF-8 validation, codepoint indexing, and string ordering/search.
- The `build ... as ... do` block syntax; `TextBuilder` is first-class now.
- C string projection (`cstr`), foreign string adoption, and explicit
  dynamic-to-fixed conversion.
- Finer lifetime summaries: calls are checked per field only for struct
  parameters of two to eight fields (not for nested paths or union alternatives),
  and the analysis is conservative after reads from a Store and at function
  values.
- A chunked non-moving allocator, recycling across calls, and removing the
  runtime generation fields.
- Array slices and dynamic list literals.
- Scoped `access` blocks for reading store data; `read` currently copies.
- Channels that carry anything but a plain value (a `Text`, a `List`, a handle,
  or a Store), channels allocated in any Store but a parallel pool, pool
  rendezvous channels, and a pool channel as a field or a result.
- Collecting the results of a repeated `parallel` arm into a pool-backed `List`
  (today they fill a fixed `Array`), and passing existing Store data to an arm
  by reference.
- A deadlock report that accounts for threads blocked in file or socket
  operations, once the language has them.
- Modules and imports.
- Whether a bare expression may be a whole program (today it is rejected: a
  program is a sequence of items, and a bare expression is not an item).
- Whether type resolution stays string comparison or becomes a real type table.
- Exact wording and formatting of diagnostics beyond the prefix described above.
