#include "checker_internal.h"


/* A string literal prints as Text; anything else is left as it is. */
static bool coerce_text_literal(CheckContext *ctx, size_t *argument)
{
    const TypedNode *node = typed_program_node(ctx->program, *argument);
    size_t text_type;
    size_t coerced;

    if (node == NULL || node->kind != TYPED_STRING_LITERAL) {
        return true;
    }
    if (!builtin_type(ctx, NYX_TYPE_DYNAMIC_STRING, &text_type) ||
        !inject_into(ctx, *argument, text_type, &coerced)) {
        return false;
    }
    *argument = coerced;
    return true;
}

/* One argument of print: a checked value that can be printed, with a string
   literal given the Text type. */
static bool check_print_argument(CheckContext *ctx, const AstNode *node,
                                 size_t argument_ast, size_t *argument)
{
    const AstNode *syntax = ast_get_node(ctx->ast, argument_ast);
    const TypedNode *typed;

    if (syntax == NULL || syntax->kind == AST_NAMED_ARG) {
        error_report_at(ctx->errors, node->pos, MESSAGE_CHECKER_PRINT_NAMED);
        return false;
    }
    if (!check_expression(ctx, argument_ast, argument) ||
        !check_default_numeric(ctx, *argument)) {
        return false;
    }
    if (!coerce_text_literal(ctx, argument)) {
        return false;
    }
    typed = typed_program_node(ctx->program, *argument);
    if (typed == NULL || !is_printable_type(ctx->program, typed->type_index)) {
        error_report_at(ctx->errors, node->pos, MESSAGE_CHECKER_PRINT_STATEMENT);
        return false;
    }
    return true;
}

/* print is a statement: it takes values, not a type argument, and yields
   nothing. */
bool check_print(CheckContext *ctx, const AstNode *node, size_t ast_index)
{
    List arguments;
    size_t nil_type;
    size_t argument;
    size_t i;
    bool ok = true;
    TypedNode typed;

    if (!ctx->in_expression_statement || node->right_child != AST_NO_NODE) {
        error_report_at(ctx->errors, node->pos, MESSAGE_CHECKER_PRINT_STATEMENT);
        return false;
    }
    if (!builtin_type(ctx, NYX_TYPE_NIL, &nil_type)) {
        return false;
    }
    list_init(&arguments, sizeof(size_t));
    for (i = 0; ok && i < node->children_count; i++) {
        ok = check_print_argument(
            ctx,
            node,
            *(const size_t *)list_get(&ctx->ast->children,
                                      node->children_start + i),
            &argument);
        if (ok) {
            checker_ok(ctx->errors, list_push(&arguments, &argument),
                       MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
        }
    }
    typed = new_node(TYPED_PRINT, nil_type, node->pos, TYPED_NO_NODE,
                     TYPED_NO_NODE);
    typed.children_start = ok ? push_typed_children(ctx, &arguments) : 0;
    typed.children_count = arguments.length;
    list_free(&arguments);
    return ok && emit_node(ctx, &typed, ast_index, &(size_t){0});
}
