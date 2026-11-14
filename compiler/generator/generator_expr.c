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
    switch (op) {
    case NIPPY_OPERATOR_EQ:
    case NIPPY_OPERATOR_STRICT_EQ:
        return "==";
    case NIPPY_OPERATOR_NE:
        return "!=";
    case NIPPY_OPERATOR_LT:
        return "<";
    case NIPPY_OPERATOR_LE:
        return "<=";
    case NIPPY_OPERATOR_GT:
        return ">";
    case NIPPY_OPERATOR_GE:
        return ">=";
    case NIPPY_OPERATOR_ADD:
        return "+";
    case NIPPY_OPERATOR_SUB:
        return "-";
    case NIPPY_OPERATOR_MUL:
        return "*";
    case NIPPY_OPERATOR_DIV:
        return "/";
    case NIPPY_OPERATOR_MOD:
        return "%";
    case NIPPY_OPERATOR_BIT_AND:
        return "&";
    case NIPPY_OPERATOR_BIT_OR:
        return "|";
    case NIPPY_OPERATOR_BIT_XOR:
        return "^";
    case NIPPY_OPERATOR_SHL:
        return "<<";
    case NIPPY_OPERATOR_SHR:
        return ">>";
    case NIPPY_OPERATOR_NEG:
        return "-";
    case NIPPY_OPERATOR_BIT_NOT:
        return "~";
    case NIPPY_OPERATOR_AND:
        return "&&";
    case NIPPY_OPERATOR_OR:
        return "||";
    default:
        return NULL;
    }
}

bool emit_expression(const TypedProgram *program, size_t node_index,
                     TextBuffer *buffer, ErrorState *errors)
{
    const TypedNode *node = typed_program_node(program, node_index);
    bool handled = false;

    if (node == NULL) {
        error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_EXPRESSION);
        return false;
    }
    if (node->kind == TYPED_MATCH_TEMPORARY) {
        if (!buffer_appendf(buffer, "nyx_match_%zu",
                            (size_t)node->integer_value_u)) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            return false;
        }
        return true;
    }
    if (node->kind == TYPED_RANGE_CREATE) {
        size_t a = *(const size_t *)list_get(&program->children, node->children_start);
        size_t b = *(const size_t *)list_get(&program->children, node->children_start + 1);
        if (!buffer_appendf(buffer, "((nyx_range_%zu){ ", node->type_index) ||
            !emit_expression(program, a, buffer, errors) ||
            !buffer_append(buffer, ", ") ||
            !emit_expression(program, b, buffer, errors) ||
            !buffer_append(buffer, ", ")) return false;
        if (node->children_count == 3) {
            size_t step = *(const size_t *)list_get(&program->children,
                                                     node->children_start + 2);
            if (!buffer_append(buffer, "nyx_range_step(") ||
                !emit_expression(program, step, buffer, errors) ||
                !buffer_appendf(buffer, ", __FILE__, %zu, %zu, %zu)",
                                node->pos.line, node->pos.column, node->pos.offset))
                return false;
        } else if (!buffer_append(buffer, "INT64_C(1)")) return false;
        return buffer_append(buffer, " })");
    }
    if (node->kind == TYPED_MATCH_BIND_EXPRESSION) {
        if (!buffer_appendf(buffer, "(nyx_match_%zu = ",
                            (size_t)node->integer_value_u) ||
            !emit_expression(program, node->left_child, buffer, errors) ||
            !buffer_append(buffer, ", ") ||
            !emit_expression(program, node->right_child, buffer, errors) ||
            !buffer_append(buffer, ")")) {
            if (!errors->reported) error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            return false;
        }
        return true;
    }
    if (!emit_expr_literals(program, node, buffer, errors, &handled)) {
        return false;
    }
    if (handled) {
        return true;
    }
    if (!emit_expr_strings(program, node, node_index, buffer, errors, &handled)) {
        return false;
    }
    if (handled) {
        return true;
    }
    if (!emit_expr_collections(program, node, node_index, buffer, errors, &handled)) {
        return false;
    }
    if (handled) {
        return true;
    }
    if (!emit_expr_access(program, node, buffer, errors, &handled)) {
        return false;
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
