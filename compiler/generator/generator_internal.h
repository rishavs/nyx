#ifndef NIPPY_GENERATOR_INTERNAL_H
#define NIPPY_GENERATOR_INTERNAL_H

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
} TextBuffer;

/* Internal helpers shared by the generator modules. */

bool valid_header_name(const char *name);
bool valid_line_name(const char *name);
bool valid_identifier(const char *name);
void buffer_init(TextBuffer *buffer);
void buffer_free(TextBuffer *buffer);
bool buffer_reserve(TextBuffer *buffer, size_t required);
bool buffer_append(TextBuffer *buffer, const char *text);
bool buffer_appendf(TextBuffer *buffer, const char *format, ...);
bool format_c_type(const TypedProgram *program, size_t type_index, char *out,
    size_t out_size, ErrorState *errors);
bool format_function_pointer(const TypedProgram *program, size_t type_index,
    const char *name, char *out, size_t out_size, ErrorState *errors);
bool fixed_list_element_name(const TypedProgram *program, size_t list_type_index,
    size_t element, char *out, size_t out_size, ErrorState *errors);
bool handle_element_type(const TypedProgram *program, size_t handle_type_index,
    size_t *out_element);
bool types_equivalent(const TypedProgram *program, size_t first, size_t second,
    unsigned depth);
bool find_union_alternative(const TypedProgram *program, const TypedType *union_type,
    size_t wanted, size_t *out_tag);
const TypedType *union_alternative_type(const TypedProgram *program,
    const TypedType *union_type, size_t position);
size_t find_store_declaration(const TypedProgram *program, size_t store_id);
bool store_id_is_valid(const TypedProgram *program, size_t store_id);
bool type_is_c_aggregate(const TypedProgram *program, size_t type_index);
bool emit_children_expressions(const TypedProgram *program, const TypedNode *node,
    TextBuffer *buffer, ErrorState *errors);
const char *binary_operator_text(NippyOperator op);
bool emit_expression(const TypedProgram *program, size_t node_index,
    TextBuffer *buffer, ErrorState *errors);
size_t find_struct_declaration(const TypedProgram *program, const TypedType *type);
bool struct_emitted_before(const TypedProgram *program, size_t index,
    size_t declaration_node, size_t name_index);
bool emit_struct_type(const TypedProgram *program, const TypedType *type,
    size_t declaration_node, const char *effective_source, TextBuffer *header,
    ErrorState *errors);
bool emit_union_type(const TypedProgram *program, const TypedType *type,
    size_t type_index, TextBuffer *header, ErrorState *errors);
bool emit_type_definitions(const TypedProgram *program, const char *effective_source,
    TextBuffer *header, ErrorState *errors);
bool emit_function_typedefs(const TypedProgram *program, TextBuffer *header,
    ErrorState *errors);
bool append_indent(TextBuffer *source, size_t depth);
bool emit_map_for(const TypedProgram *program, const TypedNode *node,
    const TypedType *iterable_type, size_t depth, const char *effective_source,
    TextBuffer *source, ErrorState *errors);
bool emit_block(const TypedProgram *program, size_t block_index, size_t depth,
    const char *effective_source, TextBuffer *source, ErrorState *errors);
bool emit_statement(const TypedProgram *program, size_t node_index, size_t depth,
    const char *effective_source, TextBuffer *source, ErrorState *errors);
bool emit_function(const TypedProgram *program, const TypedNode *node,
    const char *effective_source, TextBuffer *source, ErrorState *errors);
bool program_uses_string_builder(const TypedProgram *program);
bool program_uses_list(const TypedProgram *program);
bool program_uses_dynamic_list(const TypedProgram *program);
bool program_uses_slice(const TypedProgram *program);
bool program_uses_dynamic_string(const TypedProgram *program);
bool program_uses_string(const TypedProgram *program);
bool emit_string_runtime(const TypedProgram *program, TextBuffer *source,
    ErrorState *errors);
bool emit_dynamic_string_runtime(const TypedProgram *program, size_t store_count,
    TextBuffer *source, ErrorState *errors);
bool emit_dynamic_string_literals(const TypedProgram *program, TextBuffer *source,
    ErrorState *errors);
bool emit_store_runtime(TextBuffer *source, size_t store_count,
    bool needs_bytes_alloc, bool needs_aligned_alloc, ErrorState *errors);
bool emit_store_inits(const TypedProgram *program, size_t store_count,
    TextBuffer *source, ErrorState *errors);
bool emit_store_frees(size_t store_count, TextBuffer *source, ErrorState *errors);
bool emit_var_to_main(const TypedProgram *program, const TypedNode *node,
    const char *effective_source, TextBuffer *source, ErrorState *errors);
bool append_print_program(const TypedProgram *program, const TypedNode *var,
    size_t store_count, const char *effective_source, TextBuffer *source,
    ErrorState *errors);
size_t count_store_declarations(const TypedProgram *program);
bool program_uses_handle_or_store_type(const TypedProgram *program);
bool emit_builder_runtime(TextBuffer *source, ErrorState *errors);
bool emit_list_runtime(const TypedProgram *program, size_t store_count,
    TextBuffer *source, ErrorState *errors);
bool emit_slice_runtime(const TypedProgram *program, TextBuffer *source,
                        ErrorState *errors);
bool validate_store_declarations(const TypedProgram *program, size_t store_count,
    ErrorState *errors);
bool program_uses_binary_operator(const TypedProgram *program, NippyOperator op);
bool program_uses_print(const TypedProgram *program);
bool print_type_in_list(const List *types, size_t type_index);
bool collect_print_type(const TypedProgram *program, size_t type_index, List *types,
    unsigned depth, ErrorState *errors);
bool emit_print_helper(TextBuffer *source, const TypedProgram *program,
    size_t type_index, ErrorState *errors);
bool emit_print_helpers(const TypedProgram *program, TextBuffer *source,
    ErrorState *errors);
bool emit_print_statement(const TypedProgram *program, const TypedNode *node,
    TextBuffer *source, ErrorState *errors, size_t depth);
bool map_key_names(const TypedProgram *program, size_t key_type, size_t value_type,
    char *key_name, size_t key_size, char *value_name, size_t value_size,
    ErrorState *errors);
bool emit_record_runtime(const TypedProgram *program, size_t type_index,
    const TypedType *type, TextBuffer *source, ErrorState *errors);
bool emit_arithmetic_runtime(TextBuffer *source, ErrorState *errors);
bool emit_map_runtime(const TypedProgram *program, size_t store_count,
    TextBuffer *source, ErrorState *errors);
bool generator_generate(const TypedProgram *program, const char *header_name,
    const char *source_name, char **c_source, char **h_source, ErrorState *errors);
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

bool append_optional_union(TextBuffer *body, const TypedProgram *program,
                           size_t union_index, size_t element_index,
                           const char *union_name, const char *value_name,
                           const char *result_name);

bool typed_calls_name(const TypedProgram *program, size_t node_index,
                      size_t symbol_index);
bool program_uses_recursion(const TypedProgram *program);

#endif
