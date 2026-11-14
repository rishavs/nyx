#include "checker_internal.h"


size_t infer_find(const InferEnv *infer, const char *name)
{
    size_t i;

    for (i = 0; i < infer->count; i++) {
        if (infer->names[i] != NULL && strcmp(infer->names[i], name) == 0) {
            return i;
        }
    }
    return TYPED_NO_NODE;
}


void infer_type_env(const InferEnv *infer, TypeEnv *env)
{
    size_t i;

    env->count = 0;
    for (i = 0; i < infer->count; i++) {
        if (infer->types[i] != TYPED_NO_NODE) {
            env->names[env->count] = infer->names[i];
            env->types[env->count] = infer->types[i];
            env->count++;
        }
    }
}

bool ast_type_has_unbound(CheckContext *ctx, size_t ast_index,
                                 const InferEnv *infer, unsigned depth)
{
    const AstNode *node = ast_get_node(ctx->ast, ast_index);
    size_t i;

    if (node == NULL || depth == 0) {
        return true;
    }
    if (node->kind == AST_TYPE_NAME) {
        const char *name = ast_symbol_text(ctx->ast, node->name_index);
        size_t index = name == NULL ? TYPED_NO_NODE : infer_find(infer, name);

        return index != TYPED_NO_NODE && infer->types[index] == TYPED_NO_NODE;
    }
    if (node->kind == AST_TYPE_UNION || node->kind == AST_TYPE_FUNCTION) {
        for (i = 0; i < node->children_count; i++) {
            size_t child = *(const size_t *)list_get(
                &ctx->ast->children, node->children_start + i);

            if (ast_type_has_unbound(ctx, child, infer, depth - 1)) {
                return true;
            }
        }
        if (node->kind == AST_TYPE_FUNCTION && node->right_child != AST_NO_NODE &&
            ast_type_has_unbound(ctx, node->right_child, infer, depth - 1)) {
            return true;
        }
        return false;
    }
    if (node->kind == AST_TYPE_APPLICATION) {
        const char *name = ast_symbol_text(ctx->ast, node->name_index);

        if (name != NULL && infer_find(infer, name) != TYPED_NO_NODE) {
            return true;
        }
        for (i = 0; i < node->children_count; i++) {
            size_t child = *(const size_t *)list_get(
                &ctx->ast->children, node->children_start + i);

            if (ast_type_has_unbound(ctx, child, infer, depth - 1)) {
                return true;
            }
        }
    }
    return false;
}


bool infer_ast_type(CheckContext *ctx, size_t ast_type,
                           size_t concrete, InferEnv *infer,
                           const char *owner)
{
    const AstNode *node = ast_get_node(ctx->ast, ast_type);
    const TypedType *concrete_type = typed_program_type(ctx->program,
                                                        concrete);
    size_t i;

    if (node == NULL || concrete_type == NULL) {
        return false;
    }
    if (node->kind == AST_TYPE_NAME) {
        const char *name = ast_symbol_text(ctx->ast, node->name_index);
        size_t index = name == NULL ? TYPED_NO_NODE : infer_find(infer, name);

        if (index != TYPED_NO_NODE) {
            if (infer->types[index] == TYPED_NO_NODE) {
                infer->types[index] = concrete;
                return true;
            }
            return same_type(ctx->program, infer->types[index], concrete);
        }
        {
            TypeEnv env = {0};
            size_t expected;

            infer_type_env(infer, &env);
            if (!resolve_type_in_env(ctx, ast_type, &env, owner, &expected)) {
                return false;
            }
            return same_type(ctx->program, expected, concrete);
        }
    }
    if (node->kind == AST_TYPE_APPLICATION) {
        const char *name = ast_symbol_text(ctx->ast, node->name_index);
        const TypeEntry *entry;

        if (name == NULL || infer_find(infer, name) != TYPED_NO_NODE) {
            return false;
        }
        if (strcmp(name, "Handle") == 0 || strcmp(name, "Range") == 0) {
            size_t child;
            size_t element;

            if (concrete_type->kind != (strcmp(name, "Handle") == 0
                                         ? NIPPY_TYPE_HANDLE : NIPPY_TYPE_RANGE) ||
                concrete_type->child_count != 1 ||
                node->children_count != 1) {
                return false;
            }
            child = *(const size_t *)list_get(&ctx->ast->children,
                                              node->children_start);
            element = *(const size_t *)list_get(&ctx->program->type_children,
                                                concrete_type->first_child);
            return infer_ast_type(ctx, child, element, infer, owner);
        }
        entry = find_type(ctx->types, name);
        if (entry == NULL) {
            return false;
        }
        if (entry->kind == TYPE_ENTRY_STRUCT) {
            const char *source_name =
                concrete_type->source_name_index == TYPED_NO_NODE
                    ? NULL
                    : typed_program_symbol_text(
                          ctx->program, concrete_type->source_name_index);

            if (concrete_type->kind != NIPPY_TYPE_STRUCT ||
                source_name == NULL ||
                strcmp(source_name, entry->name) != 0 ||
                concrete_type->child_count != node->children_count) {
                return false;
            }
            for (i = 0; i < node->children_count; i++) {
                size_t child = *(const size_t *)list_get(
                    &ctx->ast->children, node->children_start + i);
                size_t argument = *(const size_t *)list_get(
                    &ctx->program->type_children,
                    concrete_type->first_child + i);

                if (!infer_ast_type(ctx, child, argument, infer, owner)) {
                    return false;
                }
            }
            return true;
        }
        if (ast_type_has_unbound(ctx, ast_type, infer, NIPPY_TYPE_DEPTH)) {
            return false;
        }
        {
            TypeEnv env = {0};
            size_t expected;

            infer_type_env(infer, &env);
            if (!resolve_type_in_env(ctx, ast_type, &env, owner, &expected)) {
                return false;
            }
            return same_type(ctx->program, expected, concrete);
        }
    }
    if (node->kind == AST_TYPE_UNION) {
        if (ast_type_has_unbound(ctx, ast_type, infer, NIPPY_TYPE_DEPTH)) {
            size_t unbound = TYPED_NO_NODE;
            size_t actual_count = concrete_type->kind == NIPPY_TYPE_UNION
                                      ? concrete_type->child_count : 1;
            List actuals;
            List fixed;
            List residual;
            bool ok = false;
            bool out_of_memory = false;

            list_init(&actuals, sizeof(size_t));
            list_init(&fixed, sizeof(size_t));
            list_init(&residual, sizeof(size_t));
            for (i = 0; i < actual_count; i++) {
                size_t actual = concrete_type->kind == NIPPY_TYPE_UNION
                    ? *(const size_t *)list_get(&ctx->program->type_children,
                                                concrete_type->first_child + i)
                    : concrete;
                if (!list_push(&actuals, &actual)) {
                    out_of_memory = true;
                    goto union_infer_done;
                }
            }
            for (i = 0; i < node->children_count; i++) {
                size_t child = *(const size_t *)list_get(
                    &ctx->ast->children, node->children_start + i);
                const AstNode *part = ast_get_node(ctx->ast, child);
                const char *name = part != NULL && part->kind == AST_TYPE_NAME
                    ? ast_symbol_text(ctx->ast, part->name_index) : NULL;
                size_t parameter = name == NULL ? TYPED_NO_NODE
                                                : infer_find(infer, name);
                if (parameter != TYPED_NO_NODE &&
                    infer->types[parameter] == TYPED_NO_NODE) {
                    if (unbound != TYPED_NO_NODE) goto union_infer_done;
                    unbound = parameter;
                } else {
                    TypeEnv env = {0};
                    size_t resolved;
                    if (ast_type_has_unbound(ctx, child, infer, NIPPY_TYPE_DEPTH))
                        goto union_infer_done;
                    infer_type_env(infer, &env);
                    if (!resolve_type_in_env(ctx, child, &env, owner, &resolved))
                        goto union_infer_done;
                    if (!list_push(&fixed, &resolved)) {
                        out_of_memory = true;
                        goto union_infer_done;
                    }
                }
            }
            if (unbound == TYPED_NO_NODE) goto union_infer_done;
            for (i = 0; i < actuals.length; i++) {
                size_t actual = *(const size_t *)list_get(&actuals, i);
                size_t j;
                bool covered = false;
                for (j = 0; j < fixed.length; j++) {
                    if (same_type(ctx->program, actual,
                                  *(const size_t *)list_get(&fixed, j))) {
                        covered = true;
                        break;
                    }
                }
                if (!covered && !list_push(&residual, &actual)) {
                    out_of_memory = true;
                    goto union_infer_done;
                }
            }
            if (residual.length == 1) {
                infer->types[unbound] = *(const size_t *)list_get(&residual, 0);
                ok = true;
            } else if (residual.length > 1) {
                ok = intern_union(ctx, &residual, &infer->types[unbound]);
            } else {
                /* A later argument can still determine this parameter. */
                ok = true;
            }
        union_infer_done:
            list_free(&actuals);
            list_free(&fixed);
            list_free(&residual);
            if (out_of_memory && !ctx->errors->reported) {
                error_report(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
            }
            return ok;
        }
        {
            TypeEnv env = {0};
            size_t expected;

            infer_type_env(infer, &env);
            if (!resolve_type_in_env(ctx, ast_type, &env, owner, &expected)) {
                return false;
            }
            return same_type(ctx->program, expected, concrete);
        }
    }
    if (node->kind == AST_TYPE_FUNCTION) {
        const TypedFunctionSignature *signature;
        size_t first_parameter;
        size_t return_type;

        if (concrete_type->kind != NIPPY_TYPE_FUNCTION) {
            return false;
        }
        signature = typed_program_signature(ctx->program,
                                            concrete_type->signature_index);
        if (signature == NULL ||
            signature->parameter_count != node->children_count) {
            return false;
        }
        first_parameter = signature->first_parameter;
        return_type = signature->return_type;
        for (i = 0; i < node->children_count; i++) {
            size_t child = *(const size_t *)list_get(
                &ctx->ast->children, node->children_start + i);
            size_t parameter = *(const size_t *)list_get(
                &ctx->program->signature_parameters, first_parameter + i);

            if (!infer_ast_type(ctx, child, parameter, infer, owner)) {
                return false;
            }
        }
        if (node->right_child == AST_NO_NODE) {
            const TypedType *resolved = typed_program_type(ctx->program,
                                                           return_type);
            return resolved != NULL && resolved->kind == NIPPY_TYPE_NIL;
        }
        return infer_ast_type(ctx, node->right_child, return_type, infer,
                              owner);
    }
    return false;
}


const char *infer_first_unbound(const InferEnv *infer)
{
    size_t i;

    for (i = 0; i < infer->count; i++) {
        if (infer->types[i] == TYPED_NO_NODE) {
            return infer->names[i];
        }
    }
    return infer->count == 0 ? "?" : infer->names[0];
}


bool infer_finish(CheckContext *ctx, InferEnv *infer, TypeEnv *env,
                         const char *owner)
{
    size_t i;

    env->count = 0;
    for (i = 0; i < infer->count; i++) {
        if (infer->types[i] == TYPED_NO_NODE) {
            error_report(ctx->errors, MESSAGE_CHECKER_CANNOT_INFER_TYPE,
                         infer->names[i], owner);
            return false;
        }
        {
            const TypedType *type = typed_program_type(ctx->program,
                                                       infer->types[i]);

            if (type != NULL && type->kind == NIPPY_TYPE_NIL) {
                error_report(ctx->errors, MESSAGE_CHECKER_NIL_TYPE);
                return false;
            }
        }
        env->names[env->count] = infer->names[i];
        env->types[env->count] = infer->types[i];
        env->count++;
    }
    return true;
}


bool constructor_value_fits(CheckContext *ctx, size_t value,
                                   const TypeEntry *entry, size_t field_index,
                                   size_t argument_index, SourcePos pos,
                                   size_t *out_value)
{
    size_t field_type_index = struct_field_type(ctx->program,
                                                typed_program_node(
                                                    ctx->program,
                                                    struct_declaration_node(
                                                        ctx, entry)),
                                                field_index);

    if (!inject_into(ctx, value, field_type_index, out_value)) {
        error_report_at(ctx->errors, pos,
                        MESSAGE_CHECKER_CONSTRUCTOR_ARG_TYPE,
                        argument_index, entry->name);
        return false;
    }
    return true;
}


const AstNode *template_field(const CheckContext *ctx,
                                     const AstNode *declaration, size_t index,
                                     size_t *out_ast)
{
    size_t child = *(const size_t *)list_get(&ctx->ast->children,
                                             declaration->children_start +
                                                 index);
    if (out_ast != NULL) {
        *out_ast = child;
    }
    return ast_get_node(ctx->ast, child);
}


size_t template_field_index(const CheckContext *ctx,
                                   const AstNode *declaration,
                                   const char *name)
{
    size_t i;

    for (i = 0; i < declaration->children_count; i++) {
        const AstNode *field = template_field(ctx, declaration, i, NULL);
        const char *field_name;

        if (field == NULL) {
            continue;
        }
        field_name = ast_symbol_text(ctx->ast, field->name_index);
        if (field_name != NULL && strcmp(field_name, name) == 0) {
            return i;
        }
    }
    return TYPED_NO_NODE;
}


const char *struct_field_name(const TypedProgram *program,
                                     const TypedNode *type_decl,
                                     size_t field_index)
{
    size_t child = *(const size_t *)list_get(
        &program->children, type_decl->children_start + field_index);
    const TypedNode *field = typed_program_node(program, child);

    return field == NULL ? NULL
                         : typed_program_symbol_text(program, field->name_index);
}


size_t struct_field_type(const TypedProgram *program,
                                const TypedNode *type_decl,
                                size_t field_index)
{
    size_t child = *(const size_t *)list_get(
        &program->children, type_decl->children_start + field_index);
    const TypedNode *field = typed_program_node(program, child);

    return field == NULL ? TYPED_NO_NODE : field->type_index;
}


size_t struct_declaration_node(const CheckContext *ctx,
                                    const TypeEntry *entry)
{
    const TypedType *type;

    if (entry == NULL || entry->type_index == TYPED_NO_NODE) {
        return TYPED_NO_NODE;
    }
    type = typed_program_type(ctx->program, entry->type_index);
    return type == NULL ? TYPED_NO_NODE : type->declaration_node;
}
