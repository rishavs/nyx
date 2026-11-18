#include "generator_internal.h"

typedef bool (*EmitExprStringsHandler)(const TypedProgram *program, const TypedNode *node, size_t node_index, TextBuffer *buffer, ErrorState *errors, bool *handled);

static bool is_string_type(NyxType kind)
{
    return kind == NYX_TYPE_STRING || kind == NYX_TYPE_DYNAMIC_STRING;
}

static bool emit_string_string_literal_format_c(const TypedType *type, const TypedProgram *program, const TypedNode *node, char (*type_name)[64], ErrorState *errors, TextBuffer *buffer, size_t *i)
{
    if (type->kind != NYX_TYPE_STRING ||
        !format_c_type(program, node->type_index, (*type_name),
                       sizeof((*type_name)), errors)) {
        if (!errors->reported) {
            error_report(errors,
                         MESSAGE_GENERATOR_UNSUPPORTED_EXPRESSION);
        }
        return false;
    }
    buffer_appendf(buffer, "(%s){ { ", (*type_name));
    for ((*i) = 0; (*i) < node->string_length; (*i)++) {
        const unsigned char *byte = list_get(
            &program->bytes, node->string_offset + (*i));

        if (byte == NULL) {
            return false;
        }
        buffer_appendf(buffer, "%u, ", (unsigned)*byte);
    }
    buffer_appendf(buffer, "0 }, %zu }", node->string_length);
    return true;
}

static bool emit_string_string_literal(const TypedProgram *program, const TypedNode *node, size_t node_index, TextBuffer *buffer, ErrorState *errors, bool *handled)
{
    *handled = true;
    const TypedType *type = typed_program_type(program, node->type_index);
    char type_name[NYX_IDENTIFIER_MAX + 32];
    size_t i;

    if (type == NULL) {
        error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_EXPRESSION);
        return false;
    }
    if (type->kind == NYX_TYPE_DYNAMIC_STRING) {
        buffer_appendf(buffer,
                        "(nyx_string){ nyx_dyn_str_%zu, 0, 0, "
                        "%zu, 0 }",
                        node_index, node->string_length);
        return true;
    }
    if (!emit_string_string_literal_format_c(type, program, node, &type_name, errors, buffer, &i)) {
        return false;
    }
    return true;
}

/* The length of a Text is checked at the use; a String's is a field. */
static bool emit_string_string_len(const TypedProgram *program, const TypedNode *node, size_t node_index, TextBuffer *buffer, ErrorState *errors, bool *handled)
{
    char tail[96];

    (void)node_index;
    *handled = true;
    if (typed_program_node_kind(program, node->left_child) ==
        NYX_TYPE_DYNAMIC_STRING) {
        position_text(node, tail, sizeof(tail));
        strcat(tail, ")");
        return emit_operand(program, node, buffer, errors,
                            "nyx_string_len_at(", tail);
    }
    return emit_operand(program, node, buffer, errors, "((int32_t)(",
                        ").length)");
}

/* One byte of a string, bounds-checked. */
static bool emit_string_string_at(const TypedProgram *program, const TypedNode *node, size_t node_index, TextBuffer *buffer, ErrorState *errors, bool *handled)
{
    const TypedType *receiver_type = typed_program_type(
        program, typed_program_node(program, node->left_child)->type_index);
    char head[64];
    char tail[96];

    (void)node_index;
    *handled = true;
    if (receiver_type == NULL || !is_string_type(receiver_type->kind)) {
        error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_EXPRESSION);
        return false;
    }
    if (receiver_type->kind == NYX_TYPE_DYNAMIC_STRING) {
        snprintf(head, sizeof(head), "nyx_string_byte_at(");
    } else {
        snprintf(head, sizeof(head), "nyx_string_%zu_at(",
                 receiver_type->fixed_length);
    }
    position_text(node, tail, sizeof(tail));
    strcat(tail, ")");
    return emit_operands(program, node, buffer, errors, head, ", ", tail);
}

static bool emit_string_string_slice(const TypedProgram *program, const TypedNode *node, size_t node_index, TextBuffer *buffer, ErrorState *errors, bool *handled)
{
    (void)node_index;
    *handled = true;
    if (node->left_child == TYPED_NO_NODE ||
        node->children_count != 2) {
        error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_EXPRESSION);
        return false;
    }
    {
        size_t start = *(const size_t *)list_get(
            &program->children, node->children_start);
        size_t length = *(const size_t *)list_get(
            &program->children, node->children_start + 1);

        buffer_append(buffer, "nyx_string_slice(");
        if (!emit_expression(program, node->left_child, buffer,
                         errors)) {
            return false;
        }
        buffer_append(buffer, ", ");
        if (!emit_expression(program, start, buffer, errors)) {
            return false;
        }
        buffer_append(buffer, ", ");
        if (!emit_expression(program, length, buffer, errors)) {
            return false;
        }
        buffer_appendf(buffer, ", __FILE__, %zu, %zu, %zu)",
                        node->pos.line, node->pos.column,
                        node->pos.offset);
    }
    return true;
}

static bool emit_string_string_concat_store_reference_is_valid(const TypedNode *node, const TypedProgram *program, ErrorState *errors, TextBuffer *buffer)
{
    if (node->left_child == TYPED_NO_NODE ||
        !store_reference_is_valid(program, node)) {
        error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_EXPRESSION);
        return false;
    }
    buffer_appendf(buffer, "nyx_string_%s(",
                    node->kind == TYPED_STRING_CONCAT ? "concat" : "copy");
    if (!emit_store_reference(program, node, buffer, errors)) {
        return false;
    }
    buffer_append(buffer, ", ");
    if (!emit_expression(program, node->left_child, buffer, errors)) {
        return false;
    }
    return true;
}

static bool emit_string_string_concat(const TypedProgram *program, const TypedNode *node, size_t node_index, TextBuffer *buffer, ErrorState *errors, bool *handled)
{
    (void)node_index;
    *handled = true;
    if (!emit_string_string_concat_store_reference_is_valid(node, program, errors, buffer)) {
        return false;
    }
    if (node->kind == TYPED_STRING_CONCAT) {
        if (node->right_child == TYPED_NO_NODE) {
            return false;
        }
        buffer_append(buffer, ", ");
        if (!emit_expression(program, node->right_child, buffer,
                         errors)) {
            return false;
        }
    }
    buffer_appendf(buffer, ", __FILE__, %zu, %zu, %zu)",
                    node->pos.line, node->pos.column, node->pos.offset);
    return true;
}

static bool emit_string_string_from_fixed(const TypedProgram *program, const TypedNode *node, size_t node_index, TextBuffer *buffer, ErrorState *errors, bool *handled)
{
    (void)node_index;
    *handled = true;
    const TypedNode *source = typed_program_node(program,
                                                 node->left_child);
    const TypedType *source_type =
        source == NULL
            ? NULL
            : typed_program_type(program, source->type_index);

    if (source_type == NULL || source_type->kind != NYX_TYPE_STRING ||
        !store_reference_is_valid(program, node)) {
        error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_EXPRESSION);
        return false;
    }
    buffer_appendf(buffer, "nyx_string_from_fixed_%zu(",
                    source_type->fixed_length);
    if (!emit_store_reference(program, node, buffer, errors)) {
        return false;
    }
    buffer_append(buffer, ", ");
    if (!emit_expression(program, node->left_child, buffer, errors)) {
        return false;
    }
    buffer_appendf(buffer, ", __FILE__, %zu, %zu, %zu)",
                    node->pos.line, node->pos.column, node->pos.offset);
    return true;
}

static bool emit_string_builder_create(const TypedProgram *program, const TypedNode *node, size_t node_index, TextBuffer *buffer, ErrorState *errors, bool *handled)
{
    (void)node_index;
    *handled = true;
    if (node->left_child == TYPED_NO_NODE ||
        !store_reference_is_valid(program, node)) {
        error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_EXPRESSION);
        return false;
    }
    buffer_append(buffer, "nyx_builder_create(");
    if (!emit_store_reference(program, node, buffer, errors)) {
        return false;
    }
    buffer_append(buffer, ", ");
    if (!emit_expression(program, node->left_child, buffer, errors)) {
        return false;
    }
    buffer_appendf(buffer, ", __FILE__, %zu, %zu, %zu)",
                    node->pos.line, node->pos.column, node->pos.offset);
    return true;
}

static bool emit_string_builder_append(const TypedProgram *program, const TypedNode *node, size_t node_index, TextBuffer *buffer, ErrorState *errors, bool *handled)
{
    (void)node_index;
    *handled = true;
    if (node->left_child == TYPED_NO_NODE ||
        node->right_child == TYPED_NO_NODE) {
        error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_EXPRESSION);
        return false;
    }
    buffer_append(buffer,
                   node->kind == TYPED_BUILDER_APPEND
                       ? "nyx_builder_append("
                       : "nyx_builder_append_byte(");
    if (!emit_expression(program, node->left_child, buffer, errors)) {
        return false;
    }
    buffer_append(buffer, ", ");
    if (!emit_expression(program, node->right_child, buffer, errors)) {
        return false;
    }
    buffer_appendf(buffer, ", __FILE__, %zu, %zu, %zu)",
                    node->pos.line, node->pos.column, node->pos.offset);
    return true;
}

static bool emit_string_builder_len(const TypedProgram *program, const TypedNode *node, size_t node_index, TextBuffer *buffer, ErrorState *errors, bool *handled)
{
    (void)node_index;
    *handled = true;
    if (node->left_child == TYPED_NO_NODE) {
        return false;
    }
    buffer_append(buffer, "nyx_builder_len(");
    if (!emit_expression(program, node->left_child, buffer, errors)) {
        return false;
    }
    buffer_appendf(buffer, ", __FILE__, %zu, %zu, %zu)",
                    node->pos.line, node->pos.column, node->pos.offset);
    return true;
}

static bool emit_string_builder_finish(const TypedProgram *program, const TypedNode *node, size_t node_index, TextBuffer *buffer, ErrorState *errors, bool *handled)
{
    (void)node_index;
    *handled = true;
    if (node->left_child == TYPED_NO_NODE) {
        return false;
    }
    buffer_append(buffer, "nyx_builder_finish(");
    if (!emit_expression(program, node->left_child, buffer, errors)) {
        return false;
    }
    buffer_appendf(buffer, ", __FILE__, %zu, %zu, %zu)",
                    node->pos.line, node->pos.column, node->pos.offset);
    return true;
}

static const struct {
    int key;
    EmitExprStringsHandler handler;
} emit_expr_strings_handlers[] = {
    {TYPED_STRING_LITERAL, emit_string_string_literal},
    {TYPED_STRING_LEN, emit_string_string_len},
    {TYPED_STRING_AT, emit_string_string_at},
    {TYPED_STRING_SLICE, emit_string_string_slice},
    {TYPED_STRING_CONCAT, emit_string_string_concat},
    {TYPED_STRING_COPY, emit_string_string_concat},
    {TYPED_STRING_FROM_FIXED, emit_string_string_from_fixed},
    {TYPED_BUILDER_CREATE, emit_string_builder_create},
    {TYPED_BUILDER_APPEND, emit_string_builder_append},
    {TYPED_BUILDER_APPEND_BYTE, emit_string_builder_append},
    {TYPED_BUILDER_LEN, emit_string_builder_len},
    {TYPED_BUILDER_FINISH, emit_string_builder_finish},
};

bool emit_expr_strings(const TypedProgram *program, const TypedNode *node,
                size_t node_index,
                TextBuffer *buffer, ErrorState *errors, bool *handled)
{
    for (size_t i = 0; i < NYX_COUNT_OF(emit_expr_strings_handlers); i++) {
        if (emit_expr_strings_handlers[i].key == (int)node->kind) {
            return emit_expr_strings_handlers[i].handler(program, node, node_index, buffer, errors, handled);
        }
    }
    return true;
}
