#include "generator_internal.h"

typedef bool (*LiteralEmitter)(const TypedProgram *program,
                               const TypedNode *node, TextBuffer *buffer,
                               ErrorState *errors);

static bool emit_unsupported_expression(ErrorState *errors)
{
    error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_EXPRESSION);
    return false;
}

/* An Int32 or Byte literal is its own C literal; any other width is cast. */
static bool emit_integer_literal(const TypedProgram *program,
                                 const TypedNode *node, TextBuffer *buffer,
                                 ErrorState *errors)
{
    char type_name[NYX_IDENTIFIER_MAX + 32];
    NyxType kind = typed_program_type(program, node->type_index)->kind;

    if (kind == NYX_TYPE_INT32 || kind == NYX_TYPE_BYTE) {
        return buffer_appendf(buffer, "%d", node->integer_value);
    }
    if (!format_c_type(program, node->type_index, type_name, sizeof(type_name),
                       errors)) {
        return false;
    }
    return buffer_appendf(buffer, "((%s)%lluULL)", type_name,
                          (unsigned long long)node->integer_value_u);
}

static bool emit_float_literal(const TypedProgram *program,
                               const TypedNode *node, TextBuffer *buffer,
                               ErrorState *errors)
{
    char type_name[NYX_IDENTIFIER_MAX + 32];

    if (!format_c_type(program, node->type_index, type_name, sizeof(type_name),
                       errors)) {
        return false;
    }
    return buffer_appendf(buffer, "((%s)%.17g)", type_name, node->float_value);
}

static bool emit_bool_literal(const TypedProgram *program,
                              const TypedNode *node, TextBuffer *buffer,
                              ErrorState *errors)
{
    (void)program;
    (void)errors;
    return buffer_append(buffer, node->integer_value ? "true" : "false");
}

static bool emit_nil_literal(const TypedProgram *program, const TypedNode *node,
                             TextBuffer *buffer, ErrorState *errors)
{
    (void)program;
    (void)node;
    (void)errors;
    return buffer_append(buffer, "((nyx_nil)0)");
}

static bool emit_identifier(const TypedProgram *program, const TypedNode *node,
                            TextBuffer *buffer, ErrorState *errors)
{
    const TypedType *type = typed_program_type(program, node->type_index);
    char name_text[USER_NAME_CAPACITY];
    const char *name;

    if (type != NULL && type->kind == NYX_TYPE_STORE) {
        return emit_store_reference(program, node, buffer, errors);
    }
    if (node->is_self) {
        return buffer_append(buffer, "(*nyx_self)");
    }
    name = checked_user_name(program, node->name_index, name_text,
                             sizeof(name_text), errors);
    return name != NULL && buffer_append(buffer, name);
}

/* A value of a union: its tag, and the alternative's value when it has one. */
static bool emit_sum_inject(const TypedProgram *program, const TypedNode *node,
                            TextBuffer *buffer, ErrorState *errors)
{
    const TypedType *union_type = typed_program_type(program, node->type_index);
    const TypedType *alternative =
        union_alternative_type(program, union_type, (size_t)node->integer_value);
    char union_name[NYX_IDENTIFIER_MAX + 32];
    bool ok;

    if (!format_c_type(program, node->type_index, union_name,
                       sizeof(union_name), errors)) {
        return false;
    }
    if (alternative->kind == NYX_TYPE_NIL) {
        return buffer_appendf(buffer, "((%s){ .tag = %d })", union_name,
                              node->integer_value);
    }
    if (node->left_child == TYPED_NO_NODE) {
        return emit_unsupported_expression(errors);
    }
    buffer_appendf(buffer, "((%s){ .tag = %d, .value.v%d = ", union_name,
                   node->integer_value, node->integer_value);
    ok = emit_expression(program, node->left_child, buffer, errors);
    buffer_append(buffer, " })");
    return ok;
}

/* The alternative a union value is narrowed to. */
static bool emit_sum_payload(const TypedProgram *program, const TypedNode *node,
                             TextBuffer *buffer, ErrorState *errors)
{
    bool ok;

    if (typed_program_type(program, node->type_index)->kind == NYX_TYPE_NIL) {
        /* Nil has no payload member in the union, only a tag; the value of the
           narrowed alternative is the unit value itself. */
        buffer_append(buffer, "((void)(");
        ok = emit_expression(program, node->left_child, buffer, errors);
        buffer_append(buffer, "), (nyx_nil)0)");
        return ok;
    }
    buffer_append(buffer, "(");
    ok = emit_expression(program, node->left_child, buffer, errors);
    buffer_appendf(buffer, ".value.v%d)", node->integer_value);
    return ok;
}

static bool emit_if_expression(const TypedProgram *program,
                               const TypedNode *node, TextBuffer *buffer,
                               ErrorState *errors)
{
    size_t else_expression = *(const size_t *)list_get(&program->children,
                                                       node->children_start);
    bool ok;

    buffer_append(buffer, "(");
    ok = emit_expression(program, node->left_child, buffer, errors);
    buffer_append(buffer, " ? ");
    ok = ok && emit_expression(program, node->right_child, buffer, errors);
    buffer_append(buffer, " : ");
    ok = ok && emit_expression(program, else_expression, buffer, errors);
    buffer_append(buffer, ")");
    return ok;
}

/* The tag a type test on a union looks for. A tested type absent from the union
   is a decided false: `*present` is false. */
static bool is_test_tag(const TypedProgram *program, const TypedNode *node,
                        const TypedType *union_type, size_t *tag,
                        bool *present)
{
    *present = true;
    if (node->auxiliary_type != TYPED_NO_NODE) {
        *present = find_union_alternative(program, union_type,
                                          node->auxiliary_type, tag);
        return true;
    }
    *tag = (size_t)node->integer_value;
    return node->integer_value >= 0 &&
           (size_t)node->integer_value < union_type->child_count;
}

static bool emit_is_union(const TypedProgram *program, const TypedNode *node,
                          const TypedType *union_type, TextBuffer *buffer,
                          ErrorState *errors)
{
    size_t tag = 0;
    bool present;
    bool ok;

    if (!is_test_tag(program, node, union_type, &tag, &present)) {
        return emit_unsupported_expression(errors);
    }
    if (!present) {
        return buffer_append(buffer, "false");
    }
    buffer_append(buffer, "(");
    ok = emit_expression(program, node->left_child, buffer, errors);
    buffer_appendf(buffer, ".tag == %zu)", tag);
    return ok;
}

/* A type test on a non-union value is decided statically. */
static bool emit_is_static(const TypedProgram *program, const TypedNode *node,
                           const TypedNode *value, TextBuffer *buffer,
                           ErrorState *errors)
{
    bool result;

    if (node->auxiliary_type == TYPED_NO_NODE && node->integer_value < 0) {
        return emit_unsupported_expression(errors);
    }
    result = node->auxiliary_type != TYPED_NO_NODE
                 ? typed_types_equivalent(program, value->type_index,
                                          node->auxiliary_type, 16)
                 : node->integer_value != 0;
    return buffer_append(buffer, result ? "true" : "false");
}

static bool emit_is(const TypedProgram *program, const TypedNode *node,
                    TextBuffer *buffer, ErrorState *errors)
{
    const TypedNode *value = typed_program_node(program, node->left_child);
    const TypedType *value_type =
        value == NULL ? NULL : typed_program_type(program, value->type_index);

    if (value_type == NULL) {
        return emit_unsupported_expression(errors);
    }
    if (value_type->kind == NYX_TYPE_UNION) {
        return emit_is_union(program, node, value_type, buffer, errors);
    }
    return emit_is_static(program, node, value, buffer, errors);
}

static const struct {
    TypedNodeKind kind;
    LiteralEmitter emit;
} literal_emitters[] = {
    {TYPED_INTEGER_LITERAL, emit_integer_literal},
    {TYPED_FLOAT_LITERAL, emit_float_literal},
    {TYPED_BOOL_LITERAL, emit_bool_literal},
    {TYPED_IDENTIFIER, emit_identifier},
    {TYPED_NIL_LITERAL, emit_nil_literal},
    {TYPED_SUM_INJECT, emit_sum_inject},
    {TYPED_SUM_PAYLOAD, emit_sum_payload},
    {TYPED_IF_EXPRESSION, emit_if_expression},
    {TYPED_IS, emit_is},
};

bool emit_expr_literals(const TypedProgram *program, const TypedNode *node,
                        TextBuffer *buffer, ErrorState *errors, bool *handled)
{
    size_t i;

    for (i = 0; i < NYX_COUNT_OF(literal_emitters); i++) {
        if (literal_emitters[i].kind == node->kind) {
            *handled = true;
            return literal_emitters[i].emit(program, node, buffer, errors);
        }
    }
    return true;
}
