# Nippy architecture

How the compiler is put together, what each stage owns, and why. This file
describes the code as it exists; language rules live in `SEMANTICS.md`, the
accepted source language in `GRAMMAR.ebnf`, the C API in `API.md`, and build
test instructions in `BUILD.md`.

## Shape

Nippy is a translator, not an interpreter. It reads a `.nip` source string and
produces two C11 strings (a `.c` and a `.h`). The driver writes them beside the
requested output binary and shells out to `cc -std=c11` to produce the
executable. There is no bytecode, no VM, and no runtime library.

The compiler is written in C11 and links against libc only.

## Pipeline

```text
source  ->  tokens  ->  syntax tree  ->  typed program  ->  C11 text
            lexer.c     parser.c         checker.c          generator.c
                                        analyser.c
```

`compiler/compiler.c` drives the order. Each stage consumes the previous stage's
artifact through a `const` pointer and produces a new artifact; none of them
writes into the artifact it was given.

The driver frees the syntax tree as soon as the checker returns, so the analyser
and generator cannot reach it even by accident. That is the pipeline's
forward-only rule enforced structurally rather than by convention.

| Stage | Consumes | Produces | Source |
|---|---|---|---|
| Lexer | source string | `LexerResult`: token list, or an error list | `compiler/lexer.c` |
| Parser | token list and string-byte pool | `Ast`: flattened syntax tree | `compiler/parser.c` |
| Checker | syntax tree | `TypedProgram`: typed, link-resolved program | `compiler/checker.c` |
| Analyser | typed program | nothing (validates only) | `compiler/analyser.c` |
| Generator | typed program | owned C11 `.c` and `.h` text | `compiler/generator.c` |

### Forward-only rules

These are not style preferences; they keep each stage independently testable and
the diagnostics attributable to a single stage.

1. **One artifact per stage.** A stage consumes the artifact its immediate
   predecessor produced, and nothing else. The generator sees the typed program,
   never the token list.
2. **Read-only inputs.** Every stage takes its input through a `const` pointer
   and produces a *new* artifact. It never writes into the artifact it was
   given.
3. **No reaching back.** No stage re-derives or re-walks an earlier artifact to
   recover information it should have been handed.
4. **Nothing after the generator.** The C11 text is the final artifact. No
   post-pass rewrites, reformats, or patches emitted code; anything the output
   needs must be decided before it is emitted.
5. **Forward-only diagnostics.** A stage reports against its own input. An error
   is not discovered in a later stage and retrofitted onto an earlier one.

Each phase consumes validated output from the previous phase. Do not reparse or
reinterpret invalid output, and do not add back channels between phases. Make
facts needed downstream explicit in the preceding representation.

Corollary: the four artifacts are four distinct things. If two stages share one
in-memory object, the pipeline is not forward-only, however clean the call
sequence looks.

### Lexer

Scans the source once, tracking `line` and `column` alongside the byte offset so
every token and every lexer error carries a `SourcePos`. It returns *either* a
token list *or* an error list, never both. If any error is recorded the token
list is discarded, so the parser never sees a partial program. All whitespace,
including newlines, is skipped; it only separates tokens. Line boundaries have
no syntactic meaning and indentation is not significant.

Tokens are plain values stored inline in a `List`: no pointers, so the list can
be moved or freed without walking its contents. string literals are the one
variable-length token payload: decoded bytes live in `LexerResult.text_bytes`,
and the token stores an offset and length into that pool. Escapes are decoded
during scanning; NUL bytes, raw newlines, unterminated literals, and unknown
escapes are lexer errors. Keyword recognition happens
during scanning (`global`, `var`, `type`, `fun`, `return`, `if`, `then`,
`elseif`, `else`, `while`, `for`, `in`, `do`, `break`, `continue`, `true`,
`false`, `end`); everything else is
an identifier. Two-character operators (`==`, `!=`, `<=`, `>=`, `&&`, `||`) are
recognised before their single-character prefixes. A double quote starts a
string literal.

### Parser

Recursive descent over the token list, producing a flattened `Ast`. Nodes are
stored inline in `Ast.nodes` and refer to each other by index, with
`AST_NO_NODE` (`(size_t)-1`) meaning "no child". Top-level declarations are
recorded in `Ast.roots`, in source order.

Nodes whose arity varies — struct fields, function parameters, blocks, `if`
branches, call arguments, union alternatives, type arguments, and function-type
parameters — store their children as one contiguous run of indices in
`Ast.children`, recorded by `children_start` and `children_count`. A child
expression or statement can therefore push its own nodes without splitting its
parent's run: every variable-arity node appends its run only after all of its
children have been parsed. Binary, unary, control-flow condition, and leaf nodes
use `left_child`/`right_child` directly. This keeps the flat-index model intact
without adding sibling links or pointers.

Type expressions are parsed recursively: `AST_TYPE_NAME`, `AST_TYPE_UNION`,
`AST_TYPE_APPLICATION`, `AST_TYPE_FIXED_STRING`, `AST_TYPE_LIST`,
`AST_TYPE_FIXED_LIST`, `AST_TYPE_SLICE`, `AST_TYPE_FUNCTION`, and parenthesized
groups. `List<T>` is dynamic; `Array<T,N>` is fixed-capacity with a runtime
length; `Slice<T>` is a read-only view over a dynamic list. A `[a, b]`
expression becomes `AST_LIST_LITERAL`; index access becomes `AST_INDEX` and
index assignment becomes `AST_INDEX_ASSIGNMENT`. `String<N>` is parsed specially because `N` is an integer
capacity, not a type argument; bare `Text` is an ordinary type name resolved to
the dynamic string type. A
declaration of the form `type Name = Type` becomes `AST_TYPE_ALIAS`; the
field form remains `AST_TYPE_DECLARATION`. Generic declarations put their type
parameters in a separate `Ast.type_parameters` run, recorded on the declaration
by `type_parameters_start`/`type_parameters_count`. A value postfix
`typeof x is T` becomes `AST_IS`; `nil` becomes `AST_NIL_LITERAL`; `x = value`
becomes `AST_ASSIGNMENT`; a `global` store declaration sets
`AstNode.is_global`; a string literal becomes `AST_STRING_LITERAL`, with its bytes copied from the lexer pool into
`Ast.bytes` and referenced by offset and length. Parentheses are flattened away
during parsing.

Function bodies are blocks of statements. `if`/`elseif`/`else` is parsed as an
`AST_IF` whose children are the then-block and optional else-block, plus
`AST_IF_EXPRESSION` for expression branches; an
`elseif` chain is desugared into nested `if` nodes in the else branch. `while`
stores its condition and body block. `for` stores its optional two bindings,
iterable, and body block. Statements are `return`, `if`, `while`, `for`,
`break`, and `continue`.

Identifiers are interned: `ast_intern_symbol` maps text to an index into
`Ast.symbols`, and nodes hold the index. Re-interning the same spelling returns
the same index.

The parser accepts only what it can build: it never partially reports a
declaration. Function-type parameters, function parameters, type fields, and
call arguments are gathered in scratch lists and appended as one run once their
whole construct has been parsed, which keeps each node's child run adjacent.

### Checker

Reads the syntax tree through a `const` pointer and writes a separate
`TypedProgram`. It does the semantic work the later stages must not repeat:

- resolves type expressions against built-ins (`Int32`, `Bool`, `Byte`, `Nil`,
  `Text`, `String<N>`, `TextBuilder`, `List`, `Array`, `Slice`, `Store`,
  `Handle`, `Error`), declared
  structs and aliases, generic applications, unions, and structural function
  types;
- expands aliases under a type-parameter environment, instantiates generic
  structs on demand, flattens/deduplicates/sorts union alternatives, and interns
  structurally identical unions;
- rejects duplicate top-level names, duplicate parameters and type parameters,
  redefinitions of a built-in, generic-arity mistakes, and by-value recursive
  types;
- builds `TypedType`, `TypedFunctionSignature`, and signature-parameter records
  in `TypedProgram`; a struct type carries its concrete declaration node and a
  generic instantiation carries its type-argument run;
- checks constructor arity, field names, and argument types, for both positional
  and named argument lists; generic constructors infer their type arguments from
  the arguments;
- checks function-call arity and argument types against the callee's function
  type, including calls through function-typed variables and parameters;
- types the reserved `print` builtin as a statement with any number of
  arguments, contextualising string literals as `Text` and checking that each
  argument is printable;
- types `Error.throw(header, description)` as an `Error` whose filename and
  source position come from the compiler configuration and call site;
- types boolean and nil literals, `typeof x is T`, and the comparison, equality, and
  logical operators;
- checks `if`/`while` conditions are `Bool`;
- checks `for` iterables and binds the loop value and optional `Int32` index;
- checks every `return` expression against the return type, and rejects
  `break`/`continue` outside a loop;
- proves that every function returns on every path, using the structured
  statement tree (a `while` or `for` never guarantees a return);
- tracks `const` bindings and rejects assignment to them; `const` does not
  restrict mutation through collection values;
- checks assignment targets and types, and deactivates any active narrowing
  of the target after the right-hand side has been checked;
- infers a variable's type from its initializer when the annotation is omitted,
  and checks assignability when both sides state a type;
- injects an alternative value into a union at initializers, returns, call
  arguments, and constructor arguments;
- narrows `name` to `T` inside the then-block of `if typeof name is T then`, and
  emits a payload projection node for uses of the narrowed name;
- types store declarations, records their ids and constant byte sizes, and
  makes store names visible to function bodies without making them values;
  `global` declarations are required to have type Store;
- types `alloc<T>(value)`, `reset()`, `read()`, and `write(value)`; requires a
  fixed type for the allocation element (no allocator-backed fields) and builds
  `Handle<T>` records; `Text`, `List`, and `TextBuilder` use their dedicated
  constructors;
- types integer literals as `Byte` in Byte contexts and range-checks them;
- types string literals as fixed `String<N>` or static dynamic `Text` from
  context, checks fixed byte length against capacity, and rejects uncontextual
  literals and ambiguous union alternatives;
- types `byte_len()`, `byte_at(i)`, `slice(start, length)`, and content equality
  for dynamic strings, plus `store.concat`, `store.copy`, and `store.text`;
- types `store.builder(capacity)` and the `TextBuilder` methods `append`,
  `append_byte`, `byte_len`, and `finish`;
- types fixed `Array<T,N>` literals with capacity/length and element coercion,
  plus dynamic `store.list<T>(capacity)`, indexing, `push`, `pop`, and `len`;
  mutation requires a list variable; function, union, and `Text` elements are
  accepted; collections are rejected as direct elements; `pop` builds and
  tag-remaps the optional union;
- types `Slice<T>` views, list/sub-slices, and read-only slice indexing;
- types fixed-string-keyed `Record<K,V,N>` and `Table<K,V>` maps, including
  Record literals, indexing, `has`, `remove`, and `len`;
- types `Text` as a single item and `TextBuilder` as a collection, so `Text`
  can be a struct/union/parameter/return and `TextBuilder` cannot be an
  `alloc`/`Handle` element;
- builds the built-in `Error` type and its three configured fixed-string field
  types, checks `Error(...)` argument count and types, and types its six field
  methods;
- copies string bytes into `TypedProgram.bytes`, so the typed program outlives
  the syntax tree;
- rejects store/function field and union-alternative types, and Store in
  parameters, returns, and ordinary values;
- monomorphizes generic functions: a call infers type arguments from the value
  arguments, checks the generic body under that concrete binding (with a fresh
  AST-to-typed mapping), and emits a concrete function. Recursive calls reuse
  the in-progress instantiation;
- copies every name it keeps into `TypedProgram.symbols`, so the typed program
  outlives the syntax tree.

The checker owns four linear tables: declared types (`TypeEntry`), functions
(`FunctionEntry`), program-level variables, and the current function's
parameters, plus linear instantiation tables for generic structs and functions.
Type identity is structural for unions and functions and nominal for structs.
Union alternatives are stored in a canonical order, so structural equality is a
linear walk.

### Analyser

A read-only pass over the typed program that validates its internal links:
root indices, direct child indices, variable-arity child indices, symbol
indices, type indices, function-signature links, union and struct type-child
runs, struct declaration links, handle element types, store ids, string-literal
byte ranges and capacity, the shape of Error construct/field nodes, and the
auxiliary type on an `.is` test. It
produces no artifact, because memory and ownership analysis does not exist yet.
When the bidirectional-pointer rules land, this stage either refines the typed
program or is split out into a distinct analysis artifact — it must not become a
second mutation pass over the syntax tree, which it never receives.

### Generator

Consumes the typed program only. It emits:

- **the header**, holding the generated include guard, `<stdbool.h>`,
  `<stdint.h>`, `<stddef.h>` when a store or handle is present, the
  `nippy_nil` typedef when Nil is used, the opaque `nippy_handle` and
  `nippy_store` records, one inline `nippy_string_N` struct per fixed-string
  capacity, the `nippy_error` struct when Error is used, one `typedef struct`
  per struct type and generic instantiation, one tagged `typedef struct` per union type, one
  function-pointer `typedef` per function signature (`nippy_fun_N`), and — when
  the program is a single integer variable — the declaration of
  `nippy_program_value`;
- **the source**, holding `#include "<generated header>"`, the arena runtime
  when a store is declared, the string bounds/equality helpers when a string or
  Error type is used, one function definition per non-generic function
  and per concrete generic instantiation, one `#line` directive per emitted
  declaration or statement, the variable initializers, store init/free calls,
  call statements, and `main`.

Type definitions are emitted from `TypedProgram.types` in index order. The
checker creates types bottom-up, so this order is topological and every type
appears after the types it mentions. Type-declaration nodes that are also
program roots are skipped during the root pass to avoid emitting them twice.

Generated C is internal IR. It is not required to be human-readable or
formatted, and its text is not a stable interface. It *is* required to compile
cleanly under `-std=c11 -Wall -Wextra -Wpedantic`, which is why unused
initialised variables and unused parameters get a `(void)name;` line. Every
declaration and statement emitted is preceded by `#line <line> "<source>"` so a
C-compiler diagnostic points back at the Nippy source. Function types are
emitted as typedefs so every generated declaration is a simple `type name` pair,
including function-typed parameters and variables. `Bool` maps to `bool`;
`Nil` maps to a one-byte `nippy_nil`; a union is a `uint32_t tag` plus a union
of payload members named by alternative position. `if`, `while`, `for`, `break`, and
`continue` are emitted as the corresponding C statements. `print` emits a
sequence of temporaries and generated per-type pretty-printer helpers; helpers
recurse through collections, structs, and unions using
`CompilerConfig.max_print_depth` as the runtime limit.
`Error.throw` emits a compound literal with the source filename and position. `elseif` chains
are emitted as nested `else { if ... }` blocks. A `TYPED_SUM_INJECT` becomes a
compound literal, `TYPED_IS` (from `typeof x is T`) becomes a tag comparison (or a literal when the
checker decided it statically), and `TYPED_SUM_PAYLOAD` reads the payload member
directly.

A fixed `TYPED_STRING_LITERAL` becomes a compound literal with its byte values, a
NUL, zero padding, and the literal length. A dynamic literal points at a
generated `static const uint8_t` array and becomes a `nippy_string` descriptor.
`TYPED_STRING_LEN` reads the stored length (checking staleness for dynamic
strings), `TYPED_STRING_AT` calls a bounds-checked accessor, and string equality
calls a byte comparison. Dynamic `slice`, `concat`, `copy`, and fixed-to-dynamic
conversion are emitted as `nippy_string_*` runtime helpers. `TextBuilder`
becomes a `nippy_builder` descriptor pointing at store-backed state; append,
growth, length, and finish use `nippy_builder_*` helpers. Arrays become
`nippy_list_<index>` structs and fixed `get` helpers. Dynamic lists become a
`nippy_list` descriptor pointing at store-backed state; create, push, pop,
index, set, and length use `nippy_list_*` helpers. Arrays carry an inline
length and have per-type push/at/pop helpers. Fixed function elements emit an
inline element typedef; function union payloads are emitted inline. `Slice<T>`
uses a `nippy_slice` descriptor and non-owning read helpers. `TYPED_ERROR_CONSTRUCT`
becomes a `nippy_error` compound literal and `TYPED_ERROR_FIELD` reads one of its
members.

A `TYPED_ASSIGNMENT` emits a C assignment to the target variable; the value is
already coerced to the declared type, including union injection. Narrowing is
not a runtime construct, so deactivating it in the checker is enough.

A `TYPED_STORE_DECLARATION` becomes an entry in the global store table and an
init call at the start of `main`; it is never a C local. `TYPED_STORE_ALLOC`
calls the arena allocator with `sizeof(T)` and `_Alignof(T)`, `TYPED_HANDLE_READ`
dereferences the checked handle pointer, `TYPED_STORE_RESET` clears the store and
bumps its generation, and `TYPED_HANDLE_WRITE` copies through the checked
pointer. The runtime grows by doubling, retains capacity on reset, and panics
deterministically on OOM, stale handles, invalid store ids, and out-of-range
handles. `TYPED_EXPRESSION_STATEMENT` emits `(void)(call);`. Record and Table helpers
use FNV-1a hashing with linear probing; Table helpers allocate arena state and
rehash on growth.

Because the number of declarations and fields is not fixed, the generator
composes text in a growable `TextBuffer` and validates every interpolated
identifier and filename before use.

## Failure behaviour

The compiler is fail-closed: a stage either produces a complete artifact or
reports a diagnostic and returns false. There is no partial success.

- Every allocation is checked. `compiler/alloc.c` wraps `malloc`/`realloc` so
  the test suite can force an allocation to fail and prove each error path.
  The wrappers are plain pass-throughs in normal operation.
- A stage that fails does not hand its artifact on; the driver frees what was
  acquired and reports.
- If compilation fails with no diagnostic recorded, the message is exactly
  `Unknown Error`, produced by `error_unknown_if_none`.
- `CompilerResult` either holds both generated sources or neither. On failure
  `c_source` and `h_source` are `NULL` and only `error_message` is set.
- The driver refuses sources larger than 4096 bytes and sources containing a
  NUL byte, and returns non-zero without writing a binary when it refuses.

## Diagnostics

Every message the compiler can emit lives in `include/messages.h` as a
`MESSAGE_*` macro; no stage writes a user-facing string inline. Tests reference
the same macros, so wording and assertions cannot drift apart, and
`make test` fails if a message is added without a test that invokes it.

Source-level diagnostics are prefixed `file:line:column (byte offset): `, where
line and column are 1-based and the offset is a 0-based byte index. Messages
with no source position (driver file I/O, out-of-memory) are emitted without the
prefix but still carry the offending path where there is one.

## Data structures

Collections are `List` (`include/list.h`), the only owning dynamic container:
elements are stored inline, `list_push` copies a value in, `list_pop` copies it
out before shrinking. Push doubles the allocation (minimum capacity 4); pop
halves it once the length falls to a quarter of capacity. Because elements are
stored inline and no element holds a pointer, a list can be moved or freed
without walking its contents — which is why identifiers are fixed-size
`char[32]` arrays referenced by index rather than `char *`.

Fixed-size records and buffers are used where the size is a hard language limit:
`Token.text`, `AstSymbol.text`, and `TypedSymbol.text` are
`char[NIPPY_IDENTIFIER_MAX]`, and `ErrorState.message` is a fixed
`ERROR_MESSAGE_CAPACITY` (4096) byte buffer whose truncation is handled.

`Ast` keeps a separate `type_parameters` run because a generic declaration's
value children and type parameters must not share one run. string literal bytes
are variable-length too, so `LexerResult.text_bytes`, `Ast.bytes`, and
`TypedProgram.bytes` are byte pools; tokens and literal nodes store offsets and
lengths rather than pointers. `TypedType.fixed_length` is the byte capacity of a
`String<N>`. `TypedType` also records a union's alternatives, a struct
instantiation's type arguments, or a `Handle<T>`'s element type as a run in
`TypedProgram.type_children`, a struct's concrete declaration node, and the
source name used for generic inference. `TypedNode.store_id` links a store
declaration, allocation, or reset to its global store. The checker keeps its declared-type,
function, variable, struct-instantiation, and function-instantiation tables as
linear `List`s of fixed-size records; a `List` element never owns another
`List`.

Two structures sit outside `List` by design:

- `TextBuffer` (`compiler/generator.c`) is a growable `char *` used to compose
  generated source, since the output is text rather than an array of elements.
- `LexerResult`, `Ast`, `TypedProgram`, `CompilerResult`, and `ErrorState` are
  plain structs that aggregate lists, counts, and flags.

The checker's AST-to-typed index map, struct type table, function table,
variable table, and the parser's scratch lists are all `List`s of fixed-size
records (or of `size_t`), searched linearly. `Ast.children` and
`TypedProgram.children` hold variable-arity child indices, including block
statements and `if` branches; `TypedProgram.types`, `.signatures`, and
`.signature_parameters` hold resolved types. All of these are copied indices,
never pointers.

## Module layout

| Path | Contents |
|---|---|
| `compiler/` | One `.c` per stage, plus the driver and shared helpers |
| `include/` | Public headers, same names as their implementation |
| `tests/` | `harness.c` shared reporting, plus one source per suite |
| `bin/` | Build output (git-ignored) |
| `tmp/` | Scratch space for temporary scripts and probes (git-ignored) |

| File | Role |
|---|---|
| `compiler/main.c` | Command-line driver: read, compile, write, invoke `cc` |
| `compiler/compiler.c` | `compiler_compile`: drives the stages, owns the result |
| `compiler/lexer.c` | Source text to tokens or lexer errors |
| `compiler/parser.c` | Tokens to flattened `Ast` |
| `compiler/checker.c` | `Ast` to `TypedProgram` |
| `compiler/analyser.c` | Validates `TypedProgram` links |
| `compiler/generator.c` | `TypedProgram` to C11 text |
| `compiler/ast.c` | `Ast` lifecycle and symbol interning |
| `compiler/typed_program.c` | `TypedProgram` lifecycle and symbol copying |
| `compiler/list.c` | The owning list container |
| `compiler/error.c` | Fixed-buffer diagnostic accumulation |
| `compiler/alloc.c` | Checked allocation wrappers with a test-failure hook |

## Known gaps

- No incremental compilation; `make` rebuilds every translation unit.
- No concurrency story.
- Program-level variables are not visible inside function bodies; only stores
  are. Local variables are block-scoped and cannot shadow parameters or other
  visible names.
- No ownership or lifetime analysis; the analyser validates links only.
- The type, function, and variable tables are linear string searches.
- Function types are emitted as one typedef per signature occurrence, so
  duplicate but structurally identical typedefs are common.
- Generic calls only infer type arguments from value arguments. A type
  parameter that appears only in a union or alias application with unbound
  parameters is not inferable, and there is no explicit call-site type-argument
  syntax.
- Generic functions are checked per instantiation, so a generic function that is
  never called is not body-checked and produces no code.
- `typeof`/`match` type patterns require a single non-union target; union-subset tests and
  narrowing of the else branch are not implemented.
- A generic constructor infers from its arguments only; there is no
  expected-type propagation from a variable annotation.
- string literals are contextual only: there is no standalone literal type, so
  a generic constructor or function cannot infer a fixed string type from a
  literal argument. Use an annotated variable or a parameter.
- `Text` and `String<N>` never convert implicitly. The maximum `String<N>`
  capacity and the three Error field capacities come from `CompilerConfig`;
  Error defaults to 32/128/128 bytes and is clamped to the configured string
  maximum. Dynamic strings have no C string projection, dynamic-to-fixed
  conversion, region/escape checking, or UTF-8 codepoint API yet. The
  `build ... as ... do` block syntax is also unimplemented; `TextBuilder` is
  first-class and its copies alias the same state.
- `Error` fields are readable directly but not writable. `Error` is a large
  inline value, so unions and copies of it are expensive.
- A union value is not assignable to a different union; there is no subset
  widening.
- Function-typed struct fields are rejected; function-typed union alternatives
  and Array elements are supported with inline function-pointer payloads.
  Function types with function-typed parameters/returns are rejected by the
  inline C-type path; methods are the intended alternative for passing objects.
- Stores are global and non-first-class: no local store, no store parameter or
  return, and no copy. There is no per-object free; reset is the only
  deallocation. Allocation failure aborts rather than returning a value.
- Handles are opaque; index, store id, and generation are not exposed. `read`
  and `write` copy values; scoped access does not exist yet.
- Byte supports equality only; arithmetic and Byte/Int32 conversion are absent.
