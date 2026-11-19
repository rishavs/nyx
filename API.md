# Nyx C API

The public headers in `include/` and what a caller may rely on. The command-line
driver in `compiler/main.c` is just one client of this API.

Everything here is C11 and depends only on libc.

## Entry points

| Header | Purpose |
|---|---|
| `compiler.h` | The one-shot API: source string in, C11 text out |
| `limits_config.h` | Named defaults and fixed compiler safety limits |
| `error.h` | `ErrorState`, the diagnostic accumulator |
| `prelude.h` | `prelude_is_needed`, `prelude_append`: the language's own declarations (`ErrorKind`), added to a program's tokens when it names them |
| `alloc.h` | Checked allocation wrappers and the test failure hook |
| `messages.h` | Every user-facing message, as `MESSAGE_*` macros |
| `source_pos.h` | `SourcePos`: byte offset plus 1-based line and column |
| `identifier.h` | `NYX_IDENTIFIER_MAX`, the shared name limit, and `NYX_GENERATED_PREFIX`, the reserved emitted-name prefix |
| `operator.h` | `NyxOperator`, shared by syntax and typed nodes |
| `list.h` | `List`, the owning dynamic array |

Internal headers under `compiler/<stage>/` are implementation details and are
not part of this API. The test runner (`tests/harness/harness.h`) is used only
by the test binaries and is not installed with `include/`; it is described in
`BUILD.md`.

Stage headers (`lexer.h`, `parser.h`, `checker.h`, `analyser.h`, `generator.h`)
expose the individual pipeline stages. `ast.h` and `typed_program.h` expose the
artifacts those stages exchange. Application code should prefer `compiler.h`.

## `compiler.h`

```c
typedef struct {
    const char *header_name;  /* generated header filename; NULL -> "nyx.h" */
    const char *source_name;  /* shown in diagnostics and #line; NULL -> "nyx.nip" */
    int error_header_capacity;      /* 0 -> default 32 */
    int error_description_capacity; /* 0 -> default 128 */
    int error_filename_capacity;    /* 0 -> default 128 */
    int max_source_bytes;           /* source-size limit; 0 -> 1 MiB */
    int max_string_capacity;        /* String<N> upper bound; 0 -> 4096 */
    int max_list_capacity;          /* Array/Record capacity bound; 0 -> 4096 */
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
the hard limit `NYX_MAX_STRING_CAPACITY` (1 MiB). `max_list_capacity` bounds
fixed `Array<T,N>` and `Record<K,V,N>` capacities; it defaults to 4096 and may
not exceed `NYX_MAX_LIST_CAPACITY` (1 MiB). All by-value types are capped at
`max_value_bytes`, which defaults to `NYX_MAX_VALUE_BYTES` (64 KiB) and may not
exceed `NYX_MAX_VALUE_BYTES_HARD` (16 MiB); larger structs, Arrays, Records,
Strings, Errors, and unions are rejected, and so are channel messages and
`parallel` arm arguments and results that exceed it.
`max_print_depth` defaults to 16 and may not exceed `NYX_MAX_PRINT_DEPTH`
(1024); explicit values must be positive.
`max_source_bytes` defaults to 1 MiB (1048576). `compiler_compile` rejects longer source
strings, and the command-line driver uses the same limit when reading a file.
The driver accepts `--max-source-bytes N` to change the limit, where `N` is a
positive integer no greater than `INT_MAX`.

Ownership rules:

- The result owns `c_source`, `h_source`, and `error_message`. Release them with
  `compiler_result_free`, which also zeroes the struct.
- On success, `succeeded` is true, both sources are non-NULL, and
  `error_message` is NULL.
- On failure, `succeeded` is false and both sources are NULL. `error_message`
  is an owned diagnostic string when its allocation succeeds; it may be NULL
  if allocating that string fails. If no stage reported an error, the diagnostic
  is `Unknown Error` when allocation succeeds.
- `compiler_result_free(NULL)` is a no-op, and freeing an already-freed result is
  safe because the fields are cleared.

`compiler_compile` is called with `source != NULL`. Passing NULL is also
handled — it returns a failed result with `compiler: source string is null` —
but the caller is responsible for the string's lifetime, and it must be
NUL-terminated. Source size is limited by `CompilerConfig.max_source_bytes`
(default 4096); both `compiler_compile` and the command-line driver enforce it.
The driver can raise its limit with `--max-source-bytes N`.

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
    bool fatal;
    const char *source_name;   /* borrowed; used by positioned messages */
    size_t length;
    char message[ERROR_MESSAGE_CAPACITY];  /* 4096 bytes */
} ErrorState;

void error_init(ErrorState *state);
void error_report(ErrorState *state, const char *format, ...);
void error_report_at(ErrorState *state, SourcePos pos, const char *format, ...);
void error_report_fatal(ErrorState *state, const char *format, ...);
void error_report_fatal_at(ErrorState *state, SourcePos pos, const char *format, ...);
void error_unknown_if_none(ErrorState *state);
```

`ErrorState` is a fixed 4096-byte buffer, not a heap object, so a caller can put
it on the stack. Messages accumulate; each one is appended with a trailing
newline, and truncation is handled rather than asserted away.

- `error_init` clears the buffer and both status flags. Call it before use.
- `error_report` appends a nonfatal message with no source position.
- `error_report_at` prefixes the message with
  `file:line:column (byte offset): `. It uses `state->source_name`, or
  `<source>` when that is NULL.
- `error_report_fatal` and `error_report_fatal_at` also set the sticky `fatal`
  flag, so recovery does not depend on the wording of the diagnostic.
- `error_unknown_if_none` appends `Unknown Error` if nothing was reported yet.
  This is what guarantees a failure always carries a diagnostic.

The `format` argument takes printf conversions. `%zu` is used for sizes and
offsets.

## `alloc.h`

```c
#define NYX_ALLOC_NEVER ((size_t)-1)

void *nyx_malloc(size_t size);
void *nyx_realloc(void *pointer, size_t size);
char *nyx_strdup(const char *text);
void nyx_alloc_fail_after(size_t successful_allocations);
void nyx_alloc_reset(void);
void nyx_alloc_begin_counting(void);
size_t nyx_alloc_attempts(void);
```

The compiler allocates through these wrappers so that every out-of-memory path
stays reachable from a test. In normal operation they are `malloc`/`realloc`
with one counter comparison.

- `nyx_alloc_fail_after(n)` lets the next `n` allocations succeed and makes the
  one after that fail, then disables itself. Failing exactly one allocation at a
  time is what lets a test observe the diagnostic that failure produces.
- `nyx_alloc_reset()` or `nyx_alloc_fail_after(NYX_ALLOC_NEVER)` disables
  the hook.
- `nyx_alloc_begin_counting()` enables test-only allocation counting;
  `nyx_alloc_attempts()` reads the count. Resetting or arming a failure
  disables counting.
- The driver arms the hook from the environment variable
  `NYX_FAIL_ALLOC_AFTER`, which is a test-only interface. Unset it in normal
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
  `char[NYX_IDENTIFIER_MAX]` names and referring to them by index.
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
    NameIndex symbol_index; /* owned hash index into symbols */
    List roots;           /* indices into nodes */
    List children;        /* indices into nodes, for variable-arity children */
    List type_parameters; /* indices into nodes, for generic declarations */
    List bytes;           /* decoded string-literal bytes */
    size_t prelude_offset; /* where the language's own declarations begin in
                              the source; SIZE_MAX when the program has none */
} Ast;

typedef struct {
    List nodes;
    List symbols;
    NameIndex symbol_index;    /* owned hash index into symbols */
    List roots;
    List children;
    List types;                /* TypedType records */
    List signatures;           /* TypedFunctionSignature records */
    List signature_parameters; /* indices into types */
    List type_children;        /* indices into types: union alternatives and
                                  struct type arguments */
    List bytes;                /* decoded string-literal bytes */
    List match_temporaries;    /* TypedMatchTemporary records */
    List match_scopes;         /* per-function heads into match_temporaries */
    int error_header_capacity;      /* built-in Error field capacities */
    int error_description_capacity;
    int error_filename_capacity;
    int max_string_capacity;        /* upper bound for String<N> */
    int max_list_capacity;          /* upper bound for Array<T,N> */
    int max_print_depth;            /* pretty-print depth */
} TypedProgram;
```

`typed_program_init` seeds the capacities with the defaults: string maximum
4096, list maximum 4096, and Error fields 32/128/128 (the header capacity is that of an `ErrorKind::Other`).
`compiler_compile` overwrites them from
`CompilerConfig`.

`AstNode` and `TypedNode` carry `left_child`/`right_child` for direct links and
`children_start`/`children_count` for a run of variable-arity children in the
owning `children` list. `AstNode` additionally carries
`type_parameters_start`/`type_parameters_count`, indexing `Ast.type_parameters`
(used by generic struct, alias, sum type, and function declarations; 0/0 when
absent). `AST_ADT_DECLARATION` has the sum type's name in `name_index` and its
`AST_ADT_CASE` nodes as children; a case has its name in `name_index` and its
`AST_FIELD` nodes as children (none for a case that carries nothing).
`AST_TYPE_CASE` has the owner in `name_index`, the case in `second_name_index`
(`AST_NO_NODE` on every other type node) and the type arguments as children.
The lexer represents `===` with `TOKEN_EQEQEQ`; binary AST and typed nodes
retain it as `NYX_OPERATOR_STRICT_EQ`.
For `AST_FUNCTION_DECLARATION` and `AST_TYPE_FUNCTION`, `right_child` is
`AST_NO_NODE` when the source omits a return type. For `AST_RETURN`, `left_child`
is `AST_NO_NODE` for a bare return. The typed representation uses internal
`Nil` results for no-result functions.
For `AST_FIELD`, `left_child` is the type and `right_child` is its optional
literal default, or `AST_NO_NODE` when absent.
`AstNode.second_name_index` is used by `AST_FOR` for its optional second
binding, by associated function declarations for the owner type, and by static
calls for the owner type. `TypedNode.second_name_index` is used by
`TYPED_FOR` for its optional second binding.
`NYX_TYPE_RANGE` has one integer type child. `TYPED_RANGE_CREATE` has
two or three expression children for its bounds and optional `Int64` step;
the checker sequences those expressions using scoped temporaries.
`TYPED_CLOCK_NOW` has type `Int64` and no operands. `TYPED_SLEEP` has type `Nil`
and one `Int64` operand in `left_child`. The analyser rejects either node with
another shape. The C generated for a program that uses either one needs POSIX
`clock_gettime` and `nanosleep`; the source defines `_POSIX_C_SOURCE` itself.
`AST_MEMBER_CALL` stores its receiver in `left_child`, its optional type
argument in `right_child`, its method name in `name_index`, and its positional
arguments in the children run. `AST_EXPRESSION_STATEMENT` stores its call
expression in `left_child`; the checker accepts it only for call expressions.
For `Type::name(...)`, `AST_CALL` stores the owner name in
`second_name_index` and sets `is_static`.
Associated declarations store their owner type syntax in `owner_type_index`;
static calls use it for an explicitly specialized owner, or `AST_NO_NODE` for
a bare owner name.
`AST_DECLARATION` sets `is_global` for a `global` declaration and `is_const`
for a `const` declaration. Associated function declaration nodes set
`is_method` or `is_static`; a static `AST_CALL` sets `is_static`. `AST_PARAMETER`
sets `is_self` for the implicit instance-method receiver. `AST_ASSIGNMENT`
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
`TypedNode.auxiliary_type` is the secondary type link: the tested type for a
`TYPED_IS`, the element type for list push/pop and `for`, or the filename type for
`TYPED_ERROR_THROW`; it is `TYPED_NO_NODE` when unused. Function, `if`,
`while`, `for`, and `match` bodies are block nodes; block statements and `if`
branches are stored in `children`.
`TYPED_NO_NODE`/`AST_NO_NODE` means "no node".

`TYPED_FUNCTION_DECLARATION.is_method` marks an instance method whose first
parameter is emitted as a pointer. `TYPED_PARAMETER.is_self` and
`TYPED_IDENTIFIER.is_self` mark the implicit receiver parameter and its uses.
`TYPED_FUNCTION_CALL.is_method_call` marks calls whose first child is the
receiver; the generator passes its address. Static functions are emitted as
ordinary function declarations and calls after the checker resolves their
owner and name.

Resolved types are records in `TypedProgram.types`:

```c
typedef enum {
    NYX_TYPE_UNKNOWN,
    NYX_TYPE_INT32,
    NYX_TYPE_BOOL,
    NYX_TYPE_BYTE,
    NYX_TYPE_NIL,
    NYX_TYPE_STRUCT,
    NYX_TYPE_FUNCTION,
    NYX_TYPE_UNION,
    NYX_TYPE_STORE,
    NYX_TYPE_HANDLE,
    NYX_TYPE_STRING,
    NYX_TYPE_DYNAMIC_STRING,
    NYX_TYPE_TEXT_BUILDER,
    NYX_TYPE_LIST,
    NYX_TYPE_FIXED_LIST,
    NYX_TYPE_SLICE,
    NYX_TYPE_RECORD,
    NYX_TYPE_TABLE,
    NYX_TYPE_STRING_LITERAL,
    NYX_TYPE_ERROR,
    NYX_TYPE_INT8,
    NYX_TYPE_INT16,
    NYX_TYPE_INT64,
    NYX_TYPE_UINT16,
    NYX_TYPE_UINT32,
    NYX_TYPE_UINT64,
    NYX_TYPE_FLOAT32,
    NYX_TYPE_FLOAT64
} NyxType;

typedef struct {
    NyxType kind;
    size_t name_index;        /* struct C name symbol, or NO_NODE */
    size_t signature_index;   /* function signature, or NO_NODE */
    size_t first_child;       /* union alternatives or struct type arguments */
    size_t child_count;
    size_t declaration_node;  /* concrete struct declaration, or NO_NODE */
    size_t source_name_index; /* source struct name for inference, or NO_NODE */
    size_t fixed_length;      /* byte capacity for NYX_TYPE_STRING */
} TypedType;

typedef struct {
    size_t first_parameter; /* into TypedProgram.signature_parameters */
    size_t parameter_count;
    size_t return_type;     /* into TypedProgram.types */
} TypedFunctionSignature;
```

Lifecycle and access helpers:

The symbol indexes are owned by the artifacts and maintained by their interning
functions. Callers must not modify their slots or mutate the symbols lists
directly.

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
NyxType typed_program_node_kind(const TypedProgram *program, size_t node_index);
size_t typed_type_part_count(const TypedProgram *program, const TypedType *type);
bool typed_type_part(const TypedProgram *program, const TypedType *type,
                     size_t index, size_t *out_type);
bool typed_types_equivalent(const TypedProgram *program, size_t first,
                            size_t second, unsigned depth);
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

`typed_program_node_kind` is the kind of the type of node `node_index`, or
`NYX_TYPE_KIND_COUNT` when the node or its type does not exist, so a caller can
compare without checking each link.

A `NYX_TYPE_UNION` type's `first_child`/`child_count` run in
`program->type_children` holds the canonical alternatives. A
`NYX_TYPE_STRUCT` type's run holds its type arguments (empty for a non-generic
struct), and `declaration_node` points at the concrete
`TYPED_TYPE_DECLARATION` node whose field children describe the layout.

The typed node kinds include `TYPED_NIL_LITERAL`, `TYPED_SUM_INJECT`,
`TYPED_SUM_PAYLOAD`, `TYPED_IS`, `TYPED_IF_EXPRESSION`,
`TYPED_STORE_DECLARATION`,
`TYPED_STORE_ALLOC`, `TYPED_HANDLE_READ`,
`TYPED_HANDLE_WRITE`, `TYPED_EXPRESSION_STATEMENT`, `TYPED_ASSIGNMENT`,
`TYPED_LOCAL_DECLARATION`, `TYPED_MEMBER_ACCESS`, `TYPED_MEMBER_WRITE`,
`TYPED_PRINT`, `TYPED_ERROR_THROW`, `TYPED_RECORD_LITERAL`,
`TYPED_RECORD_GET`, `TYPED_RECORD_SET`, `TYPED_RECORD_HAS`,
`TYPED_RECORD_REMOVE`, `TYPED_RECORD_LEN`, `TYPED_TABLE_CREATE`,
`TYPED_NUMERIC_CONVERSION`, `TYPED_TABLE_GET`, `TYPED_TABLE_GET_OPTIONAL`,
`TYPED_TABLE_SET`,
`TYPED_TABLE_HAS`,
`TYPED_TABLE_REMOVE`, `TYPED_TABLE_LEN`,
`TYPED_INTEGER_LITERAL`, `TYPED_FLOAT_LITERAL`, `TYPED_STRING_LITERAL`,
`TYPED_STRING_LEN`, `TYPED_STRING_AT`, `TYPED_STRING_SLICE`,
`TYPED_STRING_CONCAT`, `TYPED_STRING_COPY`, `TYPED_STRING_FROM_FIXED`,
`TYPED_BUILDER_CREATE`, `TYPED_BUILDER_APPEND`, `TYPED_BUILDER_APPEND_BYTE`,
`TYPED_BUILDER_LEN`, `TYPED_BUILDER_FINISH`, `TYPED_LIST_LITERAL`,
`TYPED_LIST_LEN`, `TYPED_LIST_GET`, `TYPED_LIST_GET_OPTIONAL`,
`TYPED_LIST_SET`, `TYPED_LIST_PUSH`,
`TYPED_LIST_POP`, `TYPED_LIST_CREATE`, `TYPED_SLICE_CREATE`, `TYPED_SLICE_LEN`,
`TYPED_SLICE_GET`, `TYPED_ERROR_CONSTRUCT`, `TYPED_ERROR_FIELD`,
`TYPED_CLOCK_NOW`, `TYPED_SLEEP`, `TYPED_DEFER`, `TYPED_ATOMIC_DECLARATION`,
`TYPED_ATOMIC_LOAD`, `TYPED_ATOMIC_STORE`, `TYPED_ATOMIC_ADD`, `TYPED_PARALLEL`,
`TYPED_PARALLEL_ARM`, `TYPED_PARALLEL_RESULT`, `TYPED_CORES`,
`TYPED_CHANNEL_DECLARATION`, `TYPED_CHANNEL_OP`, `TYPED_CHANNEL_CREATE`,
`TYPED_WHEN`, `TYPED_WHEN_ARM`, and `TYPED_WHEN_RESULT`. For injection and
payload projection, `integer_value` is the alternative's canonical tag.
`TYPED_IS` stores the tested type in `auxiliary_type` and a tag in
`integer_value`; a statically decided test is emitted as a boolean literal, not
a tag comparison. `TYPED_IF_EXPRESSION` has the condition in `left_child`, the
then expression in `right_child`, and the else expression in a one-child run.
`match` arms are lowered into `if` chains and `TYPED_IS` or equality nodes.
`TYPED_MATCH_TEMPORARY` reads the saved scrutinee; a
`TYPED_MATCH_BIND_EXPRESSION` or `TYPED_MATCH_BIND_STATEMENT` evaluates the
scrutinee in `left_child` once before evaluating the lowered chain in
`right_child`. Their `integer_value_u` is an index in `match_temporaries`.
Each temporary stores its type and the next temporary in its function's chain;
`match_scopes` stores the heads, with scope zero for top-level code. Function
declarations store their scope index in `integer_value_u`.

`TYPED_NUMERIC_CONVERSION` has the target type in `type_index`, the source type
in `auxiliary_type`, and the converted value in `left_child`. A conversion of a
constant is folded by the checker: its `left_child` is already a literal of the
target type and `auxiliary_type` equals `type_index`, so the generator emits no
runtime check. Integer literals store their magnitude in `integer_value_u`; a
negative constant is a `TYPED_UNARY` negation of a literal typed as the unsigned
counterpart of the signed type, because the most negative value's magnitude does
not fit the signed type. Floating literals store their value in `float_value`.
A `TYPED_STRING_LITERAL` whose `fixed_type` is true has its type fixed where it
was written (the result of `base64`, always a `Text`); context never retypes it,
and the analyser rejects one with any other type.
The analyser checks that a literal's value fits its type's width, that a float
literal has a float type, and that a conversion has a numeric child whose type
is the recorded source type.

`TYPED_ATOMIC_DECLARATION` has the cell's value type, its id in `store_id`, and
its initial value as a sign in `integer_value` and a magnitude in
`integer_value_u`. `TYPED_ATOMIC_LOAD` has the cell's type and no operand;
`TYPED_ATOMIC_ADD` has it and one operand of that type; `TYPED_ATOMIC_STORE` has
type `Nil` and one operand. The cell is named by `store_id`, and the analyser
rejects a float cell, an add on a `Bool`, or a missing operand.
`TYPED_PARALLEL` has `integer_value` 1 when it owns a pool (then `name_index` is
the pool and `left_child` its capacity), an optional `TYPED_BLOCK` then section in
`right_child`, and one `TYPED_PARALLEL_ARM` child per arm. An arm holds its call
(possibly under `TYPED_MATCH_BIND_EXPRESSION` temporaries) in `left_child`, its
binding mode in `integer_value` (0 none, 1 assigns `name_index`, 2 declares it),
and, when repeated, its lane name in `second_name_index` and two bounds in its
children. `TYPED_PARALLEL_RESULT` names a declaring arm in `integer_value_u`.
`TYPED_CORES` is an `Int32` with no operand. The generated C for a program that
uses `parallel` needs POSIX threads and `<stdatomic.h>` and is built with
`-pthread`.

`TYPED_CHANNEL_DECLARATION` has the element type, the `element | Nil` receive
type in `auxiliary_type`, its id in `store_id`, its capacity in `integer_value`
and, for a timer, the interval in `integer_value_u`. `TYPED_CHANNEL_OP` names the
channel in `store_id`, holds a `ChannelOp` in `integer_value` (`CHANNEL_OP_SEND`,
`_RECV`, `_TRY_SEND`, `_TRY_RECV`, `_CLOSE`, `_CLOSED`), and has the value being
sent in `left_child` for the two send operations only. When the operation acts on
a `Channel<T>` value rather than a global channel, `store_id` is
`TYPED_NO_NODE` and `right_child` is the expression for the channel.
`TYPED_CHANNEL_CREATE` is `pool.channel<T>(capacity)`: `type_index` is the
`NYX_TYPE_CHANNEL` type, `auxiliary_type` the element, `integer_value_u` the
`element | Nil` type, `left_child` the `Int32` capacity, and the Store is named
as for `TYPED_LIST_CREATE`. A `TYPED_PARALLEL` node's `setup_block` is the block
of declarations that run before its arms, or `TYPED_NO_NODE`; a repeated arm
that assigns a result has `integer_value` 1, two children (the bounds), and the
target Array's capacity in `integer_value_u`. `TYPED_WHEN` has one
`TYPED_WHEN_ARM` child per arm and its else block in `right_child`; an arm holds
a `TYPED_CHANNEL_OP` that is a send or a receive in `left_child` and its block in
`right_child`, and `TYPED_WHEN_RESULT` names the arm whose binding it
initialises. The analyser rejects an operation on a channel that was not
declared, a channel element that is `Nil` or a union, a send without a value,
and a `when` with no arms. The C for a program with channels needs POSIX threads
and clocks.

`TYPED_DEFER` holds its block in `left_child` and has no type and no other
operand; the analyser rejects one whose `left_child` is not a `TYPED_BLOCK`.
The block is a statement of its parent block, which the generator relies on to
place its exit code.

`TYPED_SCOPE`: `name_index` identifies the capability, `left_child` is an
Int32 capacity, and `right_child` is its block. The analyser rejects a program
in which a value from a scoped Store could outlive the scope, with
`MESSAGE_ANALYSER_ESCAPE_ASSIGN`, `_WRITE`, `_RETURN`, `_CALL`, or
`_FUNCTION_VALUE`. `analyser_analyse` runs `analyser_validate` (links and Store
capabilities), the parallel audit, and then the escape analysis;
`analyser_validate` alone skips the last two. `analyser_validate` also rejects a
node whose kind or operator is out of range, a type whose kind is, a binary
operation whose operands are missing or untyped, and a struct whose fields are
not typed fields of the program, all with the diagnostic for the link that is
wrong (`MESSAGE_ANALYSER_INVALID_NODE_LINK`, `_INVALID_TYPE_INDEX`, `_INVALID_TYPE_CHILD`).
It also checks
each Store root (a Store type, a non-negative capacity, an id no other root
holds) with `MESSAGE_ANALYSER_INVALID_STORE_ID`; `generator_generate` relies on
it and does not re-check. `CompilerConfig.skip_escape_analysis` selects the latter and
exists only so tests can exercise the runtime generation checks, which the
analysis otherwise makes unreachable; never set it for user programs.

`TypedNode.store_id` carries the global store id for a store declaration,
allocation, concat, copy, fixed-to-dynamic conversion, or builder
creation. A Store parameter reference uses `TYPED_NO_NODE` and the node's
`name_index` to represent the named capability. Handle reads and writes use
`TYPED_NO_NODE`. A
`TYPED_STORE_DECLARATION` has type `Store` and `integer_value` = initial bytes;
a `TYPED_STORE_ALLOC` has type `Handle<T>` and its value in `left_child`; a
`TYPED_HANDLE_READ` has type `T` and the handle in `left_child`; a
`TYPED_HANDLE_WRITE` has type `Nil`, the handle in `left_child`, and the value
in `right_child`. `TYPED_ASSIGNMENT` has the target variable's type and name, and
the coerced value in `left_child`. A handle type's element type is its single entry in
`program->type_children`. Handle payloads may contain Text and handles; the
checker traverses recursive payload graphs and rejects Stores, Lists, Slices,
Tables, Records, and TextBuilders. Generation and bounds remain checked when
those handles or Text values are used.
Struct type indices can be referenced by their own fields through a handle;
completed typed programs contain the resolved declaration for each struct.

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
`right_child`; `TYPED_LIST_GET_OPTIONAL` has type `T | Nil`, the list or slice
in `left_child`, the index in `right_child`, and the element type in
`auxiliary_type`; `TYPED_LIST_SET` has type `Nil`, the list in `left_child`, and
two children for index and value; `TYPED_LIST_PUSH` has type `Nil`, the list
in `left_child`, the value in `right_child`, and `auxiliary_type` for the
element type; `TYPED_LIST_POP` has type `T | Nil`, the list in `left_child`, and
`auxiliary_type` for the element type; `TYPED_LIST_LEN` has type `Int32` and the
list in `left_child`; `TYPED_LOCAL_DECLARATION` has the local's type, name, and
initializer in `left_child`; `TYPED_MEMBER_ACCESS` has the field type, object in
`left_child`, and field name in `name_index`; `TYPED_MEMBER_WRITE` has type
`Nil`, the target access in `left_child`, and the value in `right_child`;
`TYPED_FOR` has type `Nil`, the iterable in `left_child`, the body in
`right_child`, the first binding in `name_index`, the optional second binding in
`second_name_index`, and the sequence element type in `auxiliary_type`; a map
loop derives its key and value types from the iterable type; `TYPED_PRINT`
has type `Nil` and its arguments in a children run; `TYPED_RECORD_LITERAL`
has type `Record<K,V,N>` and alternating key/value pairs in a children run;
map get/set/has/remove/len nodes carry the receiver and key (or key/value)
links; `TYPED_TABLE_CREATE` has the capacity in `left_child` and the store id
in `store_id`; `TYPED_TABLE_GET_OPTIONAL` has type `V | Nil`, the table in
`left_child`, the key in `right_child`, and the value type in
`auxiliary_type`; `TYPED_ERROR_THROW` has type
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
the field index in `integer_value` (0 kind, 1 description, 2 filename, 3 line,
4 col, 5 pos, 6 the text of the kind, a `String` of the header capacity that is
read from the kind and not stored). `NYX_TYPE_ERROR` is a built-in sized type
with the fixed layout `kind: ErrorKind`, `description: String<128>`,
`filename: String<128>`, `line/col/pos: Int32` by default. The kind (a union:
`ErrorKind` is a sum type, which the checker lowers to a union of one struct per
case) and the two string types are the Error type's three entries in
`program->type_children`, in that order. `NYX_TYPE_STRING_LITERAL` is the checker's internal
uncontextual literal type; a literal is retyped to `Text` or `String<N>` by
injection, and the generator rejects any literal that remains uncontextual.
`NYX_TYPE_DYNAMIC_STRING` (public type `Text`) and
`NYX_TYPE_TEXT_BUILDER` are sized public
types with no child types. `NYX_TYPE_LIST`, `NYX_TYPE_FIXED_LIST`, and `NYX_TYPE_SLICE` have one
element type in `type_children`; Arrays also use `fixed_length`.

The `add_*` helpers append and return the new index. The caller must check
`bool` results; a false return means allocation failed. `typed_program_copy_symbol`
interns text into the typed program's own symbol table, so the artifact does not
borrow from the syntax tree. `operator.h` defines `NyxOperator`, shared by
`AstNode` and `TypedNode` for arithmetic, bitwise, comparison, equality, and
logical operators.

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

bool analyser_analyse(const TypedProgram *program, Analysis *analysis,
                      ErrorState *errors);
bool analysis_collect(const TypedProgram *program, Analysis *analysis,
                      ErrorState *errors);
void analysis_free(Analysis *analysis);
const size_t *analysis_nodes(const Analysis *analysis, TypedNodeKind kind,
                             size_t *count);
const size_t *analysis_nodes_in(const Analysis *analysis, TypedNodeKind kind,
                                size_t root, size_t *count);
const size_t *analysis_types(const Analysis *analysis, NyxType kind,
                             size_t *count);
size_t analysis_nil_type(const Analysis *analysis);
bool analysis_collect_types(const TypedProgram *program, Analysis *analysis,
                            ErrorState *errors);

bool generator_generate(const TypedProgram *program, const Analysis *analysis,
                        const char *header_name, const char *source_name,
                        char **c_source, char **h_source, ErrorState *errors);
```

Rules that hold for every stage:

- Inputs are `const`. A stage never writes into its input artifact.
- A failure means the stage did not produce a usable output; the caller must not
  use it. The diagnostic is in `errors`.
- The caller owns and initialises/frees output artifacts (`ast_init`/`ast_free`,
  `typed_program_init`/`typed_program_free`, `lexer_result_free`,
  `generator_generate`'s two strings).
- `Analysis` (`include/analysis.h`) holds a feature bitset (`analysis_has`, with
  `analysis_generations_vary` and `analysis_needs_posix` derived from it), a
  Store count, and indexes of the program's nodes and types grouped by kind
  (`analysis_nodes` gives the nodes of one kind, `analysis_nodes_in` those of one
  root, given by its position in `TypedProgram.roots`; `analysis_types` gives the
  types of one kind; all in ascending order). It also holds lists of type
  indices the generator emits from: `type_emit_order`, `print_types`,
  `equal_types`, and `equal_pairs` (two entries per pair). The indexes are one allocation, so
  a built `Analysis` is released with `analysis_free`,
  which is safe on a zeroed or already freed one. `analyser_analyse` fills it
  only on success, and `analysis` may be NULL when the caller wants the verdict
  alone. `analysis_collect` fills it with no validation, tolerating links it
  cannot read, and returns false with `MESSAGE_ANALYSER_OUT_OF_MEMORY` if it
  cannot allocate the index; use it for a program built by hand, or after
  `analyser_validate` when escape analysis is skipped. The `Analysis` passed to
  `generator_generate` must describe that same program.
- `generator_generate` sets both output pointers to NULL before doing anything,
  so they are always either both valid or both NULL.
- `lexer_tokenize` returns either tokens or errors, never both. Text-literal
  bytes live in `LexerResult.string_bytes`; a `TOKEN_STRING` stores an offset
  and length into that pool. `parser_parse` copies the bytes it needs into
  `Ast.bytes`, so it borrows the pool only for the duration of the call.

## Compatibility

There is no stability promise yet: this is a prototype, and headers change with
the language. When the API does stabilise, this file is where the guarantee gets
recorded.
