#include "generator_internal.h"

/* How many loops enclose the statement being emitted that open scopes. Scopes
   inside a loop recycle their buffer; see emit_statement. */



/* A deferred block is emitted where control leaves its block, not where the
   `defer` is written, so every block being emitted keeps a frame. The frame
   says which of its statements is current: an exit edge inside statement `n`
   runs exactly the defers that stand before `n`, in reverse order, because a
   defer is registered by reaching it and sits directly in its block. Frames
   also record the `scope` Store and loop body they belong to, so an exit can
   release Stores and stop at the right loop in the order they were entered. */


static bool emit_framed_block(const TypedProgram *program, GeneratorContext *gen, size_t block_index,
                              size_t depth, const char *effective_source,
                              TextBuffer *source, ErrorState *errors,
                              size_t scope_node, bool loop_body);

static bool emit_stmt_defer(const TypedProgram *program, GeneratorContext *gen, size_t node_index,
                            size_t depth, const char *effective_source,
                            TextBuffer *source, ErrorState *errors);

bool emit_loop_body(const TypedProgram *program, GeneratorContext *gen, size_t block_index,
                           size_t depth, const char *effective_source,
                           TextBuffer *source, ErrorState *errors)
{
    return emit_framed_block(program, gen, block_index, depth, effective_source,
                             source, errors, TYPED_NO_NODE, true);
}

static bool emit_inline_failure(TextBuffer *source, SourcePos pos,
                                const char *message)
{
    return buffer_append(source,
                         "fflush(stdout); fprintf(stderr, \""
                         MESSAGE_RUNTIME_POSITION "\\n\", __FILE__, ") &&
           buffer_appendf(source, "(size_t)%zu, (size_t)%zu, (size_t)%zu, \"%s\" + sizeof(\"" MESSAGE_RUNTIME_PREFIX "\") - 1); exit(1);",
                          pos.line, pos.column, pos.offset, message);
}


/* Scope frames live for the whole activation, not the C block of their scope,
   so a header never dangles and a recycled buffer can outlive one iteration.
   The scopes and pools of one root come from the analyser's index, in node
   order. */
bool emit_scope_frames(const TypedProgram *program, GeneratorContext *gen,
                       size_t root_position, TextBuffer *source)
{
    size_t scope_count, parallel_count, index;
    const size_t *scopes = analysis_nodes_in(gen->analysis, TYPED_SCOPE,
                                             root_position, &scope_count);
    const size_t *parallels = analysis_nodes_in(
        gen->analysis, TYPED_PARALLEL, root_position, &parallel_count);
    NodeMerge merge;

    node_merge_init(&merge, scopes, scope_count, parallels, parallel_count);
    while (node_merge_next(&merge, &index)) {
        const TypedNode *node = typed_program_node(program, index);

        /* Only a parallel block with a pool of its own is a scope. */
        if (node != NULL && node->kind == TYPED_PARALLEL &&
            node->integer_value != 1) {
            continue;
        }
        buffer_appendf(source,
                        "    nyx_scope_frame nyx_scope_frame_%zu = {0}; "
                        "(void)nyx_scope_frame_%zu;\n", index, index);
    }
    return true;
}

/* The frames of every root that is not a function: the statements that run in
   main, or in nyx_program_value. */
bool emit_program_scope_frames(const TypedProgram *program,
                               GeneratorContext *gen, TextBuffer *source)
{
    size_t position;

    for (position = 0; position < program->roots.length; position++) {
        const TypedNode *root = typed_program_node(
            program, *(const size_t *)list_get(&program->roots, position));

        if (root != NULL && root->kind != TYPED_FUNCTION_DECLARATION &&
            !emit_scope_frames(program, gen, position, source)) {
            return false;
        }
    }
    return true;
}

bool emit_scope_frame_declaration(const TypedProgram *program, GeneratorContext *gen, size_t root_position,
                                  TextBuffer *source)
{
    if (!analysis_has(gen->analysis, ANALYSIS_SCOPE)) return true;
    buffer_append(source,
                   "    nyx_scope_frame *nyx_scope_top = NULL; (void)nyx_scope_top;\n"
                   "    nyx_scope_frame *nyx_cache_top = NULL; (void)nyx_cache_top;\n");
    return root_position == NO_ROOT_POSITION ||
           emit_scope_frames(program, gen, root_position, source);
}

void emit_loop_scope_mark(GeneratorContext *gen, TextBuffer *source, size_t depth)
{
    if (!analysis_has(gen->analysis, ANALYSIS_SCOPE)) return;
    append_indent(source, depth);
    buffer_append(source, "nyx_scope_frame *nyx_loop_mark = nyx_scope_top; (void)nyx_loop_mark;\n");
}

static size_t block_child_at(const TypedProgram *program,
                             const TypedNode *block, size_t i)
{
    return *(const size_t *)list_get(&program->children,
                                     block->children_start + i);
}

/* True when a defer stands before the frame's current statement. */
static bool frame_has_defer(const TypedProgram *program,
                            const BlockFrame *frame)
{
    const TypedNode *block = typed_program_node(program, frame->block);
    size_t i;

    for (i = 0; block != NULL && i < frame->position &&
                i < block->children_count; i++) {
        const TypedNode *child =
            typed_program_node(program, block_child_at(program, block, i));

        if (child != NULL && child->kind == TYPED_DEFER) {
            return true;
        }
    }
    return false;
}

/* True when leaving the innermost block, up to the enclosing loop body when
   `to_loop`, passes a defer that has been registered. */
static bool chain_has_defer(const TypedProgram *program,
                            const GeneratorContext *gen, bool to_loop)
{
    size_t i;

    for (i = gen->frame_count; i > 0; i--) {
        if (frame_has_defer(program, &gen->frames[i - 1])) {
            return true;
        }
        if (to_loop && gen->frames[i - 1].loop_body) {
            break;
        }
    }
    return false;
}

static bool chain_has_loop(const GeneratorContext *gen)
{
    size_t i;

    for (i = gen->frame_count; i > 0; i--) {
        if (gen->frames[i - 1].loop_body) {
            return true;
        }
    }
    return false;
}

/* Emits the frame's registered defers, most recent first. Each body is its
   own C block, so its locals cannot collide with the surrounding code. */
static bool emit_frame_defers(const TypedProgram *program, GeneratorContext *gen, BlockFrame frame,
                              size_t depth, const char *effective_source,
                              TextBuffer *source, ErrorState *errors)
{
    const TypedNode *block = typed_program_node(program, frame.block);
    size_t i;

    if (block == NULL) {
        error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_STATEMENT);
        return false;
    }
    for (i = frame.position < block->children_count ? frame.position
                                                    : block->children_count;
         i > 0; i--) {
        const TypedNode *child =
            typed_program_node(program, block_child_at(program, block, i - 1));

        if (child == NULL || child->kind != TYPED_DEFER) {
            continue;
        }
        append_indent(source, depth);
        buffer_append(source, "{\n");
        if (!emit_block(program, gen, child->left_child, depth + 1,
                    effective_source, source, errors)) {
            return false;
        }
        append_indent(source, depth);
        buffer_append(source, "}\n");
    }
    return true;
}

/* Runs the exit work of every block being left, innermost first: its defers,
   then the release of its `scope` Store. Defers stand before a Store's block
   only when they were written in an outer block, so the order matches the
   order everything was entered. */
static bool emit_unwind(const TypedProgram *program, GeneratorContext *gen, bool to_loop, size_t depth,
                        const char *effective_source, TextBuffer *source,
                        ErrorState *errors)
{
    size_t i;

    for (i = gen->frame_count; i > 0; i--) {
        BlockFrame frame = gen->frames[i - 1];

        if (!emit_frame_defers(program, gen, frame, depth, effective_source, source,
                               errors)) {
            return false;
        }
        if (frame.scope_node != TYPED_NO_NODE &&
            (!append_indent(source, depth) ||
             !buffer_appendf(source,
                             "nyx_scope_cleanup(&nyx_scope_top, "
                             "nyx_scope_frame_%zu.previous);\n",
                             frame.scope_node))) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            return false;
        }
        if (to_loop && frame.loop_body) {
            break;
        }
    }
    return true;
}

static bool block_ends_in_exit(const TypedProgram *program,
                               const TypedNode *block)
{
    const TypedNode *last;

    if (block->children_count == 0) {
        return false;
    }
    last = typed_program_node(
        program, block_child_at(program, block, block->children_count - 1));
    return last != NULL && (last->kind == TYPED_RETURN ||
                            last->kind == TYPED_BREAK ||
                            last->kind == TYPED_CONTINUE);
}

static bool emit_framed_block(const TypedProgram *program, GeneratorContext *gen, size_t block_index,
                              size_t depth, const char *effective_source,
                              TextBuffer *source, ErrorState *errors,
                              size_t scope_node, bool loop_body)
{
    const TypedNode *block = typed_program_node(program, block_index);
    size_t frame_index;
    size_t i;
    bool ok = true;

    if (block == NULL || block->kind != TYPED_BLOCK ||
        gen->frame_count >= NYX_MAX_AST_DEPTH) {
        error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_STATEMENT);
        return false;
    }
    frame_index = gen->frame_count++;
    gen->frames[frame_index].block = block_index;
    gen->frames[frame_index].position = 0;
    gen->frames[frame_index].scope_node = scope_node;
    gen->frames[frame_index].loop_body = loop_body;
    for (i = 0; ok && i < block->children_count; i++) {
        gen->frames[frame_index].position = i;
        ok = emit_statement(program, gen, block_child_at(program, block, i), depth,
                            effective_source, source, errors);
    }
    if (ok && !block_ends_in_exit(program, block)) {
        gen->frames[frame_index].position = block->children_count;
        ok = emit_frame_defers(program, gen, gen->frames[frame_index], depth,
                               effective_source, source, errors);
    }
    gen->frame_count = frame_index;
    return ok;
}

bool emit_block(const TypedProgram *program, GeneratorContext *gen, size_t block_index,
                       size_t depth, const char *effective_source,
                       TextBuffer *source, ErrorState *errors)
{
    return emit_framed_block(program, gen, block_index, depth, effective_source,
                             source, errors, TYPED_NO_NODE, false);
}

bool emit_block_in_scope(const TypedProgram *program, GeneratorContext *gen, size_t block_index,
                         size_t depth, const char *effective_source,
                         TextBuffer *source, ErrorState *errors,
                         size_t scope_node)
{
    return emit_framed_block(program, gen, block_index, depth, effective_source,
                             source, errors, scope_node, false);
}


static bool opens_scope(const TypedNode *node)
{
    return node->kind == TYPED_SCOPE ||
           (node->kind == TYPED_PARALLEL && node->integer_value == 1);
}

/* The search covers one function body, at most once per node. */
static bool search_stops(const TypedNode *node, const size_t *budget)
{
    return node == NULL || node->kind == TYPED_FUNCTION_DECLARATION ||
           *budget == 0;
}

static bool subtree_has_scope_at(const TypedProgram *program, size_t index,
                                 size_t *budget)
{
    const TypedNode *node = typed_program_node(program, index);
    bool found;
    size_t i;

    if (search_stops(node, budget)) {
        return false;
    }
    (*budget)--;
    found = opens_scope(node) ||
            subtree_has_scope_at(program, node->left_child, budget) ||
            subtree_has_scope_at(program, node->right_child, budget);
    for (i = 0; !found && i < node->children_count; i++) {
        found = subtree_has_scope_at(
            program,
            *(const size_t *)list_get(&program->children,
                                      node->children_start + i),
            budget);
    }
    return found;
}

static bool subtree_has_scope(const TypedProgram *program, size_t index)
{
    size_t budget = program->nodes.length;

    return subtree_has_scope_at(program, index, &budget);
}

static bool emit_statement_body(const TypedProgram *program, GeneratorContext *gen, size_t node_index,
                                size_t depth, const char *effective_source,
                                TextBuffer *source, ErrorState *errors);

/* A loop whose body opens scopes reuses each scope's buffer across iterations.
   The cached buffers are released when the loop ends, or by a return. */
bool emit_statement(const TypedProgram *program, GeneratorContext *gen, size_t node_index,
                    size_t depth, const char *effective_source,
                    TextBuffer *source, ErrorState *errors)
{
    const TypedNode *node = typed_program_node(program, node_index);
    bool ok;

    if (node == NULL || (node->kind != TYPED_WHILE && node->kind != TYPED_FOR) ||
        !subtree_has_scope(program, node_index)) {
        return emit_statement_body(program, gen, node_index, depth, effective_source,
                                   source, errors);
    }
    append_indent(source, depth);
    buffer_appendf(source,
                    "{ nyx_scope_frame *nyx_cache_mark_%zu = nyx_cache_top;\n",
                    node_index);
    gen->scope_loop_depth++;
    ok = emit_statement_body(program, gen, node_index, depth + 1, effective_source,
                             source, errors);
    gen->scope_loop_depth--;
    if (!ok) {
        return false;
    }
    append_indent(source, depth + 1);
    buffer_appendf(source,
                    "nyx_scope_release(&nyx_cache_top, nyx_cache_mark_%zu); }\n",
                    node_index);
    return true;
}

typedef bool (*EmitStatementBodyHandler)(const TypedProgram *program, GeneratorContext *gen, size_t node_index, size_t depth, const char *effective_source, TextBuffer *source, ErrorState *errors);

static bool emit_stmt_match_bind_statement(const TypedProgram *program, GeneratorContext *gen, size_t node_index, size_t depth, const char *effective_source, TextBuffer *source, ErrorState *errors)
{
    const TypedNode *node = typed_program_node(program, node_index);
    (void)node_index;
    append_indent(source, depth);
    buffer_append(source, "{\n");
    append_indent(source, depth + 1);
    buffer_appendf(source, "nyx_match_%zu = ",
                    (size_t)node->integer_value_u);
    if (!emit_expression(program, node->left_child, source, errors)) {
        return false;
    }
    buffer_append(source, ";\n");
    append_indent(source, depth + 1);
    buffer_appendf(source, "(void)nyx_match_%zu;\n",
                    (size_t)node->integer_value_u);
    if (!emit_statement(program, gen, node->right_child, depth + 1,
                    effective_source, source, errors)) {
        return false;
    }
    append_indent(source, depth);
    buffer_append(source, "}\n");
    return true;
}

static bool emit_stmt_scope(const TypedProgram *program, GeneratorContext *gen, size_t node_index, size_t depth, const char *effective_source, TextBuffer *source, ErrorState *errors)
{
    const TypedNode *node = typed_program_node(program, node_index);
    char store_name[USER_NAME_CAPACITY];
    const char *name = user_c_name(program, node->name_index, store_name, sizeof(store_name));
    if (!valid_identifier(name)) {
        return false;
    }
    append_indent(source, depth);
    buffer_append(source, "{\n");
    append_indent(source, depth + 1);
    buffer_appendf(source, "int32_t nyx_scope_capacity_%zu = ", node_index);
    if (!emit_expression(program, node->left_child, source, errors)) {
        return false;
    }
    buffer_append(source, ";\n");
    append_indent(source, depth + 1);
    append_indent(source, depth + 1);
    buffer_appendf(source,
        gen->scope_loop_depth > 0
            ? "nyx_scope_enter_cached(&nyx_scope_top, &nyx_cache_top, &nyx_scope_frame_%zu, nyx_scope_capacity_%zu, __FILE__, %zu, %zu, %zu);\n"
            : "nyx_scope_enter(&nyx_scope_top, &nyx_scope_frame_%zu, nyx_scope_capacity_%zu, __FILE__, %zu, %zu, %zu);\n",
        node_index, node_index, node->pos.line, node->pos.column, node->pos.offset);
    append_indent(source, depth + 1);
    buffer_appendf(source, "nyx_store *%s = &nyx_scope_frame_%zu.store; (void)%s;\n",
                    name, node_index, name);
    if (!emit_framed_block(program, gen, node->right_child, depth + 1, effective_source, source, errors, node_index, false)) {
        return false;
    }
    append_indent(source, depth + 1);
    buffer_appendf(source, "nyx_scope_cleanup(&nyx_scope_top, nyx_scope_frame_%zu.previous);\n", node_index);
    append_indent(source, depth);
    buffer_append(source, "}\n");
    return true;
}

struct statement_handler_row {
    int key;
    EmitStatementBodyHandler handler;
};

static const struct statement_handler_row emit_statement_body_handlers1[] = {
    {TYPED_MATCH_BIND_STATEMENT, emit_stmt_match_bind_statement},
    {TYPED_SCOPE, emit_stmt_scope},
};

static bool emit_stmt_return(const TypedProgram *program, GeneratorContext *gen, size_t node_index, size_t depth, const char *effective_source, TextBuffer *source, ErrorState *errors)
{
    (void)gen;
    const TypedNode *node = typed_program_node(program, node_index);
    (void)node_index;
    (void)effective_source;
    append_indent(source, depth);
    buffer_append(source, "return ");
    if (!emit_expression(program, node->left_child, source, errors)) {
        return false;
    }
    buffer_append(source, ";\n");
    return true;
}

static bool emit_stmt_break(const TypedProgram *program, GeneratorContext *gen, size_t node_index, size_t depth, const char *effective_source, TextBuffer *source, ErrorState *errors)
{
    const TypedNode *node = typed_program_node(program, node_index);
    (void)node_index;
    if (chain_has_defer(program, gen, true) &&
        (!chain_has_loop(gen) ||
         !emit_unwind(program, gen, true, depth, effective_source, source,
                      errors))) {
        if (!errors->reported) {
            error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_STATEMENT);
        }
        return false;
    }
    if (analysis_has(gen->analysis, ANALYSIS_SCOPE) &&
        (!append_indent(source, depth) ||
         !buffer_append(source, "nyx_scope_cleanup(&nyx_scope_top, nyx_loop_mark);\n"))) {
        error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
        return false;
    }
    append_indent(source, depth);
    buffer_append(source,
                   node->kind == TYPED_BREAK ? "break;\n"
                                             : "continue;\n");
    return true;
}

static bool emit_stmt_if_node(const TypedProgram *program, GeneratorContext *gen, size_t node_index, ErrorState *errors, TextBuffer *source, size_t depth, const char *effective_source, size_t *else_block)
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
    append_indent(source, depth);
    buffer_append(source, "if (");
    if (!emit_expression(program, node->left_child, source, errors)) {
        return false;
    }
    buffer_append(source, ") {\n");
    if (!emit_block(program, gen, then_block, depth + 1, effective_source,
                source, errors)) {
        return false;
    }
    return true;
}

static bool emit_stmt_if_else_block(const TypedProgram *program, GeneratorContext *gen, size_t node_index, ErrorState *errors, TextBuffer *source, size_t depth, const char *effective_source)
{
    size_t else_block = {0};
    if (!emit_stmt_if_node(program, gen, node_index, errors, source, depth, effective_source, &else_block)) {
        return false;
    }
    if (else_block != TYPED_NO_NODE) {
        append_indent(source, depth);
        buffer_append(source, "} else {\n");
        if (!emit_block(program, gen, else_block, depth + 1, effective_source,
                    source, errors)) {
            return false;
        }
    }
    append_indent(source, depth);
    buffer_append(source, "}\n");
    return true;
}

static bool emit_stmt_if(const TypedProgram *program, GeneratorContext *gen, size_t node_index, size_t depth, const char *effective_source, TextBuffer *source, ErrorState *errors)
{
    if (!emit_stmt_if_else_block(program, gen, node_index, errors, source, depth, effective_source)) {
        return false;
    }
    return true;
}

static bool emit_stmt_while(const TypedProgram *program, GeneratorContext *gen, size_t node_index, size_t depth, const char *effective_source, TextBuffer *source, ErrorState *errors)
{
    const TypedNode *node = typed_program_node(program, node_index);
    (void)node_index;
    append_indent(source, depth);
    buffer_append(source, "while (");
    if (!emit_expression(program, node->left_child, source, errors)) {
        return false;
    }
    buffer_append(source, ") {\n");
    emit_loop_scope_mark(gen, source, depth + 1);
    if (!emit_loop_body(program, gen, node->right_child, depth + 1,
                    effective_source, source, errors)) {
        return false;
    }
    append_indent(source, depth);
    buffer_append(source, "}\n");
    return true;
}

static bool emit_stmt_table_set(const TypedProgram *program, GeneratorContext *gen, size_t node_index, size_t depth, const char *effective_source, TextBuffer *source, ErrorState *errors)
{
    (void)gen;
    const TypedNode *node = typed_program_node(program, node_index);
    (void)node_index;
    (void)effective_source;
    size_t key = *(const size_t *)list_get(&program->children,
                                           node->children_start);
    size_t value = *(const size_t *)list_get(&program->children,
                                             node->children_start + 1);
    const TypedNode *receiver = typed_program_node(
        program, node->left_child);

    if (receiver == NULL) {
        return false;
    }
    append_indent(source, depth);
    buffer_appendf(source, "nyx_table_%zu_set(&(",
                    receiver->type_index);
    if (!emit_expression(program, node->left_child, source, errors)) {
        return false;
    }
    buffer_append(source, "), ");
    if (!emit_expression(program, key, source, errors)) {
        return false;
    }
    buffer_append(source, ", ");
    if (!emit_expression(program, value, source, errors)) {
        return false;
    }
    buffer_appendf(source, ", __FILE__, %zu, %zu, %zu);\n",
                    node->pos.line, node->pos.column, node->pos.offset);
    return true;
}

static bool emit_stmt_record_set(const TypedProgram *program, GeneratorContext *gen, size_t node_index, size_t depth, const char *effective_source, TextBuffer *source, ErrorState *errors)
{
    (void)gen;
    const TypedNode *node = typed_program_node(program, node_index);
    (void)node_index;
    (void)effective_source;
    size_t key = *(const size_t *)list_get(&program->children,
                                           node->children_start);
    size_t value = *(const size_t *)list_get(&program->children,
                                             node->children_start + 1);
    const TypedNode *receiver = typed_program_node(
        program, node->left_child);

    if (receiver == NULL) {
        return false;
    }
    append_indent(source, depth);
    buffer_append(source, "nyx_record_");
    buffer_appendf(source, "%zu", receiver->type_index);
    buffer_append(source, "_set(&(");
    if (!emit_expression(program, node->left_child, source, errors)) {
        return false;
    }
    buffer_append(source, "), ");
    if (!emit_expression(program, key, source, errors)) {
        return false;
    }
    buffer_append(source, ", ");
    if (!emit_expression(program, value, source, errors)) {
        return false;
    }
    buffer_appendf(source, ", __FILE__, %zu, %zu, %zu);\n",
                    node->pos.line, node->pos.column, node->pos.offset);
    return true;
}

static bool emit_stmt_local_declaration(const TypedProgram *program, GeneratorContext *gen, size_t node_index, size_t depth, const char *effective_source, TextBuffer *source, ErrorState *errors)
{
    const TypedNode *node = typed_program_node(program, node_index);
    char name_text[USER_NAME_CAPACITY];
    const char *name = user_c_name(program, node->name_index, name_text,
                                   sizeof(name_text));
    char type_name[NYX_IDENTIFIER_MAX + 32];
    TypedNodeKind initialiser;
    bool ok;

    (void)gen;
    (void)effective_source;
    if (name == NULL || node->left_child == TYPED_NO_NODE ||
        !format_c_type(program, node->type_index, type_name, sizeof(type_name),
                       errors)) {
        error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_STATEMENT);
        return false;
    }
    buffer_line(source, depth, "%s %s = ", type_name, name);
    ok = emit_expression(program, node->left_child, source, errors);
    buffer_append(source, ";\n");
    /* A `with var` result the then section never reads is not an error. */
    initialiser = typed_program_node(program, node->left_child)->kind;
    if (initialiser == TYPED_PARALLEL_RESULT || initialiser == TYPED_WHEN_RESULT) {
        buffer_line(source, depth, "(void)%s;\n", name);
    }
    return ok;
}

static bool emit_stmt_member_write(const TypedProgram *program, GeneratorContext *gen, size_t node_index, size_t depth, const char *effective_source, TextBuffer *source, ErrorState *errors)
{
    (void)gen;
    const TypedNode *node = typed_program_node(program, node_index);
    (void)node_index;
    (void)effective_source;
    append_indent(source, depth);
    buffer_append(source, "(void)(");
    if (!emit_expression(program, node->left_child, source, errors)) {
        return false;
    }
    buffer_append(source, " = ");
    if (!emit_expression(program, node->right_child, source, errors)) {
        return false;
    }
    buffer_append(source, ");\n");
    return true;
}

static bool emit_stmt_assignment(const TypedProgram *program, GeneratorContext *gen, size_t node_index, size_t depth, const char *effective_source, TextBuffer *source, ErrorState *errors)
{
    (void)gen;
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
    append_indent(source, depth);
    buffer_appendf(source, "%s = ", name);
    if (!emit_expression(program, node->left_child, source, errors)) {
        return false;
    }
    buffer_append(source, ";\n");
    return true;
}

static bool emit_stmt_list_set_append_indent(TextBuffer *source, size_t depth, const TypedProgram *program, size_t node_index, ErrorState *errors)
{
    append_indent(source, depth);
    buffer_append(source, "(void)(");
    if (!emit_expression(program, node_index, source, errors)) {
        return false;
    }
    buffer_append(source, ");\n");
    return true;
}

static bool emit_stmt_list_set(const TypedProgram *program, GeneratorContext *gen, size_t node_index, size_t depth, const char *effective_source, TextBuffer *source, ErrorState *errors)
{
    (void)gen;
    const TypedNode *node = typed_program_node(program, node_index);
    (void)effective_source;
    const TypedNode *receiver = typed_program_node(program, node->left_child);
    const TypedType *receiver_type = receiver == NULL ? NULL :
        typed_program_type(program, receiver->type_index);
    if (receiver_type != NULL && receiver_type->kind == NYX_TYPE_LIST) {
        size_t index = *(const size_t *)list_get(&program->children, node->children_start);
        size_t value = *(const size_t *)list_get(&program->children, node->children_start + 1);
        char element_name[NYX_IDENTIFIER_MAX + 32];

        if (!format_c_type(program, node->auxiliary_type, element_name,
                       sizeof(element_name), errors)) {
            return false;
        }
        append_indent(source, depth);
        buffer_append(source, "{\n");
        append_indent(source, depth + 1);
        buffer_appendf(source, "nyx_list nyx_set_list_%zu = ", node_index);
        if (!emit_expression(program, node->left_child, source, errors)) {
            return false;
        }
        buffer_append(source, ";\n");
        append_indent(source, depth + 1);
        buffer_appendf(source, "int32_t nyx_set_index_%zu = ", node_index);
        if (!emit_expression(program, index, source, errors)) {
            return false;
        }
        buffer_append(source, ";\n");
        append_indent(source, depth + 1);
        buffer_appendf(source,
            "(void)nyx_list_data_at(nyx_set_list_%zu, nyx_set_index_%zu, "
            "sizeof(%s), __FILE__, %zu, %zu, %zu);\n",
            node_index, node_index, element_name, node->pos.line,
            node->pos.column, node->pos.offset);
        append_indent(source, depth + 1);
        buffer_appendf(source, "%s nyx_set_value_%zu = ", element_name, node_index);
        if (!emit_expression(program, value, source, errors)) {
            return false;
        }
        buffer_append(source, ";\n");
        append_indent(source, depth + 1);
        buffer_appendf(source,
            "*(%s *)nyx_list_data_at(nyx_set_list_%zu, nyx_set_index_%zu, "
            "sizeof(%s), __FILE__, %zu, %zu, %zu) = nyx_set_value_%zu;\n",
            element_name, node_index, node_index, element_name,
            node->pos.line, node->pos.column, node->pos.offset, node_index);
        append_indent(source, depth);
        buffer_append(source, "}\n");
        return true;
    }
    if (!emit_stmt_list_set_append_indent(source, depth, program, node_index, errors)) {
        return false;
    }
    return true;
}

static bool emit_stmt_print(const TypedProgram *program, GeneratorContext *gen, size_t node_index, size_t depth, const char *effective_source, TextBuffer *source, ErrorState *errors)
{
    const TypedNode *node = typed_program_node(program, node_index);
    (void)node_index;
    (void)effective_source;
    return emit_print_statement(program, gen, node, source, errors, depth);
}

static bool emit_stmt_expression_statement(const TypedProgram *program, GeneratorContext *gen, size_t node_index, size_t depth, const char *effective_source, TextBuffer *source, ErrorState *errors)
{
    const TypedNode *node = typed_program_node(program, node_index);
    (void)node_index;
    (void)effective_source;
    const TypedNode *expression = typed_program_node(
        program, node->left_child);

    if (expression != NULL && expression->kind == TYPED_PRINT) {
        return emit_print_statement(program, gen, expression, source, errors,
                                    depth);
    }
    /* The outer cast discards the expression's value so a store reset or
       write statement is warning-free under -Wall. */
    append_indent(source, depth);
    buffer_append(source, "(void)(");
    if (!emit_expression(program, node->left_child, source, errors)) {
        return false;
    }
    buffer_append(source, ");\n");
    return true;
}

static bool emit_stmt_block(const TypedProgram *program, GeneratorContext *gen, size_t node_index, size_t depth, const char *effective_source, TextBuffer *source, ErrorState *errors)
{
    return emit_block(program, gen, node_index, depth, effective_source,
                      source, errors);
}

static const struct statement_handler_row emit_statement_body_handlers2[] = {
    {TYPED_RETURN, emit_stmt_return},
    {TYPED_BREAK, emit_stmt_break},
    {TYPED_CONTINUE, emit_stmt_break},
    {TYPED_IF, emit_stmt_if},
    {TYPED_WHILE, emit_stmt_while},
    {TYPED_FOR, emit_for_statement},
    {TYPED_TABLE_SET, emit_stmt_table_set},
    {TYPED_RECORD_SET, emit_stmt_record_set},
    {TYPED_LOCAL_DECLARATION, emit_stmt_local_declaration},
    {TYPED_MEMBER_WRITE, emit_stmt_member_write},
    {TYPED_ASSIGNMENT, emit_stmt_assignment},
    {TYPED_LIST_SET, emit_stmt_list_set},
    {TYPED_PRINT, emit_stmt_print},
    {TYPED_EXPRESSION_STATEMENT, emit_stmt_expression_statement},
    {TYPED_BLOCK, emit_stmt_block},
    {TYPED_DEFER, emit_stmt_defer},
    {TYPED_PARALLEL, emit_parallel_statement},
    {TYPED_WHEN, emit_when_statement},
};

static bool emit_statement_body_return(const TypedProgram *program, const TypedNode *node, ErrorState *errors, TextBuffer *source, size_t depth, size_t node_index)
{
    char return_type[NYX_IDENTIFIER_MAX + 32];
    if (!format_c_type(program, node->type_index, return_type, sizeof(return_type), errors)) {
        return false;
    }
    append_indent(source, depth);
    buffer_append(source, "{\n");
    append_indent(source, depth + 1);
    buffer_appendf(source, "%s nyx_scope_result_%zu = ", return_type, node_index);
    if (!emit_expression(program, node->left_child, source, errors)) {
        return false;
    }
    buffer_append(source, ";\n");
    append_indent(source, depth + 1);
    buffer_append(source, "nyx_scope_cleanup(&nyx_scope_top, NULL);\n"
                           "    nyx_scope_release(&nyx_cache_top, NULL);\n");
    append_indent(source, depth + 1);
    buffer_appendf(source, "return nyx_scope_result_%zu;\n", node_index);
    append_indent(source, depth);
    buffer_append(source, "}\n");
    return true;
}

/* A return evaluates its value first, then leaves every block it is inside:
   their defers run and their Stores are released, innermost first, and only
   then does the function return the saved value. */
static bool emit_return_unwinding(const TypedProgram *program, GeneratorContext *gen,
                                  const TypedNode *node, size_t node_index,
                                  size_t depth, const char *effective_source,
                                  TextBuffer *source, ErrorState *errors)
{
    char return_type[NYX_IDENTIFIER_MAX + 32];

    if (!format_c_type(program, node->type_index, return_type,
                   sizeof(return_type), errors)) {
        return false;
    }
    append_indent(source, depth);
    buffer_append(source, "{\n");
    append_indent(source, depth + 1);
    buffer_appendf(source, "%s nyx_scope_result_%zu = ", return_type,
                    node_index);
    if (!emit_expression(program, node->left_child, source, errors)) {
        return false;
    }
    buffer_append(source, ";\n");
    if (!emit_unwind(program, gen, false, depth + 1, effective_source, source,
                 errors)) {
        return false;
    }
    if (analysis_has(gen->analysis, ANALYSIS_SCOPE) &&
        (!append_indent(source, depth + 1) ||
         !buffer_append(source, "nyx_scope_cleanup(&nyx_scope_top, NULL);\n"
                                "    nyx_scope_release(&nyx_cache_top, NULL);\n"))) {
        error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
        return false;
    }
    append_indent(source, depth + 1);
    buffer_appendf(source, "return nyx_scope_result_%zu;\n", node_index);
    append_indent(source, depth);
    buffer_append(source, "}\n");
    return true;
}

/* A defer emits nothing where it is written; its block is emitted at each exit
   of the block that holds it. It must be a statement of the block being
   emitted, or the exit code would silently omit it. */
static bool emit_stmt_defer(const TypedProgram *program, GeneratorContext *gen, size_t node_index, size_t depth, const char *effective_source, TextBuffer *source, ErrorState *errors)
{
    (void)gen;
    const BlockFrame *frame;
    const TypedNode *block;

    (void)depth;
    (void)effective_source;
    (void)source;
    if (gen->frame_count == 0) {
        error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_STATEMENT);
        return false;
    }
    frame = &gen->frames[gen->frame_count - 1];
    block = typed_program_node(program, frame->block);
    if (block == NULL || frame->position >= block->children_count ||
        block_child_at(program, block, frame->position) != node_index) {
        error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_STATEMENT);
        return false;
    }
    return true;
}

static bool emit_return_unwinding_statement(
    const TypedProgram *program, GeneratorContext *gen, size_t node_index,
    size_t depth, const char *effective_source, TextBuffer *source,
    ErrorState *errors)
{
    return emit_return_unwinding(program, gen,
                                 typed_program_node(program, node_index),
                                 node_index, depth, effective_source, source,
                                 errors);
}

static bool emit_scope_return_statement(
    const TypedProgram *program, GeneratorContext *gen, size_t node_index,
    size_t depth, const char *effective_source, TextBuffer *source,
    ErrorState *errors)
{
    (void)gen;
    (void)effective_source;
    return emit_statement_body_return(program,
                                      typed_program_node(program, node_index),
                                      errors, source, depth, node_index);
}

static EmitStatementBodyHandler find_statement_handler(
    const struct statement_handler_row *rows, size_t count, TypedNodeKind kind)
{
    size_t i;

    for (i = 0; i < count; i++) {
        if (rows[i].key == (int)kind) {
            return rows[i].handler;
        }
    }
    return NULL;
}

static bool emit_statement_body(const TypedProgram *program, GeneratorContext *gen, size_t node_index,
                                size_t depth, const char *effective_source,
                                TextBuffer *source, ErrorState *errors)
{
    const TypedNode *node = typed_program_node(program, node_index);
    EmitStatementBodyHandler handler;

    if (node == NULL) {
        error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_STATEMENT);
        return false;
    }
    buffer_appendf(source, "#line %zu \"%s\"\n", node->pos.line,
                   effective_source);
    handler = find_statement_handler(
        emit_statement_body_handlers1, NYX_COUNT_OF(emit_statement_body_handlers1),
        node->kind);
    if (handler == NULL && node->kind == TYPED_RETURN) {
        /* A return that leaves blocks with defers, or Stores to release, does
           more than return. */
        if (chain_has_defer(program, gen, false)) {
            handler = emit_return_unwinding_statement;
        } else if (analysis_has(gen->analysis, ANALYSIS_SCOPE)) {
            handler = emit_scope_return_statement;
        }
    }
    if (handler == NULL) {
        handler = find_statement_handler(
            emit_statement_body_handlers2,
            NYX_COUNT_OF(emit_statement_body_handlers2), node->kind);
    }
    if (handler == NULL) {
        error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_STATEMENT);
        return false;
    }
    return handler(program, gen, node_index, depth, effective_source, source,
                   errors);
}


/* The C name of a parameter: the receiver is always `nyx_self`. NULL when the
   node is not a parameter or its name has no C spelling. */
static const char *parameter_c_name(const TypedProgram *program,
                                    const TypedNode *parameter, char *text,
                                    size_t size)
{
    if (parameter->kind != TYPED_PARAMETER) {
        return NULL;
    }
    return parameter->is_self
               ? "nyx_self"
               : user_c_name(program, parameter->name_index, text, size);
}

static const TypedNode *function_parameter(const TypedProgram *program,
                                           const TypedNode *function, size_t i)
{
    return typed_program_node(
        program, *(const size_t *)list_get(&program->children,
                                           function->children_start + i));
}

/* The text of one parameter in a signature: the receiver of a method is a
   pointer, and every parameter after the first follows a comma. */
static void append_parameter(TextBuffer *source, size_t i, bool receiver,
                             const char *type, const char *name)
{
    buffer_appendf(source, "%s%s%s%s", i > 0 ? ", " : "", type,
                   receiver ? " *" : " ", name);
}

/* One parameter of a signature. */
static bool emit_parameter_declaration(const TypedProgram *program,
                                       const TypedNode *function, size_t i,
                                       TextBuffer *source, ErrorState *errors)
{
    const TypedNode *parameter = function_parameter(program, function, i);
    bool receiver = function->is_method && i == 0;
    char type[NYX_IDENTIFIER_MAX + 32];
    char name_text[USER_NAME_CAPACITY];
    const char *name = parameter_c_name(program, parameter, name_text,
                                        sizeof(name_text));

    if (parameter->kind != TYPED_PARAMETER || parameter->is_self != receiver) {
        error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_FUNCTION);
        return false;
    }
    if (!format_c_type(program, parameter->type_index, type, sizeof(type),
                       errors)) {
        return false;
    }
    if (name == NULL) {
        error_report(errors, MESSAGE_GENERATOR_INVALID_IDENTIFIER);
        return false;
    }
    append_parameter(source, i, receiver, type, name);
    return true;
}

/* The return type and parameters of a function, named `name`. */
static bool emit_function_signature(const TypedProgram *program,
                                    const TypedNode *node, const char *name,
                                    TextBuffer *source, ErrorState *errors)
{
    const TypedType *type = typed_program_type(program, node->type_index);
    const TypedFunctionSignature *signature =
        typed_program_signature(program, type->signature_index);
    char return_name[NYX_IDENTIFIER_MAX + 32];
    size_t i;

    if (!format_c_type(program, signature->return_type, return_name,
                       sizeof(return_name), errors)) {
        error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_FUNCTION);
        return false;
    }
    buffer_appendf(source, "%s %s(%s", return_name, name,
                   node->children_count == 0 ? "void" : "");
    for (i = 0; i < node->children_count; i++) {
        if (!emit_parameter_declaration(program, node, i, source, errors)) {
            return false;
        }
    }
    buffer_append(source, ")");
    return true;
}


/* The arguments the guarded entry point passes on to the inner function. */
static bool emit_function_call_args(const TypedProgram *program,
                                    const TypedNode *node, TextBuffer *source,
                                    ErrorState *errors)
{
    size_t i;

    for (i = 0; i < node->children_count; i++) {
        char text[USER_NAME_CAPACITY];
        const char *name = parameter_c_name(
            program, function_parameter(program, node, i), text, sizeof(text));

        if (name == NULL) {
            error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_FUNCTION);
            return false;
        }
        buffer_appendf(source, i > 0 ? ", %s" : "%s", name);
    }
    return true;
}


/* A parameter the body never reads is not a warning. */
static bool emit_function_unused_params(const TypedProgram *program,
                                        const TypedNode *node,
                                        TextBuffer *source)
{
    size_t i;

    for (i = 0; i < node->children_count; i++) {
        char text[USER_NAME_CAPACITY];
        const char *name = parameter_c_name(
            program, function_parameter(program, node, i), text, sizeof(text));

        if (name == NULL) {
            return false;
        }
        buffer_appendf(source, "    (void)%s;\n", name);
    }
    return true;
}


/* True when the function's body calls it by its own name, which is direct
   recursion and is what earns it a depth guard. */
static bool function_calls_itself(const TypedProgram *program,
                                  GeneratorContext *gen, size_t root_position,
                                  const TypedNode *function)
{
    size_t count, i;
    const size_t *calls = analysis_nodes_in(gen->analysis, TYPED_FUNCTION_CALL,
                                            root_position, &count);

    for (i = 0; i < count; i++) {
        const TypedNode *call = typed_program_node(program, calls[i]);

        if (call != NULL && call->name_index == function->name_index) {
            return true;
        }
    }
    return false;
}

/* The function's definition under `name`: its locals, then its block. */
static bool emit_function_definition(const TypedProgram *program,
                                     GeneratorContext *gen,
                                     const TypedNode *node, size_t root_position,
                                     const char *name,
                                     const char *effective_source,
                                     TextBuffer *source, ErrorState *errors)
{
    const TypedType *type = typed_program_type(program, node->type_index);
    NyxType result = typed_program_type(
                         program,
                         typed_program_signature(program, type->signature_index)
                             ->return_type)
                         ->kind;
    bool ok = emit_function_signature(program, node, name, source, errors);

    buffer_append(source, "\n{\n");
    ok = ok &&
         emit_match_temporaries(program, (size_t)node->integer_value_u, source,
                                errors) &&
         emit_scope_frame_declaration(program, gen, root_position, source) &&
         emit_function_unused_params(program, node, source) &&
         emit_block(program, gen, node->left_child, 1, effective_source, source,
                    errors);
    if (result == NYX_TYPE_NIL) {
        buffer_append(source, "    return (nyx_nil)0;\n");
    }
    buffer_append(source, "}\n\n");
    return ok;
}

/* A directly recursive function is entered through a wrapper that counts the
   depth and stops a runaway recursion with a diagnostic. */
static bool emit_guarded_function(const TypedProgram *program,
                                  GeneratorContext *gen, const TypedNode *node,
                                  size_t root_position, const char *name,
                                  const char *effective_source,
                                  TextBuffer *source, ErrorState *errors)
{
    const TypedType *type = typed_program_type(program, node->type_index);
    char inner_name[NYX_IDENTIFIER_MAX + 32];
    char depth_name[NYX_IDENTIFIER_MAX + 32];
    char return_name[NYX_IDENTIFIER_MAX + 32];
    bool ok;

    if (!format_c_type(program,
                       typed_program_signature(program, type->signature_index)
                           ->return_type,
                       return_name, sizeof(return_name), errors)) {
        error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_FUNCTION);
        return false;
    }
    snprintf(inner_name, sizeof(inner_name), "nyx_inner_%s", name);
    snprintf(depth_name, sizeof(depth_name), "nyx_depth_%s", name);
    buffer_appendf(source, "static %ssize_t %s = 0;\n\n",
                   analysis_has(gen->analysis, ANALYSIS_PARALLEL)
                       ? "_Thread_local "
                       : "",
                   depth_name);
    ok = emit_function_signature(program, node, inner_name, source, errors);
    buffer_append(source, ";\n\n");
    ok = ok && emit_function_signature(program, node, name, source, errors);
    buffer_appendf(source,
                   "\n{\n    if (++%s > NYX_RECURSION_LIMIT) {\n        ",
                   depth_name);
    ok = ok && emit_inline_failure(source, node->pos, MESSAGE_RUNTIME_RECURSION);
    buffer_appendf(source,
                   "\n    }\n    %s nyx_recursion_result = %s(", return_name,
                   inner_name);
    ok = ok && emit_function_call_args(program, node, source, errors);
    buffer_appendf(source,
                   ");\n    %s--;\n    return nyx_recursion_result;\n}\n\n",
                   depth_name);
    return ok && emit_function_definition(program, gen, node, root_position,
                                          inner_name, effective_source, source,
                                          errors);
}

bool emit_function(const TypedProgram *program, GeneratorContext *gen, const TypedNode *node,
                          size_t root_position, const char *effective_source, TextBuffer *source,
                          ErrorState *errors)
{
    char name_text[USER_NAME_CAPACITY];
    const char *name = checked_user_name(program, node->name_index, name_text,
                                         sizeof(name_text), errors);
    const TypedType *type = typed_program_type(program, node->type_index);

    if (name == NULL) {
        return false;
    }
    if (type == NULL || type->kind != NYX_TYPE_FUNCTION) {
        error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_FUNCTION);
        return false;
    }
    if (!emit_parallel_trampolines(program, gen, root_position, source, errors)) {
        return false;
    }
    buffer_appendf(source, "#line %zu \"%s\"\n", node->pos.line,
                   effective_source);
    if (!function_calls_itself(program, gen, root_position, node)) {
        return emit_function_definition(program, gen, node, root_position, name,
                                        effective_source, source, errors);
    }
    return emit_guarded_function(program, gen, node, root_position, name,
                                 effective_source, source, errors);
}

void generator_context_init(GeneratorContext *gen, const Analysis *analysis,
                            const char *source_name, const char *header_name)
{
    gen->analysis = analysis;
    gen->source_name = source_name;
    gen->header_name = header_name;
    gen->program_value = NULL;
    gen->frame_count = 0;
    gen->scope_loop_depth = 0;
}
