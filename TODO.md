# TODO

## Bugs

None.

## Features

1. Add forward references and mutual recursion for functions.
2. Add input, file access, command-line arguments, exit status control, and standard-library helpers such as `abs`, `min`, `len`, and number-to-text formatting.
3. Support map key types beyond `String<N>`, chained indexed-field assignment such as `xs[0].x = 5`, and Text concatenation, ordering, indexing, and number conversion.
4. Let functions read program-level `const` and `var` bindings. Only Stores are visible inside a function today, so a program-level constant must be passed as an argument to every function that uses it.
5. Check on the 64 KiB by-value cap (`NYX_MAX_VALUE_BYTES`). It keeps coming up: a channel message and an argument or result of a `parallel` arm must fit in it, a fixed `Array` of 4096 `Int64` is already half of it, and a game's world snapshot or a request buffer passes it quickly. Its recorded reason is only "so large payloads move into a Store"; the likely real reason is C stack safety, but the recursion guard counts depth, not bytes, so the cap bounds one value and not total stack use. It is now a `CompilerConfig` limit (`max_value_bytes`, 64 KiB by default, up to 16 MiB), so a program that needs larger messages can raise it. Still open: whether the default should change, whether to tie the recursion limit to frame size (so the cap bounds total stack use, not one value), and whether large plain values should cross threads another way (for example sending ownership of a pool object); write down why.

6. Finish the CRAP clean-up (`make crap`: every checker, analyser and generator function is under 10; 52 functions in the rest of `compiler/` are at or above 10, down from 388 when the goal of under 10 was set: parser 37, the top-level files 14, lexer 1; `make crap` itself still gates at 20, and 4 functions are at or above that: `parse_block`, `parse_postfix`, `parse_parallel`, `parse_parallel_arm`). What is left is the parser's statement and expression functions (`parse_block`, `parse_parallel`, `parse_when`, `parse_postfix`), which want the same treatment as the checker: a table of statement forms and phases joined by `&&`, not more helper splitting (see "Tables instead of branches" in `ARCH.md`).


## Periodic Audits

1. Is our compiler forward flowing only? is it fail closed? Audited 2026-11-18: yes, and now enforced by `tests/compiler/architecture.c` and `tests/compiler/failure.c` (see "Compiler goals" in `ARCH.md`). Re-run `make test-sanitize` after changing a failure path.
2. Is the compiler data driven and declarative? Passes: the generator's node scans were replaced by the analyser's node index, and a quadratic constant-folding re-walk in the checker was removed (2026-11-18), then the scope-frame, trampoline, recursion, and type scans. Audited 2026-11-18: the generator's program assembly, constant folding, and fact collection are tables; the generator's emitters are rule lists and tables over sticky buffers, and what remains imperative is the parser (item 6).
3. Ensure that the CRAP score of every function is below 20 (`make crap`).
4. How often does the 64 KiB by-value cap block real programs, and is it still the right limit?
