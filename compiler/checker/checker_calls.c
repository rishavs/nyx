#include "checker_internal.h"

bool check_function_call(CheckContext *ctx, const AstNode *node,
                                const char *name, size_t type_index,
                                size_t ast_index)
{
    const TypedType *callee_type = typed_program_type(ctx->program, type_index);
    const TypedFunctionSignature *signature;
    size_t parameter_count;
    size_t first_parameter;
    size_t return_type;
    size_t children_start;
    size_t i;
    List values;
    TypedNode typed = {0};
    bool ok = false;

    if (callee_type == NULL || callee_type->kind != NIPPY_TYPE_FUNCTION) {
        error_report_at(ctx->errors, node->pos, MESSAGE_CHECKER_NOT_CALLABLE,
                        name);
        return false;
    }
    signature = typed_program_signature(ctx->program,
                                        callee_type->signature_index);
    if (signature == NULL) {
        error_report_at(ctx->errors, node->pos,
                        MESSAGE_CHECKER_UNSUPPORTED_AST_NODE);
        return false;
    }
    parameter_count = signature->parameter_count;
    first_parameter = signature->first_parameter;
    return_type = signature->return_type;

    for (i = 0; i < node->children_count; i++) {
        size_t child = *(const size_t *)list_get(&ctx->ast->children,
                                                 node->children_start + i);
        const AstNode *argument = ast_get_node(ctx->ast, child);

        if (argument == NULL) {
            error_report(ctx->errors, MESSAGE_CHECKER_MISSING_AST_NODE, child);
            return false;
        }
        if (argument->kind == AST_NAMED_ARG) {
            error_report_at(ctx->errors, argument->pos,
                            MESSAGE_CHECKER_NAMED_FUNCTION_ARG);
            return false;
        }
    }

    if (node->children_count != parameter_count) {
        error_report_at(ctx->errors, node->pos,
                        MESSAGE_CHECKER_FUNCTION_ARG_COUNT, name,
                        parameter_count, node->children_count);
        return false;
    }

    list_init(&values, sizeof(size_t));
    for (i = 0; i < node->children_count; i++) {
        size_t child = *(const size_t *)list_get(&ctx->ast->children,
                                                 node->children_start + i);
        size_t value;
        size_t parameter_type;
        size_t coerced;
        const TypedNode *value_node;

        if (!check_expression(ctx, child, &value)) {
            goto done;
        }
        value_node = typed_program_node(ctx->program, value);
        parameter_type = *(const size_t *)list_get(
            &ctx->program->signature_parameters, first_parameter + i);
        if (value_node == NULL ||
            !inject_into(ctx, value, parameter_type, &coerced)) {
            error_report_at(ctx->errors, node->pos,
                            MESSAGE_CHECKER_FUNCTION_ARG_TYPE, i + 1, name);
            goto done;
        }
        value = coerced;
        if (!list_push(&values, &value)) {
            error_report(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
            goto done;
        }
    }

    /* Append the argument run only now, after nested calls have appended
       theirs, so this call's children stay contiguous. */
    children_start = ctx->program->children.length;
    for (i = 0; i < values.length; i++) {
        size_t value = *(const size_t *)list_get(&values, i);

        if (!typed_program_push_child(ctx->program, value)) {
            error_report(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
            goto done;
        }
    }

    typed.kind = TYPED_FUNCTION_CALL;
    typed.type_index = return_type;
    typed.pos = node->pos;
    typed.name_index = typed_program_copy_symbol(ctx->program, name,
                                                 strlen(name));
    if (typed.name_index == TYPED_NO_NODE) {
        error_report(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_COPYING_NAME);
        goto done;
    }
    typed.left_child = TYPED_NO_NODE;
    typed.right_child = TYPED_NO_NODE;
    typed.children_start = children_start;
    typed.children_count = values.length;

    ok = emit_node(ctx, &typed, ast_index, &(size_t){0});

done:
    list_free(&values);
    return ok;
}

bool check_call(CheckContext *ctx, const AstNode *node,
                       size_t ast_index)
{
    const char *name = ast_symbol_text(ctx->ast, node->name_index);
    const VariableEntry *variable;
    const FunctionEntry *function;

    if (name == NULL) {
        error_report_at(ctx->errors, node->pos,
                        MESSAGE_CHECKER_MISSING_NAME_OR_TYPE);
        return false;
    }
    if (strcmp(name, "print") == 0) {
        return check_print(ctx, node, ast_index);
    }
    if (strcmp(name, "Error") == 0) {
        return check_error_constructor(ctx, node, ast_index);
    }
    if (strcmp(name, "String") == 0 ||
        strcmp(name, "Text") == 0 ||
        strcmp(name, "TextBuilder") == 0 ||
        strcmp(name, "List") == 0 ||
        strcmp(name, "Array") == 0 ||
        strcmp(name, "Slice") == 0) {
        error_report_at(ctx->errors, node->pos,
                        MESSAGE_CHECKER_CONSTRUCTOR_NOT_STRUCT, name);
        return false;
    }
    if (strcmp(name, "Store") == 0 || find_store(ctx->stores, name) != NULL) {
        error_report_at(ctx->errors, node->pos,
                        MESSAGE_CHECKER_STORE_TYPE);
        return false;
    }

    variable = find_variable(ctx->locals, name);
    if (variable == NULL && !ctx->in_function) {
        variable = find_variable(ctx->globals, name);
    }
    if (variable != NULL) {
        return check_function_call(ctx, node, name, variable->type_index,
                                   ast_index);
    }

    {
        size_t function_index = find_function_index(ctx->functions, name);

        if (function_index != TYPED_NO_NODE) {
            function = list_get(ctx->functions, function_index);
            if (function->generic) {
                return check_generic_call(ctx, node, function_index,
                                          ast_index);
            }
            return check_function_call(ctx, node, name, function->type_index,
                                       ast_index);
        }
    }

    if (ctx->in_function && find_variable(ctx->globals, name) != NULL) {
        error_report_at(ctx->errors, node->pos,
                        MESSAGE_CHECKER_GLOBAL_IN_FUNCTION, name);
        return false;
    }

    {
        size_t type_index_entry = find_type_index(ctx->types, name);

        if (type_index_entry != TYPED_NO_NODE) {
            const TypeEntry *entry =
                (const TypeEntry *)list_get(ctx->types, type_index_entry);

            if (entry->parameter_count > 0) {
                return check_generic_constructor(ctx, node,
                                                 type_index_entry, ast_index);
            }
            {
                const TypedType *constructed =
                    typed_program_type(ctx->program, entry->type_index);

                if (constructed == NULL ||
                    constructed->kind != NIPPY_TYPE_STRUCT) {
                    error_report_at(ctx->errors, node->pos,
                                    MESSAGE_CHECKER_CONSTRUCTOR_NOT_STRUCT,
                                    name);
                    return false;
                }
            }
            return check_constructor(ctx, node, entry, ast_index);
        }
    }
    if (strcmp(name, "Int32") == 0 || strcmp(name, "Bool") == 0 ||
        strcmp(name, "Nil") == 0) {
        error_report_at(ctx->errors, node->pos,
                        MESSAGE_CHECKER_CONSTRUCTOR_NOT_STRUCT, name);
        return false;
    }
    error_report_at(ctx->errors, node->pos, MESSAGE_CHECKER_UNKNOWN_CALL,
                    name);
    return false;
}

FunctionInstantiation *find_function_instantiation(
    CheckContext *ctx, size_t entry_index, const size_t *arguments,
    size_t argument_count)
{
    size_t i;

    for (i = 0; i < ctx->function_instantiations->length; i++) {
        FunctionInstantiation *record =
            (FunctionInstantiation *)list_get(ctx->function_instantiations, i);
        size_t j;
        bool same = true;

        if (record->entry != entry_index ||
            record->argument_count != argument_count) {
            continue;
        }
        for (j = 0; j < argument_count; j++) {
            if (!same_type(ctx->program, record->arguments[j], arguments[j])) {
                same = false;
                break;
            }
        }
        if (same) {
            return record;
        }
    }
    return NULL;
}

bool check_generic_body(CheckContext *ctx, size_t entry_index,
                               size_t name_index, const TypeEnv *env,
                               size_t function_type, size_t return_type)
{
    const FunctionEntry *entry =
        (const FunctionEntry *)list_get(ctx->functions, entry_index);
    const AstNode *declaration = ast_get_node(ctx->ast, entry->ast_index);
    const TypeEnv *previous_env = ctx->env;
    List *previous_mapping = ctx->mapping;
    List *previous_locals = ctx->locals;
    bool previous_in_function = ctx->in_function;
    size_t previous_loop = ctx->loop_depth;
    const char *previous_name = ctx->function_name;
    size_t previous_narrowings = ctx->narrowings->length;
    List fresh_mapping;
    List locals;
    size_t children_start = ctx->program->children.length;
    size_t typed_body;
    size_t typed_index;
    size_t i;
    bool ok = false;

    list_init(&fresh_mapping, sizeof(size_t));
    list_init(&locals, sizeof(VariableEntry));
    for (i = 0; i < ctx->ast->nodes.length; i++) {
        size_t none = TYPED_NO_NODE;

        if (!list_push(&fresh_mapping, &none)) {
            error_report(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
            goto done;
        }
    }
    ctx->mapping = &fresh_mapping;
    ctx->env = env;
    ctx->in_function = true;
    ctx->locals = &locals;
    ctx->loop_depth = 0;
    ctx->function_name = entry->name;
    ctx->narrowings->length = 0;

    for (i = 0; i < declaration->children_count; i++) {
        size_t parameter_ast = *(const size_t *)list_get(
            &ctx->ast->children, declaration->children_start + i);
        const AstNode *parameter = ast_get_node(ctx->ast, parameter_ast);
        const char *parameter_name;
        size_t parameter_type;
        size_t parameter_index;
        VariableEntry local;
        TypedNode typed_parameter = {0};

        if (parameter == NULL || parameter->kind != AST_PARAMETER) {
            error_report(ctx->errors, MESSAGE_CHECKER_MISSING_AST_NODE,
                         parameter_ast);
            goto done;
        }
        parameter_name = ast_symbol_text(ctx->ast, parameter->name_index);
        if (parameter_name == NULL) {
            error_report_at(ctx->errors, parameter->pos,
                            MESSAGE_CHECKER_MISSING_NAME_OR_TYPE);
            goto done;
        }
        if (!check_reserved_name(ctx, parameter->pos, parameter_name)) {
            goto done;
        }
        if (find_variable(&locals, parameter_name) != NULL) {
            error_report_at(ctx->errors, parameter->pos,
                            MESSAGE_CHECKER_DUPLICATE_PARAMETER,
                            parameter_name);
            goto done;
        }
        if (!resolve_type_in_env(ctx, parameter->left_child, env,
                                 parameter_name, &parameter_type)) {
            goto done;
        }
        {
            const TypedType *resolved =
                typed_program_type(ctx->program, parameter_type);

            if (resolved != NULL && resolved->kind == NIPPY_TYPE_STORE) {
                error_report_at(ctx->errors, parameter->pos,
                                MESSAGE_CHECKER_STORE_TYPE);
                goto done;
            }
        }
        typed_parameter.kind = TYPED_PARAMETER;
        typed_parameter.type_index = parameter_type;
        typed_parameter.pos = parameter->pos;
        typed_parameter.name_index = typed_program_copy_symbol(
            ctx->program, parameter_name, strlen(parameter_name));
        if (typed_parameter.name_index == TYPED_NO_NODE) {
            error_report(ctx->errors,
                         MESSAGE_CHECKER_OUT_OF_MEMORY_COPYING_NAME);
            goto done;
        }
        typed_parameter.left_child = TYPED_NO_NODE;
        typed_parameter.right_child = TYPED_NO_NODE;
        if (!typed_program_push(ctx->program, &typed_parameter,
                                &parameter_index) ||
            !typed_program_push_child(ctx->program, parameter_index)) {
            error_report(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_PROGRAM);
            goto done;
        }
        memset(&local, 0, sizeof(local));
        memcpy(local.name, parameter_name, strlen(parameter_name) + 1);
        local.type_index = parameter_type;
        if (!list_push(&locals, &local)) {
            error_report(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
            goto done;
        }
    }

    if (!check_block(ctx, declaration->left_child, return_type, &typed_body)) {
        goto done;
    }
    if (!typed_block_returns(ctx->program, typed_body)) {
        error_report_at(ctx->errors, declaration->pos,
                        MESSAGE_CHECKER_FUNCTION_MAY_NOT_RETURN, entry->name);
        goto done;
    }

    {
        TypedNode typed = {0};

        typed.kind = TYPED_FUNCTION_DECLARATION;
        typed.type_index = function_type;
        typed.pos = declaration->pos;
        typed.name_index = name_index;
        typed.left_child = typed_body;
        typed.right_child = TYPED_NO_NODE;
        typed.children_start = children_start;
        typed.children_count = declaration->children_count;
        if (!typed_program_push(ctx->program, &typed, &typed_index)) {
            error_report(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_PROGRAM);
            goto done;
        }
        if (!list_push(&ctx->program->roots, &typed_index)) {
            error_report(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
            goto done;
        }
    }
    ok = true;

done:
    ctx->mapping = previous_mapping;
    ctx->env = previous_env;
    ctx->locals = previous_locals;
    ctx->in_function = previous_in_function;
    ctx->loop_depth = previous_loop;
    ctx->function_name = previous_name;
    ctx->narrowings->length = previous_narrowings;
    list_free(&fresh_mapping);
    list_free(&locals);
    return ok;
}

bool check_generic_call(CheckContext *ctx, const AstNode *node,
                               size_t function_index, size_t ast_index)
{
    const FunctionEntry *entry =
        (const FunctionEntry *)list_get(ctx->functions, function_index);
    const AstNode *declaration = ast_get_node(ctx->ast, entry->ast_index);
    InferEnv infer = {0};
    TypeEnv env = {0};
    List values;
    List parameter_types;
    size_t return_type;
    size_t function_type;
    FunctionInstantiation *record;
    size_t i;
    bool ok = false;

    if (declaration == NULL ||
        declaration->kind != AST_FUNCTION_DECLARATION) {
        error_report_at(ctx->errors, node->pos,
                        MESSAGE_CHECKER_UNSUPPORTED_AST_NODE);
        return false;
    }
    for (i = 0; i < node->children_count; i++) {
        size_t child = *(const size_t *)list_get(&ctx->ast->children,
                                                 node->children_start + i);
        const AstNode *argument = ast_get_node(ctx->ast, child);

        if (argument == NULL) {
            error_report(ctx->errors, MESSAGE_CHECKER_MISSING_AST_NODE, child);
            return false;
        }
        if (argument->kind == AST_NAMED_ARG) {
            error_report_at(ctx->errors, argument->pos,
                            MESSAGE_CHECKER_NAMED_FUNCTION_ARG);
            return false;
        }
    }
    if (node->children_count != declaration->children_count) {
        error_report_at(ctx->errors, node->pos,
                        MESSAGE_CHECKER_FUNCTION_ARG_COUNT, entry->name,
                        declaration->children_count, node->children_count);
        return false;
    }

    for (i = 0; i < entry->parameter_count; i++) {
        infer.names[i] = entry->parameters[i];
        infer.types[i] = TYPED_NO_NODE;
    }
    infer.count = entry->parameter_count;

    list_init(&values, sizeof(size_t));
    list_init(&parameter_types, sizeof(size_t));
    for (i = 0; i < node->children_count; i++) {
        size_t argument_ast = *(const size_t *)list_get(
            &ctx->ast->children, node->children_start + i);
        size_t parameter_ast = *(const size_t *)list_get(
            &ctx->ast->children, declaration->children_start + i);
        const AstNode *parameter = ast_get_node(ctx->ast, parameter_ast);
        size_t value;
        const TypedNode *value_node;

        if (parameter == NULL || parameter->kind != AST_PARAMETER) {
            error_report(ctx->errors, MESSAGE_CHECKER_MISSING_AST_NODE,
                         parameter_ast);
            goto done;
        }
        if (!check_expression(ctx, argument_ast, &value)) {
            goto done;
        }
        value_node = typed_program_node(ctx->program, value);
        if (value_node == NULL ||
            !infer_ast_type(ctx, parameter->left_child,
                            value_node->type_index, &infer, entry->name)) {
            error_report_at(ctx->errors, node->pos,
                            MESSAGE_CHECKER_CANNOT_INFER_TYPE,
                            infer_first_unbound(&infer), entry->name);
            goto done;
        }
        if (!list_push(&values, &value)) {
            error_report(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
            goto done;
        }
    }
    if (!infer_finish(ctx, &infer, &env, entry->name)) {
        goto done;
    }

    for (i = 0; i < declaration->children_count; i++) {
        size_t parameter_ast = *(const size_t *)list_get(
            &ctx->ast->children, declaration->children_start + i);
        const AstNode *parameter = ast_get_node(ctx->ast, parameter_ast);
        size_t parameter_type;

        if (!resolve_type_in_env(ctx, parameter->left_child, &env,
                                 entry->name, &parameter_type) ||
            !list_push(&parameter_types, &parameter_type)) {
            if (!ctx->errors->reported) {
                error_report(ctx->errors,
                             MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
            }
            goto done;
        }
    }
    if (!resolve_type_in_env(ctx, declaration->right_child, &env, entry->name,
                             &return_type)) {
        goto done;
    }
    {
        const TypedType *resolved =
            typed_program_type(ctx->program, return_type);

        if (resolved != NULL && resolved->kind == NIPPY_TYPE_STORE) {
            error_report_at(ctx->errors, declaration->pos,
                            MESSAGE_CHECKER_STORE_TYPE);
            goto done;
        }
    }
    if (!add_function_type(ctx, &parameter_types, return_type,
                           &function_type)) {
        goto done;
    }

    record = find_function_instantiation(ctx, function_index, env.types,
                                         env.count);
    if (record == NULL) {
        FunctionInstantiation fresh = {0};
        size_t record_index;
        size_t generated_name;
        char generated[32];

        if (ctx->function_instantiations->length >= NIPPY_TYPE_DEPTH) {
            error_report(ctx->errors, MESSAGE_CHECKER_RECURSIVE_TYPE,
                         entry->name);
            goto done;
        }
        fresh.entry = function_index;
        fresh.argument_count = env.count;
        for (i = 0; i < env.count; i++) {
            fresh.arguments[i] = env.types[i];
        }
        fresh.type_index = function_type;
        fresh.in_progress = true;
        snprintf(generated, sizeof(generated), "nyx_genfn_%zu",
                 ctx->generated_names++);
        fresh.name_index = typed_program_copy_symbol(
            ctx->program, generated, strlen(generated));
        if (fresh.name_index == TYPED_NO_NODE) {
            error_report(ctx->errors,
                         MESSAGE_CHECKER_OUT_OF_MEMORY_COPYING_NAME);
            goto done;
        }
        if (!list_push(ctx->function_instantiations, &fresh)) {
            error_report(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
            goto done;
        }
        record_index = ctx->function_instantiations->length - 1;
        generated_name = fresh.name_index;
        if (!check_generic_body(ctx, function_index, generated_name, &env,
                                function_type, return_type)) {
            goto done;
        }
        record = (FunctionInstantiation *)list_get(
            ctx->function_instantiations, record_index);
        record->in_progress = false;
        record->emitted = true;
    }

    {
        size_t record_type_index;
        size_t record_name_index;
        const TypedType *callee;
        const TypedFunctionSignature *signature;
        size_t first_parameter;
        size_t return_type;
        List coerced;
        size_t children_start;

        record_type_index = record->type_index;
        record_name_index = record->name_index;
        callee = typed_program_type(ctx->program, record_type_index);
        signature =
            callee == NULL || callee->kind != NIPPY_TYPE_FUNCTION
                ? NULL
                : typed_program_signature(ctx->program,
                                          callee->signature_index);

        if (signature == NULL ||
            signature->parameter_count != values.length) {
            error_report_at(ctx->errors, node->pos,
                            MESSAGE_CHECKER_UNSUPPORTED_AST_NODE);
            goto done;
        }
        first_parameter = signature->first_parameter;
        return_type = signature->return_type;
        list_init(&coerced, sizeof(size_t));
        for (i = 0; i < values.length; i++) {
            size_t value = *(const size_t *)list_get(&values, i);
            size_t parameter_type = *(const size_t *)list_get(
                &ctx->program->signature_parameters, first_parameter + i);
            size_t injected;

            if (!inject_into(ctx, value, parameter_type, &injected) ||
                !list_push(&coerced, &injected)) {
                if (!ctx->errors->reported) {
                    error_report_at(ctx->errors, node->pos,
                                    MESSAGE_CHECKER_FUNCTION_ARG_TYPE,
                                    i + 1, entry->name);
                }
                list_free(&coerced);
                goto done;
            }
        }
        children_start = ctx->program->children.length;
        for (i = 0; i < coerced.length; i++) {
            size_t value = *(const size_t *)list_get(&coerced, i);

            if (!typed_program_push_child(ctx->program, value)) {
                error_report(ctx->errors,
                             MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
                list_free(&coerced);
                goto done;
            }
        }
        {
            TypedNode typed = {0};

            typed.kind = TYPED_FUNCTION_CALL;
            typed.type_index = return_type;
            typed.pos = node->pos;
            typed.name_index = record_name_index;
            typed.left_child = TYPED_NO_NODE;
            typed.right_child = TYPED_NO_NODE;
            typed.children_start = children_start;
            typed.children_count = coerced.length;
            if (!emit_node(ctx, &typed, ast_index, &(size_t){0})) {
                list_free(&coerced);
                goto done;
            }
        }
        list_free(&coerced);
    }
    ok = true;

done:
    list_free(&values);
    list_free(&parameter_types);
    return ok;
}

