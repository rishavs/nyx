# TODO

## Bugs

1. Accept `if` and `typeof` expressions as call and print arguments; allow match expressions to use general expressions as scrutinees.
2. Revisit bitwise operator precedence relative to equality and improve operator diagnostics to name operand types. **[DESIGN INPUT]**
3. Extend narrowing through logical operators, negation, early returns, array elements, and struct fields; improve generic `T | Nil` type inference.
4. Report all independent compile errors; accept a UTF-8 BOM; allow paths containing spaces in generated header names.
5. Correct misleading diagnostics for Text indexing, negative list capacity, and a full `Array.push`.
6. Prevent cascading diagnostics for over-long map keys.
7. Add source positions to runtime errors, standardize their prefixes, and preserve stdout ordering when errors are written to stderr.
8. Eliminate warnings from accepted generated C and improve compile time for large programs, including the 50,000-function case.

## Features

1. Add forward references and mutual recursion; decide whether Nil-returning functions may fall through without an explicit `return nil`.
2. Add input, file access, command-line arguments, exit status control, and standard-library helpers such as `abs`, `min`, `len`, and number-to-text formatting.
3. Support recursive and mutually recursive data types so linked structures such as `Handle<Node>` can be expressed.
4. Add counted loops.
5. Support map key types beyond `String<N>`, chained indexed-field assignment such as `xs[0].x = 5`, and Text concatenation, ordering, indexing, and number conversion.
6. Add user-defined method-call resolution such as `p.get()` and equality for structs, arrays, and handles.
7. Choose float display formatting that preserves enough digits without surprising notation. **[DESIGN INPUT]**
8. Decide inferred integer literal types at the Int32 boundary. Integer arithmetic wrapping is already specified. **[DESIGN INPUT]**
9. Decide whether equality should support union values such as `Int32 | Nil` without prior narrowing. **[DESIGN INPUT]**
