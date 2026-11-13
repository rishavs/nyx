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
