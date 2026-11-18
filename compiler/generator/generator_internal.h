#ifndef NYX_GENERATOR_INTERNAL_H
#define NYX_GENERATOR_INTERNAL_H

#include "analysis.h"
#include "limits_config.h"
#include "macros.h"
#include "generator.h"

#include "alloc.h"
#include "messages.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* A growable string used to compose generated C. */
typedef struct {
    char *text;
    size_t length;
    size_t capacity;
    /* Set by the first append that cannot allocate. Every later append then
       does nothing, so code that writes a long run of text need not check each
       write: it checks `failed` where the run ends, once. */
    bool failed;
} TextBuffer;

/* One entry of the stack of blocks being emitted: what an exit (break,
   continue, return) must unwind, innermost first. */
typedef struct {
    size_t block;      /* the TYPED_BLOCK being emitted */
    size_t position;   /* statements before this index have completed */
    size_t scope_node; /* the TYPED_SCOPE whose body this is, or TYPED_NO_NODE */
    bool loop_body;    /* the body of the innermost enclosing loop */
} BlockFrame;

/* Everything one generator_generate call knows besides the typed program: the
   analyser's facts, and the state of the emission in progress. It lives on that
   call's stack and is initialised at its start, so a call that fails midway
   cannot leave anything behind for the next one. */
typedef struct {
    const Analysis *analysis;
    const char *source_name; /* shown in the #line directives */
    const char *header_name; /* the header the source includes */
    /* The one integer variable a program that is nothing else is printed from,
       or NULL: such a program gets a nyx_program_value function. */
    const TypedNode *program_value;
    BlockFrame frames[NYX_MAX_AST_DEPTH];
    size_t frame_count;
    unsigned scope_loop_depth; /* scopes being emitted inside the current loop */
} GeneratorContext;

void generator_context_init(GeneratorContext *gen, const Analysis *analysis,
                            const char *source_name, const char *header_name);

/* Internal helpers shared by the generator modules. */

bool valid_header_name(const char *name);
bool valid_line_name(const char *name);
bool valid_identifier(const char *name);

/* Every name the program chose (types, fields, variables, parameters,
   functions) reaches the C output with this prefix. C has one global namespace
   and the headers the output includes fill it with functions, typedefs, and
   macros, so a program name such as `div`, `remove`, or `EOF` would collide
   with them. A source name can never begin with `nyx_` (the checker rejects
   it), so a prefixed name cannot collide with libc, a keyword, a macro, or a
   name the generator invents. */
#define NYX_USER_PREFIX "nyx_u_"
#define USER_NAME_CAPACITY (NYX_IDENTIFIER_MAX + 16)
/* The C spelling of the program name with symbol index `symbol_index`, written
   into `out`. Returns `out`, or NULL when the symbol is missing or too long. */
const char *user_c_name(const TypedProgram *program, size_t symbol_index,
                        char *out, size_t size);
void buffer_init(TextBuffer *buffer);
void buffer_free(TextBuffer *buffer);
bool buffer_reserve(TextBuffer *buffer, size_t required);
bool buffer_append(TextBuffer *buffer, const char *text);
bool buffer_append_buffer(TextBuffer *buffer, const TextBuffer *part);
bool buffer_appendf(TextBuffer *buffer, const char *format, ...);
bool buffer_vappendf(TextBuffer *buffer, const char *format, va_list args);
/* Appends `depth` indents, then the formatted text. */
bool buffer_line(TextBuffer *buffer, size_t depth, const char *format, ...);
/* user_c_name, reporting an invalid identifier when the name has no C
   spelling. */
const char *checked_user_name(const TypedProgram *program, size_t symbol_index,
                              char *out, size_t size, ErrorState *errors);
bool format_c_type(const TypedProgram *program, size_t type_index, char *out,
    size_t out_size, ErrorState *errors);
bool format_function_pointer(const TypedProgram *program, size_t type_index,
    const char *name, char *out, size_t out_size, ErrorState *errors);
bool fixed_list_element_name(const TypedProgram *program, size_t list_type_index,
    size_t element, char *out, size_t out_size, ErrorState *errors);
bool handle_element_type(const TypedProgram *program, size_t handle_type_index,
    size_t *out_element);
bool find_union_alternative(const TypedProgram *program, const TypedType *union_type,
    size_t wanted, size_t *out_tag);
const TypedType *union_alternative_type(const TypedProgram *program,
    const TypedType *union_type, size_t position);
size_t find_store_declaration(const TypedProgram *program, size_t store_id);
bool store_id_is_valid(const TypedProgram *program, size_t store_id);
bool store_reference_is_valid(const TypedProgram *program, const TypedNode *node);
bool emit_store_reference(const TypedProgram *program, const TypedNode *node,
                          TextBuffer *buffer, ErrorState *errors);
bool type_is_c_aggregate(const TypedProgram *program, size_t type_index);
bool emit_children_expressions(const TypedProgram *program, const TypedNode *node,
    TextBuffer *buffer, ErrorState *errors);
const char *binary_operator_text(NyxOperator op);
/* `open`, the node's two operands joined by `middle`, then `close`; the
   one-operand form writes only the left child. */
bool emit_operands(const TypedProgram *program, const TypedNode *node,
                   TextBuffer *buffer, ErrorState *errors, const char *open,
                   const char *middle, const char *close);
bool emit_operand(const TypedProgram *program, const TypedNode *node,
                  TextBuffer *buffer, ErrorState *errors, const char *open,
                  const char *close);
/* `, __FILE__, line, column, offset`: where the runtime reports a failure. */
void position_text(const TypedNode *node, char *text, size_t size);
bool emit_expression(const TypedProgram *program, size_t node_index,
    TextBuffer *buffer, ErrorState *errors);
bool emit_match_temporaries(const TypedProgram *program, size_t scope,
    TextBuffer *source, ErrorState *errors);
bool struct_emitted_before(const TypedProgram *program, size_t index,
    size_t declaration_node, size_t name_index);
bool emit_struct_type(const TypedProgram *program, const TypedType *type,
    size_t declaration_node, const char *effective_source, TextBuffer *header,
    ErrorState *errors);
bool emit_union_type(const TypedProgram *program, const TypedType *type,
    size_t type_index, TextBuffer *header, ErrorState *errors);
bool emit_type_definitions(const TypedProgram *program,
                           const Analysis *analysis,
                           const char *effective_source,
                           TextBuffer *header, ErrorState *errors);
bool emit_function_typedefs(const TypedProgram *program, TextBuffer *header,
    ErrorState *errors);
bool append_indent(TextBuffer *source, size_t depth);
/* A `for` statement. */
bool emit_for_statement(const TypedProgram *program, GeneratorContext *gen,
                        size_t node_index, size_t depth,
                        const char *effective_source, TextBuffer *source,
                        ErrorState *errors);
/* The body of a loop, and the mark a scope inside it recycles its buffer
   against. */
bool emit_loop_body(const TypedProgram *program, GeneratorContext *gen,
                    size_t block_index, size_t depth,
                    const char *effective_source, TextBuffer *source,
                    ErrorState *errors);
void emit_loop_scope_mark(GeneratorContext *gen, TextBuffer *source,
                          size_t depth);
bool emit_block(const TypedProgram *program, GeneratorContext *gen, size_t block_index, size_t depth,
    const char *effective_source, TextBuffer *source, ErrorState *errors);
bool emit_statement(const TypedProgram *program, GeneratorContext *gen, size_t node_index, size_t depth,
    const char *effective_source, TextBuffer *source, ErrorState *errors);
bool emit_function(const TypedProgram *program, GeneratorContext *gen, const TypedNode *node,
    size_t root_position,
    const char *effective_source, TextBuffer *source, ErrorState *errors);
void emit_string_runtime(const TypedProgram *program, GeneratorContext *gen, TextBuffer *source);
bool emit_dynamic_string_runtime(const TypedProgram *program, GeneratorContext *gen,
    TextBuffer *source, ErrorState *errors);
bool emit_dynamic_string_literals(const TypedProgram *program, GeneratorContext *gen, TextBuffer *source);
bool emit_store_runtime(GeneratorContext *gen, TextBuffer *source, size_t store_count,
    bool needs_bytes_alloc, bool needs_aligned_alloc, bool pool,
    ErrorState *errors);
bool emit_store_inits(const TypedProgram *program, size_t store_count,
    const char *effective_source, TextBuffer *source, ErrorState *errors);
void emit_store_frees(size_t store_count, TextBuffer *source);
bool emit_var_to_main(const TypedProgram *program, const TypedNode *node,
    const char *effective_source, TextBuffer *source, ErrorState *errors);
bool append_print_program(const TypedProgram *program, GeneratorContext *gen, const TypedNode *var,
    size_t store_count, const char *effective_source, TextBuffer *source,
    ErrorState *errors);
/* The position of a root in the program's roots, which the analyser's node
   index is keyed by, or none: code that belongs to no root of its own. */
#define NO_ROOT_POSITION ((size_t)-1)
bool emit_scope_frame_declaration(const TypedProgram *program,
                                  GeneratorContext *gen, size_t root_position,
                                  TextBuffer *source);
bool emit_scope_frames(const TypedProgram *program, GeneratorContext *gen,
                       size_t root_position, TextBuffer *source);
bool emit_program_scope_frames(const TypedProgram *program,
                               GeneratorContext *gen, TextBuffer *source);
bool emit_builder_runtime(GeneratorContext *gen, TextBuffer *source, ErrorState *errors);
bool emit_list_runtime(const TypedProgram *program, GeneratorContext *gen,
    TextBuffer *source, ErrorState *errors);
bool emit_slice_runtime(const TypedProgram *program, GeneratorContext *gen, TextBuffer *source,
                        ErrorState *errors);
/* The static storage, locking, and typed entry points of every channel. */
bool emit_channel_runtime(const TypedProgram *program, GeneratorContext *gen, TextBuffer *source,
                          ErrorState *errors);
bool emit_when_statement(const TypedProgram *program, GeneratorContext *gen, size_t node_index,
                         size_t depth, const char *effective_source,
                         TextBuffer *source, ErrorState *errors);
bool emit_when_result(const TypedProgram *program, const TypedNode *node,
                      TextBuffer *buffer, ErrorState *errors);
bool emit_channel_create_expression(const TypedProgram *program,
                                    const TypedNode *node, TextBuffer *buffer,
                                    ErrorState *errors);
bool emit_channel_op_expression(const TypedProgram *program,
                                const TypedNode *node, TextBuffer *buffer,
                                ErrorState *errors);
/* The thread entry point and argument record of every parallel arm in the
   subtree, which must precede the function that starts them. */
bool emit_parallel_trampolines(const TypedProgram *program,
                               GeneratorContext *gen, size_t root_position,
                               TextBuffer *source, ErrorState *errors);
bool emit_program_trampolines(const TypedProgram *program,
                              GeneratorContext *gen, TextBuffer *source,
                              ErrorState *errors);
/* The pool of reusable worker threads that run parallel arms. */
bool emit_worker_runtime(GeneratorContext *gen, TextBuffer *source,
                         ErrorState *errors);
bool emit_parallel_statement(const TypedProgram *program, GeneratorContext *gen, size_t node_index,
                             size_t depth, const char *effective_source,
                             TextBuffer *source, ErrorState *errors);
/* A block that is the body of the `scope` or pool node `scope_node`. */
bool emit_block_in_scope(const TypedProgram *program, GeneratorContext *gen, size_t block_index,
                         size_t depth, const char *effective_source,
                         TextBuffer *source, ErrorState *errors,
                         size_t scope_node);
/* A static `_Atomic` cell for each `global x: Atomic<T>`. */
bool emit_atomic_declarations(const TypedProgram *program, TextBuffer *source,
                              ErrorState *errors);
bool emit_print_helper(GeneratorContext *gen, TextBuffer *source, const TypedProgram *program,
    size_t type_index, ErrorState *errors);
bool emit_print_helpers(const TypedProgram *program, GeneratorContext *gen, TextBuffer *source,
    ErrorState *errors);
bool emit_equality_helpers(const TypedProgram *program, GeneratorContext *gen, TextBuffer *source,
                           ErrorState *errors);
bool emit_print_statement(const TypedProgram *program, GeneratorContext *gen, const TypedNode *node,
    TextBuffer *source, ErrorState *errors, size_t depth);
bool map_key_names(const TypedProgram *program, size_t key_type, size_t value_type,
    char *key_name, size_t key_size, char *value_name, size_t value_size,
    ErrorState *errors);
bool emit_record_runtime(const TypedProgram *program, size_t type_index,
    const TypedType *type, TextBuffer *source, ErrorState *errors);
bool emit_arithmetic_runtime(TextBuffer *source, ErrorState *errors);
bool emit_map_runtime(const TypedProgram *program, GeneratorContext *gen, size_t store_count,
    TextBuffer *source, ErrorState *errors);
bool emit_expr_literals(const TypedProgram *program, const TypedNode *node,
    TextBuffer *buffer, ErrorState *errors, bool *handled);
bool emit_expr_strings(const TypedProgram *program, const TypedNode *node,
    size_t node_index, TextBuffer *buffer, ErrorState *errors, bool *handled);
bool emit_expr_collections(const TypedProgram *program, const TypedNode *node,
    size_t node_index, TextBuffer *buffer, ErrorState *errors, bool *handled);
bool emit_expr_access(const TypedProgram *program, const TypedNode *node,
    TextBuffer *buffer, ErrorState *errors, bool *handled);
bool emit_expr_operators(const TypedProgram *program, const TypedNode *node,
    TextBuffer *buffer, ErrorState *errors, bool *handled);

bool find_nil_tag(const TypedProgram *program, GeneratorContext *gen, const TypedType *union_type,
                  size_t *out_tag);
bool append_optional_union(GeneratorContext *gen, TextBuffer *body, const TypedProgram *program,
                           size_t union_index, size_t element_index,
                           const char *union_name, const char *value_name,
                           const char *result_name);


bool emit_conversion_runtime(TextBuffer *source, ErrorState *errors);

bool append_generation_check(TextBuffer *source, GeneratorContext *gen,
                             const char *store_generation,
                             const char *handle_generation, const char *fail);

/* Two ascending lists of node indices walked together as one, in order. */
typedef struct {
    const size_t *a;
    const size_t *b;
    size_t a_count;
    size_t b_count;
    size_t a_next;
    size_t b_next;
} NodeMerge;

void node_merge_init(NodeMerge *merge, const size_t *a, size_t a_count,
                     const size_t *b, size_t b_count);
bool node_merge_next(NodeMerge *merge, size_t *index);

#endif
