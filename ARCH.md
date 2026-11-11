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
| Parser | token list | `Ast`: flattened syntax tree | `compiler/parser.c` |
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
be moved or freed without walking its contents. Keyword recognition happens
during scanning (`var`, `type`, `fun`, `return`, `if`, `then`, `elseif`,
`else`, `while`, `break`, `continue`, `true`, `false`, `end`); everything else is
an identifier. Two-character operators (`==`, `!=`, `<=`, `>=`, `&&`, `||`) are
recognised before their single-character prefixes.

### Parser

Recursive descent over the token list, producing a flattened `Ast`. Nodes are
stored inline in `Ast.nodes` and refer to each other by index, with
`AST_NO_NODE` (`(size_t)-1`) meaning "no child". Top-level declarations are
recorded in `Ast.roots`, in source order.

Nodes whose arity varies — type declarations, function declarations, blocks,
`if` branches, call arguments, and function-type parameters — store their
children as one contiguous run of indices in `Ast.children`, recorded by
`children_start` and `children_count`. A child expression or statement can
therefore push its own nodes without splitting its parent's run: every
variable-arity node appends its run only after all of its children have been
parsed. Binary, unary, control-flow condition, and leaf nodes use
`left_child`/`right_child` directly. This keeps the flat-index model intact
without adding sibling links or pointers.

Function bodies are blocks of statements. `if`/`elseif`/`else` is parsed as an
`AST_IF` whose children are the then-block and optional else-block; an
`elseif` chain is desugared into nested `if` nodes in the else branch. `while`
stores its condition and body block. Statements are `return`, `if`, `while`,
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

- resolves type names against the built-in `Int32` and `Bool`, a table of
  declared struct types, and structural function types;
- builds `TypedType`, `TypedFunctionSignature`, and signature-parameter records
  in `TypedProgram`, resolving nested function types before the signature that
  contains them;
- rejects duplicate top-level names, duplicate parameters, and shadowing of
  `Int32` or `Bool`;
- checks constructor arity, field names, and argument types, for both
  positional and named argument lists;
- checks function-call arity and argument types against the callee's function
  type, including calls through function-typed variables and parameters;
- types boolean literals and the comparison, equality, and logical operators;
- checks that `if` and `while` conditions are `Bool`;
- checks every `return` expression against the function's return type, and
  rejects `break`/`continue` outside a loop;
- proves that every function returns on every path, using the structured
  statement tree (a `while` never guarantees a return);
- infers a variable's type from its initialiser when the declaration omits it,
  and checks agreement when both sides state a type;
- copies every name it keeps into `TypedProgram.symbols`, so the typed program
  outlives the syntax tree.

The type table is a `List` of `TypeEntry` records searched linearly: names are
compared as strings, and there is no hashing yet. Function and variable tables
are also linear `List`s. Function types are compared structurally, so two
independently written `Fun<(Int32):Int32>` annotations are the same type.

### Analyser

A read-only pass over the typed program that validates its internal links:
root indices, direct child indices, variable-arity child indices, symbol
indices, type indices, and function-signature links. It produces no artifact,
because memory and ownership analysis does not exist yet. When the
bidirectional-pointer rules land, this stage either refines the typed program or
is split out into a distinct analysis artifact — it must not become a second
mutation pass over the syntax tree, which it never receives.

### Generator

Consumes the typed program only. It emits:

- **the header**, holding the generated include guard, `<stdbool.h>`,
  `<stdint.h>`, one `typedef struct` per declared type, one function-pointer
  `typedef` per function signature (`nippy_fun_N`), and — when the program is a
  single integer variable — the declaration of `nippy_program_value`;
- **the source**, holding `#include "<generated header>"`, one function
  definition per declared function, one `#line` directive per emitted
  declaration or statement, the variable initialisers, and `main`.

Generated C is internal IR. It is not required to be human-readable or
formatted, and its text is not a stable interface. It *is* required to compile
cleanly under `-std=c11 -Wall -Wextra -Wpedantic`, which is why unused
initialised variables and unused parameters get a `(void)name;` line. Every
declaration and statement emitted is preceded by `#line <line> "<source>"` so a
C-compiler diagnostic points back at the Nippy source. Function types are
emitted as typedefs so every generated declaration is a simple `type name` pair,
including function-typed parameters and variables. `Bool` maps to `bool`;
`if`, `while`, `break`, and `continue` are emitted as the corresponding C
statements, and `elseif` chains are emitted as nested `else { if ... }` blocks.

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
| `tests/` | `runner.c` harness, `test.c` cases, `test_cases.h` declarations |
| `examples/` | Sample Nippy programs |
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
- No local variables, assignment, or scope handling. Function bodies have
  statements but no way to declare or mutate local storage.
- Program-level variables are not visible inside function bodies.
- No ownership or lifetime analysis; the analyser validates links only.
- The type, function, and variable tables are linear string searches.
- Function types are emitted as one typedef per signature occurrence, so
  duplicate but structurally identical typedefs are common.
