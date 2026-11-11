# Nippy C API

The public headers in `include/` and what a caller may rely on. The command-line
driver in `compiler/main.c` is just one client of this API.

Everything here is C11 and depends only on libc.

## Entry points

| Header | Purpose |
|---|---|
| `compiler.h` | The one-shot API: source string in, C11 text out |
| `error.h` | `ErrorState`, the diagnostic accumulator |
| `alloc.h` | Checked allocation wrappers and the test failure hook |
| `messages.h` | Every user-facing message, as `MESSAGE_*` macros |
| `source_pos.h` | `SourcePos`: byte offset plus 1-based line and column |
| `identifier.h` | `NIPPY_IDENTIFIER_MAX`, the shared name limit |
| `operator.h` | `NippyOperator`, shared by syntax and typed nodes |
| `list.h` | `List`, the owning dynamic array |

Stage headers (`lexer.h`, `parser.h`, `checker.h`, `analyser.h`, `generator.h`)
expose the individual pipeline stages. `ast.h` and `typed_program.h` expose the
artifacts those stages exchange. Application code should prefer `compiler.h`.

## `compiler.h`

```c
typedef struct {
    const char *header_name;  /* generated header filename; NULL -> "nippy.h" */
    const char *source_name;  /* shown in diagnostics and #line; NULL -> "nippy.nip" */
} CompilerConfig;

typedef struct {
    bool succeeded;
    char *c_source;
    char *h_source;
    char *error_message;
} CompilerResult;

CompilerResult compiler_compile(const char *source, const CompilerConfig *config);
void compiler_result_free(CompilerResult *result);
```

`compiler_compile` runs lexer, parser, checker, analyser, and generator and
returns owned strings. Both string pointers in `config` are borrowed for the
duration of the call and are never freed by the compiler.

Ownership rules:

- The result owns `c_source`, `h_source`, and `error_message`. Release them with
  `compiler_result_free`, which also zeroes the struct.
- On success, `succeeded` is true, both sources are non-NULL, and
  `error_message` is NULL.
- On failure, `succeeded` is false, both sources are NULL, and `error_message`
  is a heap string with at least one message. It is never NULL on failure: if no
  stage reported anything, it is exactly `Unknown Error`.
- `compiler_result_free(NULL)` is a no-op, and freeing an already-freed result is
  safe because the fields are cleared.

`compiler_compile` is called with `source != NULL`. Passing NULL is also
handled — it returns a failed result with `compiler: source string is null` —
but the caller is responsible for the string's lifetime, and it must be
NUL-terminated. The 4096-byte size limit is a driver policy, not an API
constraint.

Example:

```c
CompilerConfig config = {0};
CompilerResult result = compiler_compile("var x : Int32 = 13\n", &config);

if (result.succeeded) {
    fputs(result.c_source, stdout);
} else {
    fputs(result.error_message, stderr);
}
compiler_result_free(&result);
```

## `error.h`

```c
typedef struct {
    bool reported;
    const char *source_name;   /* borrowed; used by positioned messages */
    size_t length;
    char message[ERROR_MESSAGE_CAPACITY];  /* 4096 bytes */
} ErrorState;

void error_init(ErrorState *state);
void error_report(ErrorState *state, const char *format, ...);
void error_report_at(ErrorState *state, SourcePos pos, const char *format, ...);
void error_unknown_if_none(ErrorState *state);
```

`ErrorState` is a fixed 4096-byte buffer, not a heap object, so a caller can put
it on the stack. Messages accumulate; each one is appended with a trailing
newline, and truncation is handled rather than asserted away.

- `error_init` clears the buffer and the `reported` flag. Call it before use.
- `error_report` appends a message with no source position. Use it for driver
  and out-of-memory diagnostics.
- `error_report_at` prefixes the message with
  `file:line:column (byte offset): `. It uses `state->source_name`, or
  `<source>` when that is NULL.
- `error_unknown_if_none` appends `Unknown Error` if nothing was reported yet.
  This is what guarantees a failure always carries a diagnostic.

The `format` argument takes printf conversions. `%zu` is used for sizes and
offsets.

## `alloc.h`

```c
#define NIPPY_ALLOC_NEVER ((size_t)-1)

void *nippy_malloc(size_t size);
void *nippy_realloc(void *pointer, size_t size);
char *nippy_strdup(const char *text);
void nippy_alloc_fail_after(size_t successful_allocations);
void nippy_alloc_reset(void);
```

The compiler allocates through these wrappers so that every out-of-memory path
stays reachable from a test. In normal operation they are `malloc`/`realloc`
with one counter comparison.

- `nippy_alloc_fail_after(n)` lets the next `n` allocations succeed and makes the
  one after that fail, then disables itself. Failing exactly one allocation at a
  time is what lets a test observe the diagnostic that failure produces.
- `nippy_alloc_reset()` or `nippy_alloc_fail_after(NIPPY_ALLOC_NEVER)` disables
  the hook.
- The driver arms the hook from the environment variable
  `NIPPY_FAIL_ALLOC_AFTER`, which is a test-only interface. Unset it in normal
  use.

Callers outside the test suite should not use the wrappers — they exist so that
failure handling is testable, not as a general allocation API. `BUILD.md`
describes how the suite uses the hook.

## `source_pos.h`

```c
typedef struct {
    size_t offset;  /* 0-based byte offset into the source */
    size_t line;    /* 1-based */
    size_t column;  /* 1-based, from the start of the line */
} SourcePos;
```

Tokens, `AstNode`s, and `TypedNode`s each carry a `SourcePos` for the first
character of what they represent. That is what makes positioned diagnostics and
`#line` emission possible without re-deriving positions downstream.

## `list.h`

```c
typedef struct {
    void *items;
    size_t length;
    size_t capacity;
    size_t item_size;
} List;

void list_init(List *list, size_t item_size);
bool list_push(List *list, const void *item);
bool list_pop(List *list, void *out_item);
const void *list_get(const List *list, size_t index);
void list_free(List *list);
```

A contiguous, owning list of fixed-size values. Elements are copied in on push
and copied out on pop; capacity doubles from a minimum of 4 and halves once the
length falls to a quarter of the capacity.

Contract:

- Values are stored inline, so an element must not hold a pointer into other
  elements. The compiler works around this by storing fixed-size
  `char[NIPPY_IDENTIFIER_MAX]` names and referring to them by index.
- `list_push` and `list_pop` return false on allocation failure or misuse
  (NULL list, zero `item_size`, empty pop). Every caller must check.
- `list_get` returns NULL when `index` is out of range, so out-of-range reads
  surface as NULL rather than as undefined behaviour.
- `list_free` releases the storage and zeroes the struct; it accepts NULL.
- `list_pop` shrinks but does not free unless the list becomes empty.

## `ast.h` and `typed_program.h`

Both artifacts are flat: nodes live inline in a `List` and refer to each other
and to names by index.

```c
typedef struct {
    List nodes;
    List symbols;
    List roots;           /* indices into nodes */
    List children;        /* indices into nodes, for variable-arity children */
    List type_parameters; /* indices into nodes, for generic declarations */
} Ast;

typedef struct {
    List nodes;
    List symbols;
    List roots;
    List children;
    List types;                /* TypedType records */
    List signatures;           /* TypedFunctionSignature records */
    List signature_parameters; /* indices into types */
    List type_children;        /* indices into types: union alternatives and
                                  struct type arguments */
} TypedProgram;
```

`AstNode` and `TypedNode` carry `left_child`/`right_child` for direct links and
`children_start`/`children_count` for a run of variable-arity children in the
owning `children` list. `AstNode` additionally carries
`type_parameters_start`/`type_parameters_count`, indexing `Ast.type_parameters`
(used by generic struct, alias, and function declarations; 0/0 when absent).
`AST_MEMBER_CALL` stores its receiver in `left_child`, its optional type
argument in `right_child`, its method name in `name_index`, and its positional
arguments in the children run. `AST_EXPRESSION_STATEMENT` stores its call
expression in `left_child`; the checker accepts it only for call expressions.
`TypedNode.auxiliary_type` holds the tested type for a `TYPED_IS` node and is
`TYPED_NO_NODE` otherwise. Function bodies and `if`/`while` bodies are block
nodes; block statements and `if` branches are stored in `children`.
`TYPED_NO_NODE`/`AST_NO_NODE` means "no node".

Resolved types are records in `TypedProgram.types`:

```c
typedef enum {
    NIPPY_TYPE_UNKNOWN,
    NIPPY_TYPE_INT32,
    NIPPY_TYPE_BOOL,
    NIPPY_TYPE_BYTE,
    NIPPY_TYPE_NIL,
    NIPPY_TYPE_STRUCT,
    NIPPY_TYPE_FUNCTION,
    NIPPY_TYPE_UNION,
    NIPPY_TYPE_STORE,
    NIPPY_TYPE_HANDLE
} NippyType;

typedef struct {
    NippyType kind;
    size_t name_index;        /* struct C name symbol, or NO_NODE */
    size_t signature_index;   /* function signature, or NO_NODE */
    size_t first_child;       /* union alternatives or struct type arguments */
    size_t child_count;
    size_t declaration_node;  /* concrete struct declaration, or NO_NODE */
    size_t source_name_index; /* source struct name for inference, or NO_NODE */
} TypedType;

typedef struct {
    size_t first_parameter; /* into TypedProgram.signature_parameters */
    size_t parameter_count;
    size_t return_type;     /* into TypedProgram.types */
} TypedFunctionSignature;
```

Lifecycle and access helpers:

```c
void ast_init(Ast *ast);
void ast_free(Ast *ast);
const AstNode *ast_get_node(const Ast *ast, size_t index);
size_t ast_intern_symbol(Ast *ast, const char *text, size_t length);
const char *ast_symbol_text(const Ast *ast, size_t index);
bool ast_push_child(Ast *ast, size_t index);
bool ast_push_type_parameter(Ast *ast, size_t index);

void typed_program_init(TypedProgram *program);
void typed_program_free(TypedProgram *program);
const TypedNode *typed_program_node(const TypedProgram *program, size_t index);
bool typed_program_push(TypedProgram *program, const TypedNode *node,
                        size_t *out_index);
size_t typed_program_copy_symbol(TypedProgram *program, const char *text,
                                 size_t length);
const char *typed_program_symbol_text(const TypedProgram *program,
                                      size_t index);
const TypedType *typed_program_type(const TypedProgram *program, size_t index);
bool typed_program_add_type(TypedProgram *program, const TypedType *type,
                            size_t *out_index);
const TypedFunctionSignature *typed_program_signature(
    const TypedProgram *program, size_t index);
bool typed_program_add_signature(TypedProgram *program,
                                 const TypedFunctionSignature *signature,
                                 size_t *out_index);
bool typed_program_add_signature_parameter(TypedProgram *program,
                                           size_t type_index);
bool typed_program_add_type_child(TypedProgram *program, size_t type_index);
bool typed_program_push_child(TypedProgram *program, size_t index);
```

A `NIPPY_TYPE_UNION` type's `first_child`/`child_count` run in
`program->type_children` holds the canonical alternatives. A
`NIPPY_TYPE_STRUCT` type's run holds its type arguments (empty for a non-generic
struct), and `declaration_node` points at the concrete
`TYPED_TYPE_DECLARATION` node whose field children describe the layout.

The typed node kinds include `TYPED_NIL_LITERAL`, `TYPED_SUM_INJECT`,
`TYPED_SUM_PAYLOAD`, `TYPED_IS`, `TYPED_STORE_DECLARATION`,
`TYPED_STORE_ALLOC`, `TYPED_STORE_RESET`, `TYPED_HANDLE_READ`,
`TYPED_HANDLE_WRITE`, and `TYPED_EXPRESSION_STATEMENT`. For injection and
payload projection, `integer_value` is the alternative's canonical tag.
`TYPED_IS` stores the tested type in `auxiliary_type` and a tag in
`integer_value`; a statically decided test is emitted as a boolean literal, not
a tag comparison.

`TypedNode.store_id` carries the global store id for a store declaration,
allocation, or reset, and is `TYPED_NO_NODE` elsewhere. A
`TYPED_STORE_DECLARATION` has type `Store` and `integer_value` = initial bytes;
a `TYPED_STORE_ALLOC` has type `Handle<T>` and its value in `left_child`; a
`TYPED_HANDLE_READ` has type `T` and the handle in `left_child`; a
`TYPED_HANDLE_WRITE` has type `Nil`, the handle in `left_child`, and the value
in `right_child`. A handle type's element type is its single entry in
`program->type_children`.

The `add_*` helpers append and return the new index. The caller must check
`bool` results; a false return means allocation failed. `typed_program_copy_symbol`
interns text into the typed program's own symbol table, so the artifact does not
borrow from the syntax tree. `operator.h` defines `NippyOperator`, shared by
`AstNode` and `TypedNode` for arithmetic, comparison, equality, and logical
operators.

## Stage API

These are internal to the compiler but stable enough to test directly; the test
binary links them all.

```c
LexerResult lexer_tokenize(const char *code);
void        lexer_result_free(LexerResult *result);

bool parser_parse(const TokenList *tokens, Ast *ast, ErrorState *errors);
void ast_init(Ast *ast);
void ast_free(Ast *ast);

bool checker_check(const Ast *ast, TypedProgram *program, ErrorState *errors);
void typed_program_init(TypedProgram *program);
void typed_program_free(TypedProgram *program);

bool analyser_analyse(const TypedProgram *program, ErrorState *errors);

bool generator_generate(const TypedProgram *program, const char *header_name,
                        const char *source_name, char **c_source,
                        char **h_source, ErrorState *errors);
```

Rules that hold for every stage:

- Inputs are `const`. A stage never writes into its input artifact.
- A failure means the stage did not produce a usable output; the caller must not
  use it. The diagnostic is in `errors`.
- The caller owns and initialises/frees output artifacts (`ast_init`/`ast_free`,
  `typed_program_init`/`typed_program_free`, `lexer_result_free`,
  `generator_generate`'s two strings).
- `generator_generate` sets both output pointers to NULL before doing anything,
  so they are always either both valid or both NULL.
- `lexer_tokenize` returns either tokens or errors, never both.

## Compatibility

There is no stability promise yet: this is a prototype, and headers change with
the language. When the API does stabilise, this file is where the guarantee gets
recorded.
