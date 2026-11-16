#include "generator_internal.h"

/* How many loops enclose the statement being emitted that open scopes. Scopes
   inside a loop recycle their buffer; see emit_statement. */
static _Thread_local unsigned scope_loop_depth;


static bool emit_loop_scope_mark(const TypedProgram *program, TextBuffer *source,
                                 ErrorState *errors, size_t depth);

static bool emit_inline_failure(TextBuffer *source, SourcePos pos,
                                const char *message)
{
    return buffer_append(source,
                         "fflush(stdout); fprintf(stderr, \""
                         MESSAGE_RUNTIME_POSITION "\\n\", __FILE__, ") &&
           buffer_appendf(source, "(size_t)%zu, (size_t)%zu, (size_t)%zu, \"%s\" + sizeof(\"" MESSAGE_RUNTIME_PREFIX "\") - 1); exit(1);",
                          pos.line, pos.column, pos.offset, message);
}


static bool emit_map_for_iterable(const TypedProgram *program, const TypedNode *node, TextBuffer *source, size_t depth, size_t offset, ErrorState *errors)
{
    const TypedNode *iterable = typed_program_node(program,
                                                   node->left_child);
    size_t table_type = iterable == NULL ? TYPED_NO_NODE
                                         : iterable->type_index;

    if (table_type == TYPED_NO_NODE ||
        !append_indent(source, depth) ||
        !buffer_append(source, "{\n") ||
        !append_indent(source, depth + 1) ||
        !buffer_appendf(source, "nyx_table nyx_for_src_%zu = ",
                        offset) ||
        !emit_expression(program, node->left_child, source, errors) ||
        !buffer_append(source, ";\n") ||
        !append_indent(source, depth + 1) ||
        !buffer_appendf(source,
                        "for (size_t nyx_for_i_%zu = 0;; "
                        "nyx_for_i_%zu++) {\n",
                        offset, offset) ||
        !append_indent(source, depth + 2) ||
        !buffer_appendf(source,
                        "nyx_table_state_%zu *nyx_for_state_%zu = "
                        "nyx_table_%zu_state_at(nyx_for_src_%zu, __FILE__, %zu, %zu, %zu);\n",
                        table_type, offset, table_type, offset,
                        node->pos.line, node->pos.column, node->pos.offset) ||
        !append_indent(source, depth + 2) ||
        !buffer_appendf(source,
                        "if (nyx_for_i_%zu >= "
                        "nyx_for_state_%zu->buckets) break;\n",
                        offset, offset)) {
        error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
        return false;
    }
    return true;
}

static bool emit_map_for_append_indent(TextBuffer *source, size_t depth, char (*key_c)[64], size_t offset, const char *key_name, ErrorState *errors, const char *value_name, char (*value_c)[64])
{
    if (!append_indent(source, depth + 2) ||
        !buffer_appendf(source,
                        "%s *nyx_for_keys_%zu = (%s *)(void *)"
                        "(nyx_for_store_%zu->data + "
                        "nyx_for_state_%zu->keys_offset);\n",
                        (*key_c), offset, (*key_c), offset, offset) ||
        !append_indent(source, depth + 2) ||
        !buffer_appendf(source, "%s %s = nyx_for_keys_%zu["
                                "nyx_for_i_%zu];\n",
                        (*key_c), key_name, offset, offset) ||
        !append_indent(source, depth + 2) ||
        !buffer_appendf(source, "(void)%s;\n", key_name)) {
        error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
        return false;
    }
    if (value_name != NULL) {
        if (!append_indent(source, depth + 2) ||
            !buffer_appendf(source,
                            "%s *nyx_for_values_%zu = (%s *)(void *)"
                            "(nyx_for_store_%zu->data + "
                            "nyx_for_state_%zu->values_offset);\n",
                            (*value_c), offset, (*value_c), offset, offset) ||
            !append_indent(source, depth + 2) ||
            !buffer_appendf(source, "%s %s = nyx_for_values_%zu["
                                    "nyx_for_i_%zu];\n",
                            (*value_c), value_name, offset, offset) ||
            !append_indent(source, depth + 2) ||
            !buffer_appendf(source, "(void)%s;\n", value_name)) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            return false;
        }
    }
    return true;
}

static bool emit_map_for_child(const TypedNode *node, const TypedProgram *program, ErrorState *errors, TextBuffer *source, size_t offset, size_t depth, size_t buckets, char (*key_c)[64], const char *key_name)
{
    {
        size_t child = node->left_child;
        const TypedNode *iter = typed_program_node(program, child);
        char record_name[64];

        if (iter == NULL ||
            !format_c_type(program, iter->type_index, record_name,
                           sizeof(record_name), errors) ||
            !buffer_appendf(source, "%s nyx_for_src_%zu = ",
                            record_name, offset) ||
            !emit_expression(program, child, source, errors) ||
            !buffer_append(source, ";\n")) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            return false;
        }
    }
    if (!append_indent(source, depth + 1) ||
        !buffer_appendf(source,
                        "for (size_t nyx_for_i_%zu = 0; "
                        "nyx_for_i_%zu < %zu; nyx_for_i_%zu++) {\n",
                        offset, offset, buckets, offset) ||
        !append_indent(source, depth + 2) ||
        !buffer_appendf(source,
                        "if (nyx_for_src_%zu.states[nyx_for_i_%zu] "
                        "!= 1) continue;\n",
                        offset, offset) ||
        !append_indent(source, depth + 2) ||
        !buffer_appendf(source, "%s %s = nyx_for_src_%zu.keys["
                                "nyx_for_i_%zu];\n",
                        (*key_c), key_name, offset, offset) ||
        !append_indent(source, depth + 2) ||
        !buffer_appendf(source, "(void)%s;\n", key_name)) {
        error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
        return false;
    }
    return true;
}

static bool emit_map_for_buckets(const TypedType *iterable_type, TextBuffer *source, size_t depth, ErrorState *errors, const TypedNode *node, const TypedProgram *program, size_t offset, char (*key_c)[64], const char *key_name, const char *value_name, char (*value_c)[64])
{
    size_t buckets = iterable_type->fixed_length == 0
                         ? 1
                         : iterable_type->fixed_length + 1;

    if (!append_indent(source, depth) ||
        !buffer_append(source, "{\n") ||
        !append_indent(source, depth + 1)) {
        error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
        return false;
    }
    if (!emit_map_for_child(node, program, errors, source, offset, depth, buckets, key_c, key_name)) {
    return false;
}
    if (value_name != NULL) {
        if (!append_indent(source, depth + 2) ||
            !buffer_appendf(source, "%s %s = nyx_for_src_%zu.values["
                                    "nyx_for_i_%zu];\n",
                            (*value_c), value_name, offset, offset) ||
            !append_indent(source, depth + 2) ||
            !buffer_appendf(source, "(void)%s;\n", value_name)) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            return false;
        }
    }
    return true;
}

static bool emit_map_for_key(const TypedProgram *program, const TypedType *iterable_type, const TypedNode *node, const char *key_name, const char *value_name, ErrorState *errors, TextBuffer *source, size_t depth)
{
    size_t key_type = *(const size_t *)list_get(&program->type_children,
                                                iterable_type->first_child);
    size_t value_type = *(const size_t *)list_get(
        &program->type_children, iterable_type->first_child + 1);
    char key_c[NIPPY_IDENTIFIER_MAX + 32];
    char value_c[NIPPY_IDENTIFIER_MAX + 32];
    size_t offset = node->pos.offset;

    if (key_name == NULL || !valid_identifier(key_name) ||
        (value_name != NULL && !valid_identifier(value_name)) ||
        !format_c_type(program, key_type, key_c, sizeof(key_c), errors) ||
        !format_c_type(program, value_type, value_c, sizeof(value_c), errors)) {
        error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_STATEMENT);
        return false;
    }
    if (iterable_type->kind == NIPPY_TYPE_RECORD) {
        if (!emit_map_for_buckets(iterable_type, source, depth, errors, node, program, offset, &key_c, key_name, value_name, &value_c)) {
        return false;
    }
    } else {
        if (!emit_map_for_iterable(program, node, source, depth, offset, errors)) {
        return false;
    }
        if (!append_indent(source, depth + 2) ||
            !buffer_appendf(source,
                            "nyx_store *nyx_for_store_%zu = "
                            "nyx_for_src_%zu.store;\n",
                            offset, offset) ||
            !append_indent(source, depth + 2) ||
            !buffer_appendf(source,
                            "uint8_t *nyx_for_states_%zu = (uint8_t *)"
                            "(void *)(nyx_for_store_%zu->data + "
                            "nyx_for_state_%zu->states_offset);\n"
                            "        if (nyx_for_states_%zu[nyx_for_i_%zu]"
                            " != 1) continue;\n",
                            offset, offset, offset, offset, offset)) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            return false;
        }
        if (!emit_map_for_append_indent(source, depth, &key_c, offset, key_name, errors, value_name, &value_c)) {
        return false;
    }
    }
    return true;
}

bool emit_map_for(const TypedProgram *program, const TypedNode *node,
                          const TypedType *iterable_type, size_t depth,
                          const char *effective_source, TextBuffer *source,
                          ErrorState *errors)
{
    char key_name_text[USER_NAME_CAPACITY];
    const char *key_name = user_c_name(program, node->name_index,
                                  key_name_text, sizeof(key_name_text));
    char value_name_text[USER_NAME_CAPACITY];
    const char *value_name =
        node->second_name_index == TYPED_NO_NODE
            ? NULL
            : user_c_name(program, node->second_name_index, value_name_text,
                          sizeof(value_name_text));
    if (!emit_map_for_key(program, iterable_type, node, key_name, value_name, errors, source, depth)) {
        return false;
    }
    if (!emit_loop_scope_mark(program, source, errors, depth + 2) ||
        !emit_block(program, node->right_child, depth + 2, effective_source,
                    source, errors) ||
        !append_indent(source, depth + 1) ||
        !buffer_append(source, "}\n") ||
        !append_indent(source, depth) ||
        !buffer_append(source, "}\n")) {
        error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
        return false;
    }
    return true;
}


/* Scope frames live for the whole activation, not the C block of their scope,
   so a header never dangles and a recycled buffer can outlive one iteration. */
static bool emit_scope_frames_at(const TypedProgram *program, size_t index,
                                 TextBuffer *source, ErrorState *errors,
                                 size_t *budget)
{
    const TypedNode *node = typed_program_node(program, index);
    size_t i;

    /* A tree visits each node once, so the node count bounds the walk and a
       malformed, cyclic program cannot make it run forever. */
    if (node == NULL || node->kind == TYPED_FUNCTION_DECLARATION ||
        *budget == 0) {
        return true;
    }
    (*budget)--;
    if (node->kind == TYPED_SCOPE &&
        !buffer_appendf(source,
                        "    nyx_scope_frame nyx_scope_frame_%zu = {0}; "
                        "(void)nyx_scope_frame_%zu;\n", index, index)) {
        error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
        return false;
    }
    if (node->left_child != TYPED_NO_NODE &&
        !emit_scope_frames_at(program, node->left_child, source, errors,
                              budget)) {
        return false;
    }
    if (node->right_child != TYPED_NO_NODE &&
        !emit_scope_frames_at(program, node->right_child, source, errors,
                              budget)) {
        return false;
    }
    for (i = 0; i < node->children_count; i++) {
        if (!emit_scope_frames_at(
                program,
                *(const size_t *)list_get(&program->children,
                                          node->children_start + i),
                source, errors, budget)) {
            return false;
        }
    }
    return true;
}

bool emit_scope_frames(const TypedProgram *program, size_t index,
                       TextBuffer *source, ErrorState *errors)
{
    size_t budget = program->nodes.length;

    return emit_scope_frames_at(program, index, source, errors, &budget);
}

bool emit_scope_frame_declaration(const TypedProgram *program, size_t subtree,
                                  TextBuffer *source, ErrorState *errors)
{
    if (!program_uses_scope(program)) return true;
    if (!buffer_append(source,
                       "    nyx_scope_frame *nyx_scope_top = NULL; (void)nyx_scope_top;\n"
                       "    nyx_scope_frame *nyx_cache_top = NULL; (void)nyx_cache_top;\n")) {
        error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
        return false;
    }
    return subtree == TYPED_NO_NODE ||
           emit_scope_frames(program, subtree, source, errors);
}

static bool emit_loop_scope_mark(const TypedProgram *program, TextBuffer *source,
                                 ErrorState *errors, size_t depth)
{
    if (!program_uses_scope(program)) return true;
    if (!append_indent(source, depth) ||
        !buffer_append(source, "nyx_scope_frame *nyx_loop_mark = nyx_scope_top; (void)nyx_loop_mark;\n")) {
        error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
        return false;
    }
    return true;
}

bool emit_block(const TypedProgram *program, size_t block_index,
                       size_t depth, const char *effective_source,
                       TextBuffer *source, ErrorState *errors)
{
    const TypedNode *block = typed_program_node(program, block_index);
    size_t i;

    if (block == NULL || block->kind != TYPED_BLOCK) {
        error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_STATEMENT);
        return false;
    }
    for (i = 0; i < block->children_count; i++) {
        size_t child = *(const size_t *)list_get(&program->children,
                                                 block->children_start + i);

        if (!emit_statement(program, child, depth, effective_source, source,
                            errors)) {
            return false;
        }
    }
    return true;
}


static bool subtree_has_scope_at(const TypedProgram *program, size_t index,
                                 size_t *budget)
{
    const TypedNode *node = typed_program_node(program, index);
    size_t i;

    if (node == NULL || node->kind == TYPED_FUNCTION_DECLARATION ||
        *budget == 0) {
        return false;
    }
    (*budget)--;
    if (node->kind == TYPED_SCOPE) {
        return true;
    }
    if (node->left_child != TYPED_NO_NODE &&
        subtree_has_scope_at(program, node->left_child, budget)) {
        return true;
    }
    if (node->right_child != TYPED_NO_NODE &&
        subtree_has_scope_at(program, node->right_child, budget)) {
        return true;
    }
    for (i = 0; i < node->children_count; i++) {
        if (subtree_has_scope_at(
                program,
                *(const size_t *)list_get(&program->children,
                                          node->children_start + i),
                budget)) {
            return true;
        }
    }
    return false;
}

static bool subtree_has_scope(const TypedProgram *program, size_t index)
{
    size_t budget = program->nodes.length;

    return subtree_has_scope_at(program, index, &budget);
}

static bool emit_statement_body(const TypedProgram *program, size_t node_index,
                                size_t depth, const char *effective_source,
                                TextBuffer *source, ErrorState *errors);

/* A loop whose body opens scopes reuses each scope's buffer across iterations.
   The cached buffers are released when the loop ends, or by a return. */
bool emit_statement(const TypedProgram *program, size_t node_index,
                    size_t depth, const char *effective_source,
                    TextBuffer *source, ErrorState *errors)
{
    const TypedNode *node = typed_program_node(program, node_index);
    bool ok;

    if (node == NULL || (node->kind != TYPED_WHILE && node->kind != TYPED_FOR) ||
        !subtree_has_scope(program, node_index)) {
        return emit_statement_body(program, node_index, depth, effective_source,
                                   source, errors);
    }
    if (!append_indent(source, depth) ||
        !buffer_appendf(source,
                        "{ nyx_scope_frame *nyx_cache_mark_%zu = nyx_cache_top;\n",
                        node_index)) {
        error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
        return false;
    }
    scope_loop_depth++;
    ok = emit_statement_body(program, node_index, depth + 1, effective_source,
                             source, errors);
    scope_loop_depth--;
    if (!ok) {
        return false;
    }
    if (!append_indent(source, depth + 1) ||
        !buffer_appendf(source,
                        "nyx_scope_release(&nyx_cache_top, nyx_cache_mark_%zu); }\n",
                        node_index)) {
        error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
        return false;
    }
    return true;
}

typedef bool (*EmitStatementBodyHandler)(const TypedProgram *program, size_t node_index, size_t depth, const char *effective_source, TextBuffer *source, ErrorState *errors);

static bool emit_stmt_match_bind_statement(const TypedProgram *program, size_t node_index, size_t depth, const char *effective_source, TextBuffer *source, ErrorState *errors)
{
    const TypedNode *node = typed_program_node(program, node_index);
    (void)node_index;
    if (!append_indent(source, depth) ||
        !buffer_append(source, "{\n") ||
        !append_indent(source, depth + 1) ||
        !buffer_appendf(source, "nyx_match_%zu = ",
                        (size_t)node->integer_value_u) ||
        !emit_expression(program, node->left_child, source, errors) ||
        !buffer_append(source, ";\n") ||
        !append_indent(source, depth + 1) ||
        !buffer_appendf(source, "(void)nyx_match_%zu;\n",
                        (size_t)node->integer_value_u) ||
        !emit_statement(program, node->right_child, depth + 1,
                        effective_source, source, errors) ||
        !append_indent(source, depth) || !buffer_append(source, "}\n")) {
        if (!errors->reported) error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
        return false;
    }
    return true;
}

static bool emit_stmt_scope(const TypedProgram *program, size_t node_index, size_t depth, const char *effective_source, TextBuffer *source, ErrorState *errors)
{
    const TypedNode *node = typed_program_node(program, node_index);
    char store_name[USER_NAME_CAPACITY];
    const char *name = user_c_name(program, node->name_index, store_name, sizeof(store_name));
    if (!valid_identifier(name) || !append_indent(source, depth) ||
        !buffer_append(source, "{\n") || !append_indent(source, depth + 1) ||
        !buffer_appendf(source, "int32_t nyx_scope_capacity_%zu = ", node_index) ||
        !emit_expression(program, node->left_child, source, errors) ||
        !buffer_append(source, ";\n") || !append_indent(source, depth + 1) ||
        !append_indent(source, depth + 1) ||
        !buffer_appendf(source,
            scope_loop_depth > 0
                ? "nyx_scope_enter_cached(&nyx_scope_top, &nyx_cache_top, &nyx_scope_frame_%zu, nyx_scope_capacity_%zu, __FILE__, %zu, %zu, %zu);\n"
                : "nyx_scope_enter(&nyx_scope_top, &nyx_scope_frame_%zu, nyx_scope_capacity_%zu, __FILE__, %zu, %zu, %zu);\n",
            node_index, node_index, node->pos.line, node->pos.column, node->pos.offset) ||
        !append_indent(source, depth + 1) ||
        !buffer_appendf(source, "nyx_store *%s = &nyx_scope_frame_%zu.store; (void)%s;\n",
                        name, node_index, name) ||
        !emit_block(program, node->right_child, depth + 1, effective_source, source, errors) ||
        !append_indent(source, depth + 1) ||
        !buffer_appendf(source, "nyx_scope_cleanup(&nyx_scope_top, nyx_scope_frame_%zu.previous);\n", node_index) ||
        !append_indent(source, depth) || !buffer_append(source, "}\n")) {
        if (!errors->reported) error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
        return false;
    }
    return true;
}

static const struct {
    int key;
    EmitStatementBodyHandler handler;
} emit_statement_body_handlers1[] = {
    {TYPED_MATCH_BIND_STATEMENT, emit_stmt_match_bind_statement},
    {TYPED_SCOPE, emit_stmt_scope},
};

static bool emit_stmt_return(const TypedProgram *program, size_t node_index, size_t depth, const char *effective_source, TextBuffer *source, ErrorState *errors)
{
    const TypedNode *node = typed_program_node(program, node_index);
    (void)node_index;
    (void)effective_source;
    if (!append_indent(source, depth) ||
        !buffer_append(source, "return ") ||
        !emit_expression(program, node->left_child, source, errors) ||
        !buffer_append(source, ";\n")) {
        if (!errors->reported) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
        }
        return false;
    }
    return true;
}

static bool emit_stmt_break(const TypedProgram *program, size_t node_index, size_t depth, const char *effective_source, TextBuffer *source, ErrorState *errors)
{
    const TypedNode *node = typed_program_node(program, node_index);
    (void)node_index;
    (void)effective_source;
    if (program_uses_scope(program) &&
        (!append_indent(source, depth) ||
         !buffer_append(source, "nyx_scope_cleanup(&nyx_scope_top, nyx_loop_mark);\n"))) {
        error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
        return false;
    }
    if (!append_indent(source, depth) ||
        !buffer_append(source,
                       node->kind == TYPED_BREAK ? "break;\n"
                                                 : "continue;\n")) {
        error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
        return false;
    }
    return true;
}

static bool emit_stmt_if_node(const TypedProgram *program, size_t node_index, ErrorState *errors, TextBuffer *source, size_t depth, const char *effective_source, size_t *else_block)
{
    const TypedNode *node = typed_program_node(program, node_index);
    (void)node_index;
    size_t then_block;
    (*else_block) = TYPED_NO_NODE;

    if (node->children_count == 0) {
        error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_STATEMENT);
        return false;
    }
    then_block = *(const size_t *)list_get(&program->children,
                                           node->children_start);
    if (node->children_count > 1) {
        (*else_block) = *(const size_t *)list_get(&program->children,
                                               node->children_start + 1);
    }
    if (!append_indent(source, depth) ||
        !buffer_append(source, "if (") ||
        !emit_expression(program, node->left_child, source, errors) ||
        !buffer_append(source, ") {\n") ||
        !emit_block(program, then_block, depth + 1, effective_source,
                    source, errors)) {
        if (!errors->reported) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
        }
        return false;
    }
    return true;
}

static bool emit_stmt_if_else_block(const TypedProgram *program, size_t node_index, ErrorState *errors, TextBuffer *source, size_t depth, const char *effective_source)
{
    size_t else_block = {0};
    if (!emit_stmt_if_node(program, node_index, errors, source, depth, effective_source, &else_block)) {
        return false;
    }
    if (else_block != TYPED_NO_NODE) {
        if (!append_indent(source, depth) ||
            !buffer_append(source, "} else {\n") ||
            !emit_block(program, else_block, depth + 1, effective_source,
                        source, errors)) {
            if (!errors->reported) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            }
            return false;
        }
    }
    if (!append_indent(source, depth) || !buffer_append(source, "}\n")) {
        error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
        return false;
    }
    return true;
}

static bool emit_stmt_if(const TypedProgram *program, size_t node_index, size_t depth, const char *effective_source, TextBuffer *source, ErrorState *errors)
{
    if (!emit_stmt_if_else_block(program, node_index, errors, source, depth, effective_source)) {
        return false;
    }
    return true;
}

static bool emit_stmt_while(const TypedProgram *program, size_t node_index, size_t depth, const char *effective_source, TextBuffer *source, ErrorState *errors)
{
    const TypedNode *node = typed_program_node(program, node_index);
    (void)node_index;
    if (!append_indent(source, depth) ||
        !buffer_append(source, "while (") ||
        !emit_expression(program, node->left_child, source, errors) ||
        !buffer_append(source, ") {\n") ||
        !emit_loop_scope_mark(program, source, errors, depth + 1) ||
        !emit_block(program, node->right_child, depth + 1,
                    effective_source, source, errors) ||
        !append_indent(source, depth) ||
        !buffer_append(source, "}\n")) {
        if (!errors->reported) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
        }
        return false;
    }
    return true;
}

static bool emit_stmt_for_buffer_appendf_buffer_append(TextBuffer *source, size_t depth, size_t offset, ErrorState *errors)
{
    if (!buffer_append(source, ");\n") ||
        !append_indent(source, depth + 1) ||
        !buffer_appendf(source,
                        "for (size_t nyx_for_i_%zu = 0; "
                        "nyx_for_i_%zu < nyx_for_len_%zu; "
                        "nyx_for_i_%zu++) {\n",
                        offset, offset, offset, offset) ||
        !append_indent(source, depth + 2)) {
        if (!errors->reported) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
        }
        return false;
    }
    return true;
}

static bool emit_stmt_for_buffer_appendf(const TypedType *iterable_type, TextBuffer *source, size_t offset, const TypedNode *node, ErrorState *errors, size_t depth)
{
    if (iterable_type->kind == NIPPY_TYPE_LIST) {
        if (!buffer_appendf(source,
                            "nyx_list_len_at(nyx_for_src_%zu, __FILE__, %zu, %zu, %zu)",
                            offset, node->pos.line, node->pos.column,
                            node->pos.offset)) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            return false;
        }
    } else if (iterable_type->kind == NIPPY_TYPE_FIXED_LIST ||
               iterable_type->kind == NIPPY_TYPE_STRING) {
        if (!buffer_appendf(source,
                            "((int32_t)(nyx_for_src_%zu).length)",
                            offset)) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            return false;
        }
    } else if (iterable_type->kind == NIPPY_TYPE_SLICE) {
        if (!buffer_appendf(source,
                            "nyx_slice_len_at(nyx_for_src_%zu, __FILE__, %zu, %zu, %zu)",
                            offset, node->pos.line, node->pos.column,
                            node->pos.offset)) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            return false;
        }
    } else if (iterable_type->kind == NIPPY_TYPE_DYNAMIC_STRING) {
        if (!buffer_appendf(source,
                            "nyx_string_len_at(nyx_for_src_%zu, __FILE__, %zu, %zu, %zu)",
                            offset, node->pos.line, node->pos.column,
                            node->pos.offset)) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            return false;
        }
    } else {
        error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_STATEMENT);
        return false;
    }
    if (!emit_stmt_for_buffer_appendf_buffer_append(source, depth, offset, errors)) {
        return false;
    }
    return true;
}

static bool emit_stmt_for_buffer_appendf4_buffer_append(TextBuffer *source, size_t depth, const char *value_c, ErrorState *errors)
{
    if (!buffer_append(source, ";\n") ||
        !append_indent(source, depth + 2) ||
        !buffer_appendf(source, "(void)%s;\n", value_c)) {
        error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
        return false;
    }
    return true;
}

static bool emit_stmt_for_buffer_appendf4(TextBuffer *source, char (*element_name)[64], const char *value_c, ErrorState *errors, const TypedType *iterable_type, const TypedNode *iterable_node, size_t offset, const TypedNode *node, size_t depth)
{
    if (!buffer_appendf(source, "%s %s = ", (*element_name), value_c)) {
        error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
        return false;
    }
    if (iterable_type->kind == NIPPY_TYPE_FIXED_LIST) {
        if (!buffer_appendf(source, "nyx_list_%zu_get(nyx_for_src_%zu, "
                                    "nyx_for_i_%zu, __FILE__, %zu, %zu, %zu)",
                            iterable_node->type_index, offset, offset,
                            node->pos.line, node->pos.column,
                            node->pos.offset)) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            return false;
        }
    } else if (iterable_type->kind == NIPPY_TYPE_LIST) {
        if (!buffer_appendf(source,
                            "(*(%s *)nyx_list_data_at(nyx_for_src_%zu, "
                            "nyx_for_i_%zu, sizeof(%s), __FILE__, %zu, %zu, %zu))",
                            (*element_name), offset, offset, (*element_name),
                            node->pos.line, node->pos.column,
                            node->pos.offset)) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            return false;
        }
    } else if (iterable_type->kind == NIPPY_TYPE_SLICE) {
        if (!buffer_appendf(source,
                            "(*(%s *)nyx_slice_data_at(nyx_for_src_%zu, "
                            "nyx_for_i_%zu, sizeof(%s), __FILE__, %zu, %zu, %zu))",
                            (*element_name), offset, offset, (*element_name),
                            node->pos.line, node->pos.column,
                            node->pos.offset)) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            return false;
        }
    } else if (iterable_type->kind == NIPPY_TYPE_STRING) {
        if (!buffer_appendf(source, "nyx_string_%zu_at(nyx_for_src_%zu, "
                                    "nyx_for_i_%zu, __FILE__, %zu, %zu, %zu)",
                            iterable_type->fixed_length, offset, offset,
                            node->pos.line, node->pos.column,
                            node->pos.offset)) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            return false;
        }
    } else if (iterable_type->kind == NIPPY_TYPE_DYNAMIC_STRING) {
        if (!buffer_appendf(source, "nyx_string_byte_at(nyx_for_src_%zu, "
                                    "nyx_for_i_%zu, __FILE__, %zu, %zu, %zu)",
                            offset, offset, node->pos.line, node->pos.column,
                            node->pos.offset)) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            return false;
        }
    } else {
        error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_STATEMENT);
        return false;
    }
    if (!emit_stmt_for_buffer_appendf4_buffer_append(source, depth, value_c, errors)) {
        return false;
    }
    return true;
}

static bool emit_stmt_for_append_indent(bool check_length, TextBuffer *source, size_t depth, ErrorState *errors, const TypedType *iterable_type, size_t offset, const TypedNode *node)
{
    if (check_length) {
        if (!append_indent(source, depth + 2)) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            return false;
        }
        if (iterable_type->kind == NIPPY_TYPE_LIST) {
            if (!buffer_appendf(source,
                                "if ((size_t)nyx_list_len_at("
                                "nyx_for_src_%zu, __FILE__, %zu, %zu, %zu) != "
                                "nyx_for_len_%zu) { ",
                                offset, node->pos.line, node->pos.column,
                                node->pos.offset, offset) ||
                !emit_inline_failure(source, node->pos, MESSAGE_RUNTIME_FOR_MUTATED) ||
                !buffer_append(source, " }\n")) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
                return false;
            }
        } else if (!buffer_appendf(source,
                                   "if ((size_t)(nyx_for_src_%zu).length "
                                   "!= nyx_for_len_%zu) { ",
                                   offset, offset) ||
                   !emit_inline_failure(source, node->pos, MESSAGE_RUNTIME_FOR_MUTATED) ||
                   !buffer_append(source, " }\n")) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            return false;
        }
    }
    if (!append_indent(source, depth + 1) ||
        !buffer_append(source, "}\n") ||
        !append_indent(source, depth) ||
        !buffer_append(source, "}\n")) {
        error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
        return false;
    }
    return true;
}

static bool emit_stmt_for_buffer_appendf7(bool is_signed, TextBuffer *source, size_t offset, char (*element_name)[64], const char *index_c, size_t depth, const TypedProgram *program, ErrorState *errors, const TypedNode *node, const char *effective_source)
{
    if (is_signed) {
        if (!buffer_appendf(source,
            "else nyx_cur_%zu = (%s)((int64_t)nyx_cur_%zu + nyx_range_%zu.step);\n",
            offset, (*element_name), offset, offset)) return true;
    } else if (!buffer_appendf(source,
            "else nyx_cur_%zu = (%s)(nyx_range_%zu.step > 0 ? "
            "(uint64_t)nyx_cur_%zu + nyx_mag_%zu : "
            "(uint64_t)nyx_cur_%zu - nyx_mag_%zu);\n",
            offset, (*element_name), offset, offset, offset, offset, offset))
        return true;
    if (index_c != NULL &&
        (!append_indent(source, depth + 2) ||
         !buffer_appendf(source, "int32_t %s = (int32_t)nyx_idx_%zu; (void)%s;\n",
                         index_c, offset, index_c))) return true;
    if (!emit_loop_scope_mark(program, source, errors, depth + 2) ||
!emit_block(program, node->right_child, depth + 2,
                    effective_source, source, errors) ||
        !append_indent(source, depth + 1) || !buffer_append(source, "}\n") ||
        !append_indent(source, depth) || !buffer_append(source, "}\n"))
        return true;
    return false;
}

static bool emit_stmt_for_step(bool *check_length, const TypedType *iterable_type, TextBuffer *source, size_t depth, char (*iterable_name)[64], size_t offset, const TypedProgram *program, const TypedNode *node, ErrorState *errors)
{
    (*check_length) = iterable_type->kind == NIPPY_TYPE_LIST ||
                   iterable_type->kind == NIPPY_TYPE_FIXED_LIST;

    if (!append_indent(source, depth) ||
        !buffer_append(source, "{\n") ||
        !append_indent(source, depth + 1) ||
        !buffer_appendf(source, "%s nyx_for_src_%zu = ",
                        (*iterable_name), offset) ||
        !emit_expression(program, node->left_child, source, errors) ||
        !buffer_append(source, ";\n") ||
        !append_indent(source, depth + 1) ||
        !buffer_appendf(source, "size_t nyx_for_len_%zu = (size_t)(",
                        offset)) {
        if (!errors->reported) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
        }
        return false;
    }
    if (!emit_stmt_for_buffer_appendf(iterable_type, source, offset, node, errors, depth)) {
        return false;
    }
    return true;
}

static bool emit_stmt_for_stmt_for_step(bool *check_length, const TypedType *iterable_type, TextBuffer *source, size_t depth, char (*iterable_name)[64], size_t offset, const TypedProgram *program, const TypedNode *node, ErrorState *errors, char (*element_name)[64], const char *value_c, const TypedNode *iterable_node)
{
    if (!emit_stmt_for_step(check_length, iterable_type, source, depth, iterable_name, offset, program, node, errors)) {
        return false;
    }
    if ((*check_length)) {
        if (iterable_type->kind == NIPPY_TYPE_LIST) {
            if (!buffer_appendf(source,
                                "if ((size_t)nyx_list_len_at("
                                "nyx_for_src_%zu, __FILE__, %zu, %zu, %zu) != "
                                "nyx_for_len_%zu) { ",
                                offset, node->pos.line, node->pos.column,
                                node->pos.offset, offset) ||
                !emit_inline_failure(source, node->pos, MESSAGE_RUNTIME_FOR_MUTATED) ||
                !buffer_append(source, " }\n")) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
                return false;
            }
        } else if (!buffer_appendf(source,
                                   "if ((size_t)(nyx_for_src_%zu).length "
                                   "!= nyx_for_len_%zu) { ",
                                   offset, offset) ||
                   !emit_inline_failure(source, node->pos, MESSAGE_RUNTIME_FOR_MUTATED) ||
                   !buffer_append(source, " }\n")) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            return false;
        }
        if (!append_indent(source, depth + 2)) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            return false;
        }
    }
    if (!emit_stmt_for_buffer_appendf4(source, element_name, value_c, errors, iterable_type, iterable_node, offset, node, depth)) {
        return false;
    }
    return true;
}

static bool emit_stmt_for_index_c(const TypedType *iterable_type, const TypedProgram *program, const TypedNode *node, size_t depth, const char *effective_source, TextBuffer *source, ErrorState *errors, const char *value_name, const char *index_name, const TypedNode *iterable_node, char (*iterable_name)[64], char (*element_name)[64], bool *outcome)
{
    if (iterable_type != NULL &&
        (iterable_type->kind == NIPPY_TYPE_RECORD ||
         iterable_type->kind == NIPPY_TYPE_TABLE)) {
        do { *outcome = (emit_map_for(program, node, iterable_type, depth,
                            effective_source, source, errors)); return true; } while (0);
    }
    if (iterable_type == NULL || value_name == NULL ||
        !valid_identifier(value_name) ||
        (index_name != NULL && !valid_identifier(index_name)) ||
        !format_c_type(program, iterable_node->type_index, (*iterable_name),
                       sizeof((*iterable_name)), errors) ||
        !format_c_type(program, node->auxiliary_type, (*element_name),
                       sizeof((*element_name)), errors)) {
        error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_STATEMENT);
        do { *outcome = (false); return true; } while (0);
    }
    return false;
}

static bool emit_stmt_for_stmt_for_stmt_for_step(bool *check_length, const TypedType *iterable_type, TextBuffer *source, size_t depth, char (*iterable_name)[64], size_t offset, const TypedProgram *program, const TypedNode *node, ErrorState *errors, char (*element_name)[64], const char *value_c, const TypedNode *iterable_node, const char *index_c, const char *effective_source)
{
    if (!emit_stmt_for_stmt_for_step(check_length, iterable_type, source, depth, iterable_name, offset, program, node, errors, element_name, value_c, iterable_node)) {
        return false;
    }
    if (index_c != NULL) {
        if (!append_indent(source, depth + 2) ||
            !buffer_appendf(source, "int32_t %s = (int32_t)nyx_for_i_%zu;\n",
                            index_c, offset) ||
            !append_indent(source, depth + 2) ||
            !buffer_appendf(source, "(void)%s;\n", index_c)) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            return false;
        }
    }
    if (!emit_loop_scope_mark(program, source, errors, depth + 2) ||
    !emit_block(program, node->right_child, depth + 2,
                    effective_source, source, errors)) {
        return false;
    }
    if (!emit_stmt_for_append_indent((*check_length), source, depth, errors, iterable_type, offset, node)) {
        return false;
    }
    return true;
}

static bool emit_stmt_for(const TypedProgram *program, size_t node_index, size_t depth, const char *effective_source, TextBuffer *source, ErrorState *errors)
{
    const TypedNode *node = typed_program_node(program, node_index);
    (void)node_index;
    const TypedNode *iterable_node = typed_program_node(
        program, node->left_child);
    const TypedType *iterable_type =
        iterable_node == NULL
            ? NULL
            : typed_program_type(program, iterable_node->type_index);
    char value_name_text[USER_NAME_CAPACITY];
const char *value_name = user_c_name(program, node->name_index,
                              value_name_text, sizeof(value_name_text));
    char index_name_text[USER_NAME_CAPACITY];
    const char *index_name =
        node->second_name_index == TYPED_NO_NODE
            ? NULL
            : user_c_name(program, node->second_name_index,
                          index_name_text, sizeof(index_name_text));
    char iterable_name[NIPPY_IDENTIFIER_MAX + 32];
    char element_name[NIPPY_IDENTIFIER_MAX + 32];
    size_t offset = node->pos.offset;
    const char *value_c;
    const char *index_c = {0};
    bool check_length = {0};
    {
        bool outcome;

        if (emit_stmt_for_index_c(iterable_type, program, node, depth, effective_source, source, errors, value_name, index_name, iterable_node, &iterable_name, &element_name, &outcome)) {
            return outcome;
        }
    }
    value_c = value_name;
    index_c = index_name;
    if (iterable_type->kind == NIPPY_TYPE_RANGE) {
        const TypedType *element = typed_program_type(program, node->auxiliary_type);
        bool is_signed = element != NULL && nippy_type_is_signed_integer(element->kind);
        if (element == NULL || !append_indent(source, depth) ||
            !buffer_append(source, "{\n") || !append_indent(source, depth + 1) ||
            !buffer_appendf(source, "%s nyx_range_%zu = ", iterable_name, offset) ||
            !emit_expression(program, node->left_child, source, errors) ||
            !buffer_append(source, ";\n") || !append_indent(source, depth + 1) ||
            !buffer_appendf(source, "%s nyx_cur_%zu = nyx_range_%zu.start;\n",
                            element_name, offset, offset) ||
            !append_indent(source, depth + 1) ||
            !buffer_appendf(source,
                "uint64_t nyx_idx_%zu = 0;\n", offset) ||
            !append_indent(source, depth + 1) ||
            !buffer_appendf(source,
                "bool nyx_active_%zu = nyx_range_%zu.step > 0 ? "
                "nyx_cur_%zu < nyx_range_%zu.end : nyx_cur_%zu > nyx_range_%zu.end;\n",
                offset, offset, offset, offset, offset, offset) ||
            !append_indent(source, depth + 1) ||
            !buffer_appendf(source, "for (; (void)nyx_idx_%zu, nyx_active_%zu; "
                                    "nyx_idx_%zu++) {\n",
                            offset, offset, offset) || !append_indent(source, depth + 2) ||
            !buffer_appendf(source, "%s %s = nyx_cur_%zu; (void)%s;\n",
                            element_name, value_c, offset, value_c) ||
            !append_indent(source, depth + 2) ||
            !buffer_appendf(source,
                "uint64_t nyx_dist_%zu = nyx_range_%zu.step > 0 ? "
                "(uint64_t)nyx_range_%zu.end - (uint64_t)nyx_cur_%zu : "
                "(uint64_t)nyx_cur_%zu - (uint64_t)nyx_range_%zu.end;\n",
                offset, offset, offset, offset, offset, offset) ||
            !append_indent(source, depth + 2) ||
            !buffer_appendf(source,
                "uint64_t nyx_mag_%zu = nyx_range_%zu.step > 0 ? "
                "(uint64_t)nyx_range_%zu.step : UINT64_C(0) - "
                "(uint64_t)nyx_range_%zu.step;\n", offset, offset, offset, offset) ||
            !append_indent(source, depth + 2) ||
            !buffer_appendf(source,
                "if (nyx_mag_%zu >= nyx_dist_%zu) nyx_active_%zu = false;\n",
                offset, offset, offset) || !append_indent(source, depth + 2))
            goto range_error;
        if (emit_stmt_for_buffer_appendf7(is_signed, source, offset, &element_name, index_c, depth, program, errors, node, effective_source)) {
        goto range_error;
    }
        return true;
range_error:
        if (!errors->reported) error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
        return false;
    }
    if (!emit_stmt_for_stmt_for_stmt_for_step(&check_length, iterable_type, source, depth, &iterable_name, offset, program, node, errors, &element_name, value_c, iterable_node, index_c, effective_source)) {
        return false;
    }
    return true;
}

static bool emit_stmt_table_set(const TypedProgram *program, size_t node_index, size_t depth, const char *effective_source, TextBuffer *source, ErrorState *errors)
{
    const TypedNode *node = typed_program_node(program, node_index);
    (void)node_index;
    (void)effective_source;
    size_t key = *(const size_t *)list_get(&program->children,
                                           node->children_start);
    size_t value = *(const size_t *)list_get(&program->children,
                                             node->children_start + 1);
    const TypedNode *receiver = typed_program_node(
        program, node->left_child);

    if (receiver == NULL || !append_indent(source, depth) ||
        !buffer_appendf(source, "nyx_table_%zu_set(&(",
                        receiver->type_index) ||
        !emit_expression(program, node->left_child, source, errors) ||
        !buffer_append(source, "), ") ||
        !emit_expression(program, key, source, errors) ||
        !buffer_append(source, ", ") ||
        !emit_expression(program, value, source, errors) ||
        !buffer_appendf(source, ", __FILE__, %zu, %zu, %zu);\n",
                        node->pos.line, node->pos.column, node->pos.offset)) {
        if (!errors->reported) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
        }
        return false;
    }
    return true;
}

static bool emit_stmt_record_set(const TypedProgram *program, size_t node_index, size_t depth, const char *effective_source, TextBuffer *source, ErrorState *errors)
{
    const TypedNode *node = typed_program_node(program, node_index);
    (void)node_index;
    (void)effective_source;
    size_t key = *(const size_t *)list_get(&program->children,
                                           node->children_start);
    size_t value = *(const size_t *)list_get(&program->children,
                                             node->children_start + 1);
    const TypedNode *receiver = typed_program_node(
        program, node->left_child);

    if (receiver == NULL || !append_indent(source, depth) ||
        !buffer_append(source, "nyx_record_") ||
        !buffer_appendf(source, "%zu", receiver->type_index) ||
        !buffer_append(source, "_set(&(") ||
        !emit_expression(program, node->left_child, source, errors) ||
        !buffer_append(source, "), ") ||
        !emit_expression(program, key, source, errors) ||
        !buffer_append(source, ", ") ||
        !emit_expression(program, value, source, errors) ||
        !buffer_appendf(source, ", __FILE__, %zu, %zu, %zu);\n",
                        node->pos.line, node->pos.column, node->pos.offset)) {
        if (!errors->reported) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
        }
        return false;
    }
    return true;
}

static bool emit_stmt_local_declaration(const TypedProgram *program, size_t node_index, size_t depth, const char *effective_source, TextBuffer *source, ErrorState *errors)
{
    const TypedNode *node = typed_program_node(program, node_index);
    (void)node_index;
    (void)effective_source;
    char name_text[USER_NAME_CAPACITY];
const char *name = user_c_name(program, node->name_index,
                              name_text, sizeof(name_text));
    char type_name[NIPPY_IDENTIFIER_MAX + 32];

    if (!valid_identifier(name) ||
        !format_c_type(program, node->type_index, type_name,
                       sizeof(type_name), errors) ||
        node->left_child == TYPED_NO_NODE) {
        error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_STATEMENT);
        return false;
    }
    if (!append_indent(source, depth) ||
        !buffer_appendf(source, "%s %s = ", type_name, name) ||
        !emit_expression(program, node->left_child, source, errors) ||
        !buffer_append(source, ";\n")) {
        if (!errors->reported) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
        }
        return false;
    }
    return true;
}

static bool emit_stmt_member_write(const TypedProgram *program, size_t node_index, size_t depth, const char *effective_source, TextBuffer *source, ErrorState *errors)
{
    const TypedNode *node = typed_program_node(program, node_index);
    (void)node_index;
    (void)effective_source;
    if (!append_indent(source, depth) ||
        !buffer_append(source, "(void)(") ||
        !emit_expression(program, node->left_child, source, errors) ||
        !buffer_append(source, " = ") ||
        !emit_expression(program, node->right_child, source, errors) ||
        !buffer_append(source, ");\n")) {
        if (!errors->reported) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
        }
        return false;
    }
    return true;
}

static bool emit_stmt_assignment(const TypedProgram *program, size_t node_index, size_t depth, const char *effective_source, TextBuffer *source, ErrorState *errors)
{
    const TypedNode *node = typed_program_node(program, node_index);
    (void)node_index;
    (void)effective_source;
    char name_text[USER_NAME_CAPACITY];
const char *name = user_c_name(program, node->name_index,
                              name_text, sizeof(name_text));

    if (!valid_identifier(name)) {
        error_report(errors, MESSAGE_GENERATOR_INVALID_IDENTIFIER);
        return false;
    }
    if (!append_indent(source, depth) ||
        !buffer_appendf(source, "%s = ", name) ||
        !emit_expression(program, node->left_child, source, errors) ||
        !buffer_append(source, ";\n")) {
        if (!errors->reported) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
        }
        return false;
    }
    return true;
}

static bool emit_stmt_list_set_append_indent(TextBuffer *source, size_t depth, const TypedProgram *program, size_t node_index, ErrorState *errors)
{
    if (!append_indent(source, depth) ||
        !buffer_append(source, "(void)(") ||
        !emit_expression(program, node_index, source, errors) ||
        !buffer_append(source, ");\n")) {
        if (!errors->reported) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
        }
        return false;
    }
    return true;
}

static bool emit_stmt_list_set(const TypedProgram *program, size_t node_index, size_t depth, const char *effective_source, TextBuffer *source, ErrorState *errors)
{
    const TypedNode *node = typed_program_node(program, node_index);
    (void)effective_source;
    const TypedNode *receiver = typed_program_node(program, node->left_child);
    const TypedType *receiver_type = receiver == NULL ? NULL :
        typed_program_type(program, receiver->type_index);
    if (receiver_type != NULL && receiver_type->kind == NIPPY_TYPE_LIST) {
        size_t index = *(const size_t *)list_get(&program->children, node->children_start);
        size_t value = *(const size_t *)list_get(&program->children, node->children_start + 1);
        char element_name[NIPPY_IDENTIFIER_MAX + 32];

        if (!format_c_type(program, node->auxiliary_type, element_name,
                           sizeof(element_name), errors) ||
            !append_indent(source, depth) || !buffer_append(source, "{\n") ||
            !append_indent(source, depth + 1) ||
            !buffer_appendf(source, "nyx_list nyx_set_list_%zu = ", node_index) ||
            !emit_expression(program, node->left_child, source, errors) ||
            !buffer_append(source, ";\n") || !append_indent(source, depth + 1) ||
            !buffer_appendf(source, "int32_t nyx_set_index_%zu = ", node_index) ||
            !emit_expression(program, index, source, errors) ||
            !buffer_append(source, ";\n") || !append_indent(source, depth + 1) ||
            !buffer_appendf(source,
                "(void)nyx_list_data_at(nyx_set_list_%zu, nyx_set_index_%zu, "
                "sizeof(%s), __FILE__, %zu, %zu, %zu);\n",
                node_index, node_index, element_name, node->pos.line,
                node->pos.column, node->pos.offset) ||
            !append_indent(source, depth + 1) ||
            !buffer_appendf(source, "%s nyx_set_value_%zu = ", element_name, node_index) ||
            !emit_expression(program, value, source, errors) ||
            !buffer_append(source, ";\n") || !append_indent(source, depth + 1) ||
            !buffer_appendf(source,
                "*(%s *)nyx_list_data_at(nyx_set_list_%zu, nyx_set_index_%zu, "
                "sizeof(%s), __FILE__, %zu, %zu, %zu) = nyx_set_value_%zu;\n",
                element_name, node_index, node_index, element_name,
                node->pos.line, node->pos.column, node->pos.offset, node_index) ||
            !append_indent(source, depth) || !buffer_append(source, "}\n")) {
            if (!errors->reported) error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            return false;
        }
        return true;
    }
    if (!emit_stmt_list_set_append_indent(source, depth, program, node_index, errors)) {
        return false;
    }
    return true;
}

static bool emit_stmt_print(const TypedProgram *program, size_t node_index, size_t depth, const char *effective_source, TextBuffer *source, ErrorState *errors)
{
    const TypedNode *node = typed_program_node(program, node_index);
    (void)node_index;
    (void)effective_source;
    return emit_print_statement(program, node, source, errors, depth);
}

static bool emit_stmt_expression_statement(const TypedProgram *program, size_t node_index, size_t depth, const char *effective_source, TextBuffer *source, ErrorState *errors)
{
    const TypedNode *node = typed_program_node(program, node_index);
    (void)node_index;
    (void)effective_source;
    const TypedNode *expression = typed_program_node(
        program, node->left_child);

    if (expression != NULL && expression->kind == TYPED_PRINT) {
        return emit_print_statement(program, expression, source, errors,
                                    depth);
    }
    /* The outer cast discards the expression's value so a store reset or
       write statement is warning-free under -Wall. */
    if (!append_indent(source, depth) ||
        !buffer_append(source, "(void)(") ||
        !emit_expression(program, node->left_child, source, errors) ||
        !buffer_append(source, ");\n")) {
        if (!errors->reported) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
        }
        return false;
    }
    return true;
}

static bool emit_stmt_block(const TypedProgram *program, size_t node_index, size_t depth, const char *effective_source, TextBuffer *source, ErrorState *errors)
{
    return emit_block(program, node_index, depth, effective_source,
                      source, errors);
}

static const struct {
    int key;
    EmitStatementBodyHandler handler;
} emit_statement_body_handlers2[] = {
    {TYPED_RETURN, emit_stmt_return},
    {TYPED_BREAK, emit_stmt_break},
    {TYPED_CONTINUE, emit_stmt_break},
    {TYPED_IF, emit_stmt_if},
    {TYPED_WHILE, emit_stmt_while},
    {TYPED_FOR, emit_stmt_for},
    {TYPED_TABLE_SET, emit_stmt_table_set},
    {TYPED_RECORD_SET, emit_stmt_record_set},
    {TYPED_LOCAL_DECLARATION, emit_stmt_local_declaration},
    {TYPED_MEMBER_WRITE, emit_stmt_member_write},
    {TYPED_ASSIGNMENT, emit_stmt_assignment},
    {TYPED_LIST_SET, emit_stmt_list_set},
    {TYPED_PRINT, emit_stmt_print},
    {TYPED_EXPRESSION_STATEMENT, emit_stmt_expression_statement},
    {TYPED_BLOCK, emit_stmt_block},
};

static bool emit_statement_body_return(const TypedProgram *program, const TypedNode *node, ErrorState *errors, TextBuffer *source, size_t depth, size_t node_index)
{
    char return_type[NIPPY_IDENTIFIER_MAX + 32];
    if (!format_c_type(program, node->type_index, return_type, sizeof(return_type), errors) ||
        !append_indent(source, depth) || !buffer_append(source, "{\n") ||
        !append_indent(source, depth + 1) ||
        !buffer_appendf(source, "%s nyx_scope_result_%zu = ", return_type, node_index) ||
        !emit_expression(program, node->left_child, source, errors) ||
        !buffer_append(source, ";\n") || !append_indent(source, depth + 1) ||
        !buffer_append(source, "nyx_scope_cleanup(&nyx_scope_top, NULL);\n"
                               "    nyx_scope_release(&nyx_cache_top, NULL);\n") ||
        !append_indent(source, depth + 1) ||
        !buffer_appendf(source, "return nyx_scope_result_%zu;\n", node_index) ||
        !append_indent(source, depth) || !buffer_append(source, "}\n")) {
        if (!errors->reported) error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
        return false;
    }
    return true;
}

static bool emit_statement_body(const TypedProgram *program, size_t node_index,
                                size_t depth, const char *effective_source,
                                TextBuffer *source, ErrorState *errors)
{
    const TypedNode *node = typed_program_node(program, node_index);

    if (node == NULL) {
        error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_STATEMENT);
        return false;
    }
    if (!buffer_appendf(source, "#line %zu \"%s\"\n", node->pos.line,
                        effective_source)) {
        error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
        return false;
    }

    for (size_t i = 0; i < NIPPY_COUNT_OF(emit_statement_body_handlers1); i++) {
        if (emit_statement_body_handlers1[i].key == (int)node->kind) {
            return emit_statement_body_handlers1[i].handler(program, node_index, depth, effective_source, source, errors);
        }
    }

    if (node->kind == TYPED_RETURN && program_uses_scope(program)) {
        if (!emit_statement_body_return(program, node, errors, source, depth, node_index)) {
        return false;
    }
        return true;
    }

    for (size_t i = 0; i < NIPPY_COUNT_OF(emit_statement_body_handlers2); i++) {
        if (emit_statement_body_handlers2[i].key == (int)node->kind) {
            return emit_statement_body_handlers2[i].handler(program, node_index, depth, effective_source, source, errors);
        }
    }

    error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_STATEMENT);
    return false;
}


static bool emit_function_signature_child_step(const TypedNode *node, size_t i, const char **parameter_name, ErrorState *errors, TextBuffer *source, char (*parameter_type)[64])
{
    if (node->is_method && i == 0) {
        (*parameter_name) = "nyx_self";
    }
    if (!valid_identifier((*parameter_name))) {
        error_report(errors, MESSAGE_GENERATOR_INVALID_IDENTIFIER);
        return false;
    }
    if (i > 0 && !buffer_append(source, ", ")) {
        error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
        return false;
    }
    if (!((node->is_method && i == 0)
              ? buffer_appendf(source, "%s *%s", (*parameter_type),
                              (*parameter_name))
              : buffer_appendf(source, "%s %s", (*parameter_type),
                              (*parameter_name)))) {
        error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
        return false;
    }
    return true;
}

static bool emit_function_signature_child(const TypedProgram *program, const TypedNode *node, size_t i, ErrorState *errors, TextBuffer *source)
{
    size_t child = *(const size_t *)list_get(&program->children,
                                             node->children_start + i);
    const TypedNode *parameter = typed_program_node(program, child);
    char parameter_text[USER_NAME_CAPACITY];
    const char *parameter_name;
    char parameter_type[NIPPY_IDENTIFIER_MAX + 32];

    if (parameter == NULL || parameter->kind != TYPED_PARAMETER ||
        !format_c_type(program, parameter->type_index, parameter_type,
                       sizeof(parameter_type), errors)) {
        if (!errors->reported) {
            error_report(errors,
                         MESSAGE_GENERATOR_UNSUPPORTED_FUNCTION);
        }
        return false;
    }
    parameter_name = user_c_name(program, parameter->name_index,
                                 parameter_text,
                                 sizeof(parameter_text));
    if (parameter->is_self != (node->is_method && i == 0)) {
        error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_FUNCTION);
        return false;
    }
    if (!emit_function_signature_child_step(node, i, &parameter_name, errors, source, &parameter_type)) {
        return false;
    }
    return true;
}

static bool emit_function_signature_i(const char *name, const TypedType *type, ErrorState *errors, const TypedFunctionSignature **signature, const TypedProgram *program, char (*return_name)[64], TextBuffer *source)
{
    if (!valid_identifier(name) || type == NULL ||
        type->kind != NIPPY_TYPE_FUNCTION) {
        error_report(errors, valid_identifier(name)
                                 ? MESSAGE_GENERATOR_UNSUPPORTED_FUNCTION
                                 : MESSAGE_GENERATOR_INVALID_IDENTIFIER);
        return false;
    }
    (*signature) = typed_program_signature(program, type->signature_index);
    if ((*signature) == NULL ||
        !format_c_type(program, (*signature)->return_type, (*return_name),
                       sizeof((*return_name)), errors)) {
        if (!errors->reported) {
            error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_FUNCTION);
        }
        return false;
    }
    if (!buffer_appendf(source, "%s %s(", (*return_name), name)) {
        error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
        return false;
    }
    return true;
}

static bool emit_function_signature(const TypedProgram *program,
                                    const TypedNode *node, const char *name,
                                    TextBuffer *source, ErrorState *errors)
{
    const TypedType *type = typed_program_type(program, node->type_index);
    const TypedFunctionSignature *signature;
    char return_name[NIPPY_IDENTIFIER_MAX + 32];
    size_t i = {0};
    if (!emit_function_signature_i(name, type, errors, &signature, program, &return_name, source)) {
        return false;
    }
    if (node->children_count == 0) {
        if (!buffer_append(source, "void")) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            return false;
        }
    } else {
        for (i = 0; i < node->children_count; i++) {
            if (!emit_function_signature_child(program, node, i, errors, source)) {
        return false;
    }
        }
    }
    return buffer_append(source, ")");
}


static bool emit_function_call_args(const TypedProgram *program,
                                    const TypedNode *node, TextBuffer *source,
                                    ErrorState *errors)
{
    size_t i;

    for (i = 0; i < node->children_count; i++) {
        size_t child = *(const size_t *)list_get(&program->children,
                                                 node->children_start + i);
        const TypedNode *parameter = typed_program_node(program, child);
        char parameter_text[USER_NAME_CAPACITY];
        const char *parameter_name;

        if (parameter == NULL || parameter->kind != TYPED_PARAMETER) {
            error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_FUNCTION);
            return false;
        }
        parameter_name = parameter->is_self
                             ? "nyx_self"
                             : user_c_name(program, parameter->name_index,
                                           parameter_text,
                                           sizeof(parameter_text));
        if (parameter_name == NULL) {
            error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_FUNCTION);
            return false;
        }
        if (i > 0 && !buffer_append(source, ", ")) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            return false;
        }
        if (!buffer_append(source, parameter_name)) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            return false;
        }
    }
    return true;
}


static bool emit_function_unused_params(const TypedProgram *program,
                                        const TypedNode *node,
                                        TextBuffer *source,
                                        ErrorState *errors)
{
    size_t i;

    for (i = 0; i < node->children_count; i++) {
        size_t child = *(const size_t *)list_get(&program->children,
                                                 node->children_start + i);
        const TypedNode *parameter = typed_program_node(program, child);
        char parameter_name_text[USER_NAME_CAPACITY];
        const char *parameter_name = parameter == NULL
                                         ? NULL
                                         : parameter->is_self
                                               ? "nyx_self"
                                               : user_c_name(
                                                     program,
                                                     parameter->name_index,
                                                     parameter_name_text,
                                                     sizeof(parameter_name_text));

        if (parameter_name == NULL ||
            !buffer_appendf(source, "    (void)%s;\n", parameter_name)) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            return false;
        }
    }
    return true;
}


static bool emit_function_typed_calls_name(bool *recursive, const TypedProgram *program, const TypedNode *node, const TypedType **result_type, const TypedFunctionSignature *signature, TextBuffer *source, const char *effective_source, ErrorState *errors, const char *name, bool *outcome)
{
    (*recursive) = typed_calls_name(program, node->left_child,
                                 node->name_index);
    (*result_type) = signature == NULL ? NULL :
                  typed_program_type(program, signature->return_type);

    if (!buffer_appendf(source, "#line %zu \"%s\"\n", node->pos.line,
                        effective_source)) {
        error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
        do { *outcome = (false); return true; } while (0);
    }
    if (!(*recursive)) {
        if (!emit_function_signature(program, node, name, source, errors) ||
            !buffer_append(source, "\n{\n") ||
            !emit_match_temporaries(program, (size_t)node->integer_value_u,
                                    source, errors) ||
            !emit_scope_frame_declaration(program, node->left_child, source, errors) ||
            !emit_function_unused_params(program, node, source, errors) ||
            !emit_block(program, node->left_child, 1, effective_source,
                        source, errors) ||
            ((*result_type) != NULL && (*result_type)->kind == NIPPY_TYPE_NIL &&
             !buffer_append(source, "    return (nyx_nil)0;\n")) ||
            !buffer_append(source, "}\n\n")) {
            if (!errors->reported) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            }
            do { *outcome = (false); return true; } while (0);
        }
        do { *outcome = (true); return true; } while (0);
    }
    return false;
}

static bool emit_function_valid_identifier(const char *name, const TypedType *type, ErrorState *errors)
{
    if (!valid_identifier(name) || type == NULL ||
        type->kind != NIPPY_TYPE_FUNCTION) {
        error_report(errors, valid_identifier(name)
                                 ? MESSAGE_GENERATOR_UNSUPPORTED_FUNCTION
                                 : MESSAGE_GENERATOR_INVALID_IDENTIFIER);
        return false;
    }
    return true;
}

bool emit_function(const TypedProgram *program, const TypedNode *node,
                          const char *effective_source, TextBuffer *source,
                          ErrorState *errors)
{
    char name_text[USER_NAME_CAPACITY];
    const char *name = user_c_name(program, node->name_index,
                                  name_text, sizeof(name_text));
    const TypedType *type = typed_program_type(program, node->type_index);
    const TypedFunctionSignature *signature;
    const TypedType *result_type;
    bool recursive;
    char inner_name[NIPPY_IDENTIFIER_MAX + 32];
    char depth_name[NIPPY_IDENTIFIER_MAX + 32];
    char return_name[NIPPY_IDENTIFIER_MAX + 32];

    if (!emit_function_valid_identifier(name, type, errors)) {
        return false;
    }
    signature = typed_program_signature(program, type->signature_index);
    {
        bool outcome;

        if (emit_function_typed_calls_name(&recursive, program, node, &result_type, signature, source, effective_source, errors, name, &outcome)) {
            return outcome;
        }
    }
    if (signature == NULL ||
        !format_c_type(program, signature->return_type, return_name,
                       sizeof(return_name), errors)) {
        error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_FUNCTION);
        return false;
    }
    snprintf(inner_name, sizeof(inner_name), "nyx_inner_%s", name);
    snprintf(depth_name, sizeof(depth_name), "nyx_depth_%s", name);
    if (!buffer_appendf(source, "static size_t %s = 0;\n\n",
                        depth_name) ||
        !emit_function_signature(program, node, inner_name, source, errors) ||
        !buffer_append(source, ";\n\n") ||
        !emit_function_signature(program, node, name, source, errors) ||
        !buffer_append(source, "\n{\n") ||
        !buffer_appendf(source,
                        "    if (++%s > NIPPY_RECURSION_LIMIT) {\n"
                        "        ", depth_name) ||
        !emit_inline_failure(source, node->pos, MESSAGE_RUNTIME_RECURSION) ||
        !buffer_appendf(source,
                        "\n"
                        "    }\n"
                        "    %s nyx_recursion_result = %s(",
                        return_name, inner_name) ||
        !emit_function_call_args(program, node, source, errors) ||
        !buffer_appendf(source,
                        ");\n"
                        "    %s--;\n"
                        "    return nyx_recursion_result;\n"
                        "}\n\n",
                        depth_name) ||
        !emit_function_signature(program, node, inner_name, source, errors) ||
        !buffer_append(source, "\n{\n") ||
        !emit_match_temporaries(program, (size_t)node->integer_value_u,
                                source, errors) ||
        !emit_scope_frame_declaration(program, node->left_child, source, errors) ||
        !emit_function_unused_params(program, node, source, errors) ||
        !emit_block(program, node->left_child, 1, effective_source, source,
                    errors) ||
        (result_type != NULL && result_type->kind == NIPPY_TYPE_NIL &&
         !buffer_append(source, "    return (nyx_nil)0;\n")) ||
        !buffer_append(source, "}\n\n")) {
        if (!errors->reported) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
        }
        return false;
    }
    return true;
}
