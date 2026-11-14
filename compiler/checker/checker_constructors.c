#include "checker_internal.h"


bool check_generic_constructor(CheckContext *ctx, const AstNode *node,
                                      size_t entry_index, size_t ast_index)
{
    const TypeEntry *entry = list_get(ctx->types, entry_index);
    const AstNode *declaration = ast_get_node(ctx->ast, entry->ast_index);
    InferEnv infer = {0};
    TypeEnv env = {0};
    size_t arguments[NIPPY_MAX_TYPE_PARAMETERS];
    size_t type_index;
    TypeEntry concrete;
    size_t i;
    bool named = false;

    if (entry == NULL || declaration == NULL ||
        declaration->kind != AST_TYPE_DECLARATION) {
        error_report_at(ctx->errors, node->pos,
                        MESSAGE_CHECKER_UNSUPPORTED_AST_NODE);
        return false;
    }
    if (ctx->expected_ast == ast_index) {
        const TypedType *expected = typed_program_type(ctx->program,
                                                       ctx->expected_type);
        const char *source_name = expected == NULL ||
                                  expected->source_name_index == TYPED_NO_NODE
                                      ? NULL : typed_program_symbol_text(
                                          ctx->program,
                                          expected->source_name_index);

        if (expected != NULL && expected->kind == NIPPY_TYPE_STRUCT &&
            source_name != NULL && strcmp(source_name, entry->name) == 0 &&
            expected->child_count == entry->parameter_count) {
            concrete = *entry;
            concrete.type_index = ctx->expected_type;
            return check_constructor(ctx, node, &concrete, ast_index);
        }
    }
    for (i = 0; i < entry->parameter_count; i++) {
        infer.names[i] = entry->parameters[i];
        infer.types[i] = TYPED_NO_NODE;
    }
    infer.count = entry->parameter_count;

    if (node->children_count > 0) {
        size_t first = *(const size_t *)list_get(&ctx->ast->children,
                                                 node->children_start);
        const AstNode *argument = ast_get_node(ctx->ast, first);

        named = argument != NULL && argument->kind == AST_NAMED_ARG;
    }

    if (!named) {
        if (node->children_count > declaration->children_count) {
            error_report_at(ctx->errors, node->pos,
                            MESSAGE_CHECKER_CONSTRUCTOR_ARG_COUNT,
                            entry->name, declaration->children_count,
                            node->children_count);
            return false;
        }
        for (i = 0; i < declaration->children_count; i++) {
            const AstNode *field = template_field(ctx, declaration, i, NULL);
            size_t argument_ast = i < node->children_count
                ? *(const size_t *)list_get(&ctx->ast->children,
                                            node->children_start + i)
                : field == NULL ? AST_NO_NODE : field->right_child;
            size_t value;
            const TypedNode *value_node;

            if (field == NULL || argument_ast == AST_NO_NODE) {
                error_report_at(ctx->errors, node->pos,
                                MESSAGE_CHECKER_CONSTRUCTOR_ARG_COUNT,
                                entry->name, declaration->children_count,
                                node->children_count);
                return false;
            }
            if (!check_expression(ctx, argument_ast, &value)) {
                return false;
            }
            value_node = typed_program_node(ctx->program, value);
            if (value_node == NULL ||
                (ast_type_has_unbound(ctx, field->left_child, &infer,
                                      NIPPY_TYPE_DEPTH) &&
                 (!check_default_numeric(ctx, value) ||
                  !infer_ast_type(ctx, field->left_child,
                                  value_node->type_index, &infer,
                                  entry->name)))) {
                error_report_at(ctx->errors, node->pos,
                                MESSAGE_CHECKER_CANNOT_INFER_TYPE,
                                infer_first_unbound(&infer), entry->name);
                return false;
            }
        }
    } else {
        for (i = 0; i < node->children_count; i++) {
            size_t argument_ast = *(const size_t *)list_get(
                &ctx->ast->children, node->children_start + i);
            const AstNode *argument = ast_get_node(ctx->ast, argument_ast);
            const char *argument_name;

            if (argument == NULL || argument->kind != AST_NAMED_ARG) {
                error_report_at(ctx->errors, node->pos,
                                MESSAGE_CHECKER_UNSUPPORTED_AST_NODE);
                return false;
            }
            argument_name = ast_symbol_text(ctx->ast, argument->name_index);
            if (template_field_index(ctx, declaration, argument_name) ==
                TYPED_NO_NODE) {
                error_report_at(ctx->errors, argument->pos,
                                MESSAGE_CHECKER_UNKNOWN_CONSTRUCTOR_FIELD,
                                argument_name);
                return false;
            }
            {
                size_t j;

                for (j = 0; j < i; j++) {
                    size_t other_ast = *(const size_t *)list_get(
                        &ctx->ast->children, node->children_start + j);
                    const AstNode *other = ast_get_node(ctx->ast, other_ast);
                    const char *other_name = ast_symbol_text(
                        ctx->ast, other->name_index);

                    if (other_name != NULL &&
                        strcmp(other_name, argument_name) == 0) {
                        error_report_at(ctx->errors, argument->pos,
                                        MESSAGE_CHECKER_DUPLICATE_CONSTRUCTOR_ARG,
                                        argument_name);
                        return false;
                    }
                }
            }
        }
        for (i = 0; i < declaration->children_count; i++) {
            const AstNode *field = template_field(ctx, declaration, i, NULL);
            const char *field_name = field == NULL
                                         ? NULL
                                         : ast_symbol_text(ctx->ast,
                                                           field->name_index);
            const AstNode *found = NULL;
            size_t j;

            for (j = 0; j < node->children_count; j++) {
                size_t argument_ast = *(const size_t *)list_get(
                    &ctx->ast->children, node->children_start + j);
                const AstNode *argument = ast_get_node(ctx->ast, argument_ast);
                const char *argument_name = ast_symbol_text(
                    ctx->ast, argument->name_index);

                if (argument_name != NULL && field_name != NULL &&
                    strcmp(argument_name, field_name) == 0) {
                    found = argument;
                    break;
                }
            }
            if (found == NULL) {
                if (field == NULL || field->right_child == AST_NO_NODE) {
                    error_report_at(ctx->errors, node->pos,
                                    MESSAGE_CHECKER_MISSING_CONSTRUCTOR_ARG,
                                    field_name);
                    return false;
                }
            }
            {
                size_t value;
                const TypedNode *value_node;

                if (!check_expression(ctx, found == NULL ? field->right_child
                                                          : found->left_child,
                                      &value)) {
                    return false;
                }
                value_node = typed_program_node(ctx->program, value);
                if (value_node == NULL ||
                    (ast_type_has_unbound(ctx, field->left_child, &infer,
                                          NIPPY_TYPE_DEPTH) &&
                     (!check_default_numeric(ctx, value) ||
                      !infer_ast_type(ctx, field->left_child,
                                      value_node->type_index, &infer,
                                      entry->name)))) {
                    error_report_at(ctx->errors, node->pos,
                                    MESSAGE_CHECKER_CANNOT_INFER_TYPE,
                                    infer_first_unbound(&infer), entry->name);
                    return false;
                }
            }
        }
    }

    if (!infer_finish(ctx, &infer, &env, entry->name)) {
        return false;
    }
    for (i = 0; i < env.count; i++) {
        arguments[i] = env.types[i];
    }
    if (!instantiate_struct(ctx, entry_index, arguments, env.count,
                            &type_index)) {
        return false;
    }
    concrete = *entry;
    concrete.type_index = type_index;
    return check_constructor(ctx, node, &concrete, ast_index);
}



bool check_constructor(CheckContext *ctx, const AstNode *node,
                              const TypeEntry *entry, size_t ast_index)
{
    /* Checking an argument adds nodes, which can move the node array, so the
       declaration is kept as an index and fetched where it is read. */
    size_t decl_index = struct_declaration_node(ctx, entry);
    const TypedNode *type_decl = typed_program_node(ctx->program, decl_index);
    size_t field_count;
    const AstNode *declaration = ast_get_node(ctx->ast, entry->ast_index);
    bool named = false;
    size_t i;
    List values;
    TypedNode typed = {0};
    bool ok = false;

    if (type_decl == NULL || type_decl->kind != TYPED_TYPE_DECLARATION) {
        error_report_at(ctx->errors, node->pos,
                        MESSAGE_CHECKER_UNSUPPORTED_AST_NODE);
        return false;
    }
    field_count = type_decl->children_count;
    if (node->children_count > 0) {
        size_t first = *(const size_t *)list_get(&ctx->ast->children,
                                                 node->children_start);
        const AstNode *first_argument = ast_get_node(ctx->ast, first);

        if (first_argument == NULL) {
            error_report(ctx->errors, MESSAGE_CHECKER_MISSING_AST_NODE, first);
            return false;
        }
        named = first_argument->kind == AST_NAMED_ARG;
    }

    list_init(&values, sizeof(size_t));

    if (!named) {
        if (node->children_count > field_count) {
            error_report_at(ctx->errors, node->pos,
                            MESSAGE_CHECKER_CONSTRUCTOR_ARG_COUNT,
                            entry->name, field_count, node->children_count);
            goto done;
        }
        for (i = 0; i < field_count; i++) {
            const AstNode *field = template_field(ctx, declaration, i, NULL);
            size_t child;
            size_t value;
            size_t coerced;
            size_t field_type = struct_field_type(
                ctx->program, typed_program_node(ctx->program, decl_index), i);

            if (i < node->children_count) {
                child = *(const size_t *)list_get(&ctx->ast->children,
                                                  node->children_start + i);
            } else if (field != NULL && field->right_child != AST_NO_NODE) {
                child = field->right_child;
            } else {
                error_report_at(ctx->errors, node->pos,
                                MESSAGE_CHECKER_CONSTRUCTOR_ARG_COUNT,
                                entry->name, field_count, node->children_count);
                goto done;
            }
            if (!check_expression_expected(ctx, child, field_type, &value)) {
                goto done;
            }
            if (!constructor_value_fits(ctx, value, entry, i, i + 1,
                                         node->pos, &coerced)) {
                goto done;
            }
            if (!list_push(&values, &coerced)) {
                error_report(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
                goto done;
            }
        }
    } else {
        /* Reject unknown field names before reporting missing ones, so a
           typo reads as "unknown field" rather than "missing field". */
        for (i = 0; i < node->children_count; i++) {
            size_t child = *(const size_t *)list_get(
                &ctx->ast->children, node->children_start + i);
            const AstNode *argument = ast_get_node(ctx->ast, child);
            const char *argument_name;
            bool known = false;
            size_t j;

            if (argument == NULL || argument->kind != AST_NAMED_ARG) {
                error_report_at(ctx->errors, node->pos,
                                MESSAGE_CHECKER_UNSUPPORTED_AST_NODE);
                goto done;
            }
            argument_name = ast_symbol_text(ctx->ast, argument->name_index);
            for (j = 0; j < field_count; j++) {
                const char *field_name = struct_field_name(
                    ctx->program,
                    typed_program_node(ctx->program, decl_index), j);

                if (argument_name != NULL && field_name != NULL &&
                    strcmp(argument_name, field_name) == 0) {
                    known = true;
                    break;
                }
            }
            if (!known) {
                error_report_at(ctx->errors, argument->pos,
                                MESSAGE_CHECKER_UNKNOWN_CONSTRUCTOR_FIELD,
                                argument_name);
                goto done;
            }
        }

        for (i = 0; i < field_count; i++) {
            const char *field_name = struct_field_name(
                ctx->program,
                typed_program_node(ctx->program, decl_index), i);
            const AstNode *found = NULL;
            size_t j;

            for (j = 0; j < node->children_count; j++) {
                size_t child = *(const size_t *)list_get(
                    &ctx->ast->children, node->children_start + j);
                const AstNode *argument = ast_get_node(ctx->ast, child);
                const char *argument_name = ast_symbol_text(
                    ctx->ast, argument->name_index);

                if (argument_name != NULL && field_name != NULL &&
                    strcmp(argument_name, field_name) == 0) {
                    if (found != NULL) {
                        error_report_at(ctx->errors, argument->pos,
                                        MESSAGE_CHECKER_DUPLICATE_CONSTRUCTOR_ARG,
                                        field_name);
                        goto done;
                    }
                    found = argument;
                }
            }
            if (found == NULL) {
                const AstNode *field = template_field(ctx, declaration, i, NULL);
                size_t value, coerced;
                size_t field_type = struct_field_type(
                    ctx->program, typed_program_node(ctx->program, decl_index), i);
                if (field == NULL || field->right_child == AST_NO_NODE) {
                    error_report_at(ctx->errors, node->pos,
                                    MESSAGE_CHECKER_MISSING_CONSTRUCTOR_ARG,
                                    field_name);
                    goto done;
                }
                if (!check_expression_expected(ctx, field->right_child,
                                               field_type, &value) ||
                    !constructor_value_fits(ctx, value, entry, i, i + 1,
                                            node->pos, &coerced)) goto done;
                if (!list_push(&values, &coerced)) {
                    error_report(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
                    goto done;
                }
                continue;
            }
            {
                size_t value;
                size_t coerced;
                size_t field_type = struct_field_type(
                    ctx->program, typed_program_node(ctx->program, decl_index),
                    i);

                if (!check_expression_expected(ctx, found->left_child,
                                               field_type, &value)) {
                    goto done;
                }
                if (!constructor_value_fits(ctx, value, entry, i, i + 1,
                                            found->pos, &coerced)) {
                    goto done;
                }
                if (!list_push(&values, &coerced)) {
                    error_report(ctx->errors,
                                 MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
                    goto done;
                }
            }
        }
    }

    {
        size_t children_start = ctx->program->children.length;

        /* Nested constructors in the arguments have appended their own runs
           already, so this constructor's values become one contiguous run. */
        for (i = 0; i < values.length; i++) {
            size_t value = *(const size_t *)list_get(&values, i);

            if (!typed_program_push_child(ctx->program, value)) {
                error_report(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
                goto done;
            }
        }

        typed.kind = TYPED_STRUCT_CONSTRUCTOR;
        typed.type_index = entry->type_index;
        typed.pos = node->pos;
        typed.name_index = TYPED_NO_NODE;
        typed.left_child = TYPED_NO_NODE;
        typed.right_child = TYPED_NO_NODE;
        typed.children_start = children_start;
        typed.children_count = field_count;

        ok = emit_node(ctx, &typed, ast_index, &(size_t){0});
    }

done:
    list_free(&values);
    return ok;
}




bool check_error_throw(CheckContext *ctx, const AstNode *node,
                             size_t ast_index)
{
    size_t error;
    size_t header_type;
    size_t description_type;
    size_t filename_type;
    size_t values[2];
    const char *filename;
    size_t filename_length;
    size_t capacity;
    size_t string_offset;
    size_t i;
    TypedNode typed = {0};

    if (!error_type(ctx, &error) ||
        !error_field_type(ctx, error, 0, &header_type) ||
        !error_field_type(ctx, error, 1, &description_type) ||
        !error_field_type(ctx, error, 2, &filename_type)) {
        return false;
    }
    if (node->children_count != 2) {
        error_report_at(ctx->errors, node->pos,
                        MESSAGE_CHECKER_ERROR_THROW_ARGS);
        return false;
    }
    for (i = 0; i < 2; i++) {
        size_t child = *(const size_t *)list_get(
            &ctx->ast->children, node->children_start + i);
        const AstNode *argument = ast_get_node(ctx->ast, child);
        size_t value;
        size_t coerced;
        size_t expected = i == 0 ? header_type : description_type;

        if (argument == NULL || argument->kind == AST_NAMED_ARG) {
            error_report_at(ctx->errors, node->pos,
                            MESSAGE_CHECKER_ERROR_THROW_ARGS);
            return false;
        }
        if (!check_expression(ctx, child, &value) ||
            !inject_into(ctx, value, expected, &coerced)) {
            if (!ctx->errors->reported) {
                error_report_at(ctx->errors, node->pos,
                                MESSAGE_CHECKER_ERROR_THROW_ARGS);
            }
            return false;
        }
        values[i] = coerced;
    }
    {
        const TypedType *filename_record = typed_program_type(ctx->program,
                                                              filename_type);

        if (filename_record == NULL ||
            filename_record->kind != NIPPY_TYPE_STRING) {
            error_report(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
            return false;
        }
        capacity = filename_record->fixed_length;
    }
    filename = ctx->errors->source_name == NULL
                   ? "nippy.nip"
                   : ctx->errors->source_name;
    filename_length = strlen(filename);
    if (filename_length > capacity) {
        filename_length = capacity;
    }
    string_offset = ctx->program->bytes.length;
    for (i = 0; i < filename_length; i++) {
        if (!typed_program_push_byte(ctx->program,
                                     (unsigned char)filename[i])) {
            error_report(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
            return false;
        }
    }
    typed.kind = TYPED_ERROR_THROW;
    typed.type_index = error;
    typed.pos = node->pos;
    typed.name_index = TYPED_NO_NODE;
    typed.left_child = TYPED_NO_NODE;
    typed.right_child = TYPED_NO_NODE;
    typed.auxiliary_type = filename_type;
    typed.store_id = TYPED_NO_NODE;
    typed.string_offset = string_offset;
    typed.string_length = filename_length;
    typed.children_start = ctx->program->children.length;
    typed.children_count = 2;
    for (i = 0; i < 2; i++) {
        if (!typed_program_push_child(ctx->program, values[i])) {
            error_report(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
            return false;
        }
    }
    return emit_node(ctx, &typed, ast_index, &(size_t){0});
}



bool check_error_constructor(CheckContext *ctx, const AstNode *node,
                                    size_t ast_index)
{
    size_t error;
    size_t header;
    size_t description;
    size_t filename;
    size_t int32_type;
    size_t expected[6];
    List values;
    size_t i;
    bool ok = false;
    TypedNode typed = {0};

    if (!error_type(ctx, &error) ||
        !error_field_type(ctx, error, 0, &header) ||
        !error_field_type(ctx, error, 1, &description) ||
        !error_field_type(ctx, error, 2, &filename) ||
        !builtin_type(ctx, NIPPY_TYPE_INT32, &int32_type)) {
        return false;
    }
    if (node->children_count != 6) {
        error_report_at(ctx->errors, node->pos,
                        MESSAGE_CHECKER_ERROR_ARGS);
        return false;
    }
    expected[0] = header;
    expected[1] = description;
    expected[2] = filename;
    expected[3] = int32_type;
    expected[4] = int32_type;
    expected[5] = int32_type;

    list_init(&values, sizeof(size_t));
    for (i = 0; i < 6; i++) {
        size_t child = *(const size_t *)list_get(
            &ctx->ast->children, node->children_start + i);
        const AstNode *argument = ast_get_node(ctx->ast, child);
        size_t value;
        size_t coerced;

        if (argument == NULL || argument->kind == AST_NAMED_ARG) {
            error_report_at(ctx->errors, node->pos,
                            MESSAGE_CHECKER_ERROR_ARGS);
            goto done;
        }
        if (!check_expression(ctx, child, &value)) {
            goto done;
        }
        if (!inject_into(ctx, value, expected[i], &coerced)) {
            error_report_at(ctx->errors, argument->pos,
                            MESSAGE_CHECKER_ERROR_ARG_TYPE, i + 1);
            goto done;
        }
        if (!list_push(&values, &coerced)) {
            error_report(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
            goto done;
        }
    }
    typed.kind = TYPED_ERROR_CONSTRUCT;
    typed.type_index = error;
    typed.pos = node->pos;
    typed.name_index = TYPED_NO_NODE;
    typed.left_child = TYPED_NO_NODE;
    typed.right_child = TYPED_NO_NODE;
    typed.auxiliary_type = TYPED_NO_NODE;
    typed.store_id = TYPED_NO_NODE;
    typed.children_start = ctx->program->children.length;
    typed.children_count = values.length;
    for (i = 0; i < values.length; i++) {
        size_t value = *(const size_t *)list_get(&values, i);

        if (!typed_program_push_child(ctx->program, value)) {
            error_report(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
            goto done;
        }
    }
    if (!emit_node(ctx, &typed, ast_index, &(size_t){0})) {
        goto done;
    }
    ok = true;

done:
    list_free(&values);
    return ok;
}

