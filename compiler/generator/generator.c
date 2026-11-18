#include "generator_internal.h"

/* What a root node of the typed program becomes in the generated C. */
typedef enum {
    ROOT_UNSUPPORTED,
    ROOT_NONE,       /* declared elsewhere: types, Stores, atomics, channels */
    ROOT_VARIABLE,   /* a program-level variable, initialised in main */
    ROOT_EXPRESSION, /* an expression statement */
    ROOT_STATEMENT,  /* a statement that runs in main */
    ROOT_FUNCTION
} RootKind;

static const struct {
    TypedNodeKind node;
    RootKind root;
} root_kinds[] = {
    {TYPED_TYPE_DECLARATION, ROOT_NONE},
    {TYPED_STORE_DECLARATION, ROOT_NONE},
    {TYPED_ATOMIC_DECLARATION, ROOT_NONE},
    {TYPED_CHANNEL_DECLARATION, ROOT_NONE},
    {TYPED_DECLARATION, ROOT_VARIABLE},
    {TYPED_EXPRESSION_STATEMENT, ROOT_EXPRESSION},
    {TYPED_SCOPE, ROOT_STATEMENT},
    {TYPED_PARALLEL, ROOT_STATEMENT},
    {TYPED_WHEN, ROOT_STATEMENT},
    {TYPED_ASSIGNMENT, ROOT_STATEMENT},
    {TYPED_LIST_SET, ROOT_STATEMENT},
    {TYPED_MEMBER_WRITE, ROOT_STATEMENT},
    {TYPED_RECORD_SET, ROOT_STATEMENT},
    {TYPED_TABLE_SET, ROOT_STATEMENT},
    {TYPED_FOR, ROOT_STATEMENT},
    {TYPED_WHILE, ROOT_STATEMENT},
    {TYPED_MATCH_BIND_STATEMENT, ROOT_STATEMENT},
    {TYPED_IF, ROOT_STATEMENT},
    {TYPED_FUNCTION_DECLARATION, ROOT_FUNCTION}
};

static RootKind root_kind(TypedNodeKind kind)
{
    size_t i;

    for (i = 0; i < NYX_COUNT_OF(root_kinds); i++) {
        if (root_kinds[i].node == kind) return root_kinds[i].root;
    }
    return ROOT_UNSUPPORTED;
}

/* What the roots say about the program as a whole. A program that is a single
   Int32 or Int64 variable and nothing else is printed by nyx_program_value. */
typedef struct {
    size_t variables;
    const TypedNode *single_variable;
    bool has_expression_statement;
    /* single_variable when it is the whole program and an Int32 or Int64. */
    const TypedNode *program_value;
} RootSummary;

static bool type_has_kind(const TypedProgram *program, size_t type_index,
                          NyxType kind)
{
    const TypedType *type = typed_program_type(program, type_index);

    return type != NULL && type->kind == kind;
}

static bool is_program_value_type(const TypedProgram *program,
                                 const TypedNode *variable)
{
    return type_has_kind(program, variable->type_index, NYX_TYPE_INT32) ||
           type_has_kind(program, variable->type_index, NYX_TYPE_INT64);
}

static bool summarise_roots(const TypedProgram *program, RootSummary *summary,
                            ErrorState *errors)
{
    size_t i;

    memset(summary, 0, sizeof(*summary));
    for (i = 0; i < program->roots.length; i++) {
        const TypedNode *node = typed_program_node(
            program, *(const size_t *)list_get(&program->roots, i));
        RootKind kind = root_kind(node->kind);

        if (kind == ROOT_UNSUPPORTED) {
            error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_ROOT);
            return false;
        }
        if (kind == ROOT_VARIABLE) {
            summary->variables++;
            summary->single_variable = node;
        }
        summary->has_expression_statement |= kind == ROOT_EXPRESSION;
    }
    if (!summary->has_expression_statement && summary->variables == 1 &&
        is_program_value_type(program, summary->single_variable)) {
        summary->program_value = summary->single_variable;
    }
    return true;
}

/* One root that runs in main: a variable, or a statement. */
static bool emit_root(const TypedProgram *program, GeneratorContext *gen,
                      size_t position, const char *effective_source,
                      TextBuffer *source, ErrorState *errors)
{
    size_t root = *(const size_t *)list_get(&program->roots, position);
    const TypedNode *node = typed_program_node(program, root);
    RootKind kind = root_kind(node->kind);

    if (kind == ROOT_VARIABLE) {
        return emit_var_to_main(program, node, effective_source, source, errors);
    }
    return (kind != ROOT_EXPRESSION && kind != ROOT_STATEMENT) ||
           emit_statement(program, gen, root, 1, effective_source, source,
                          errors);
}

/* A program that is more than one value: `main` runs the roots in order. */
static bool emit_main_program(const TypedProgram *program, GeneratorContext *gen,
                              size_t store_count, const char *effective_source,
                              TextBuffer *source, ErrorState *errors)
{
    size_t i;
    bool ok = emit_program_trampolines(program, gen, source, errors);

    buffer_append(source, "int main(void)\n{\n");
    ok = ok && emit_match_temporaries(program, 0, source, errors) &&
         emit_scope_frame_declaration(program, gen, NO_ROOT_POSITION, source) &&
         emit_store_inits(program, store_count, effective_source, source,
                          errors) &&
         emit_program_scope_frames(program, gen, source);
    for (i = 0; ok && i < program->roots.length; i++) {
        ok = emit_root(program, gen, i, effective_source, source, errors);
    }
    emit_store_frees(store_count, source);
    buffer_append(source, "    return 0;\n}\n");
    return ok;
}

static bool emit_functions(const TypedProgram *program, GeneratorContext *gen,
                           const char *effective_source, TextBuffer *source,
                           ErrorState *errors)
{
    size_t i;

    for (i = 0; i < program->roots.length; i++) {
        const TypedNode *node = typed_program_node(
            program, *(const size_t *)list_get(&program->roots, i));

        if (node->kind == TYPED_FUNCTION_DECLARATION &&
            !emit_function(program, gen, node, i, effective_source, source,
                           errors)) {
            return false;
        }
    }
    return true;
}


/* The generated program is assembled from pieces. Each is fixed C text, or a
   function that writes C, together with the facts that decide whether the
   program gets it: it is emitted when any of `any_of` holds (or `any_of` is
   zero), all of `all_of` hold, and none of `none_of` do. The tables below are
   the whole story of what a program's header and source contain and in what
   order, so adding a runtime feature is a row here and a bit in analysis.h. */
typedef bool (*PieceEmit)(const TypedProgram *program, GeneratorContext *gen,
                          TextBuffer *out, ErrorState *errors);

typedef struct {
    uint32_t any_of;
    uint32_t all_of;
    uint32_t none_of;
    const char *text;
    PieceEmit emit;
} Piece;

static bool piece_applies(const Piece *piece, const Analysis *analysis)
{
    uint32_t have = analysis->features;

    return (piece->any_of == 0 || (have & piece->any_of) != 0) &&
           (have & piece->all_of) == piece->all_of &&
           (have & piece->none_of) == 0;
}

static bool emit_pieces(const Piece *pieces, size_t count,
                        const TypedProgram *program, GeneratorContext *gen,
                        TextBuffer *out, ErrorState *errors)
{
    size_t i;

    for (i = 0; i < count; i++) {
        bool ok;

        if (!piece_applies(&pieces[i], gen->analysis)) continue;
        ok = pieces[i].text != NULL
                 ? buffer_append(out, pieces[i].text)
                 : pieces[i].emit(program, gen, out, errors);
        if (!ok || out->failed) {
            if (!errors->reported) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            }
            return false;
        }
    }
    return true;
}

#define ANALYSIS_NEEDS_SIZE_T                                                 \
    (ANALYSIS_STORE_OR_HANDLE | ANALYSIS_DYNAMIC_STRING | ANALYSIS_LIST |     \
     ANALYSIS_SLICE)
#define ANALYSIS_NEEDS_UNUSED_MACRO                                           \
    (ANALYSIS_NEEDS_SIZE_T | ANALYSIS_CHECKED_ARITHMETIC | ANALYSIS_CONVERSION)

/* Everything that makes the program include the C library at all. */
#define ANALYSIS_NEEDS_LIBC                                                   \
    (ANALYSIS_STORE_OR_HANDLE | ANALYSIS_STRING | ANALYSIS_DYNAMIC_STRING |   \
     ANALYSIS_LIST | ANALYSIS_SLICE | ANALYSIS_PRINT | ANALYSIS_RECORD |      \
     ANALYSIS_TABLE | ANALYSIS_CHECKED_ARITHMETIC | ANALYSIS_RECURSION |      \
     ANALYSIS_CONVERSION | ANALYSIS_RANGE | ANALYSIS_NOW | ANALYSIS_SLEEP |   \
     ANALYSIS_ATOMICS | ANALYSIS_PARALLEL | ANALYSIS_CORES |                  \
     ANALYSIS_CHANNELS)

static bool piece_type_definitions(const TypedProgram *program,
                                   GeneratorContext *gen, TextBuffer *out,
                                   ErrorState *errors)
{
    return emit_type_definitions(program, gen->analysis, gen->source_name, out,
                                 errors);
}

static bool piece_function_typedefs(const TypedProgram *program,
                                    GeneratorContext *gen, TextBuffer *out,
                                    ErrorState *errors)
{
    (void)gen;
    return emit_function_typedefs(program, out, errors);
}

/* A program that is one integer variable has a function that returns it. */
static bool piece_program_value_declaration(const TypedProgram *program,
                                            GeneratorContext *gen,
                                            TextBuffer *out,
                                            ErrorState *errors)
{
    (void)errors;
    const TypedType *type;

    if (gen->program_value == NULL) {
        return true;
    }
    type = typed_program_type(program, gen->program_value->type_index);
    buffer_appendf(out, "%s nyx_program_value(void);\n\n",
                    type->kind == NYX_TYPE_INT64 ? "int64_t" : "int32_t");
    return true;
}

static const Piece header_pieces[] = {
    {.text = "#ifndef NYX_GENERATED_H\n"
             "#define NYX_GENERATED_H\n\n"
             "#include <stdbool.h>\n"
             "#include <stdint.h>\n"},
    {.any_of = ANALYSIS_NEEDS_SIZE_T | ANALYSIS_STRING,
     .text = "#include <stddef.h>\n"},
    {.text = "\n"},
    {.any_of = ANALYSIS_RECORD | ANALYSIS_TABLE, .text = "#include <stddef.h>\n"},
    {.any_of = ANALYSIS_NEEDS_SIZE_T | ANALYSIS_TABLE,
     .text = "typedef struct nyx_store nyx_store;\n\n"},
    {.any_of = ANALYSIS_STORE_OR_HANDLE, .all_of = ANALYSIS_POOL,
     .text = "#include <stdatomic.h>\n\n"},
    {.any_of = ANALYSIS_STORE_OR_HANDLE,
     .text = "typedef struct {\n"
             "    nyx_store *store;\n"
             "    size_t offset;\n"
             "    uint64_t generation;\n"
             "} nyx_handle;\n\n"
             "struct nyx_store {\n"
             "    unsigned char *data;\n"
             "    size_t used;\n"
             "    size_t capacity;\n"
             "    uint64_t generation;\n"},
    {.any_of = ANALYSIS_STORE_OR_HANDLE, .all_of = ANALYSIS_POOL,
     .text = "    int pool;              /* a parallel block's fixed-capacity store */\n"
             "    _Atomic size_t cursor; /* where the next allocation starts */\n"},
    {.any_of = ANALYSIS_STORE_OR_HANDLE, .text = "};\n\n"},
    {.emit = piece_type_definitions},
    {.emit = piece_function_typedefs},
    {.emit = piece_program_value_declaration},
    {.text = "#endif\n"}
};

static bool piece_arithmetic(const TypedProgram *program, GeneratorContext *gen,
                             TextBuffer *out, ErrorState *errors)
{
    (void)program;
    (void)gen;
    return emit_arithmetic_runtime(out, errors);
}

static bool piece_conversion(const TypedProgram *program, GeneratorContext *gen,
                             TextBuffer *out, ErrorState *errors)
{
    (void)program;
    (void)gen;
    return emit_conversion_runtime(out, errors);
}

static bool piece_store(const TypedProgram *program, GeneratorContext *gen,
                        TextBuffer *out, ErrorState *errors)
{
    const Analysis *analysis = gen->analysis;

    (void)program;
    return emit_store_runtime(
        gen, out, analysis->store_count,
        analysis_has(analysis, ANALYSIS_DYNAMIC_STRING),
        analysis_has(analysis, ANALYSIS_DYNAMIC_LIST) ||
            analysis_has(analysis, ANALYSIS_TABLE),
        analysis_has(analysis, ANALYSIS_POOL), errors);
}

static bool piece_string(const TypedProgram *program, GeneratorContext *gen,
                         TextBuffer *out, ErrorState *errors)
{
    (void)errors;
    (void)gen;
    emit_string_runtime(program, gen, out);
    return true;
}

static bool piece_dynamic_string(const TypedProgram *program,
                                 GeneratorContext *gen, TextBuffer *out,
                                 ErrorState *errors)
{
    return emit_dynamic_string_runtime(program, gen, out, errors);
}

static bool piece_dynamic_literals(const TypedProgram *program,
                                   GeneratorContext *gen, TextBuffer *out,
                                   ErrorState *errors)
{
    (void)errors;
    (void)gen;
    return emit_dynamic_string_literals(program, gen, out);
}

static bool piece_builder(const TypedProgram *program, GeneratorContext *gen,
                          TextBuffer *out, ErrorState *errors)
{
    (void)program;
    return emit_builder_runtime(gen, out, errors);
}

static bool piece_list(const TypedProgram *program, GeneratorContext *gen,
                       TextBuffer *out, ErrorState *errors)
{
    return emit_list_runtime(program, gen, out, errors);
}

static bool piece_slice(const TypedProgram *program, GeneratorContext *gen,
                        TextBuffer *out, ErrorState *errors)
{
    return emit_slice_runtime(program, gen, out, errors);
}

static bool piece_map(const TypedProgram *program, GeneratorContext *gen,
                      TextBuffer *out, ErrorState *errors)
{
    return emit_map_runtime(program, gen, gen->analysis->store_count, out,
                            errors);
}

static bool piece_equality(const TypedProgram *program, GeneratorContext *gen,
                           TextBuffer *out, ErrorState *errors)
{
    (void)gen;
    return emit_equality_helpers(program, gen, out, errors);
}

static bool piece_print(const TypedProgram *program, GeneratorContext *gen,
                        TextBuffer *out, ErrorState *errors)
{
    return emit_print_helpers(program, gen, out, errors);
}

static bool piece_atomics(const TypedProgram *program, GeneratorContext *gen,
                          TextBuffer *out, ErrorState *errors)
{
    (void)gen;
    return emit_atomic_declarations(program, out, errors);
}

static bool piece_channels(const TypedProgram *program, GeneratorContext *gen,
                           TextBuffer *out, ErrorState *errors)
{
    return emit_channel_runtime(program, gen, out, errors);
}

static bool piece_workers(const TypedProgram *program, GeneratorContext *gen,
                          TextBuffer *out, ErrorState *errors)
{
    (void)program;
    return emit_worker_runtime(gen, out, errors);
}

/* What follows `#include` of the program's own header, in order. */
static bool piece_include_own_header(const TypedProgram *program,
                                     GeneratorContext *gen, TextBuffer *out,
                                     ErrorState *errors)
{
    (void)errors;
    (void)program;
    buffer_appendf(out, "#include \"%s\"\n\n", gen->header_name);
    return true;
}

/* A program that is one integer variable still needs stdio to print it, even
   when it uses nothing else from the C library. */
static bool piece_stdio_for_program_value(const TypedProgram *program,
                                          GeneratorContext *gen,
                                          TextBuffer *out, ErrorState *errors)
{
    (void)errors;
    (void)program;
    if (gen->program_value == NULL ||
        (gen->analysis->features & ANALYSIS_NEEDS_LIBC) != 0) {
        return true;
    }
    buffer_append(out, "#include <stdio.h>\n\n");
    return true;
}

static const Piece source_pieces[] = {
    /* clock_gettime and nanosleep are POSIX, and the driver builds with
       -std=c11, so the feature macro must precede the first system include. */
    {.any_of = ANALYSIS_POSIX_FEATURES,
     .text = "#define _POSIX_C_SOURCE 200809L\n\n"},
    {.emit = piece_include_own_header},
    {.emit = piece_stdio_for_program_value},
    /* system headers and the helpers that stand in for them */
    {.any_of = ANALYSIS_NEEDS_LIBC,
     .text = "#include <stdio.h>\n"
             "#include <stdlib.h>\n"
             "#include <string.h>\n\n"},
    {.any_of = ANALYSIS_CONVERSION, .text = "#include <float.h>\n\n"},
    {.any_of = ANALYSIS_NOW | ANALYSIS_SLEEP | ANALYSIS_PARALLEL |
               ANALYSIS_CHANNELS,
     .text = "#include <errno.h>\n"
             "#include <time.h>\n\n"},
    /* The wrappers are the one place the memory-order policy is written down:
       every atomic operation is sequentially consistent. */
    {.any_of = ANALYSIS_ATOMICS | ANALYSIS_PARALLEL,
     .text =
                       "#include <stdatomic.h>\n"
                       "#ifdef __STDC_NO_ATOMICS__\n"
                       "#error \"this program needs C11 atomics\"\n"
                       "#endif\n"
                       "#define nyx_atomic_load(cell) atomic_load_explicit((cell), memory_order_seq_cst)\n"
                       "#define nyx_atomic_store(cell, value) atomic_store_explicit((cell), (value), memory_order_seq_cst)\n"
                       "#define nyx_atomic_add(cell, value) atomic_fetch_add_explicit((cell), (value), memory_order_seq_cst)\n\n"},
    {.any_of = ANALYSIS_PARALLEL | ANALYSIS_CORES,
     .text = "#include <unistd.h>\n\n"},
    {.any_of = ANALYSIS_PARALLEL | ANALYSIS_CHANNELS,
     .text = "#include <pthread.h>\n\n"},
    /* Two threads can trap together, and calling exit twice is undefined. The
       first caller exits; any other waits for the process to end. The macro
       goes after every system include so the generated traps reach it. */
    {.any_of = ANALYSIS_PARALLEL,
     .text =
                       "static atomic_flag nyx_exiting = ATOMIC_FLAG_INIT;\n"
                       "static void nyx_exit_once(int status) {\n"
                       "    if (atomic_flag_test_and_set(&nyx_exiting)) {\n"
                       "        for (;;) { struct timespec ts = {1, 0}; nanosleep(&ts, NULL); }\n"
                       "    }\n"
                       "    (exit)(status);\n"
                       "}\n"
                       "#define exit(status) nyx_exit_once(status)\n\n"},
    {.any_of = ANALYSIS_CORES,
     .text =
                       "static inline int32_t nyx_cores(void) {\n"
                       "    long n = sysconf(_SC_NPROCESSORS_ONLN);\n"
                       "    return n < 1 ? 1 : n > INT32_MAX ? INT32_MAX : (int32_t)n;\n"
                       "}\n\n"},
    /* small runtime functions */
    {.any_of = ANALYSIS_RANGE,
     .text =
        "static inline int64_t nyx_range_step(int64_t step, const char *file, "
        "size_t line, size_t col, size_t pos) {\n"
        "    if (step == 0) { fflush(stdout); "
        "fprintf(stderr, \"" MESSAGE_RUNTIME_POSITION "\\n\", "
        "file, line, col, pos, \"" MESSAGE_RUNTIME_RANGE_ZERO_STEP "\"); exit(1); }\n"
        "    return step;\n}\n\n"},
    {.any_of = ANALYSIS_NOW,
     .text =
        "static inline int64_t nyx_clock_now(const char *file, size_t line, "
        "size_t col, size_t pos) {\n"
        "    struct timespec ts;\n"
        "    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) { fflush(stdout); "
        "fprintf(stderr, \"" MESSAGE_RUNTIME_POSITION "\\n\", "
        "file, line, col, pos, \"" MESSAGE_RUNTIME_CLOCK_UNAVAILABLE "\"); "
        "exit(1); }\n"
        "    return (int64_t)ts.tv_sec * INT64_C(1000) + "
        "(int64_t)(ts.tv_nsec / 1000000);\n}\n\n"},
    /* Separate from now(): sleep returns Nil, and a program that only reads the
       clock has no Nil typedef for it to name. */
    {.any_of = ANALYSIS_SLEEP,
     .text =
        "static inline nyx_nil nyx_sleep(int64_t ms, const char *file, "
        "size_t line, size_t col, size_t pos) {\n"
        "    struct timespec ts;\n"
        "    if (ms < 0) { fflush(stdout); "
        "fprintf(stderr, \"" MESSAGE_RUNTIME_POSITION "\\n\", "
        "file, line, col, pos, \"" MESSAGE_RUNTIME_SLEEP_NEGATIVE "\"); "
        "exit(1); }\n"
        "    ts.tv_sec = (time_t)(ms / 1000);\n"
        "    ts.tv_nsec = (long)(ms % 1000) * 1000000L;\n"
        "    while (nanosleep(&ts, &ts) != 0 && errno == EINTR) {}\n"
        "    return (nyx_nil)0;\n}\n\n"},
    {.any_of = ANALYSIS_RECURSION,
     .text = "#define NYX_RECURSION_LIMIT " NYX_STRINGIFY(
                 NYX_RUNTIME_RECURSION_LIMIT) "\n\n"},
    {.any_of = ANALYSIS_NEEDS_UNUSED_MACRO,
     .text =
                           "#if defined(__GNUC__) || defined(__clang__)\n"
                           "#define NYX_UNUSED __attribute__((unused))\n"
                           "#else\n"
                           "#define NYX_UNUSED\n"
                           "#endif\n\n"},
    /* exact comparison of numbers of different domains */
    {.any_of = ANALYSIS_EQUALITY_SIGNED_UNSIGNED,
     .text =
     "static inline bool nyx_eq_su(int64_t s, uint64_t u)\n"
     "{ return s >= 0 && (uint64_t)s == u; }\n"
     "static inline bool nyx_eq_us(uint64_t u, int64_t s)\n"
     "{ return nyx_eq_su(s, u); }\n\n"},
    {.any_of = ANALYSIS_EQUALITY_UNSIGNED_FLOAT | ANALYSIS_EQUALITY_SIGNED_FLOAT,
     .text =
     "static inline bool nyx_eq_uf(uint64_t u, double f)\n"
     "{\n"
     "    if (!(f >= 0.0 && f < 18446744073709551616.0)) return false;\n"
     "    return (uint64_t)f == u && (double)u == f;\n"
     "}\n"
     "static inline bool nyx_eq_fu(double f, uint64_t u)\n"
     "{ return nyx_eq_uf(u, f); }\n\n"},
    {.any_of = ANALYSIS_EQUALITY_SIGNED_FLOAT,
     .text =
     "static inline bool nyx_eq_sf(int64_t s, double f)\n"
     "{\n"
     "    if (s < 0) return f < 0.0 && nyx_eq_uf((uint64_t)(-(s + 1)) + 1, -f);\n"
     "    return nyx_eq_uf((uint64_t)s, f);\n"
     "}\n"
     "static inline bool nyx_eq_fs(double f, int64_t s)\n"
     "{ return nyx_eq_sf(s, f); }\n\n"},
    /* arithmetic, conversions, and the Store runtime everything allocates in */
    {.any_of = ANALYSIS_CHECKED_ARITHMETIC | ANALYSIS_CONVERSION,
     .emit = piece_arithmetic},
    {.any_of = ANALYSIS_CONVERSION, .emit = piece_conversion},
    {.any_of = ANALYSIS_STORE_OR_HANDLE, .emit = piece_store},
    {.any_of = ANALYSIS_SCOPE,
     .text =
        "typedef struct nyx_scope_frame {\n"
        "    nyx_store store;\n"
        "    struct nyx_scope_frame *previous; /* entered scopes, innermost first */\n"
        "    struct nyx_scope_frame *cached;   /* buffers a loop is keeping */\n"
        "    int recycled;\n"
        "} nyx_scope_frame;\n"
        "/* Every entered scope gets a generation no other Store has had, so a\n"
        "   handle kept past its scope cannot match a later one. */\n"},
    {.all_of = ANALYSIS_SCOPE | ANALYSIS_PARALLEL,
     .text = "static _Atomic uint64_t nyx_scope_generation = 1;\n"},
    {.all_of = ANALYSIS_SCOPE, .none_of = ANALYSIS_PARALLEL,
     .text = "static uint64_t nyx_scope_generation = 1;\n"},
    {.any_of = ANALYSIS_SCOPE,
     .text =
        "static NYX_UNUSED void nyx_scope_enter(nyx_scope_frame **top, nyx_scope_frame *frame, int32_t capacity, const char *file, size_t line, size_t column, size_t pos) {\n"
        "    if (capacity < 0) nyx_panic_at(\"" MESSAGE_RUNTIME_NEGATIVE_CAPACITY "\", file, line, column, pos);\n"
        "    nyx_store_init_at(&frame->store, (size_t)capacity, file, line, column, pos);\n"
        "    frame->store.generation = ++nyx_scope_generation;\n"
        "    frame->recycled = 0;\n"
        "    frame->previous = *top; *top = frame;\n"
        "}\n"
        "/* Inside a loop: the first entry allocates and the loop keeps the buffer\n"
        "   until it ends; later entries start it empty again. */\n"
        "static NYX_UNUSED void nyx_scope_enter_cached(nyx_scope_frame **top, nyx_scope_frame **cache, nyx_scope_frame *frame, int32_t capacity, const char *file, size_t line, size_t column, size_t pos) {\n"
        "    if (capacity < 0) nyx_panic_at(\"" MESSAGE_RUNTIME_NEGATIVE_CAPACITY "\", file, line, column, pos);\n"
        "    if (frame->store.data == NULL) {\n"
        "        nyx_store_init_at(&frame->store, (size_t)capacity, file, line, column, pos);\n"
        "        frame->cached = *cache; *cache = frame;\n"
        "    } else {\n"
        "        frame->store.used = 0;\n"
        "    }\n"
        "    frame->store.generation = ++nyx_scope_generation;\n"
        "    frame->recycled = 1;\n"
        "    frame->previous = *top; *top = frame;\n"
        "}\n"
        "static NYX_UNUSED void nyx_scope_cleanup(nyx_scope_frame **top, nyx_scope_frame *mark) {\n"
        "    while (*top != mark) {\n"
        "        nyx_scope_frame *frame = *top; *top = frame->previous;\n"
        "        if (frame->recycled) { frame->store.used = 0; frame->store.generation = 0; }\n"
        "        else nyx_store_free(&frame->store);\n"
        "    }\n"
        "}\n"
        "static NYX_UNUSED void nyx_scope_release(nyx_scope_frame **cache, nyx_scope_frame *mark) {\n"
        "    while (*cache != mark) { nyx_scope_frame *frame = *cache; *cache = frame->cached; nyx_store_free(&frame->store); }\n"
        "}\n\n"},
    /* The store of a parallel block. Its capacity is fixed, so its buffer never
       moves while threads allocate from it, and `used` is set to the whole
       capacity so bounds checks never read a counter another thread writes. */
    {.any_of = ANALYSIS_POOL,
     .text =
        "static NYX_UNUSED void nyx_pool_enter(nyx_scope_frame **top, nyx_scope_frame *frame, int32_t capacity, const char *file, size_t line, size_t column, size_t pos) {\n"
        "    if (capacity < 0) nyx_panic_at(\"" MESSAGE_RUNTIME_NEGATIVE_CAPACITY "\", file, line, column, pos);\n"
        "    nyx_store_init_at(&frame->store, (size_t)capacity, file, line, column, pos);\n"
        "    frame->store.generation = ++nyx_scope_generation;\n"
        "    frame->store.pool = 1;\n"
        "    atomic_store_explicit(&frame->store.cursor, 0, memory_order_seq_cst);\n"
        "    frame->store.used = frame->store.capacity;\n"
        "    frame->recycled = 0;\n"
        "    frame->previous = *top; *top = frame;\n"
        "}\n\n"},
    /* strings, collections, and what they need */
    {.any_of = ANALYSIS_STRING, .emit = piece_string},
    {.any_of = ANALYSIS_DYNAMIC_STRING, .emit = piece_dynamic_string},
    {.any_of = ANALYSIS_DYNAMIC_STRING, .emit = piece_dynamic_literals},
    {.any_of = ANALYSIS_STRING_BUILDER, .emit = piece_builder},
    {.any_of = ANALYSIS_LIST, .emit = piece_list},
    {.all_of = ANALYSIS_SLICE | ANALYSIS_DYNAMIC_LIST, .emit = piece_slice},
    {.any_of = ANALYSIS_RECORD | ANALYSIS_TABLE, .emit = piece_map},
    {.emit = piece_equality},
    /* printing floats: the shortest text that reads back as the same number */
    {.any_of = ANALYSIS_PRINT_FLOAT,
     .text =
        "#include <math.h>\n"
        "static void nyx_print_float(double value, int single)\n"
        "{\n"
        "    char buf[128], digits[32], candidate[64];\n"
        "    unsigned long long n = 0, chosen = 0;\n"
        "    int exponent = 0, decimal_power = 0, precision, i, count;\n"
        "    if (isnan(value)) { fputs(\"nan\", stdout); return; }\n"
        "    if (signbit(value)) { fputc('-', stdout); value = -value; }\n"
        "    if (isinf(value)) { fputs(\"inf\", stdout); return; }\n"
        "    if (value == 0.0) { fputs(\"0.0\", stdout); return; }\n"
        "    for (precision = 1; precision <= (single ? 9 : 17); precision++) {\n"
        "        char *ep;\n"
        "        snprintf(buf, sizeof(buf), \"%.*e\", precision - 1, value);\n"
        "        ep = strchr(buf, 'e');\n"
        "        exponent = (int)strtol(ep + 1, NULL, 10);\n"
        "        n = 0;\n"
        "        for (char *p = buf; p < ep; p++)\n"
        "            if (*p >= '0' && *p <= '9') n = n * 10 + (unsigned)(*p - '0');\n"
        "        decimal_power = exponent - (precision - 1);\n"
        "        for (i = 0; i < 3; i++) {\n"
        "            unsigned long long trial = i == 0 ? n : (i == 1 ? n - 1 : n + 1);\n"
        "            if (trial == 0) continue;\n"
        "            snprintf(candidate, sizeof(candidate), \"%llue%d\", trial, decimal_power);\n"
        "            if (single ? (double)strtof(candidate, NULL) == value\n"
        "                       : strtod(candidate, NULL) == value) {\n"
        "                chosen = trial;\n"
        "                break;\n"
        "            }\n"
        "        }\n"
        "        if (chosen != 0) break;\n"
        "    }\n"
        "    while (chosen % 10 == 0) { chosen /= 10; decimal_power++; }\n"
        "    snprintf(digits, sizeof(digits), \"%llu\", chosen);\n"
        "    count = (int)strlen(digits);\n"
        "    exponent = decimal_power + count - 1;\n"
        "    if (exponent >= -6 && exponent <= 15) {\n"
        "        int point = count + decimal_power;\n"
        "        if (point <= 0) {\n"
        "            fputs(\"0.\", stdout);\n"
        "            for (i = 0; i < -point; i++) fputc('0', stdout);\n"
        "            fputs(digits, stdout);\n"
        "        } else if (point >= count) {\n"
        "            fputs(digits, stdout);\n"
        "            for (i = count; i < point; i++) fputc('0', stdout);\n"
        "            fputs(\".0\", stdout);\n"
        "        } else {\n"
        "            for (i = 0; i < count; i++) {\n"
        "                if (i == point) fputc('.', stdout);\n"
        "                fputc(digits[i], stdout);\n"
        "            }\n"
        "        }\n"
        "    } else {\n"
        "        fputc(digits[0], stdout);\n"
        "        if (count > 1) { fputc('.', stdout); fputs(digits + 1, stdout); }\n"
        "        fprintf(stdout, \"e%+d\", exponent);\n"
        "    }\n"
        "}\n"},
    {.any_of = ANALYSIS_PRINT, .emit = piece_print},
    /* concurrency */
    {.any_of = ANALYSIS_ATOMICS, .emit = piece_atomics},
    {.any_of = ANALYSIS_CHANNELS, .emit = piece_channels},
    {.emit = piece_workers}
};

/* Every piece of the program, in order. */
static bool emit_program(const TypedProgram *program, GeneratorContext *gen,
                         const Analysis *analysis, const char *effective_source,
                         TextBuffer *header, TextBuffer *source,
                         ErrorState *errors)
{
    RootSummary roots;

    if (!summarise_roots(program, &roots, errors)) {
        return false;
    }
    gen->program_value = roots.program_value;
    return emit_pieces(header_pieces, NYX_COUNT_OF(header_pieces), program, gen,
                       header, errors) &&
           emit_pieces(source_pieces, NYX_COUNT_OF(source_pieces), program, gen,
                       source, errors) &&
           emit_functions(program, gen, effective_source, source, errors) &&
           (roots.program_value != NULL
                ? append_print_program(program, gen, roots.single_variable,
                                       analysis->store_count, effective_source,
                                       source, errors)
                : emit_main_program(program, gen, analysis->store_count,
                                    effective_source, source, errors));
}

/* The names the output refers to must be safe to write into C. */
static bool names_are_valid(const char *header_name, const char *source_name,
                            ErrorState *errors)
{
    bool header = valid_header_name(header_name);
    bool line = valid_line_name(source_name);

    if (!header) {
        error_report(errors, MESSAGE_GENERATOR_INVALID_HEADER_NAME);
    } else if (!line) {
        error_report(errors, MESSAGE_GENERATOR_INVALID_LINE_NAME);
    }
    return header && line;
}

/* A stage that fails has said why; if nothing else did, it ran out of memory. */
static void hand_over(bool ok, TextBuffer *header, TextBuffer *source,
                      char **h_source, char **c_source, ErrorState *errors)
{
    if (!ok) {
        error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
        return;
    }
    *h_source = header->text;
    header->text = NULL;
    *c_source = source->text;
    source->text = NULL;
}

bool generator_generate(const TypedProgram *program, const Analysis *analysis,
                        const char *header_name, const char *source_name,
                        char **c_source, char **h_source, ErrorState *errors)
{
    GeneratorContext context;
    const char *effective_header = header_name == NULL ? "nyx.h" : header_name;
    const char *effective_source = source_name == NULL ? "nyx.nip" : source_name;
    TextBuffer header;
    TextBuffer source;
    bool ok;

    *c_source = NULL;
    *h_source = NULL;
    generator_context_init(&context, analysis, effective_source, effective_header);
    if (!names_are_valid(effective_header, effective_source, errors)) {
        return false;
    }
    buffer_init(&header);
    buffer_init(&source);
    ok = emit_program(program, &context, analysis, effective_source, &header,
                      &source, errors) &&
         !header.failed && !source.failed;
    if (ok || !errors->reported) {
        hand_over(ok, &header, &source, h_source, c_source, errors);
    }
    buffer_free(&header);
    buffer_free(&source);
    return ok;
}

bool emit_var_to_main(const TypedProgram *program, const TypedNode *node,
                             const char *effective_source, TextBuffer *source,
                             ErrorState *errors)
{
    char name_text[USER_NAME_CAPACITY];
    const char *name = user_c_name(program, node->name_index,
                                  name_text, sizeof(name_text));
    char type_name[NYX_IDENTIFIER_MAX + 32];

    if (!valid_identifier(name) ||
        !format_c_type(program, node->type_index, type_name,
                       sizeof(type_name), errors)) {
        if (!errors->reported) {
            error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_ROOT);
        }
        return false;
    }
    buffer_appendf(source, "#line %zu \"%s\"\n", node->pos.line,
                    effective_source);
    buffer_appendf(source, "    %s %s = ", type_name, name);
    if (!emit_expression(program, node->left_child, source, errors)) {
        return false;
    }
    buffer_append(source, ";\n");
    buffer_appendf(source, "    (void)%s;\n", name);
    return true;
}


/* The statements among the roots, in order. */
static bool emit_root_statements(const TypedProgram *program,
                                 GeneratorContext *gen,
                                 const char *effective_source,
                                 TextBuffer *source, ErrorState *errors)
{
    size_t i;

    for (i = 0; i < program->roots.length; i++) {
        size_t root = *(const size_t *)list_get(&program->roots, i);

        if (root_kind(typed_program_node(program, root)->kind) ==
                ROOT_STATEMENT &&
            !emit_statement(program, gen, root, 1, effective_source, source,
                            errors)) {
            return false;
        }
    }
    return true;
}

/* nyx_program_value(): the one variable's value, after the statements. */
static bool emit_program_value_function(const TypedProgram *program,
                                        GeneratorContext *gen,
                                        const TypedNode *var, const char *name,
                                        const char *c_type,
                                        const char *effective_source,
                                        TextBuffer *source, ErrorState *errors)
{
    bool ok = emit_program_trampolines(program, gen, source, errors);

    buffer_appendf(source, "%s nyx_program_value(void)\n{\n", c_type);
    ok = ok && emit_match_temporaries(program, 0, source, errors) &&
         emit_scope_frame_declaration(program, gen, NO_ROOT_POSITION, source) &&
         emit_program_scope_frames(program, gen, source);
    buffer_appendf(source, "#line %zu \"%s\"\n", var->pos.line,
                   effective_source);
    buffer_appendf(source, "    %s %s = ", c_type, name);
    ok = ok && emit_expression(program, var->left_child, source, errors);
    buffer_append(source, ";\n");
    return ok && emit_root_statements(program, gen, effective_source, source,
                                      errors);
}

bool append_print_program(const TypedProgram *program, GeneratorContext *gen,
                                 const TypedNode *var, size_t store_count,
                                 const char *effective_source,
                                 TextBuffer *source, ErrorState *errors)
{
    char name_text[USER_NAME_CAPACITY];
    const char *name = checked_user_name(program, var->name_index, name_text,
                                         sizeof(name_text), errors);
    bool wide = type_has_kind(program, var->type_index, NYX_TYPE_INT64);

    if (name == NULL ||
        !emit_program_value_function(program, gen, var, name,
                                     wide ? "int64_t" : "int32_t",
                                     effective_source, source, errors)) {
        return false;
    }
    buffer_appendf(source, "    return %s;\n", name);
    buffer_append(source, "}\n\nint main(void)\n{\n");
    if (!emit_store_inits(program, store_count, effective_source, source, errors)) {
        return false;
    }
    buffer_append(source, wide
        ? "    printf(\"%lld\\n\", (long long)nyx_program_value());\n"
        : "    printf(\"%d\\n\", (int)nyx_program_value());\n");
    emit_store_frees(store_count, source);
    buffer_append(source, "    return 0;\n}\n");
    return true;
}


bool emit_store_inits(const TypedProgram *program, size_t store_count,
                      const char *effective_source, TextBuffer *source,
                      ErrorState *errors)
{
    size_t id;

    for (id = 0; id < store_count; id++) {
        size_t declaration_index = find_store_declaration(program, id);
        const TypedNode *declaration =
            typed_program_node(program, declaration_index);

        if (declaration == NULL || declaration->kind != TYPED_STORE_DECLARATION ||
            declaration->integer_value < 0) {
            error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_ROOT);
            return false;
        }
        buffer_appendf(source, "#line %zu \"%s\"\n",
                        declaration->pos.line, effective_source);
        buffer_appendf(source,
                        "    nyx_store_init_at(&nyx_stores[%zu], %d, __FILE__, %zu, %zu, %zu);\n",
                        id, declaration->integer_value, declaration->pos.line,
                        declaration->pos.column, declaration->pos.offset);
    }
    return true;
}


void emit_store_frees(size_t store_count, TextBuffer *source)
{
    size_t id;

    for (id = 0; id < store_count; id++) {
        buffer_appendf(source, "    nyx_store_free(&nyx_stores[%zu]);\n",
                        id);
    }
}
