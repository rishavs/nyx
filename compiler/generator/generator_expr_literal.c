#include "generator_internal.h"

typedef bool (*EmitExprLiteralsHandler)(const TypedProgram *program, const TypedNode *node, TextBuffer *buffer, ErrorState *errors, bool *handled);

static bool emit_literal_integer_literal(const TypedProgram *program, const TypedNode *node, TextBuffer *buffer, ErrorState *errors, bool *handled)
{
    char type_name[NIPPY_IDENTIFIER_MAX + 32];
    *handled = true;
    {
        const TypedType *type = typed_program_type(program,
                                                   node->type_index);

        if (type != NULL &&
            (type->kind == NIPPY_TYPE_INT32 ||
             type->kind == NIPPY_TYPE_BYTE)) {
            if (!buffer_appendf(buffer, "%d", node->integer_value)) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
                return false;
            }
            return true;
        }
        if (type == NULL ||
            !format_c_type(program, node->type_index, type_name,
                           sizeof(type_name), errors) ||
            !buffer_appendf(buffer, "((%s)%lluULL)", type_name,
                            (unsigned long long)node->integer_value_u)) {
            if (!errors->reported) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            }
            return false;
        }
    }
    return true;
}

static bool emit_literal_float_literal(const TypedProgram *program, const TypedNode *node, TextBuffer *buffer, ErrorState *errors, bool *handled)
{
    char type_name[NIPPY_IDENTIFIER_MAX + 32];
    *handled = true;
    if (!format_c_type(program, node->type_index, type_name,
                       sizeof(type_name), errors) ||
        !buffer_appendf(buffer, "((%s)%.17g)", type_name,
                        node->float_value)) {
        if (!errors->reported) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
        }
        return false;
    }
    return true;
}

static bool emit_literal_bool_literal(const TypedProgram *program, const TypedNode *node, TextBuffer *buffer, ErrorState *errors, bool *handled)
{
    (void)program;
    *handled = true;
    if (!buffer_append(buffer, node->integer_value ? "true" : "false")) {
        error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
        return false;
    }
    return true;
}

static bool emit_literal_identifier(const TypedProgram *program, const TypedNode *node, TextBuffer *buffer, ErrorState *errors, bool *handled)
{
    const TypedType *identifier_type = typed_program_type(program, node->type_index);
    *handled = true;
    if (identifier_type != NULL && identifier_type->kind == NIPPY_TYPE_STORE) {
        return emit_store_reference(program, node, buffer, errors);
    }
    if (node->is_self) {
        if (!buffer_append(buffer, "(*nyx_self)")) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            return false;
        }
        return true;
    }
    char name_text[USER_NAME_CAPACITY];
const char *name = user_c_name(program, node->name_index,
                              name_text, sizeof(name_text));

    if (!valid_identifier(name)) {
        error_report(errors, MESSAGE_GENERATOR_INVALID_IDENTIFIER);
        return false;
    }
    if (!buffer_append(buffer, name)) {
        error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
        return false;
    }
    return true;
}

static bool emit_literal_nil_literal(const TypedProgram *program, const TypedNode *node, TextBuffer *buffer, ErrorState *errors, bool *handled)
{
    (void)program;
    (void)node;
    *handled = true;
    if (!buffer_append(buffer, "((nyx_nil)0)")) {
        error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
        return false;
    }
    return true;
}

static bool emit_literal_sum_inject_format_c(const TypedProgram *program, const TypedNode *node, char (*union_name)[64], ErrorState *errors, const TypedType *alternative, TextBuffer *buffer, bool *outcome)
{
    if (!format_c_type(program, node->type_index, (*union_name),
                       sizeof((*union_name)), errors)) {
        do { *outcome = (false); return true; } while (0);
    }
    if (alternative->kind == NIPPY_TYPE_NIL) {
        if (!buffer_appendf(buffer, "((%s){ .tag = %d })", (*union_name),
                            node->integer_value)) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            do { *outcome = (false); return true; } while (0);
        }
        do { *outcome = (true); return true; } while (0);
    }
    if (node->left_child == TYPED_NO_NODE) {
        error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_EXPRESSION);
        do { *outcome = (false); return true; } while (0);
    }
    if (!buffer_appendf(buffer, "((%s){ .tag = %d, .value.v%d = ",
                        (*union_name), node->integer_value,
                        node->integer_value) ||
        !emit_expression(program, node->left_child, buffer, errors) ||
        !buffer_append(buffer, " })")) {
        if (!errors->reported) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
        }
        do { *outcome = (false); return true; } while (0);
    }
    return false;
}

static bool emit_literal_sum_inject(const TypedProgram *program, const TypedNode *node, TextBuffer *buffer, ErrorState *errors, bool *handled)
{
    *handled = true;
    const TypedType *union_type = typed_program_type(program, node->type_index);
    const TypedType *alternative;
    char union_name[NIPPY_IDENTIFIER_MAX + 32];

    if (union_type == NULL || union_type->kind != NIPPY_TYPE_UNION ||
        node->integer_value < 0) {
        error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_EXPRESSION);
        return false;
    }
    alternative = union_alternative_type(program, union_type,
                                         (size_t)node->integer_value);
    if (alternative == NULL) {
        error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_EXPRESSION);
        return false;
    }
    {
        bool outcome;

        if (emit_literal_sum_inject_format_c(program, node, &union_name, errors, alternative, buffer, &outcome)) {
            return outcome;
        }
    }
    return true;
}

static bool emit_literal_sum_payload(const TypedProgram *program, const TypedNode *node, TextBuffer *buffer, ErrorState *errors, bool *handled)
{
    const TypedType *payload_type = typed_program_type(program,
                                                       node->type_index);

    *handled = true;
    if (node->integer_value < 0) {
        error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_EXPRESSION);
        return false;
    }
    if (payload_type != NULL && payload_type->kind == NIPPY_TYPE_NIL) {
        /* Nil has no payload member in the union, only a tag; the value
           of the narrowed alternative is the unit value itself. */
        if (!buffer_append(buffer, "((void)(") ||
            !emit_expression(program, node->left_child, buffer, errors) ||
            !buffer_append(buffer, "), (nyx_nil)0)")) {
            if (!errors->reported) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            }
            return false;
        }
        return true;
    }
    if (!buffer_append(buffer, "(") ||
        !emit_expression(program, node->left_child, buffer, errors) ||
        !buffer_appendf(buffer, ".value.v%d)", node->integer_value)) {
        if (!errors->reported) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
        }
        return false;
    }
    return true;
}

static bool emit_literal_if_expression(const TypedProgram *program, const TypedNode *node, TextBuffer *buffer, ErrorState *errors, bool *handled)
{
    *handled = true;
    size_t else_expression = *(const size_t *)list_get(
        &program->children, node->children_start);

    if (!buffer_append(buffer, "(") ||
        !emit_expression(program, node->left_child, buffer, errors) ||
        !buffer_append(buffer, " ? ") ||
        !emit_expression(program, node->right_child, buffer, errors) ||
        !buffer_append(buffer, " : ") ||
        !emit_expression(program, else_expression, buffer, errors) ||
        !buffer_append(buffer, ")")) {
        if (!errors->reported) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
        }
        return false;
    }
    return true;
}

static bool emit_literal_is_buffer_append(bool runtime, TextBuffer *buffer, ErrorState *errors, const TypedProgram *program, const TypedNode *node, size_t tag, bool *outcome)
{
    if (!runtime) {
        /* A tested type absent from the union is a decided false. */
        if (!buffer_append(buffer, "false")) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            do { *outcome = (false); return true; } while (0);
        }
        do { *outcome = (true); return true; } while (0);
    }
    if (!buffer_append(buffer, "(") ||
        !emit_expression(program, node->left_child, buffer, errors) ||
        !buffer_appendf(buffer, ".tag == %zu)", tag)) {
        if (!errors->reported) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
        }
        do { *outcome = (false); return true; } while (0);
    }
    return false;
}

static bool emit_literal_is_value(const TypedNode *value, const TypedProgram *program, ErrorState *errors, const TypedNode *node, TextBuffer *buffer, bool *outcome2)
{
    const TypedType *value_type =
        value == NULL ? NULL : typed_program_type(program, value->type_index);

    if (value == NULL || value_type == NULL) {
        error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_EXPRESSION);
        do { *outcome2 = (false); return true; } while (0);
    }
    if (value_type->kind == NIPPY_TYPE_UNION) {
        size_t tag = 0;
        bool runtime = true;

        if (node->auxiliary_type != TYPED_NO_NODE) {
            runtime = find_union_alternative(program, value_type,
                                             node->auxiliary_type, &tag);
        } else if (node->integer_value >= 0 &&
                   (size_t)node->integer_value < value_type->child_count) {
            tag = (size_t)node->integer_value;
        } else {
            error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_EXPRESSION);
            do { *outcome2 = (false); return true; } while (0);
        }
        {
        bool outcome;

        if (emit_literal_is_buffer_append(runtime, buffer, errors, program, node, tag, &outcome)) {
            do { *outcome2 = (outcome); return true; } while (0);
        }
    }
        do { *outcome2 = (true); return true; } while (0);
    }
    return false;
}

static bool emit_literal_is(const TypedProgram *program, const TypedNode *node, TextBuffer *buffer, ErrorState *errors, bool *handled)
{
    *handled = true;
    const TypedNode *value = typed_program_node(program, node->left_child);
    {
        bool outcome2;

        if (emit_literal_is_value(value, program, errors, node, buffer, &outcome2)) {
            return outcome2;
        }
    }
    {
        /* A type test on a non-union value is decided statically. */
        bool result;

        if (node->auxiliary_type != TYPED_NO_NODE) {
            result = types_equivalent(program, value->type_index,
                                      node->auxiliary_type, 16);
        } else if (node->integer_value >= 0) {
            result = node->integer_value != 0;
        } else {
            error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_EXPRESSION);
            return false;
        }
        if (!buffer_append(buffer, result ? "true" : "false")) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            return false;
        }
        return true;
    }
}

static const struct {
    int key;
    EmitExprLiteralsHandler handler;
} emit_expr_literals_handlers[] = {
    {TYPED_INTEGER_LITERAL, emit_literal_integer_literal},
    {TYPED_FLOAT_LITERAL, emit_literal_float_literal},
    {TYPED_BOOL_LITERAL, emit_literal_bool_literal},
    {TYPED_IDENTIFIER, emit_literal_identifier},
    {TYPED_NIL_LITERAL, emit_literal_nil_literal},
    {TYPED_SUM_INJECT, emit_literal_sum_inject},
    {TYPED_SUM_PAYLOAD, emit_literal_sum_payload},
    {TYPED_IF_EXPRESSION, emit_literal_if_expression},
    {TYPED_IS, emit_literal_is},
};

bool emit_expr_literals(const TypedProgram *program, const TypedNode *node,
                TextBuffer *buffer, ErrorState *errors, bool *handled)
{
    for (size_t i = 0; i < NIPPY_COUNT_OF(emit_expr_literals_handlers); i++) {
        if (emit_expr_literals_handlers[i].key == (int)node->kind) {
            return emit_expr_literals_handlers[i].handler(program, node, buffer, errors, handled);
        }
    }
    return true;
}
