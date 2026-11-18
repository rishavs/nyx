#include "generator_internal.h"

typedef bool (*EmitExprCollectionsHandler)(const TypedProgram *program, const TypedNode *node, size_t node_index, TextBuffer *buffer, ErrorState *errors, bool *handled);

static bool emit_collection_list_literal(const TypedProgram *program, const TypedNode *node, size_t node_index, TextBuffer *buffer, ErrorState *errors, bool *handled)
{
    (void)node_index;
    *handled = true;
    char type_name[NYX_IDENTIFIER_MAX + 32];
    const TypedType *type = typed_program_type(program, node->type_index);

    if (type == NULL || type->kind != NYX_TYPE_FIXED_LIST ||
        !format_c_type(program, node->type_index, type_name,
                       sizeof(type_name), errors)) {
        error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_EXPRESSION);
        return false;
    }
    if (node->children_count == 0) {
        buffer_appendf(buffer, "(%s){ .length = 0 }", type_name);
        return true;
    }
    buffer_appendf(buffer, "(%s){ { ", type_name);
    if (!emit_children_expressions(program, node, buffer, errors)) {
        return false;
    }
    buffer_appendf(buffer, " }, %zu }", node->children_count);
    return true;
}

static bool emit_collection_list_len(const TypedProgram *program, const TypedNode *node, size_t node_index, TextBuffer *buffer, ErrorState *errors, bool *handled)
{
    (void)node_index;
    *handled = true;
    const TypedNode *receiver =
        typed_program_node(program, node->left_child);
    const TypedType *receiver_type =
        receiver == NULL
            ? NULL
            : typed_program_type(program, receiver->type_index);

    if (receiver_type == NULL) {
        error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_EXPRESSION);
        return false;
    }
    if (receiver_type->kind == NYX_TYPE_FIXED_LIST) {
        buffer_append(buffer, "((int32_t)(");
        if (!emit_expression(program, node->left_child, buffer, errors)) {
            return false;
        }
        buffer_append(buffer, ").length)");
        return true;
    }
    buffer_append(buffer, "nyx_list_len_at(");
    if (!emit_expression(program, node->left_child, buffer, errors)) {
        return false;
    }
    buffer_appendf(buffer, ", __FILE__, %zu, %zu, %zu)",
                    node->pos.line, node->pos.column, node->pos.offset);
    return true;
}

static bool emit_collection_list_get_optional(const TypedProgram *program, const TypedNode *node, size_t node_index, TextBuffer *buffer, ErrorState *errors, bool *handled)
{
    NyxType kind = typed_program_node_kind(program, node->left_child);
    char head[64];
    char tail[96] = "";

    *handled = true;
    if (kind == NYX_TYPE_KIND_COUNT) {
        error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_EXPRESSION);
        return false;
    }
    snprintf(head, sizeof(head),
             kind == NYX_TYPE_SLICE ? "nyx_slice_get_optional_%zu("
                                    : "nyx_list_get_optional_%zu(",
             node_index);
    if (kind == NYX_TYPE_LIST || kind == NYX_TYPE_SLICE) {
        position_text(node, tail, sizeof(tail));
    }
    strcat(tail, ")");
    return emit_operands(program, node, buffer, errors, head, ", ", tail);
}

/* An element read: an Array is read through its checked accessor, a List through
   its data pointer. */
static bool emit_collection_list_get(const TypedProgram *program, const TypedNode *node, size_t node_index, TextBuffer *buffer, ErrorState *errors, bool *handled)
{
    const TypedNode *receiver = typed_program_node(program, node->left_child);
    NyxType kind = typed_program_node_kind(program, node->left_child);
    char element_name[NYX_IDENTIFIER_MAX + 32];
    char head[NYX_IDENTIFIER_MAX + 64];
    char tail[NYX_IDENTIFIER_MAX + 128];

    (void)node_index;
    *handled = true;
    if (kind == NYX_TYPE_KIND_COUNT ||
        !format_c_type(program, node->type_index, element_name,
                       sizeof(element_name), errors)) {
        error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_EXPRESSION);
        return false;
    }
    if (kind == NYX_TYPE_FIXED_LIST) {
        snprintf(head, sizeof(head), "nyx_list_%zu_get(", receiver->type_index);
        position_text(node, tail, sizeof(tail));
        strcat(tail, ")");
    } else {
        snprintf(head, sizeof(head), "(*(%s *)nyx_list_data_at(", element_name);
        snprintf(tail, sizeof(tail), ", sizeof(%s), __FILE__, %zu, %zu, %zu))",
                 element_name, node->pos.line, node->pos.column,
                 node->pos.offset);
    }
    return emit_operands(program, node, buffer, errors, head, ", ", tail);
}

/* An Array element write used as an expression. A List write is always a
   statement, which emit_stmt_list_set expands inline. */
static bool emit_collection_list_set(const TypedProgram *program, const TypedNode *node, size_t node_index, TextBuffer *buffer, ErrorState *errors, bool *handled)
{
    const TypedNode *receiver = typed_program_node(program, node->left_child);
    const TypedType *receiver_type =
        receiver == NULL
            ? NULL
            : typed_program_type(program, receiver->type_index);
    char element_name[NYX_IDENTIFIER_MAX + 32];
    char name_text[USER_NAME_CAPACITY];
    const char *name = user_c_name(program, node->name_index, name_text,
                                   sizeof(name_text));
    size_t index;
    size_t value;

    (void)node_index;
    *handled = true;
    if (receiver_type == NULL || receiver_type->kind != NYX_TYPE_FIXED_LIST ||
        node->children_count != 2 || !valid_identifier(name) ||
        !format_c_type(program, node->auxiliary_type, element_name,
                       sizeof(element_name), errors)) {
        error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_EXPRESSION);
        return false;
    }
    index = *(const size_t *)list_get(&program->children, node->children_start);
    value = *(const size_t *)list_get(&program->children,
                                      node->children_start + 1);
    buffer_appendf(buffer, "(*nyx_list_%zu_at(&%s, ",
                    receiver->type_index, name);
    if (!emit_expression(program, index, buffer, errors)) {
        return false;
    }
    buffer_appendf(buffer, ", __FILE__, %zu, %zu, %zu) = ",
                    node->pos.line, node->pos.column, node->pos.offset);
    if (!emit_expression(program, value, buffer, errors)) {
        return false;
    }
    buffer_append(buffer, ")");
    return true;
}

static bool emit_collection_list_push(const TypedProgram *program, const TypedNode *node, size_t node_index, TextBuffer *buffer, ErrorState *errors, bool *handled)
{
    (void)node_index;
    *handled = true;
    const TypedNode *receiver =
        typed_program_node(program, node->left_child);
    const TypedType *receiver_type =
        receiver == NULL
            ? NULL
            : typed_program_type(program, receiver->type_index);
    const TypedType *element_type =
        typed_program_type(program, node->auxiliary_type);
    char name_text[USER_NAME_CAPACITY];
const char *name = user_c_name(program, node->name_index,
                              name_text, sizeof(name_text));
    char element_name[NYX_IDENTIFIER_MAX + 32];

    if (receiver_type == NULL || element_type == NULL ||
        !valid_identifier(name) ||
        !format_c_type(program, node->auxiliary_type, element_name,
                       sizeof(element_name), errors)) {
        error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_EXPRESSION);
        return false;
    }
    if (receiver_type->kind == NYX_TYPE_FIXED_LIST) {
        buffer_appendf(buffer, "nyx_list_%zu_push(&%s, ",
                        receiver->type_index, name);
        if (!emit_expression(program, node->right_child, buffer, errors)) {
            return false;
        }
        buffer_appendf(buffer, ", __FILE__, %zu, %zu, %zu)",
                        node->pos.line, node->pos.column,
                        node->pos.offset);
        return true;
    }
    buffer_appendf(buffer, "nyx_list_push(%s, (%s[]){ ", name,
                    element_name);
    if (!emit_expression(program, node->right_child, buffer, errors)) {
        return false;
    }
    buffer_appendf(buffer, " }, sizeof(%s), _Alignof(%s), __FILE__, %zu, %zu, %zu)",
                    element_name, element_name, node->pos.line,
                    node->pos.column, node->pos.offset);
    return true;
}

static bool emit_collection_list_pop_buffer_appendf(TextBuffer *buffer, size_t node_index, ErrorState *errors, const TypedType *receiver_type, const char *name, const TypedNode *node)
{
    buffer_appendf(buffer, "nyx_list_pop_%zu(", node_index);
    if (receiver_type->kind == NYX_TYPE_FIXED_LIST) {
        buffer_appendf(buffer, "&%s", name);
    } else if (!buffer_append(buffer, name)) {
        error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
        return false;
    }
    if ((receiver_type->kind == NYX_TYPE_LIST &&
     !buffer_appendf(buffer, ", __FILE__, %zu, %zu, %zu",
                     node->pos.line, node->pos.column, node->pos.offset))) {
        return false;
    }
    buffer_append(buffer, ")");
    return true;
}

static bool emit_collection_list_pop(const TypedProgram *program, const TypedNode *node, size_t node_index, TextBuffer *buffer, ErrorState *errors, bool *handled)
{
    *handled = true;
    const TypedNode *receiver =
        typed_program_node(program, node->left_child);
    const TypedType *receiver_type =
        receiver == NULL
            ? NULL
            : typed_program_type(program, receiver->type_index);
    char name_text[USER_NAME_CAPACITY];
const char *name = user_c_name(program, node->name_index,
                              name_text, sizeof(name_text));

    if (receiver_type == NULL || !valid_identifier(name)) {
        error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_EXPRESSION);
        return false;
    }
    if (!emit_collection_list_pop_buffer_appendf(buffer, node_index, errors, receiver_type, name, node)) {
        return false;
    }
    return true;
}

static bool emit_collection_list_create(const TypedProgram *program, const TypedNode *node, size_t node_index, TextBuffer *buffer, ErrorState *errors, bool *handled)
{
    (void)node_index;
    *handled = true;
    const TypedType *element_type =
        typed_program_type(program, node->auxiliary_type);
    char element_name[NYX_IDENTIFIER_MAX + 32];

    if (element_type == NULL ||
        !store_reference_is_valid(program, node) ||
        !format_c_type(program, node->auxiliary_type, element_name,
                       sizeof(element_name), errors)) {
        error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_EXPRESSION);
        return false;
    }
    buffer_append(buffer, "nyx_list_create(");
    if (!emit_store_reference(program, node, buffer, errors)) {
        return false;
    }
    buffer_append(buffer, ", ");
    if (!emit_expression(program, node->left_child, buffer, errors)) {
        return false;
    }
    buffer_appendf(buffer, ", sizeof(%s), _Alignof(%s), __FILE__, %zu, %zu, %zu)",
                    element_name, element_name, node->pos.line,
                    node->pos.column, node->pos.offset);
    return true;
}

static bool emit_collection_slice_create(const TypedProgram *program, const TypedNode *node, size_t node_index, TextBuffer *buffer, ErrorState *errors, bool *handled)
{
    (void)node_index;
    *handled = true;
    const TypedNode *receiver =
        typed_program_node(program, node->left_child);
    const TypedType *receiver_type =
        receiver == NULL
            ? NULL
            : typed_program_type(program, receiver->type_index);
    char element_name[NYX_IDENTIFIER_MAX + 32];
    size_t start;
    size_t length;

    if (receiver_type == NULL || node->children_count != 2 ||
        !format_c_type(program, node->auxiliary_type, element_name,
                       sizeof(element_name), errors)) {
        error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_EXPRESSION);
        return false;
    }
    start = *(const size_t *)list_get(&program->children,
                                      node->children_start);
    length = *(const size_t *)list_get(&program->children,
                                       node->children_start + 1);
    buffer_appendf(buffer, "nyx_slice_%s(",
                    receiver_type->kind == NYX_TYPE_SLICE
                        ? "sub"
                        : "from_list");
    if (!emit_expression(program, node->left_child, buffer, errors)) {
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
    buffer_appendf(buffer, ", sizeof(%s), __FILE__, %zu, %zu, %zu)",
                    element_name, node->pos.line, node->pos.column,
                    node->pos.offset);
    return true;
}

static bool emit_collection_slice_len(const TypedProgram *program, const TypedNode *node, size_t node_index, TextBuffer *buffer, ErrorState *errors, bool *handled)
{
    (void)node_index;
    *handled = true;
    buffer_append(buffer, "nyx_slice_len_at(");
    if (!emit_expression(program, node->left_child, buffer, errors)) {
        return false;
    }
    buffer_appendf(buffer, ", __FILE__, %zu, %zu, %zu)",
                    node->pos.line, node->pos.column, node->pos.offset);
    return true;
}

static bool emit_collection_slice_get(const TypedProgram *program, const TypedNode *node, size_t node_index, TextBuffer *buffer, ErrorState *errors, bool *handled)
{
    (void)node_index;
    *handled = true;
    char element_name[NYX_IDENTIFIER_MAX + 32];

    if (!format_c_type(program, node->type_index, element_name,
                       sizeof(element_name), errors)) {
        error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_EXPRESSION);
        return false;
    }
    buffer_appendf(buffer, "(*(%s *)nyx_slice_data_at(",
                    element_name);
    if (!emit_expression(program, node->left_child, buffer, errors)) {
        return false;
    }
    buffer_append(buffer, ", ");
    if (!emit_expression(program, node->right_child, buffer, errors)) {
        return false;
    }
    buffer_appendf(buffer, ", sizeof(%s), __FILE__, %zu, %zu, %zu))",
                    element_name, node->pos.line, node->pos.column,
                    node->pos.offset);
    return true;
}

/* A Record literal's C type, and the C types of its keys and values. */
static bool record_literal_types(const TypedProgram *program,
                                 const TypedNode *node, ErrorState *errors,
                                 char *record_name, char *key_name,
                                 char *value_name)
{
    const TypedType *type = typed_program_type(program, node->type_index);
    size_t key_type;
    size_t value_type;

    if (type == NULL || type->child_count != 2 ||
        !format_c_type(program, node->type_index, record_name, 64, errors)) {
        error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_EXPRESSION);
        return false;
    }
    key_type = *(const size_t *)list_get(&program->type_children,
                                         type->first_child);
    value_type = *(const size_t *)list_get(&program->type_children,
                                           type->first_child + 1);
    return format_c_type(program, key_type, key_name, 64, errors) &&
           format_c_type(program, value_type, value_name, 64, errors);
}

/* The children alternate key, value: the keys in one list, the values in
   another. */
static bool emit_record_parts(const TypedProgram *program, const TypedNode *node,
                              size_t first, TextBuffer *buffer,
                              ErrorState *errors)
{
    size_t i;

    for (i = first; i + 1 - first < node->children_count; i += 2) {
        if (i != first) {
            buffer_append(buffer, ", ");
        }
        if (!emit_expression(program,
                             *(const size_t *)list_get(&program->children,
                                                       node->children_start + i),
                             buffer, errors)) {
            return false;
        }
    }
    return true;
}

static bool emit_collection_record_literal(const TypedProgram *program, const TypedNode *node, size_t node_index, TextBuffer *buffer, ErrorState *errors, bool *handled)
{
    char record_name[64];
    char key_name[64];
    char value_name[64];
    bool ok;

    (void)node_index;
    *handled = true;
    if (!record_literal_types(program, node, errors, record_name, key_name,
                              value_name)) {
        return false;
    }
    if (node->children_count == 0) {
        return buffer_appendf(buffer, "(%s){0}", record_name);
    }
    buffer_appendf(buffer, "nyx_record_%zu_literal((%s[]){ ", node->type_index,
                   key_name);
    ok = emit_record_parts(program, node, 0, buffer, errors);
    buffer_appendf(buffer, " }, (%s[]){ ", value_name);
    ok = ok && emit_record_parts(program, node, 1, buffer, errors);
    buffer_appendf(buffer, " }, %zu, __FILE__, %zu, %zu, %zu)",
                   node->children_count / 2, node->pos.line, node->pos.column,
                   node->pos.offset);
    return ok;
}

static bool emit_collection_table_create(const TypedProgram *program, const TypedNode *node, size_t node_index, TextBuffer *buffer, ErrorState *errors, bool *handled)
{
    (void)node_index;
    *handled = true;
    buffer_appendf(buffer, "nyx_table_%zu_create(", node->type_index);
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

static bool emit_collection_table_get_optional(const TypedProgram *program, const TypedNode *node, size_t node_index, TextBuffer *buffer, ErrorState *errors, bool *handled)
{
    *handled = true;
    const TypedNode *receiver = typed_program_node(program,
                                                   node->left_child);

    if (receiver == NULL) {
        return false;
    }
    buffer_appendf(buffer, "nyx_table_%zu_get_optional_%zu(",
                    receiver->type_index, node_index);
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

static bool emit_collection_table_get(const TypedProgram *program, const TypedNode *node, size_t node_index, TextBuffer *buffer, ErrorState *errors, bool *handled)
{
    (void)node_index;
    *handled = true;
    const TypedNode *receiver = typed_program_node(program,
                                                   node->left_child);

    if (receiver == NULL) {
        return false;
    }
    buffer_appendf(buffer, "nyx_table_%zu_get_required(",
                    receiver->type_index);
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

/* has, remove and len on a Table. remove takes the Table by address, since it
   changes it. */
static bool emit_collection_table_has(const TypedProgram *program, const TypedNode *node, size_t node_index, TextBuffer *buffer, ErrorState *errors, bool *handled)
{
    const TypedNode *receiver = typed_program_node(program, node->left_child);
    char head[64];
    char tail[96];

    (void)node_index;
    *handled = true;
    if (receiver == NULL) {
        error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_EXPRESSION);
        return false;
    }
    position_text(node, tail, sizeof(tail));
    strcat(tail, ")");
    if (node->kind == TYPED_TABLE_LEN) {
        snprintf(head, sizeof(head), "nyx_table_%zu_len(", receiver->type_index);
        return emit_operand(program, node, buffer, errors, head, tail);
    }
    snprintf(head, sizeof(head), "nyx_table_%zu_%s(%s", receiver->type_index,
             node->kind == TYPED_TABLE_HAS ? "has" : "remove",
             node->kind == TYPED_TABLE_REMOVE ? "&(" : "");
    return emit_operands(program, node, buffer, errors, head,
                         node->kind == TYPED_TABLE_REMOVE ? "), " : ", ", tail);
}

static bool emit_collection_record_get(const TypedProgram *program, const TypedNode *node, size_t node_index, TextBuffer *buffer, ErrorState *errors, bool *handled)
{
    (void)node_index;
    *handled = true;
    const TypedType *receiver_type = NULL;
    const TypedNode *receiver = typed_program_node(program,
                                                   node->left_child);

    receiver_type = receiver == NULL
                        ? NULL
                        : typed_program_type(program,
                                             receiver->type_index);
    if (receiver_type == NULL ||
        receiver_type->kind != NYX_TYPE_RECORD) {
        error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_EXPRESSION);
        return false;
    }
    buffer_appendf(buffer, "nyx_record_%zu_get_required(&(",
                    receiver->type_index);
    if (!emit_expression(program, node->left_child, buffer, errors)) {
        return false;
    }
    buffer_append(buffer, "), ");
    if (!emit_expression(program, node->right_child, buffer, errors)) {
        return false;
    }
    buffer_appendf(buffer, ", __FILE__, %zu, %zu, %zu)",
                    node->pos.line, node->pos.column, node->pos.offset);
    return true;
}

static bool emit_collection_record_has(const TypedProgram *program, const TypedNode *node, size_t node_index, TextBuffer *buffer, ErrorState *errors, bool *handled)
{
    (void)node_index;
    *handled = true;
    const TypedNode *receiver = typed_program_node(program,
                                                   node->left_child);
    const char *suffix = node->kind == TYPED_RECORD_HAS
                             ? "has"
                             : (node->kind == TYPED_RECORD_REMOVE
                                    ? "remove"
                                    : "len");
    const char *prefix = node->kind == TYPED_RECORD_LEN
                             ? "nyx_record_%zu_len(&("
                             : "nyx_record_%zu_%s(&(";

    if (receiver == NULL) {
        error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_EXPRESSION);
        return false;
    }
    if (node->kind == TYPED_RECORD_LEN) {
        buffer_appendf(buffer, prefix, receiver->type_index);
        if (!emit_expression(program, node->left_child, buffer, errors)) {
            return false;
        }
        buffer_append(buffer, "))");
    } else {
        buffer_appendf(buffer, prefix, receiver->type_index,
                        suffix);
        if (!emit_expression(program, node->left_child, buffer, errors)) {
            return false;
        }
        buffer_append(buffer, "), ");
        if (!emit_expression(program, node->right_child, buffer, errors)) {
            return false;
        }
        buffer_append(buffer, ")");
    }
    return true;
}

static const struct {
    int key;
    EmitExprCollectionsHandler handler;
} emit_expr_collections_handlers[] = {
    {TYPED_LIST_LITERAL, emit_collection_list_literal},
    {TYPED_LIST_LEN, emit_collection_list_len},
    {TYPED_LIST_GET_OPTIONAL, emit_collection_list_get_optional},
    {TYPED_LIST_GET, emit_collection_list_get},
    {TYPED_LIST_SET, emit_collection_list_set},
    {TYPED_LIST_PUSH, emit_collection_list_push},
    {TYPED_LIST_POP, emit_collection_list_pop},
    {TYPED_LIST_CREATE, emit_collection_list_create},
    {TYPED_SLICE_CREATE, emit_collection_slice_create},
    {TYPED_SLICE_LEN, emit_collection_slice_len},
    {TYPED_SLICE_GET, emit_collection_slice_get},
    {TYPED_RECORD_LITERAL, emit_collection_record_literal},
    {TYPED_TABLE_CREATE, emit_collection_table_create},
    {TYPED_TABLE_GET_OPTIONAL, emit_collection_table_get_optional},
    {TYPED_TABLE_GET, emit_collection_table_get},
    {TYPED_TABLE_HAS, emit_collection_table_has},
    {TYPED_TABLE_REMOVE, emit_collection_table_has},
    {TYPED_TABLE_LEN, emit_collection_table_has},
    {TYPED_RECORD_GET, emit_collection_record_get},
    {TYPED_RECORD_HAS, emit_collection_record_has},
    {TYPED_RECORD_REMOVE, emit_collection_record_has},
    {TYPED_RECORD_LEN, emit_collection_record_has},
};

bool emit_expr_collections(const TypedProgram *program, const TypedNode *node,
                size_t node_index,
                TextBuffer *buffer, ErrorState *errors, bool *handled)
{
    for (size_t i = 0; i < NYX_COUNT_OF(emit_expr_collections_handlers); i++) {
        if (emit_expr_collections_handlers[i].key == (int)node->kind) {
            return emit_expr_collections_handlers[i].handler(program, node, node_index, buffer, errors, handled);
        }
    }
    return true;
}
