# AGENTS.md

Guidance for agents working in this repository. The language goals below are the
design authority. Everything else — pipeline, build, semantics, grammar, API —
lives in the documents listed under [Documentation map](#documentation-map).

## What this is

**Nippy** is a small compiler for a language of the same name. It is intended to
be a **simple, minimalistic "better C"**: one obvious way to express things, no
undefined behaviour waiting in the corners, and a pointer model that is explicit
about ownership instead of trusting the programmer to remember it.

The compiler is written in **C11** and translates Nippy source into **C11**
source, then invokes the system C compiler to produce a native binary. There is
no interpreter, no VM, and no runtime library. The repository is a prototype:
the language is being designed and implemented at the same time.

## Target platforms

### Supported target

| OS | Architecture | Dependencies |
|---|---|---|
| POSIX | x86_64 | Linux kernel 6.18 (LTS), libclang (expected preinstalled; not packaged) |

This is the only target that is built and tested.

### Proposed targets

Not built or tested; listed as direction only.

| OS | Architectures |
|---|---|
| Windows | x86_64, aarch64, riscv |
| macOS | x86_64, aarch64, riscv |

## Language Goals & Philosophy

The goals below are the design authority for the language. When a proposed
feature conflicts with them, the feature changes, not the goals. The five
pillars come first and are the tests every design decision has to pass; the
numbered rules are the concrete consequences.

### Pillars

#### 1. Cohesive (Organic Integration)

- Every language feature emerges naturally from a small set of core primitives
  rather than introducing disconnected, special-cased concepts.
- Features stack logically on top of each other (for example, methods are
  expressed simply as functions operating on data types), making the language
  feel like a single organic system.

#### 2. Coherent (Semantic Soundness)

- The underlying runtime behaviour and mental model remain completely
  predictable, without arbitrary compiler exceptions or hidden "magic".
- Core rules — such as memory lifetimes and evaluation order — apply uniformly
  across all language features, without sudden rule breaks.

#### 3. Consistent (Symbol Integrity)

- Keywords, operators, and syntax tokens always carry the exact same meaning and
  behaviour regardless of where they appear.
- Developers never have to re-learn what a symbol does when moving from one
  context to another.

#### 4. Uniform (Structural Regularity)

- Grammar shapes and code structures follow a consistent, repeating template,
  drastically minimising syntactic edge cases.
- Similar syntactic categories share identical layouts; for example, control
  structures and blocks all adhere to the same evaluation shape.

#### 5. Intuitive (Predictable Extrapolation)

- Acts as the natural outcome of the first four pillars, enabling developers to
  accurately guess the syntax and behaviour of unfamiliar features.
- Leverages human pattern recognition so users can write correct code for new
  APIs or data structures on their first try without consulting documentation.

### Rules

1. There should only be one obvious way to do things.
2. The language surface must remain small and clean.
3. Prioritise simplicity, elegance, expressiveness, and composability.
4. Nippy is a high-level systems programming language with a low-level core.
   Users should be able to do everything with Nippy that they can do with C.
5. Static typing.
6. Compiles to C11. The generated C is internal IR: it is not required to be
   human-readable or formatted.
7. Trivial import of C libraries and tools.
8. Every issue that can be caught at compile time with small and local checks, must be caught at compile time.
9. Fast compilation.
10. Trivial concurrency and multithreading.
11. Low ceremony: minimise boilerplate and syntactic noise.
12. Promote readability over compactness and complicated instructions.
13. Lowest possible runtime overhead.
14. No undefined behaviour. Nippy must solve for C's shortcomings here.
15. The compiler should catch every memory error that a local analysis can
    decide, without adding a language concept or disproportionate checker
    complexity.
16. Do not carry heavy ownership and lifetime semantics; prefer compiler-assisted
    manual memory management.
17. Newlines and indentation are not syntax. Whitespace only separates tokens,
    as in Lua; declarations and statements end where the grammar ends, not at a
    line boundary.
18. The surface syntax belongs to the Lua/Luau family: keyword-delimited blocks
    with `end`, `then`/`elseif`/`else`, no braces for blocks or semicolons, and
    type annotations attached with `:`. Braces are reserved for record
    literals. Prefer that shape when adding constructs.

### Where the goals and the code disagree

These are tracked rather than hidden. Do not "resolve" one by quietly relaxing
the goal.

| Goal | Current state |
|---|---|
| Rule 8 — incremental compilation | Absent for Nippy programs: every compile regenerates all the C. The repository's own build is incremental (one object per source, headers tracked). |
| Rule 10 — concurrency | Absent, and no design recorded yet. |
| Rule 14/15 — no UB, locally checked memory errors | Stores provide checked arena allocation and generation-checked handles; fixed strings, dynamic-string descriptors, builders, arrays, lists, slices, records, and tables are runtime-checked for bounds and staleness; direct recursion has a runtime depth guard; by-value types are capped at 64 KiB; the analyser validates typed-program links, but there is no ownership or lifetime analysis yet. |

## Documentation map

| Document | Owns |
|---|---|
| `README.md` | What works today, for a new user |
| `ARCH.md` | Pipeline, stage ownership, data structures, failure behaviour, known gaps |
| `API.md` | What callers of `include/` may rely on: signatures, ownership, failure contracts |
| `SEMANTICS.md` | What a Nippy program means and which programs are accepted |
| `GRAMMAR.ebnf` | The accepted source grammar in EBNF |
| `BUILD.md` | Build targets, flags, test harness, allocation-failure testing, tooling |

**Keep these in lockstep with the code.** A change is not complete until the
affected documents match the new code. If you add a message, a stage, a node
kind, a keyword, a public function, or a rejection rule, update the document
that covers it in the *same* change.

`SEMANTICS.md` and `GRAMMAR.ebnf` describe only what is implemented; intentions
go under `SEMANTICS.md`'s "Not yet decided or not yet implemented" list. Never
claim a feature works because it is listed as decided. Language status, rejected
programs, and open questions all live in `SEMANTICS.md`; do not duplicate them
here.

## Working rules

- `BUILD.md` owns build, flags, tests, and tooling. Run `make test` before
  claiming a change is complete, and build clean under
  `-Wall -Wextra -Wpedantic`; a warning is a failing build in review.
- The pipeline is forward-only. Each stage reads its predecessor through a
  `const` pointer and produces a new artifact; no stage re-walks an earlier
  artifact, and nothing runs after the generator. See `ARCH.md`.
- The four artifacts are four distinct things. Do not re-alias them or add back
  channels between stages. `compiler_compile` frees the syntax tree after the
  checker so the analyser and generator cannot reach it.
- When adding a stage or node kind, update the AST, checker, analyser,
  generator, and tests together. A half-wired stage fails later and obscures
  the real cause.
- Compiler invariants — `List` ownership, flat AST, no pointers in stored
  elements, owned `TypedProgram` symbols, checked allocations, input limits —
  are in `ARCH.md` and `API.md`. Breaking one is a design change, not a
  refactor.
- Do not commit build artefacts; `bin/` is ignored.
- Keep scratch work in `tmp/`: temporary scripts, probe programs, generated
  samples. If a script becomes load-bearing, move it to `tests/` or `compiler/`
  and wire it into the Makefile.

## Code conventions

Match what is already there; consistency in this codebase is worth more than
personal preference.

- **C11 everywhere**, for both the compiler and the C it emits. No new
  dependencies beyond libc and those named under
  [Target platforms](#target-platforms); today the compiler links libc only.
- **Indentation:** 4 spaces, no tabs. Lines stay within roughly 88 columns.
- **Braces:** opening brace on its own line for function definitions, on the
  same line for `if`/`for`/`while`/`switch`. `case` labels sit at the
  `switch`'s indentation level.
- **Headers:** guarded as `NIPPY_<FILE>_H`. Public modules get a header in
  `include/` and a `COMPILER_SOURCES` entry. Internal helpers shared by several
  modules in one stage get a private `compiler/<stage>/<stage>_internal.h`;
  they are not part of the public API. Every new `.c` file needs a
  `COMPILER_SOURCES` entry.
- **Naming:** functions `module_verb` (`lexer_tokenize`), types `PascalCase`
  with a module prefix (`AstNode`), enum constants and macros
  `SCREAMING_SNAKE_CASE`, struct fields `snake_case`.
- **Error messages** are lowercase and prefixed with the stage name. Keep `%zu`
  for sizes and offsets. Every user-facing string is a `MESSAGE_*` macro in
  `include/messages.h`, and every macro needs a test that asserts against it.
- **Comments** use `/* ... */`, explain *why* rather than restating the code,
  and stay rare enough to remain meaningful.
