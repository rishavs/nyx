#include "checker_internal.h"


bool eval_const_int(CheckContext *ctx, size_t ast_index, int *out)
{
    const AstNode *node = ast_get_node(ctx->ast, ast_index);

    if (node == NULL) {
        error_report(ctx->errors, MESSAGE_CHECKER_MISSING_AST_NODE, ast_index);
        return false;
    }
    if (node->kind == AST_INTEGER_LITERAL) {
        *out = node->integer_value;
        return true;
    }
    if (node->kind == AST_BINARY && node->op == NIPPY_OPERATOR_ADD) {
        int left;
        int right;
        long long sum;

        if (!eval_const_int(ctx, node->left_child, &left) ||
            !eval_const_int(ctx, node->right_child, &right)) {
            return false;
        }
        sum = (long long)left + (long long)right;
        if (sum < 0 || sum > 2147483647LL) {
            error_report_at(ctx->errors, node->pos,
                            MESSAGE_CHECKER_STORE_SIZE);
            return false;
        }
        *out = (int)sum;
        return true;
    }
    error_report_at(ctx->errors, node->pos, MESSAGE_CHECKER_STORE_SIZE);
    return false;
}


bool check_store_declaration(CheckContext *ctx, const AstNode *node,
                                    const char *name, size_t ast_index,
                                    size_t store_type)
{
    const AstNode *initialiser = ast_get_node(ctx->ast, node->left_child);
    const char *constructor;
    size_t size_ast;
    int size;
    StoreEntry entry;
    TypedNode typed = {0};

    if (initialiser == NULL || initialiser->kind != AST_CALL) {
        error_report_at(ctx->errors, node->pos,
                        MESSAGE_CHECKER_STORE_INITIALIZER, name);
        return false;
    }
    constructor = ast_symbol_text(ctx->ast, initialiser->name_index);
    if (constructor == NULL || strcmp(constructor, "Store") != 0 ||
        initialiser->children_count != 1) {
        error_report_at(ctx->errors, node->pos,
                        MESSAGE_CHECKER_STORE_INITIALIZER, name);
        return false;
    }
    size_ast = *(const size_t *)list_get(&ctx->ast->children,
                                         initialiser->children_start);
    {
        const AstNode *argument = ast_get_node(ctx->ast, size_ast);

        if (argument == NULL || argument->kind == AST_NAMED_ARG) {
            error_report_at(ctx->errors, node->pos,
                            MESSAGE_CHECKER_STORE_INITIALIZER, name);
            return false;
        }
    }
    if (!eval_const_int(ctx, size_ast, &size)) {
        return false;
    }
    if (find_store(ctx->stores, name) != NULL) {
        error_report_at(ctx->errors, node->pos,
                        MESSAGE_CHECKER_DUPLICATE_NAME, name);
        return false;
    }

    memset(&entry, 0, sizeof(entry));
    memcpy(entry.name, name, strlen(name) + 1);
    entry.id = ctx->stores->length;
    entry.initial_size = size;
    if (!list_push(ctx->stores, &entry)) {
        error_report(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
        return false;
    }

    typed.kind = TYPED_STORE_DECLARATION;
    typed.type_index = store_type;
    typed.pos = node->pos;
    typed.name_index = typed_program_copy_symbol(ctx->program, name,
                                                 strlen(name));
    if (typed.name_index == TYPED_NO_NODE) {
        error_report(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_COPYING_NAME);
        return false;
    }
    typed.store_id = entry.id;
    typed.integer_value = size;
    typed.left_child = TYPED_NO_NODE;
    typed.right_child = TYPED_NO_NODE;
    typed.children_start = 0;
    typed.children_count = 0;
    return emit_node(ctx, &typed, ast_index, &(size_t){0});
}


bool check_var_declaration(CheckContext *ctx, const AstNode *node,
                                  size_t ast_index)
{
    const char *name = ast_symbol_text(ctx->ast, node->name_index);
    size_t declared_type = TYPED_NO_NODE;
    size_t initialiser;
    const TypedNode *initialiser_node;
    size_t variable_type;
    TypedNode typed = {0};

    if (name == NULL) {
        error_report_at(ctx->errors, node->pos,
                        MESSAGE_CHECKER_MISSING_NAME_OR_TYPE);
        return false;
    }
    if (!check_reserved_name(ctx, node->pos, name)) {
        return false;
    }
    if (find_variable(ctx->globals, name) != NULL ||
        find_function(ctx->functions, name) != NULL ||
        find_type(ctx->types, name) != NULL ||
        find_store(ctx->stores, name) != NULL ||
        strcmp(name, "Int32") == 0 || strcmp(name, "Bool") == 0 ||
        strcmp(name, "Nil") == 0 || strcmp(name, "Byte") == 0 ||
        strcmp(name, "Store") == 0 || strcmp(name, "Handle") == 0 ||
        strcmp(name, "String") == 0 ||
        strcmp(name, "Text") == 0 ||
        strcmp(name, "TextBuilder") == 0 ||
        strcmp(name, "List") == 0 ||
        strcmp(name, "Array") == 0 ||
        strcmp(name, "Slice") == 0 ||
        strcmp(name, "Error") == 0 ||
        strcmp(name, "print") == 0) {
        error_report_at(ctx->errors, node->pos, MESSAGE_CHECKER_DUPLICATE_NAME,
                        name);
        return false;
    }
    if (node->left_child == AST_NO_NODE) {
        error_report_at(ctx->errors, node->pos, MESSAGE_CHECKER_NO_INITIALISER,
                        name);
        return false;
    }

    if (node->right_child != AST_NO_NODE &&
        !resolve_type_node(ctx, node->right_child, name, &declared_type)) {
        return false;
    }
    if (declared_type != TYPED_NO_NODE) {
        const TypedType *annotated =
            typed_program_type(ctx->program, declared_type);

        if (node->is_global &&
            (annotated == NULL || annotated->kind != NIPPY_TYPE_STORE)) {
            error_report_at(ctx->errors, node->pos,
                            MESSAGE_CHECKER_GLOBAL_DECLARATION, name);
            return false;
        }
        if (annotated != NULL && annotated->kind == NIPPY_TYPE_STORE) {
            if (node->is_const) {
                error_report_at(ctx->errors, node->pos,
                                MESSAGE_CHECKER_CONST_STORE);
                return false;
            }
            return check_store_declaration(ctx, node, name, ast_index,
                                           declared_type);
        }
    } else if (node->is_global) {
        error_report_at(ctx->errors, node->pos,
                        MESSAGE_CHECKER_GLOBAL_DECLARATION, name);
        return false;
    }
    if (!check_expression(ctx, node->left_child, &initialiser)) {
        return false;
    }
    initialiser_node = typed_program_node(ctx->program, initialiser);
    if (initialiser_node == NULL) {
        error_report_at(ctx->errors, node->pos,
                        MESSAGE_CHECKER_MISSING_AST_NODE, node->left_child);
        return false;
    }

    if (declared_type == TYPED_NO_NODE) {
        const TypedNode *inferred = typed_program_node(ctx->program,
                                                       initialiser);

        if (inferred != NULL) {
            const TypedType *inferred_type = typed_program_type(
                ctx->program, inferred->type_index);

            if (inferred->kind == TYPED_LIST_LITERAL &&
                inferred->type_index == TYPED_NO_NODE) {
                error_report_at(ctx->errors, node->pos,
                                MESSAGE_CHECKER_LIST_INFER);
                return false;
            }
            if (inferred->kind == TYPED_RECORD_LITERAL &&
                inferred->type_index == TYPED_NO_NODE) {
                error_report_at(ctx->errors, node->pos,
                                MESSAGE_CHECKER_RECORD_LITERAL_CONTEXT);
                return false;
            }
            if (inferred_type != NULL &&
                inferred_type->kind == NIPPY_TYPE_STORE) {
                error_report_at(ctx->errors, node->pos,
                                MESSAGE_CHECKER_STORE_INITIALIZER, name);
                return false;
            }
        }
    }

    if (declared_type != TYPED_NO_NODE) {
        size_t coerced;

        if (!inject_into(ctx, initialiser, declared_type, &coerced)) {
            char declared_text[128];

            type_ast_text(ctx->ast, node->right_child, declared_text,
                          sizeof(declared_text));
            error_report_at(ctx->errors, node->pos,
                            MESSAGE_CHECKER_INITIALISER_TYPE_MISMATCH, name,
                            declared_text);
            return false;
        }
        initialiser = coerced;
        variable_type = declared_type;
    } else {
        const TypedType *inferred_type = typed_program_type(
            ctx->program, initialiser_node->type_index);

        if (inferred_type != NULL &&
            inferred_type->kind == NIPPY_TYPE_STRING_LITERAL) {
            error_report_at(ctx->errors, node->pos,
                            MESSAGE_CHECKER_STRING_LITERAL_CONTEXT);
            return false;
        }
        variable_type = initialiser_node->type_index;
    }

    typed.kind = TYPED_DECLARATION;
    typed.type_index = variable_type;
    typed.pos = node->pos;
    typed.name_index = typed_program_copy_symbol(ctx->program, name,
                                                 strlen(name));
    if (typed.name_index == TYPED_NO_NODE) {
        error_report(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_COPYING_NAME);
        return false;
    }
    typed.left_child = initialiser;
    typed.right_child = TYPED_NO_NODE;
    typed.children_start = 0;
    typed.children_count = 0;

    if (!emit_node(ctx, &typed, ast_index, &(size_t){0})) {
        return false;
    }

    {
        VariableEntry entry;

        memset(&entry, 0, sizeof(entry));
        memcpy(entry.name, name, strlen(name) + 1);
        entry.type_index = variable_type;
        entry.is_const = node->is_const;
        if (!list_push(ctx->globals, &entry)) {
            error_report(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
            return false;
        }
    }
    return true;
}


bool check_type_declaration(CheckContext *ctx, const AstNode *node,
                                   size_t ast_index)
{
    const char *name = ast_symbol_text(ctx->ast, node->name_index);
    TypeEntry entry = {0};
    size_t entry_index;
    size_t i;

    if (name == NULL) {
        error_report_at(ctx->errors, node->pos,
                        MESSAGE_CHECKER_MISSING_NAME_OR_TYPE);
        return false;
    }
    if (!check_reserved_name(ctx, node->pos, name)) {
        return false;
    }
    if (strcmp(name, "Int32") == 0) {
        error_report_at(ctx->errors, node->pos,
                        MESSAGE_CHECKER_REDEFINES_INT32);
        return false;
    }
    if (strcmp(name, "Bool") == 0) {
        error_report_at(ctx->errors, node->pos,
                        MESSAGE_CHECKER_REDEFINES_BOOL);
        return false;
    }
    if (strcmp(name, "Nil") == 0) {
        error_report_at(ctx->errors, node->pos,
                        MESSAGE_CHECKER_REDEFINES_NIL);
        return false;
    }
    if (strcmp(name, "String") == 0) {
        error_report_at(ctx->errors, node->pos,
                        MESSAGE_CHECKER_REDEFINES_STRING);
        return false;
    }
    if (strcmp(name, "Text") == 0) {
        error_report_at(ctx->errors, node->pos,
                        MESSAGE_CHECKER_REDEFINES_TEXT);
        return false;
    }
    if (strcmp(name, "TextBuilder") == 0) {
        error_report_at(ctx->errors, node->pos,
                        MESSAGE_CHECKER_REDEFINES_BUILDER);
        return false;
    }
    if (strcmp(name, "List") == 0) {
        error_report_at(ctx->errors, node->pos,
                        MESSAGE_CHECKER_REDEFINES_LIST);
        return false;
    }
    if (strcmp(name, "Array") == 0) {
        error_report_at(ctx->errors, node->pos,
                        MESSAGE_CHECKER_REDEFINES_ARRAY);
        return false;
    }
    if (strcmp(name, "Slice") == 0) {
        error_report_at(ctx->errors, node->pos,
                        MESSAGE_CHECKER_REDEFINES_SLICE);
        return false;
    }
    if (strcmp(name, "Error") == 0) {
        error_report_at(ctx->errors, node->pos,
                        MESSAGE_CHECKER_REDEFINES_ERROR);
        return false;
    }
    if (strcmp(name, "Record") == 0) {
        error_report_at(ctx->errors, node->pos,
                        MESSAGE_CHECKER_REDEFINES_RECORD);
        return false;
    }
    if (strcmp(name, "Table") == 0) {
        error_report_at(ctx->errors, node->pos,
                        MESSAGE_CHECKER_REDEFINES_TABLE);
        return false;
    }
    if (find_type_index(ctx->types, name) != TYPED_NO_NODE) {
        error_report_at(ctx->errors, node->pos, MESSAGE_CHECKER_DUPLICATE_TYPE,
                        name);
        return false;
    }
    if (find_variable(ctx->globals, name) != NULL ||
        find_function(ctx->functions, name) != NULL ||
        find_store(ctx->stores, name) != NULL) {
        error_report_at(ctx->errors, node->pos, MESSAGE_CHECKER_DUPLICATE_NAME,
                        name);
        return false;
    }
    if (node->type_parameters_count > NIPPY_MAX_TYPE_PARAMETERS) {
        error_report_at(ctx->errors, node->pos,
                        MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
        return false;
    }

    memcpy(entry.name, name, strlen(name) + 1);
    entry.kind = node->kind == AST_TYPE_ALIAS ? TYPE_ENTRY_ALIAS
                                              : TYPE_ENTRY_STRUCT;
    entry.ast_index = ast_index;
    entry.type_index = TYPED_NO_NODE;
    for (i = 0; i < node->type_parameters_count; i++) {
        size_t child = *(const size_t *)list_get(
            &ctx->ast->type_parameters,
            node->type_parameters_start + i);
        const AstNode *parameter = ast_get_node(ctx->ast, child);
        const char *parameter_name;

        if (parameter == NULL || parameter->kind != AST_TYPE_PARAMETER) {
            error_report(ctx->errors, MESSAGE_CHECKER_MISSING_AST_NODE, child);
            return false;
        }
        parameter_name = ast_symbol_text(ctx->ast, parameter->name_index);
        if (parameter_name == NULL) {
            error_report_at(ctx->errors, parameter->pos,
                            MESSAGE_CHECKER_MISSING_NAME_OR_TYPE);
            return false;
        }
        if (!check_reserved_name(ctx, parameter->pos, parameter_name)) {
            return false;
        }
        if (type_parameter_index(entry.parameters, entry.parameter_count,
                                 parameter_name) != TYPED_NO_NODE) {
            error_report_at(ctx->errors, parameter->pos,
                            MESSAGE_CHECKER_DUPLICATE_TYPE_PARAMETER,
                            parameter_name);
            return false;
        }
        memcpy(entry.parameters[entry.parameter_count], parameter_name,
               strlen(parameter_name) + 1);
        entry.parameter_count++;
    }

    if (!list_push(ctx->types, &entry)) {
        error_report(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
        return false;
    }
    entry_index = ctx->types->length - 1;

    if (entry.kind == TYPE_ENTRY_ALIAS) {
        /* A non-generic alias is resolved at its declaration so unknown or
           invalid types are diagnosed even when the alias is never used. It
           has no C type and no root. Generic aliases expand per application. */
        if (entry.parameter_count == 0) {
            size_t type_index;

            if (!expand_alias(ctx, &entry, NULL, 0, &type_index)) {
                return false;
            }
            ((TypeEntry *)list_get(ctx->types, entry_index))->type_index =
                type_index;
        }
        return true;
    }
    if (node->children_count == 0) {
        error_report_at(ctx->errors, node->pos,
                        MESSAGE_CHECKER_TYPE_HAS_NO_FIELDS);
        return false;
    }
    if (entry.parameter_count == 0) {
        size_t type_index;

        if (!instantiate_struct(ctx, entry_index, NULL, 0, &type_index)) {
            return false;
        }
        ((TypeEntry *)list_get(ctx->types, entry_index))->type_index =
            type_index;
    }
    return true;
}
