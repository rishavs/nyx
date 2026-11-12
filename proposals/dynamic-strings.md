# Dynamic Strings

**Status:** core and first-class StringBuilder implemented; region checking deferred
**Goal:** add dynamic-length, arena-backed strings to Nippy while keeping the
fixed `String<N>` type and the checked-handle memory model.

## Implementation status

Implemented in the current compiler:

- bare `String` as a dynamic, immutable descriptor;
- static dynamic string literals with no allocation;
- `byte_len`, `byte_at`, `slice`, and content equality;
- `store.concat`, `store.copy`, and `store.string` (fixed to dynamic);
- dynamic strings as parameters, returns, struct fields, union alternatives,
  `Handle<String>` elements, and assignment values;
- `StringBuilder` with `append`, `append_byte`, `byte_len`, and `finish`;
- runtime store-id, generation, and bounds checks.

Deferred:

- the `build ... as ... do` block syntax;
- C string projection and explicit dynamic-to-fixed conversion;
- compile-time region/escape checking, which depends on scoped arenas;
- UTF-8 validation and codepoint APIs.

The runtime model is still the current program-level Store with `reset()`, so
dynamic strings are invalidated by reset just like handles. The region examples
below describe the intended scoped-arena model, not what the compiler enforces
today.

## Summary

Nippy currently has only fixed-capacity inline strings:

```nippy
var name: String<16> = "nyx"
```

This proposal adds a dynamic `String` type:

- arbitrary byte length, allocated in a `Store`;
- immutable values;
- zero-copy slices;
- content equality;
- arena-backed and region-checked like handles;
- static string literals that need no allocation;
- explicit concatenation into a chosen store;
- a scoped builder for efficient incremental growth.

`String<N>` remains the fixed inline type. `String` and `String<N>` do not
convert implicitly.

## Goals

1. Dynamic length without a fixed capacity in the type.
2. No raw pointers in the surface language.
3. Immutable values, so copying and slicing cannot create mutable aliases.
4. Bounded per-request/frame memory when used in scoped arenas.
5. Zero-copy slicing.
6. UTF-8 by convention, byte-indexed.
7. C interoperability through NUL-terminated data.
8. Compile-time region checking consistent with scoped arenas.
9. Efficient repeated append through a builder, without exposing mutable shared
   strings.

## Non-goals

- A general-purpose `StringBuffer`/`Vec<Byte>` API.
- Codepoint indexing or Unicode normalization.
- UTF-8 validation on every construction.
- Implicit conversion between `String`, `String<N>`, and C strings.
- In-place mutation of `String` values.
- Per-object free; strings live in an arena.

## Type model

```text
String       dynamic-length, immutable, arena/static-backed
String<N>    fixed-capacity, inline (existing)
StringBuilder first-class store-backed builder
```

A `String` value is a **span descriptor**:

```text
String = { store id, byte offset, byte length, generation, region }
```

- The store id is a sentinel for static literal data.
- The offset is a byte offset, not a pointer, so arena growth is safe.
- The generation checks stale values after reset; with the scoped-arena model,
  reset is not used, so generation is defensive.
- The region is the lifetime of the backing store; static literals are
  `'static`.
- There is no capacity in `String`; capacity belongs to a builder.

`String` is immutable. Slicing and copying only copy the descriptor.

## String literals

A literal can be typed as either fixed or dynamic according to context:

```nippy
var fixed: String<8> = "nyx"   // inline fixed value
var dynamic: String = "nyx"    // static dynamic String
```

A dynamic string literal is stored in a static read-only blob and has a static
region. It needs no store and never becomes stale.

The literal is NUL-terminated in the blob. `byte_len()` excludes the NUL.

Rules:

- expected `String<N>`: fixed literal, length checked against N;
- expected `String`: static dynamic literal, no allocation;
- no context: still an error, as for fixed strings today;
- a union containing both `String` and `String<N>` alternatives must be
  disambiguated by context or rejected as ambiguous.

## Operations

### Basic

```nippy
var s: String = "hello"

var n: Int32 = s.byte_len()
var b: Byte = s.byte_at(0)
var tail: String = s.slice(1, 4)      // "ello", zero-copy
var same: Bool = s == "hello"
```

- `byte_len()` is the byte length, not a codepoint count.
- `byte_at(i)` is bounds-checked and returns a `Byte`.
- `slice(start, length)` returns a view with the same store and region.
- `==`/`!=` compare content bytewise.
- All methods are safe on static and arena-backed strings.

### Concatenation

Concatenation returns a new string and requires a destination store:

```nippy
var joined: String = concat(borrow request, "hello ", name)
```

or as a store method:

```nippy
var joined: String = request.concat("hello ", name)
```

Recommended surface:

```text
store.concat(a, b) -> String
store.copy(s)     -> String
store.string(s)   -> String   // copy a fixed String<N> into the store
```

- `concat` allocates `len(a) + len(b) + 1` bytes in the store, copies both, and
  writes the NUL.
- `copy` duplicates an existing dynamic string into the destination store. This
  is the explicit way to move a value from a scoped store to an outer store.
- `string` copies a fixed `String<N>` into the store as a dynamic String.
- The result is in the destination store's region.
- No implicit concatenation operator. `+` remains arithmetic.

### Slicing

```nippy
var word: String = s.slice(6, 5)
```

`slice` is a zero-copy view. The result has the same store and region as the
source. A slice of a scoped string cannot escape that scope.

## Builder

Repeated `concat` is quadratic because it allocates a new string every time.
The builder provides amortized growth without making `String` mutable.

The implemented builder is a first-class store-backed value:

```nippy
global arena: Store = Store(256)

var b: StringBuilder = arena.builder(16)
b.append("hello")
b.append_byte(44)
b.append(" world")
var s: String = b.finish()
```

- `store.builder(capacity)` allocates builder state and an initial buffer in the
  store. Capacity may be zero.
- `append(String)` appends a dynamic string or a literal typed as one.
- `append_byte(Byte)` appends one byte.
- `byte_len()` returns the current length.
- `finish()` allocates a fresh immutable `String` copy. The builder remains
  usable.
- Growth allocates a larger buffer in the same store, copies the old bytes, and
  abandons the old buffer until reset.
- `StringBuilder` is a value. Copies of the descriptor alias the same builder
  state, so appends through one copy are visible through another.
- `finish()` copies, so a finished `String` is not affected by later appends.
- `StringBuilder` is invalidated by `store.reset()` like handles and strings.

A future `build ... as ... do` block could hide the builder binding and prevent
aliasing, but it is not required for the first implementation. The current
first-class builder needs no move semantics or uniqueness rules.

## Storage and lifetimes

Dynamic strings follow the handle/region rules:

- `String` carries its backing region.
- A scoped string cannot escape its scope.
- A static literal is `'static` and can be used anywhere.
- A program-level store's strings are `'static` and persist until exit.
- Moving a string to an outer scope requires `store.copy` or `store.concat` into
  that outer store.
- `slice` keeps the same region as its source.

Examples:

```nippy
global root: Store = Store(1024 * 1024)

scope request = Store(16384) do
  var b: StringBuilder = request.builder(64)
  b.append("/users/")
  b.append(user_id)
  var path: String = b.finish()

  var body: String = request.concat("path=", path)

  // Explicitly retain a copy in the root store.
  root_string = root.copy(body)
end
```

`root_string` is `'static`; `body` dies with `request`.

## Memory growth and servers

The same limitation as handles applies:

- program-level strings grow until process exit;
- scoped strings are reclaimed at scope exit;
- there is no per-string free.

A server should keep transient response strings in request scopes and only copy
small summaries into the root store. Caches and sessions need dynamic arenas, as
described in the scoped-arena proposal.

A builder in a per-request scope is ideal: growth is bounded by the request and
reclaimed at the end.

## UTF-8 and NUL

- Strings are byte sequences. UTF-8 is a convention, not enforced.
- `byte_len` and `byte_at` are byte-oriented.
- Every full dynamic string is NUL-terminated for C interop; `byte_len` excludes
  the terminator. A `slice` is a view and is not independently NUL-terminated,
  so C projection of a slice must copy or add a terminator.
- String literals cannot contain NUL. A builder can append NUL bytes; methods
  are length-based. A future C projection must not treat the first NUL as the
  end in that case.
- Codepoint APIs, validation, normalization, and case folding are future work.

## C interoperability

`String` to C:

- the runtime resolves the span to a `const char *` at the call boundary;
- the pointer is valid only while the backing store is alive and not moved;
- C must not retain the pointer across a later allocation or scope exit;
- a C-facing helper can copy into C-owned memory if retention is required.

C to `String`:

- a returned `const char *` is not adopted implicitly;
- it must be copied into an explicit store with `store.copy_cstr` or an
  equivalent interop helper;
- Nippy does not take ownership of foreign memory.

Static literal pointers are stable for the program lifetime.

## Fixed and dynamic strings

No implicit conversion:

```text
String<N>  -> String    explicit store.string(fixed)
String     -> String<N> explicit checked/fallible conversion (future)
```

The fixed type remains useful for small bounded fields, C structs, and protocol
constants. The dynamic type is for request bodies, paths, formatting, and
user-generated text.

## Alternatives considered

### Mutable String with capacity

A `String` value that appends in place would share its buffer through copies.
Appending through one alias would be visible through another, and growth would
invalidate other descriptors. Solving that requires unique ownership or
copy-on-write. The builder block avoids the problem without those concepts.

### `Handle<StringBuffer>`

A handle to a mutable buffer would make strings indirect and would expose
aliasing. `String` as an immutable span is cheaper to copy and slice.

### Concat only

`concat` is simple, but incremental formatting is quadratic. A first-class
`StringBuilder` was implemented instead; the `build ... as ... do` block remains
a possible future ergonomic wrapper.

## Implementation phases

1. Parse bare `String` as the dynamic type; keep `String<N>` fixed.
2. Add static dynamic string literals with a static region and sentinel store.
3. Add the `String` descriptor, `byte_len`, `byte_at`, `slice`, and equality.
4. Add store-backed `concat`, `copy`, and `string` with region checks.
5. Add the `build ... as ... do ... end` block and builder emission.
6. Add C interop `cstr` projection and explicit copy-in.
7. Add fixed-to-dynamic and checked dynamic-to-fixed conversions.
8. Add tests for scoped escape, growth, slicing, NUL, and C boundaries.

## Open questions

1. Is `build ... as ... do` the right syntax, or should the builder be a
   first-class move-only value later?
2. Should a literal with no context default to static `String`, or remain
   context-only like fixed literals today?
3. How is a union containing both `String` and `String<N>` disambiguated?
4. Should `concat` be a store method, a free function, or both?
5. Do we allow embedded NUL with a separate C-interop caveat?
6. Is UTF-8 validation ever performed, or is it always byte-oriented?
7. How much capacity slack should a builder reserve, and is it observable?
8. Should `String` be allowed as a `Handle<String>` allocation element? It is
   sized; the answer affects copying and lifetime notation.
