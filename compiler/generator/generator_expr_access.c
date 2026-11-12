#include "generator_internal.h"

bool emit_expr_access(const TypedProgram *program, const TypedNode *node,
                TextBuffer *buffer, ErrorState *errors, bool *handled)
{
    if (node->kind == TYPED_MEMBER_ACCESS) {
        *handled = true;
        const char *field = typed_program_symbol_text(program,
                                                      node->name_index);

        if (!valid_identifier(field) ||
            !buffer_append(buffer, "(") ||
            !emit_expression(program, node->left_child, buffer, errors) ||
            !buffer_appendf(buffer, ").%s", field)) {
            if (!errors->reported) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            }
            return false;
        }
        return true;
    }
    if (node->kind == TYPED_ERROR_THROW) {
        *handled = true;
        char filename_type[NIPPY_IDENTIFIER_MAX + 32];
        size_t i;

        if (!format_c_type(program, node->auxiliary_type, filename_type,
                           sizeof(filename_type), errors) ||
            node->children_count != 2) {
            error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_EXPRESSION);
            return false;
        }
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
        if (!buffer_append(buffer, ", ") ||
            !emit_expression(program, *(const size_t *)list_get(
                                         &program->children,
                                         node->children_start + 1),
                            buffer, errors) ||
            !buffer_appendf(buffer, ", (%s){ { ", filename_type)) {
            if (!errors->reported) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            }
            return false;
        }
        for (i = 0; i < node->string_length; i++) {
            unsigned char byte = *(const unsigned char *)list_get(
                &program->bytes, node->string_offset + i);

            if (!buffer_appendf(buffer, "%u%s", (unsigned)byte,
                                i + 1 == node->string_length ? "" : ", ")) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
                return false;
            }
        }
        if (!buffer_appendf(buffer,
                            "%s0 }, %zu }, %d, %d, %d }",
                            node->string_length == 0 ? "" : ", ",
                            node->string_length, (int)node->pos.line,
                            (int)node->pos.column, (int)node->pos.offset)) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            return false;
        }
        return true;
    }
    if (node->kind == TYPED_ERROR_CONSTRUCT) {
        *handled = true;
        if (!buffer_append(buffer, "(nyx_error){ ") ||
            !emit_children_expressions(program, node, buffer, errors) ||
            !buffer_append(buffer, " }")) {
            if (!errors->reported) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            }
            return false;
        }
        return true;
    }
    if (node->kind == TYPED_ERROR_FIELD) {
        *handled = true;
        static const char *const fields[] = {
            "header", "description", "filename", "line", "col", "pos"
        };

        if (node->integer_value < 0 || node->integer_value > 5) {
            error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_EXPRESSION);
            return false;
        }
        if (!buffer_append(buffer, "(") ||
            !emit_expression(program, node->left_child, buffer, errors) ||
            !buffer_appendf(buffer, ").%s",
                            fields[node->integer_value])) {
            if (!errors->reported) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            }
            return false;
        }
        return true;
    }
    return true;
}
