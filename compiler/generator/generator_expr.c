#include "generator_internal.h"


bool emit_children_expressions(const TypedProgram *program,
                                      const TypedNode *node,
                                      TextBuffer *buffer, ErrorState *errors)
{
    size_t i;

    for (i = 0; i < node->children_count; i++) {
        size_t child = *(const size_t *)list_get(&program->children,
                                                 node->children_start + i);

        if (i > 0 && !buffer_append(buffer, ", ")) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            return false;
        }
        if (!emit_expression(program, child, buffer, errors)) {
            return false;
        }
    }
    return true;
}


const char *binary_operator_text(NippyOperator op)
{
    static const struct {
        NippyOperator op;
        const char *text;
    } spellings[] = {
        {NIPPY_OPERATOR_EQ, "=="}, {NIPPY_OPERATOR_STRICT_EQ, "=="},
        {NIPPY_OPERATOR_NE, "!="}, {NIPPY_OPERATOR_LT, "<"},
        {NIPPY_OPERATOR_LE, "<="}, {NIPPY_OPERATOR_GT, ">"},
        {NIPPY_OPERATOR_GE, ">="}, {NIPPY_OPERATOR_ADD, "+"},
        {NIPPY_OPERATOR_SUB, "-"}, {NIPPY_OPERATOR_MUL, "*"},
        {NIPPY_OPERATOR_DIV, "/"}, {NIPPY_OPERATOR_MOD, "%"},
        {NIPPY_OPERATOR_BIT_AND, "&"}, {NIPPY_OPERATOR_BIT_OR, "|"},
        {NIPPY_OPERATOR_BIT_XOR, "^"}, {NIPPY_OPERATOR_SHL, "<<"},
        {NIPPY_OPERATOR_SHR, ">>"}, {NIPPY_OPERATOR_NEG, "-"},
        {NIPPY_OPERATOR_BIT_NOT, "~"}, {NIPPY_OPERATOR_AND, "&&"},
        {NIPPY_OPERATOR_OR, "||"},
    };
    size_t i;

    for (i = 0; i < NIPPY_COUNT_OF(spellings); i++) {
        if (spellings[i].op == op) {
            return spellings[i].text;
        }
    }
    return NULL;
}

bool emit_expression(const TypedProgram *program, size_t node_index, TextBuffer *buffer, ErrorState *errors);

static bool emit_expression_node_a(const TypedProgram *program, const TypedNode **node, TextBuffer *buffer, ErrorState *errors, bool *outcome)
{
    size_t a = *(const size_t *)list_get(&program->children, (*node)->children_start);
    size_t b = *(const size_t *)list_get(&program->children, (*node)->children_start + 1);
    if (!buffer_appendf(buffer, "((nyx_range_%zu){ ", (*node)->type_index) ||
        !emit_expression(program, a, buffer, errors) ||
        !buffer_append(buffer, ", ") ||
        !emit_expression(program, b, buffer, errors) ||
        !buffer_append(buffer, ", ")) do { *outcome = (false); return false; } while (0);
    if ((*node)->children_count == 3) {
        size_t step = *(const size_t *)list_get(&program->children,
                                                 (*node)->children_start + 2);
        if (!buffer_append(buffer, "nyx_range_step(") ||
            !emit_expression(program, step, buffer, errors) ||
            !buffer_appendf(buffer, ", __FILE__, %zu, %zu, %zu)",
                            (*node)->pos.line, (*node)->pos.column, (*node)->pos.offset))
            do { *outcome = (false); return false; } while (0);
    } else if (!buffer_append(buffer, "INT64_C(1)")) do { *outcome = (false); return false; } while (0);
    return true;
}

static bool emit_expression_node(const TypedProgram *program, size_t node_index, ErrorState *errors, TextBuffer *buffer, const TypedNode **node, bool *handled, bool *outcome)
{
    (*node) = typed_program_node(program, node_index);
    (*handled) = false;

    if ((*node) == NULL) {
        error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_EXPRESSION);
        do { *outcome = (false); return true; } while (0);
    }
    if ((*node)->kind == TYPED_MATCH_TEMPORARY) {
        if (!buffer_appendf(buffer, "nyx_match_%zu",
                            (size_t)(*node)->integer_value_u)) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            do { *outcome = (false); return true; } while (0);
        }
        do { *outcome = (true); return true; } while (0);
    }
    if ((*node)->kind == TYPED_RANGE_CREATE) {
        if (!emit_expression_node_a(program, node, buffer, errors, outcome)) {
        return true;
    }
        do { *outcome = (buffer_append(buffer, " })")); return true; } while (0);
    }
    return false;
}

bool emit_expression(const TypedProgram *program, size_t node_index, TextBuffer *buffer, ErrorState *errors);

static bool emit_expression_buffer_appendf_buffer_appendf(const TypedNode *node, TextBuffer *buffer, const TypedProgram *program, ErrorState *errors, bool *outcome2, bool *handled)
{
    if (node->kind == TYPED_MATCH_BIND_EXPRESSION) {
        if (!buffer_appendf(buffer, "(nyx_match_%zu = ",
                            (size_t)node->integer_value_u) ||
            !emit_expression(program, node->left_child, buffer, errors) ||
            !buffer_append(buffer, ", ") ||
            !emit_expression(program, node->right_child, buffer, errors) ||
            !buffer_append(buffer, ")")) {
            if (!errors->reported) error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            do { *outcome2 = (false); return false; } while (0);
        }
        do { *outcome2 = (true); return false; } while (0);
    }
    if (!emit_expr_literals(program, node, buffer, errors, handled)) {
        do { *outcome2 = (false); return false; } while (0);
    }
    if ((*handled)) {
        do { *outcome2 = (true); return false; } while (0);
    }
    return true;
}

static bool emit_expression_buffer_appendf(const TypedNode *node, TextBuffer *buffer, const TypedProgram *program, ErrorState *errors, bool *handled, size_t node_index, bool *outcome2)
{
    if (!emit_expression_buffer_appendf_buffer_appendf(node, buffer, program, errors, outcome2, handled)) {
        return true;
    }
    if (!emit_expr_strings(program, node, node_index, buffer, errors, handled)) {
        do { *outcome2 = (false); return true; } while (0);
    }
    if ((*handled)) {
        do { *outcome2 = (true); return true; } while (0);
    }
    if (!emit_expr_collections(program, node, node_index, buffer, errors, handled)) {
        do { *outcome2 = (false); return true; } while (0);
    }
    if ((*handled)) {
        do { *outcome2 = (true); return true; } while (0);
    }
    if (!emit_expr_access(program, node, buffer, errors, handled)) {
        do { *outcome2 = (false); return true; } while (0);
    }
    return false;
}

bool emit_expression(const TypedProgram *program, size_t node_index,
                     TextBuffer *buffer, ErrorState *errors)
{
    const TypedNode *node = {0};
    bool handled = {0};
    {
        bool outcome;

        if (emit_expression_node(program, node_index, errors, buffer, &node, &handled, &outcome)) {
            return outcome;
        }
    }
    {
        bool outcome2;

        if (emit_expression_buffer_appendf(node, buffer, program, errors, &handled, node_index, &outcome2)) {
            return outcome2;
        }
    }
    if (handled) {
        return true;
    }
    if (!emit_expr_operators(program, node, buffer, errors, &handled)) {
        return false;
    }
    if (handled) {
        return true;
    }
    error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_EXPRESSION);
    return false;
}
