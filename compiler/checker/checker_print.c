#include "checker_internal.h"


bool is_printable_type_depth(const TypedProgram *program,
                                    size_t type_index, unsigned depth)
{
    const TypedType *type = typed_program_type(program, type_index);
    size_t i;

    if (type == NULL || depth == 0) {
        return false;
    }
    switch (type->kind) {
    case NIPPY_TYPE_INT8:
    case NIPPY_TYPE_INT16:
    case NIPPY_TYPE_INT32:
    case NIPPY_TYPE_INT64:
    case NIPPY_TYPE_BYTE:
    case NIPPY_TYPE_UINT16:
    case NIPPY_TYPE_UINT32:
    case NIPPY_TYPE_UINT64:
    case NIPPY_TYPE_FLOAT32:
    case NIPPY_TYPE_FLOAT64:
    case NIPPY_TYPE_BOOL:
    case NIPPY_TYPE_NIL:
    case NIPPY_TYPE_STRING:
    case NIPPY_TYPE_DYNAMIC_STRING:
    case NIPPY_TYPE_TEXT_BUILDER:
    case NIPPY_TYPE_ERROR:
    case NIPPY_TYPE_FUNCTION:
    case NIPPY_TYPE_HANDLE:
        return true;
    case NIPPY_TYPE_RECORD:
    case NIPPY_TYPE_TABLE:
        if (type->child_count != 2) {
            return false;
        }
        for (i = 0; i < 2; i++) {
            size_t part = *(const size_t *)list_get(
                &program->type_children, type->first_child + i);

            if (!is_printable_type_depth(program, part, depth - 1)) {
                return false;
            }
        }
        return true;
    case NIPPY_TYPE_FIXED_LIST:
    case NIPPY_TYPE_LIST:
    case NIPPY_TYPE_SLICE:
        if (type->child_count != 1) {
            return false;
        }
        return is_printable_type_depth(
            program, *(const size_t *)list_get(&program->type_children,
                                               type->first_child),
            depth - 1);
    case NIPPY_TYPE_UNION:
        for (i = 0; i < type->child_count; i++) {
            size_t alternative = *(const size_t *)list_get(
                &program->type_children, type->first_child + i);

            if (!is_printable_type_depth(program, alternative, depth - 1)) {
                return false;
            }
        }
        return true;
    case NIPPY_TYPE_STRUCT: {
        const TypedNode *declaration = typed_program_node(
            program, type->declaration_node);

        if (declaration == NULL) {
            return false;
        }
        for (i = 0; i < declaration->children_count; i++) {
            size_t child = *(const size_t *)list_get(
                &program->children, declaration->children_start + i);
            const TypedNode *field = typed_program_node(program, child);

            if (field == NULL ||
                !is_printable_type_depth(program, field->type_index,
                                         depth - 1)) {
                return false;
            }
        }
        return true;
    }
    default:
        return false;
    }
}


bool is_printable_type(const TypedProgram *program, size_t type_index)
{
    return is_printable_type_depth(program, type_index, NIPPY_TYPE_DEPTH);
}


bool check_print(CheckContext *ctx, const AstNode *node,
                        size_t ast_index)
{
    List children;
    size_t nil_type;
    TypedNode typed = {0};
    size_t i;

    if (!ctx->in_expression_statement) {
        error_report_at(ctx->errors, node->pos,
                        MESSAGE_CHECKER_PRINT_STATEMENT);
        return false;
    }
    if (node->right_child != AST_NO_NODE) {
        error_report_at(ctx->errors, node->pos,
                        MESSAGE_CHECKER_PRINT_STATEMENT);
        return false;
    }
    if (!builtin_type(ctx, NIPPY_TYPE_NIL, &nil_type)) {
        return false;
    }
    list_init(&children, sizeof(size_t));
    for (i = 0; i < node->children_count; i++) {
        size_t argument_ast = *(const size_t *)list_get(
            &ctx->ast->children, node->children_start + i);
        const AstNode *argument_node = ast_get_node(ctx->ast, argument_ast);
        size_t argument;
        const TypedNode *argument_typed;
        size_t text_type;

        if (argument_node == NULL || argument_node->kind == AST_NAMED_ARG) {
            error_report_at(ctx->errors, node->pos,
                            MESSAGE_CHECKER_PRINT_NAMED);
            list_free(&children);
            return false;
        }
        if (!check_expression(ctx, argument_ast, &argument)) {
            list_free(&children);
            return false;
        }
        argument_typed = typed_program_node(ctx->program, argument);
        if (argument_typed == NULL) {
            error_report_at(ctx->errors, node->pos,
                            MESSAGE_CHECKER_MISSING_AST_NODE, argument_ast);
            list_free(&children);
            return false;
        }
        if (argument_typed->kind == TYPED_STRING_LITERAL) {
            size_t coerced;

            if (!builtin_type(ctx, NIPPY_TYPE_DYNAMIC_STRING, &text_type) ||
                !inject_into(ctx, argument, text_type, &coerced)) {
                list_free(&children);
                return false;
            }
            argument = coerced;
            argument_typed = typed_program_node(ctx->program, argument);
        }
        if (argument_typed == NULL ||
            !is_printable_type(ctx->program, argument_typed->type_index)) {
            error_report_at(ctx->errors, node->pos,
                            MESSAGE_CHECKER_PRINT_STATEMENT);
            list_free(&children);
            return false;
        }
        if (!list_push(&children, &argument)) {
            error_report(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
            list_free(&children);
            return false;
        }
    }
    {
        size_t start = ctx->program->children.length;

        for (i = 0; i < children.length; i++) {
            size_t child = *(const size_t *)list_get(&children, i);

            if (!typed_program_push_child(ctx->program, child)) {
                error_report(ctx->errors,
                             MESSAGE_CHECKER_OUT_OF_MEMORY_PROGRAM);
                list_free(&children);
                return false;
            }
        }
        typed.kind = TYPED_PRINT;
        typed.type_index = nil_type;
        typed.pos = node->pos;
        typed.name_index = TYPED_NO_NODE;
        typed.left_child = TYPED_NO_NODE;
        typed.right_child = TYPED_NO_NODE;
        typed.children_start = start;
        typed.children_count = children.length;
    }
    list_free(&children);
    return emit_node(ctx, &typed, ast_index, &(size_t){0});
}

