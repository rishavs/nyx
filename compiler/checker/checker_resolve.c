#include "checker_internal.h"


bool instantiate_struct(CheckContext *ctx, size_t entry_index,
                               const size_t *arguments, size_t argument_count,
                               size_t *out_type)
{
    const TypeEntry *entry = list_get(ctx->types, entry_index);
    size_t i;

    if (entry == NULL) {
        error_report(ctx->errors, MESSAGE_CHECKER_UNSUPPORTED_AST_NODE);
        return false;
    }
    for (i = 0; i < ctx->struct_instantiations->length; i++) {
        StructInstantiation *record =
            (StructInstantiation *)list_get(ctx->struct_instantiations, i);

        if (record->entry == entry_index &&
            record->argument_count == argument_count) {
            size_t j;
            bool same = true;

            for (j = 0; j < argument_count; j++) {
                if (!same_type(ctx->program, record->arguments[j],
                               arguments[j])) {
                    same = false;
                    break;
                }
            }
            if (same) {
                if (record->in_progress) {
                    error_report(ctx->errors, MESSAGE_CHECKER_RECURSIVE_TYPE,
                                 entry->name);
                    return false;
                }
                *out_type = record->type_index;
                return true;
            }
        }
    }

    {
        StructInstantiation record = {0};
        TypeEnv env = {0};
        size_t record_index;
        size_t first_argument;
        size_t declaration_index;
        size_t type_index;
        size_t typed_fields_start = ctx->program->children.length;
        const AstNode *declaration;
        char generated[32];
        size_t name_index;

        if (argument_count != entry->parameter_count) {
            error_report(ctx->errors, MESSAGE_CHECKER_TYPE_ARGUMENT_COUNT,
                         entry->name, entry->parameter_count, argument_count);
            return false;
        }
        record.entry = entry_index;
        record.argument_count = argument_count;
        record.in_progress = true;
        for (i = 0; i < argument_count; i++) {
            record.arguments[i] = arguments[i];
            env.names[i] = entry->parameters[i];
            env.types[i] = arguments[i];
        }
        env.count = argument_count;
        if (!list_push(ctx->struct_instantiations, &record)) {
            error_report(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
            return false;
        }
        record_index = ctx->struct_instantiations->length - 1;

        snprintf(generated, sizeof(generated), "nyx_gen_%zu",
                 ctx->generated_names++);
        name_index = typed_program_copy_symbol(ctx->program, generated,
                                               strlen(generated));
        if (name_index == TYPED_NO_NODE) {
            error_report(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_COPYING_NAME);
            return false;
        }

        declaration = ast_get_node(ctx->ast, entry->ast_index);
        if (declaration == NULL ||
            declaration->kind != AST_TYPE_DECLARATION) {
            error_report(ctx->errors, MESSAGE_CHECKER_UNSUPPORTED_AST_NODE);
            return false;
        }
        for (i = 0; i < declaration->children_count; i++) {
            size_t field_ast = *(const size_t *)list_get(
                &ctx->ast->children, declaration->children_start + i);
            const AstNode *field = ast_get_node(ctx->ast, field_ast);
            const char *field_name;
            size_t field_type;
            TypedNode typed_field = {0};
            size_t field_index;

            if (field == NULL || field->kind != AST_FIELD) {
                error_report(ctx->errors, MESSAGE_CHECKER_MISSING_AST_NODE,
                             field_ast);
                return false;
            }
            field_name = ast_symbol_text(ctx->ast, field->name_index);
            if (!check_reserved_name(ctx, field->pos, field_name)) {
                return false;
            }
            if (!resolve_type_in_env(ctx, field->left_child, &env, field_name,
                                     &field_type)) {
                return false;
            }
            {
                const TypedType *resolved =
                    typed_program_type(ctx->program, field_type);

                if (resolved != NULL &&
                    (resolved->kind == NIPPY_TYPE_FUNCTION ||
                     resolved->kind == NIPPY_TYPE_STORE)) {
                    error_report_at(ctx->errors, field->pos,
                                    MESSAGE_CHECKER_FIELD_TYPE, field_name);
                    return false;
                }
            }
            typed_field.kind = TYPED_FIELD;
            typed_field.type_index = field_type;
            typed_field.pos = field->pos;
            typed_field.name_index = typed_program_copy_symbol(
                ctx->program, field_name, strlen(field_name));
            if (typed_field.name_index == TYPED_NO_NODE) {
                error_report(ctx->errors,
                             MESSAGE_CHECKER_OUT_OF_MEMORY_COPYING_NAME);
                return false;
            }
            typed_field.left_child = TYPED_NO_NODE;
            typed_field.right_child = TYPED_NO_NODE;
            typed_field.children_start = 0;
            typed_field.children_count = 0;
            if (!typed_program_push(ctx->program, &typed_field, &field_index) ||
                !typed_program_push_child(ctx->program, field_index)) {
                error_report(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_PROGRAM);
                return false;
            }
        }

        {
            TypedNode typed_declaration = {0};

            typed_declaration.kind = TYPED_TYPE_DECLARATION;
            typed_declaration.type_index = TYPED_NO_NODE;
            typed_declaration.pos = declaration->pos;
            typed_declaration.name_index = name_index;
            typed_declaration.left_child = TYPED_NO_NODE;
            typed_declaration.right_child = TYPED_NO_NODE;
            typed_declaration.children_start = typed_fields_start;
            typed_declaration.children_count = declaration->children_count;
            if (!typed_program_push(ctx->program, &typed_declaration,
                                    &declaration_index)) {
                error_report(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_PROGRAM);
                return false;
            }
        }

        first_argument = ctx->program->type_children.length;
        for (i = 0; i < argument_count; i++) {
            if (!typed_program_add_type_child(ctx->program, arguments[i])) {
                error_report(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
                return false;
            }
        }

        {
            TypedType type = {0};
            size_t source_name = typed_program_copy_symbol(
                ctx->program, entry->name, strlen(entry->name));

            if (source_name == TYPED_NO_NODE) {
                error_report(ctx->errors,
                             MESSAGE_CHECKER_OUT_OF_MEMORY_COPYING_NAME);
                return false;
            }
            type.kind = NIPPY_TYPE_STRUCT;
            type.name_index = name_index;
            type.signature_index = TYPED_NO_NODE;
            type.first_child = first_argument;
            type.child_count = argument_count;
            type.declaration_node = declaration_index;
            type.source_name_index = source_name;
            if (!typed_program_add_type(ctx->program, &type, &type_index)) {
                error_report(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
                return false;
            }
        }

        {
            StructInstantiation *record =
                (StructInstantiation *)list_get(ctx->struct_instantiations,
                                                record_index);
            record->type_index = type_index;
            record->in_progress = false;
        }
        if (!check_type_value_size(ctx, type_index, declaration->pos)) {
            return false;
        }
        *out_type = type_index;
        return true;
    }
}


bool expand_alias(CheckContext *ctx, const TypeEntry *entry,
                         const size_t *arguments, size_t argument_count,
                         size_t *out_type)
{
    TypeEnv env = {0};
    const AstNode *alias;
    size_t i;

    if (argument_count != entry->parameter_count) {
        error_report(ctx->errors, MESSAGE_CHECKER_TYPE_ARGUMENT_COUNT,
                     entry->name, entry->parameter_count, argument_count);
        return false;
    }
    alias = ast_get_node(ctx->ast, entry->ast_index);
    if (alias == NULL || alias->kind != AST_TYPE_ALIAS) {
        error_report(ctx->errors, MESSAGE_CHECKER_UNSUPPORTED_AST_NODE);
        return false;
    }
    for (i = 0; i < argument_count; i++) {
        env.names[i] = entry->parameters[i];
        env.types[i] = arguments[i];
    }
    env.count = argument_count;
    if (ctx->depth >= NIPPY_TYPE_DEPTH) {
        error_report(ctx->errors, MESSAGE_CHECKER_RECURSIVE_TYPE, entry->name);
        return false;
    }
    ctx->depth++;
    if (!resolve_type_in_env(ctx, alias->right_child, &env, entry->name,
                             out_type)) {
        ctx->depth--;
        return false;
    }
    ctx->depth--;
    return true;
}


bool resolve_type_name(CheckContext *ctx, const AstNode *node,
                              const TypeEnv *env, const char *owner,
                              size_t *out_index)
{
    const char *name = ast_symbol_text(ctx->ast, node->name_index);
    size_t entry_index;

    if (name == NULL) {
        error_report_at(ctx->errors, node->pos,
                        MESSAGE_CHECKER_MISSING_NAME_OR_TYPE);
        return false;
    }
    if (env != NULL && env_lookup(env, name, out_index)) {
        return true;
    }
    if (strcmp(name, "Int32") == 0) {
        return builtin_type(ctx, NIPPY_TYPE_INT32, out_index);
    }
    if (strcmp(name, "Bool") == 0) {
        return builtin_type(ctx, NIPPY_TYPE_BOOL, out_index);
    }
    if (strcmp(name, "Nil") == 0) {
        return builtin_type(ctx, NIPPY_TYPE_NIL, out_index);
    }
    if (strcmp(name, "Byte") == 0) {
        return builtin_type(ctx, NIPPY_TYPE_BYTE, out_index);
    }
    if (strcmp(name, "Store") == 0) {
        return builtin_type(ctx, NIPPY_TYPE_STORE, out_index);
    }
    if (strcmp(name, "String") == 0) {
        error_report_at(ctx->errors, node->pos,
                        MESSAGE_CHECKER_STRING_ARGS);
        return false;
    }
    if (strcmp(name, "Text") == 0) {
        return builtin_type(ctx, NIPPY_TYPE_DYNAMIC_STRING, out_index);
    }
    if (strcmp(name, "TextBuilder") == 0) {
        return text_builder_type(ctx, out_index);
    }
    if (strcmp(name, "List") == 0) {
        error_report_at(ctx->errors, node->pos, MESSAGE_CHECKER_LIST_ARGS);
        return false;
    }
    if (strcmp(name, "Array") == 0) {
        error_report_at(ctx->errors, node->pos, MESSAGE_CHECKER_ARRAY_ARGS);
        return false;
    }
    if (strcmp(name, "Record") == 0) {
        error_report_at(ctx->errors, node->pos, MESSAGE_CHECKER_RECORD_ARGS);
        return false;
    }
    if (strcmp(name, "Table") == 0) {
        error_report_at(ctx->errors, node->pos, MESSAGE_CHECKER_TABLE_ARGS);
        return false;
    }
    if (strcmp(name, "Slice") == 0) {
        error_report_at(ctx->errors, node->pos, MESSAGE_CHECKER_SLICE_ARGS);
        return false;
    }
    if (strcmp(name, "Error") == 0) {
        return error_type(ctx, out_index);
    }
    if (strcmp(name, "Handle") == 0) {
        error_report_at(ctx->errors, node->pos,
                        MESSAGE_CHECKER_MISSING_TYPE_ARGUMENTS, name);
        return false;
    }
    entry_index = find_type_index(ctx->types, name);
    if (entry_index == TYPED_NO_NODE) {
        error_report_at(ctx->errors, node->pos, MESSAGE_CHECKER_UNKNOWN_TYPE,
                        name, owner == NULL ? "value" : owner);
        return false;
    }
    {
        const TypeEntry *entry = list_get(ctx->types, entry_index);

        if (entry->kind == TYPE_ENTRY_ALIAS) {
            if (entry->parameter_count != 0) {
                error_report_at(ctx->errors, node->pos,
                                MESSAGE_CHECKER_MISSING_TYPE_ARGUMENTS, name);
                return false;
            }
            if (entry->type_index != TYPED_NO_NODE) {
                *out_index = entry->type_index;
                return true;
            }
            return expand_alias(ctx, entry, NULL, 0, out_index);
        }
        if (entry->parameter_count != 0) {
            error_report_at(ctx->errors, node->pos,
                            MESSAGE_CHECKER_MISSING_TYPE_ARGUMENTS, name);
            return false;
        }
        if (entry->type_index == TYPED_NO_NODE) {
            return instantiate_struct(ctx, entry_index, NULL, 0, out_index);
        }
        *out_index = entry->type_index;
        return true;
    }
}


bool resolve_type_application(CheckContext *ctx, const AstNode *node,
                                     const TypeEnv *env, const char *owner,
                                     size_t *out_index)
{
    const char *name = ast_symbol_text(ctx->ast, node->name_index);
    size_t entry_index;
    List arguments;
    size_t i;
    bool ok = false;

    if (name == NULL) {
        error_report_at(ctx->errors, node->pos,
                        MESSAGE_CHECKER_MISSING_NAME_OR_TYPE);
        return false;
    }
    if (strcmp(name, "Handle") == 0) {
        size_t element;

        if (node->children_count != 1) {
            error_report_at(ctx->errors, node->pos,
                            MESSAGE_CHECKER_HANDLE_ELEMENT);
            return false;
        }
        {
            size_t child = *(const size_t *)list_get(
                &ctx->ast->children, node->children_start);

            if (!resolve_type_in_env(ctx, child, env, owner, &element)) {
                return false;
            }
        }
        return handle_type(ctx, element, out_index);
    }
    if (strcmp(name, "Int32") == 0 || strcmp(name, "Bool") == 0 ||
        strcmp(name, "Nil") == 0 || strcmp(name, "Byte") == 0 ||
        strcmp(name, "Store") == 0 || strcmp(name, "String") == 0 ||
        strcmp(name, "Text") == 0 ||
        strcmp(name, "TextBuilder") == 0 ||
        strcmp(name, "List") == 0 ||
        strcmp(name, "Array") == 0 ||
        strcmp(name, "Slice") == 0 ||
        strcmp(name, "Record") == 0 ||
        strcmp(name, "Table") == 0 ||
        strcmp(name, "Error") == 0) {
        error_report_at(ctx->errors, node->pos,
                        MESSAGE_CHECKER_TYPE_NOT_GENERIC, name);
        return false;
    }
    entry_index = find_type_index(ctx->types, name);
    if (entry_index == TYPED_NO_NODE) {
        error_report_at(ctx->errors, node->pos, MESSAGE_CHECKER_UNKNOWN_TYPE,
                        name, owner == NULL ? "value" : owner);
        return false;
    }

    list_init(&arguments, sizeof(size_t));
    for (i = 0; i < node->children_count; i++) {
        size_t child = *(const size_t *)list_get(&ctx->ast->children,
                                                 node->children_start + i);
        size_t argument;

        if (!resolve_type_in_env(ctx, child, env, owner, &argument) ||
            !list_push(&arguments, &argument)) {
            if (!ctx->errors->reported) {
                error_report(ctx->errors,
                             MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
            }
            list_free(&arguments);
            return false;
        }
    }

    {
        const TypeEntry *entry = list_get(ctx->types, entry_index);

        if (entry->parameter_count == 0) {
            error_report_at(ctx->errors, node->pos,
                            MESSAGE_CHECKER_TYPE_NOT_GENERIC, name);
        } else if (entry->kind == TYPE_ENTRY_ALIAS) {
            ok = expand_alias(ctx, entry,
                              (const size_t *)arguments.items,
                              arguments.length, out_index);
        } else {
            ok = instantiate_struct(ctx, entry_index,
                                    (const size_t *)arguments.items,
                                    arguments.length, out_index);
        }
    }
    list_free(&arguments);
    return ok;
}


bool resolve_type_in_env(CheckContext *ctx, size_t ast_index,
                                const TypeEnv *env, const char *owner,
                                size_t *out_index)
{
    const AstNode *node = ast_get_node(ctx->ast, ast_index);
    size_t i;

    if (node == NULL) {
        error_report(ctx->errors, MESSAGE_CHECKER_MISSING_AST_NODE, ast_index);
        return false;
    }
    if (node->kind == AST_TYPE_NAME) {
        return resolve_type_name(ctx, node, env, owner, out_index);
    }
    if (node->kind == AST_TYPE_FIXED_STRING) {
        return string_type(ctx, node->integer_value, out_index);
    }
    if (node->kind == AST_TYPE_SLICE) {
        size_t element;

        if (!resolve_type_in_env(ctx, node->left_child, env, owner,
                                 &element)) {
            return false;
        }
        return slice_type(ctx, element, out_index);
    }
    if (node->kind == AST_TYPE_RECORD || node->kind == AST_TYPE_TABLE) {
        size_t key_type;
        size_t value_type;

        if (!resolve_type_in_env(ctx, node->left_child, env, owner,
                                 &key_type) ||
            !resolve_type_in_env(ctx, node->right_child, env, owner,
                                 &value_type)) {
            return false;
        }
        if (node->kind == AST_TYPE_TABLE) {
            return map_type(ctx, key_type, value_type, false, 0, out_index);
        }
        if (node->integer_value < 0 ||
            node->integer_value > ctx->program->max_list_capacity) {
            error_report_at(ctx->errors, node->pos,
                            MESSAGE_CHECKER_RECORD_CAPACITY);
            return false;
        }
        return map_type(ctx, key_type, value_type, true,
                        (size_t)node->integer_value, out_index);
    }
    if (node->kind == AST_TYPE_LIST ||
        node->kind == AST_TYPE_FIXED_LIST) {
        size_t element;

        if (!resolve_type_in_env(ctx, node->left_child, env, owner,
                                 &element)) {
            return false;
        }
        if (node->kind == AST_TYPE_LIST) {
            return list_type(ctx, element, false, 0, out_index);
        }
        if (node->integer_value < 0 ||
            node->integer_value > ctx->program->max_list_capacity) {
            error_report_at(ctx->errors, node->pos,
                            MESSAGE_CHECKER_ARRAY_CAPACITY,
                            ctx->program->max_list_capacity);
            return false;
        }
        return list_type(ctx, element, true,
                         (size_t)node->integer_value, out_index);
    }
    if (node->kind == AST_TYPE_APPLICATION) {
        return resolve_type_application(ctx, node, env, owner, out_index);
    }
    if (node->kind == AST_TYPE_UNION) {
        List alternatives;

        list_init(&alternatives, sizeof(size_t));
        for (i = 0; i < node->children_count; i++) {
            size_t child = *(const size_t *)list_get(
                &ctx->ast->children, node->children_start + i);
            size_t alternative;

            if (!resolve_type_in_env(ctx, child, env, owner, &alternative) ||
                !list_push(&alternatives, &alternative)) {
                if (!ctx->errors->reported) {
                    error_report(ctx->errors,
                                 MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
                }
                list_free(&alternatives);
                return false;
            }
            {
                const TypedType *resolved =
                    typed_program_type(ctx->program, alternative);

                if (resolved != NULL &&
                    resolved->kind == NIPPY_TYPE_STORE) {
                    error_report_at(ctx->errors, node->pos,
                                    MESSAGE_CHECKER_UNION_FUNCTION);
                    list_free(&alternatives);
                    return false;
                }
            }
        }
        {
            bool ok = intern_union(ctx, &alternatives, out_index);
            list_free(&alternatives);
            return ok;
        }
    }
    if (node->kind == AST_TYPE_FUNCTION) {
        List parameter_types;
        size_t return_type;
        bool ok = false;

        list_init(&parameter_types, sizeof(size_t));
        for (i = 0; i < node->children_count; i++) {
            size_t child = *(const size_t *)list_get(
                &ctx->ast->children, node->children_start + i);
            size_t parameter_type;

            if (!resolve_type_in_env(ctx, child, env, owner,
                                     &parameter_type)) {
                goto function_type_done;
            }
            if (!list_push(&parameter_types, &parameter_type)) {
                error_report(ctx->errors,
                             MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
                goto function_type_done;
            }
        }
        if (!resolve_type_in_env(ctx, node->right_child, env, owner,
                                 &return_type)) {
            goto function_type_done;
        }
        ok = add_function_type(ctx, &parameter_types, return_type, out_index);

    function_type_done:
        list_free(&parameter_types);
        return ok;
    }

    error_report_at(ctx->errors, node->pos, MESSAGE_CHECKER_UNSUPPORTED_AST_NODE);
    return false;
}


bool resolve_type_node(CheckContext *ctx, size_t ast_index,
                              const char *owner, size_t *out_index)
{
    return resolve_type_in_env(ctx, ast_index, ctx->env, owner, out_index);
}
