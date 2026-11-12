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
| `identifier.h` | `NIPPY_IDENTIFIER_MAX`, the shared name limit, and `NIPPY_GENERATED_PREFIX`, the reserved emitted-name prefix |
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
    int error_header_capacity;      /* 0 -> default 32 */
    int error_description_capacity; /* 0 -> default 128 */
    int error_filename_capacity;    /* 0 -> default 128 */
    int max_source_bytes;           /* source-size limit; 0 -> 4096 */
    int max_string_capacity;        /* String<N> upper bound; 0 -> 4096 */
    int max_list_capacity;          /* Array<T,N> upper bound; 0 -> 4096 */
    int max_print_depth;            /* pretty-print depth; 0 -> 16 */
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
duration of the call and are never freed by the compiler. Configuration values
are copied. Error capacities default to 32, 128, and 128 and are clamped to
`max_string_capacity`; an explicit Error capacity outside `1..max_string_capacity`
fails the compilation. `max_string_capacity` defaults to 4096 and may not exceed
the hard limit `NIPPY_MAX_STRING_CAPACITY` (1 MiB). `max_list_capacity` defaults
to 4096 and may not exceed `NIPPY_MAX_LIST_CAPACITY` (1 MiB).
`max_print_depth` defaults to 16 and may not exceed `NIPPY_MAX_PRINT_DEPTH`
(1024); explicit values must be positive.
`max_source_bytes` defaults to 4096;
`compiler_compile` rejects longer source strings, and the command-line driver
uses the same limit when reading a file.

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
NUL-terminated. Source size is limited by `CompilerConfig.max_source_bytes`
(default 4096); both `compiler_compile` and the command-line driver enforce it.

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
  surface as NULL rather than as undefined behaviour. The returned pointer is
  borrowed from the list's current allocation and is invalidated by any
  `list_push`, `list_pop`, or `list_free` on that list. Copy what is needed
  before a call that can grow the list.
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
    List bytes;           /* decoded string-literal bytes */
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
    List bytes;                /* decoded string-literal bytes */
    int error_header_capacity;      /* built-in Error field capacities */
    int error_description_capacity;
    int error_filename_capacity;
    int max_string_capacity;        /* upper bound for String<N> */
    int max_list_capacity;          /* upper bound for Array<T,N> */
    int max_print_depth;            /* pretty-print depth */
} TypedProgram;
```

`typed_program_init` seeds the capacities with the defaults: string maximum
4096, list maximum 4096, and Error fields 32/128/128. `compiler_compile` overwrites them from
`CompilerConfig`.

`AstNode` and `TypedNode` carry `left_child`/`right_child` for direct links and
`children_start`/`children_count` for a run of variable-arity children in the
owning `children` list. `AstNode` additionally carries
`type_parameters_start`/`type_parameters_count`, indexing `Ast.type_parameters`
(used by generic struct, alias, and function declarations; 0/0 when absent).
`second_name_index` is meaningful only for `AST_FOR`/`TYPED_FOR`, where it
holds the optional second binding.
`AST_MEMBER_CALL` stores its receiver in `left_child`, its optional type
argument in `right_child`, its method name in `name_index`, and its positional
arguments in the children run. `AST_EXPRESSION_STATEMENT` stores its call
expression in `left_child`; the checker accepts it only for call expressions.
`AST_DECLARATION` sets `is_global` for a `global` declaration and `is_const`
for a `const` declaration. `AST_ASSIGNMENT`
stores the target name in `name_index` and the value in `left_child`. `AST_STRING_LITERAL` stores an offset and byte length into `Ast.bytes`;
`AST_TYPE_FIXED_STRING` stores its capacity in `integer_value`.
`AST_TYPE_LIST` has its element type in `left_child`; `AST_TYPE_FIXED_LIST`
also stores its capacity in `integer_value`; `AST_TYPE_SLICE` has its element
type in `left_child`; `AST_LIST_LITERAL` stores its elements in an
`Ast.children` run. `AST_INDEX` has the list in `left_child` and
the index in `right_child`; `AST_INDEX_ASSIGNMENT` has the indexed target in
`left_child` and the value in `right_child`. `AST_MEMBER_ACCESS` has the object
in `left_child` and the field name in `name_index`;
`AST_MEMBER_ASSIGNMENT` has the member target in `left_child` and the value in
`right_child`. `AST_FOR` has its iterable in `left_child`, its body in
`right_child`, the first binding in `name_index`, and the optional second
binding in `second_name_index`.
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
    NIPPY_TYPE_HANDLE,
    NIPPY_TYPE_STRING,
    NIPPY_TYPE_DYNAMIC_STRING,
    NIPPY_TYPE_TEXT_BUILDER,
    NIPPY_TYPE_LIST,
    NIPPY_TYPE_FIXED_LIST,
    NIPPY_TYPE_SLICE,
    NIPPY_TYPE_STRING_LITERAL,
    NIPPY_TYPE_ERROR
} NippyType;

typedef struct {
    NippyType kind;
    size_t name_index;        /* struct C name symbol, or NO_NODE */
    size_t signature_index;   /* function signature, or NO_NODE */
    size_t first_child;       /* union alternatives or struct type arguments */
    size_t child_count;
    size_t declaration_node;  /* concrete struct declaration, or NO_NODE */
    size_t source_name_index; /* source struct name for inference, or NO_NODE */
    size_t fixed_length;      /* byte capacity for NIPPY_TYPE_STRING */
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
bool ast_push_byte(Ast *ast, unsigned char byte);

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
bool typed_program_push_byte(TypedProgram *program, unsigned char byte);
```

A `NIPPY_TYPE_UNION` type's `first_child`/`child_count` run in
`program->type_children` holds the canonical alternatives. A
`NIPPY_TYPE_STRUCT` type's run holds its type arguments (empty for a non-generic
struct), and `declaration_node` points at the concrete
`TYPED_TYPE_DECLARATION` node whose field children describe the layout.

The typed node kinds include `TYPED_NIL_LITERAL`, `TYPED_SUM_INJECT`,
`TYPED_SUM_PAYLOAD`, `TYPED_IS`, `TYPED_IF_EXPRESSION`,
`TYPED_STORE_DECLARATION`,
`TYPED_STORE_ALLOC`, `TYPED_STORE_RESET`, `TYPED_HANDLE_READ`,
`TYPED_HANDLE_WRITE`, `TYPED_EXPRESSION_STATEMENT`, `TYPED_ASSIGNMENT`,
`TYPED_LOCAL_DECLARATION`, `TYPED_MEMBER_ACCESS`, `TYPED_MEMBER_WRITE`,
`TYPED_PRINT`, `TYPED_ERROR_THROW`, `TYPED_RECORD_LITERAL`,
`TYPED_RECORD_GET`, `TYPED_RECORD_SET`, `TYPED_RECORD_HAS`,
`TYPED_RECORD_REMOVE`, `TYPED_RECORD_LEN`, `TYPED_TABLE_CREATE`,
`TYPED_TABLE_GET`, `TYPED_TABLE_SET`, `TYPED_TABLE_HAS`,
`TYPED_TABLE_REMOVE`, `TYPED_TABLE_LEN`,
`TYPED_STRING_LITERAL`,
`TYPED_STRING_LEN`, `TYPED_STRING_AT`, `TYPED_STRING_SLICE`,
`TYPED_STRING_CONCAT`, `TYPED_STRING_COPY`, `TYPED_STRING_FROM_FIXED`,
`TYPED_BUILDER_CREATE`, `TYPED_BUILDER_APPEND`, `TYPED_BUILDER_APPEND_BYTE`,
`TYPED_BUILDER_LEN`, `TYPED_BUILDER_FINISH`, `TYPED_LIST_LITERAL`,
`TYPED_LIST_LEN`, `TYPED_LIST_GET`, `TYPED_LIST_SET`, `TYPED_LIST_PUSH`,
`TYPED_LIST_POP`, `TYPED_LIST_CREATE`, `TYPED_SLICE_CREATE`, `TYPED_SLICE_LEN`,
`TYPED_SLICE_GET`, `TYPED_ERROR_CONSTRUCT`, and `TYPED_ERROR_FIELD`. For injection and
payload projection, `integer_value` is the alternative's canonical tag.
`TYPED_IS` stores the tested type in `auxiliary_type` and a tag in
`integer_value`; a statically decided test is emitted as a boolean literal, not
a tag comparison. `TYPED_IF_EXPRESSION` has the condition in `left_child`, the
then expression in `right_child`, and the else expression in a one-child run.
`match` and `typeof` tests are lowered by the parser/checker into `if` chains and
`TYPED_IS` nodes, so there is no separate typed match node.

`TypedNode.store_id` carries the global store id for a store declaration,
allocation, reset, concat, copy, fixed-to-dynamic conversion, or builder
creation, and is `TYPED_NO_NODE` elsewhere. A
`TYPED_STORE_DECLARATION` has type `Store` and `integer_value` = initial bytes;
a `TYPED_STORE_ALLOC` has type `Handle<T>` and its value in `left_child`; a
`TYPED_HANDLE_READ` has type `T` and the handle in `left_child`; a
`TYPED_HANDLE_WRITE` has type `Nil`, the handle in `left_child`, and the value
in `right_child`. `TYPED_ASSIGNMENT` has the target variable's type and name, and
the coerced value in `left_child`. A handle type's element type is its single entry in
`program->type_children`; the checker accepts a handle only for a fixed type
with no allocator-backed fields.

`TYPED_STRING_LITERAL` stores an offset and byte length into
`TypedProgram.bytes`; after contextual typing its type is `Text` (dynamic,
statically backed) or `String<N>` (fixed), whose `fixed_length` is the capacity.
`TYPED_STRING_LEN` has type `Int32` and the string in `left_child`;
`TYPED_STRING_AT` has type `Byte`, the string in `left_child`, and the index in
`right_child`; `TYPED_STRING_SLICE` has type `Text`, the string in
`left_child`, and two children for start and length. `TYPED_STRING_CONCAT` has
type `Text`, the first string in `left_child`, the second in `right_child`, and
the destination store in `store_id`; `TYPED_STRING_COPY` has the source in
`left_child`; `TYPED_STRING_FROM_FIXED` has a fixed `String<N>` in
`left_child`. All three allocate in `store_id`. `TYPED_BUILDER_CREATE` has the
capacity in `left_child` and the destination store in `store_id`;
`TYPED_LIST_CREATE` has the capacity in `left_child`, the element type in
`auxiliary_type`, and the destination store in `store_id`;
`TYPED_LIST_LITERAL` has type `Array<T,N>` and its elements in a children run;
`TYPED_LIST_GET` has type `T` with the list in `left_child` and index in
`right_child`; `TYPED_LIST_SET` has type `Nil`, the list in `left_child`, and
two children for index and value; `TYPED_LIST_PUSH` has type `Nil`, the list
in `left_child`, the value in `right_child`, and `auxiliary_type` for the
element type; `TYPED_LIST_POP` has type `T | Nil`, the list in `left_child`, and
`auxiliary_type` for the element type; `TYPED_LIST_LEN` has type `Int32` and the
list in `left_child`; `TYPED_LOCAL_DECLARATION` has the local's type, name, and
initializer in `left_child`; `TYPED_MEMBER_ACCESS` has the field type, object in
`left_child`, and field name in `name_index`; `TYPED_MEMBER_WRITE` has type
`Nil`, the target access in `left_child`, and the value in `right_child`;
`TYPED_FOR` has type `Nil`, the iterable in `left_child`, the body in
`right_child`, the value binding in `name_index`, the optional index binding in
`second_name_index`, and the element type in `auxiliary_type`; `TYPED_PRINT`
has type `Nil` and its arguments in a children run; `TYPED_RECORD_LITERAL`
has type `Record<K,V,N>` and alternating key/value pairs in a children run;
map get/set/has/remove/len nodes carry the receiver and key (or key/value)
links; `TYPED_TABLE_CREATE` has the capacity in `left_child` and the store id
in `store_id`; `TYPED_ERROR_THROW` has type
`Error`, its header/description in a children run, `auxiliary_type` for the
filename fixed-string type, and `string_offset`/`string_length` for the
generated filename bytes;
`TYPED_SLICE_CREATE` has type `Slice<T>`, the list/slice
in `left_child`, and children for start/length; `TYPED_SLICE_LEN` has type
`Int32`; `TYPED_SLICE_GET` has type `T`, the slice in `left_child`, and the
index in `right_child`;
`TYPED_BUILDER_APPEND` and `TYPED_BUILDER_APPEND_BYTE` have the builder in
`left_child` and the value in `right_child`; `TYPED_BUILDER_LEN` and
`TYPED_BUILDER_FINISH` have the builder in `left_child`. `TYPED_ERROR_CONSTRUCT` has type
`Error` and six children: header, description, filename, line, col, pos.
`TYPED_ERROR_FIELD` has the field's type, the Error value in `left_child`, and
the field index in `integer_value` (0 header, 1 description, 2 filename, 3 line,
4 col, 5 pos). `NIPPY_TYPE_ERROR` is a built-in sized type with the fixed
layout `header: String<32>`, `description: String<128>`,
`filename: String<128>`, `line/col/pos: Int32` by default. The three string
types are the Error type's three entries in `program->type_children`, in that
order. `NIPPY_TYPE_STRING_LITERAL` is the checker's internal
uncontextual literal type; a literal is retyped to `Text` or `String<N>` by
injection, and the generator rejects any literal that remains uncontextual.
`NIPPY_TYPE_DYNAMIC_STRING` (public type `Text`) and
`NIPPY_TYPE_TEXT_BUILDER` are sized public
types with no child types. `NIPPY_TYPE_LIST`, `NIPPY_TYPE_FIXED_LIST`, and `NIPPY_TYPE_SLICE` have one
element type in `type_children`; Arrays also use `fixed_length`.

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

bool parser_parse(const TokenList *tokens, const List *string_bytes, Ast *ast,
                  ErrorState *errors);
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
- `lexer_tokenize` returns either tokens or errors, never both. Text-literal
  bytes live in `LexerResult.text_bytes`; a `TOKEN_STRING` stores an offset
  and length into that pool. `parser_parse` copies the bytes it needs into
  `Ast.bytes`, so it borrows the pool only for the duration of the call.

## Compatibility

There is no stability promise yet: this is a prototype, and headers change with
the language. When the API does stabilise, this file is where the guarantee gets
recorded.
