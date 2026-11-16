# TODO

## Bugs

None.

## Features

1. Add forward references and mutual recursion for functions.
2. Add input, file access, command-line arguments, exit status control, and standard-library helpers such as `abs`, `min`, `len`, and number-to-text formatting.
3. Support map key types beyond `String<N>`, chained indexed-field assignment such as `xs[0].x = 5`, and Text concatenation, ordering, indexing, and number conversion.
4. Let functions read program-level `const` and `var` bindings. Only Stores are visible inside a function today, so a program-level constant must be passed as an argument to every function that uses it.


## Periodic Audits

1. Is our compiler forward flowing only? is it fail closed?
2. Is the compiler data driven and declarative?
3. Ensure that the CRAP score of every function is below 20 (`make crap`).