#include "generator_internal.h"

typedef bool (*EmitExprAccessHandler)(const TypedProgram *program, const TypedNode *node, TextBuffer *buffer, ErrorState *errors, bool *handled);

static bool emit_access_member_access(const TypedProgram *program, const TypedNode *node, TextBuffer *buffer, ErrorState *errors, bool *handled)
{
    *handled = true;
    char field_text[USER_NAME_CAPACITY];
const char *field = user_c_name(program, node->name_index,
                              field_text, sizeof(field_text));

    if (!valid_identifier(field)) {
        return false;
    }
    buffer_append(buffer, "(");
    if (!emit_expression(program, node->left_child, buffer, errors)) {
        return false;
    }
    buffer_appendf(buffer, ").%s", field);
    return true;
}

static bool emit_access_error_throw_buffer_append(TextBuffer *buffer, const TypedProgram *program, const TypedNode *node, ErrorState *errors, char (*filename_type)[64])
{
    if (!buffer_append(buffer, "(nyx_error){ ") ||
        !emit_expression(program, *(const size_t *)list_get(
                                     &program->children,
                                     node->children_start),
                        buffer, errors)) {
        if (!errors->reported) {
            error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_EXPRESSION);
        }
        return false;
    }
    buffer_append(buffer, ", ");
    if (!emit_expression(program, *(const size_t *)list_get(
                                 &program->children,
                                 node->children_start + 1),
                    buffer, errors)) {
        return false;
    }
    buffer_appendf(buffer, ", (%s){ { ", (*filename_type));
    return true;
}

static bool emit_access_error_throw_filename(const TypedProgram *program, const TypedNode *node, ErrorState *errors, TextBuffer *buffer)
{
    char filename_type[NYX_IDENTIFIER_MAX + 32];
    size_t i;

    if (!format_c_type(program, node->auxiliary_type, filename_type,
                       sizeof(filename_type), errors) ||
        node->children_count != 2) {
        error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_EXPRESSION);
        return false;
    }
    if (!emit_access_error_throw_buffer_append(buffer, program, node, errors, &filename_type)) {
        return false;
    }
    for (i = 0; i < node->string_length; i++) {
        unsigned char byte = *(const unsigned char *)list_get(
            &program->bytes, node->string_offset + i);

        buffer_appendf(buffer, "%u%s", (unsigned)byte,
                        i + 1 == node->string_length ? "" : ", ");
    }
    buffer_appendf(buffer,
                    "%s0 }, %zu }, %d, %d, %d }",
                    node->string_length == 0 ? "" : ", ",
                    node->string_length, (int)node->pos.line,
                    (int)node->pos.column, (int)node->pos.offset);
    return true;
}

static bool emit_access_error_throw(const TypedProgram *program, const TypedNode *node, TextBuffer *buffer, ErrorState *errors, bool *handled)
{
    *handled = true;
    if (!emit_access_error_throw_filename(program, node, errors, buffer)) {
        return false;
    }
    return true;
}

static bool emit_access_error_construct(const TypedProgram *program, const TypedNode *node, TextBuffer *buffer, ErrorState *errors, bool *handled)
{
    *handled = true;
    buffer_append(buffer, "(nyx_error){ ");
    if (!emit_children_expressions(program, node, buffer, errors)) {
        return false;
    }
    buffer_append(buffer, " }");
    return true;
}

static bool emit_access_error_field(const TypedProgram *program, const TypedNode *node, TextBuffer *buffer, ErrorState *errors, bool *handled)
{
    *handled = true;
    static const char *const fields[] = {
        "header", "description", "filename", "line", "col", "pos"
    };

    if (node->integer_value < 0 || node->integer_value > 5) {
        error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_EXPRESSION);
        return false;
    }
    buffer_append(buffer, "(");
    if (!emit_expression(program, node->left_child, buffer, errors)) {
        return false;
    }
    buffer_appendf(buffer, ").%s",
                    fields[node->integer_value]);
    return true;
}

static const struct {
    int key;
    EmitExprAccessHandler handler;
} emit_expr_access_handlers[] = {
    {TYPED_MEMBER_ACCESS, emit_access_member_access},
    {TYPED_ERROR_THROW, emit_access_error_throw},
    {TYPED_ERROR_CONSTRUCT, emit_access_error_construct},
    {TYPED_ERROR_FIELD, emit_access_error_field},
};

bool emit_expr_access(const TypedProgram *program, const TypedNode *node,
                TextBuffer *buffer, ErrorState *errors, bool *handled)
{
    for (size_t i = 0; i < NYX_COUNT_OF(emit_expr_access_handlers); i++) {
        if (emit_expr_access_handlers[i].key == (int)node->kind) {
            return emit_expr_access_handlers[i].handler(program, node, buffer, errors, handled);
        }
    }
    return true;
}
