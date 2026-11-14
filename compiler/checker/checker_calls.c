#include "checker_internal.h"

static bool check_range_call(CheckContext *ctx, const AstNode *node,
                             size_t ast_index)
{
    size_t values[3], coerced[3], temporaries[3], element, step_type, result, i, start;
    const TypedNode *first, *second;
    const TypedType *type;
    NippyType element_kind;
    NumericConstant c1, c2, step;
    bool first_literal, second_literal;
    TypedNode typed = {0};

    if (node->children_count < 2 || node->children_count > 3) goto invalid;
    for (i = 0; i < node->children_count; i++) {
        size_t child = *(const size_t *)list_get(&ctx->ast->children,
                                                 node->children_start + i);
        const AstNode *arg = ast_get_node(ctx->ast, child);
        if (arg == NULL || arg->kind == AST_NAMED_ARG) goto invalid;
        if (!check_expression(ctx, child, &values[i])) return false;
    }
    first = typed_program_node(ctx->program, values[0]);
    second = typed_program_node(ctx->program, values[1]);
    if (first == NULL || second == NULL) goto invalid;
    first_literal = numeric_constant_read(ctx->program, values[0], false, &c1);
    second_literal = numeric_constant_read(ctx->program, values[1], false, &c2);
    element = first_literal && !second_literal ? second->type_index : first->type_index;
    type = typed_program_type(ctx->program, element);
    if (type == NULL || !nippy_type_is_integer(type->kind)) goto invalid;
    element_kind = type->kind;
    if (
        !inject_into(ctx, values[0], element, &coerced[0]) ||
        !inject_into(ctx, values[1], element, &coerced[1]) ||
        !builtin_type(ctx, NIPPY_TYPE_INT64, &step_type)) goto invalid;
    if (element_kind == NIPPY_TYPE_INT64 &&
        (!check_default_numeric(ctx, coerced[0]) ||
         !check_default_numeric(ctx, coerced[1]))) return false;
    if (node->children_count == 3) {
        if (numeric_constant_read(ctx->program, values[2], true, &step) &&
            !step.is_float && step.magnitude == 0) {
            error_report_at(ctx->errors, node->pos, MESSAGE_CHECKER_RANGE_ZERO_STEP);
            return false;
        }
        if (!inject_into(ctx, values[2], step_type, &coerced[2])) goto invalid;
        if (!check_default_numeric(ctx, coerced[2])) return false;
    }
    if (!range_type(ctx, element, &result)) return false;
    for (i = 0; i < node->children_count; i++) {
        TypedMatchTemporary temporary;
        TypedNode reference = {0};
        size_t *scope = (size_t *)list_get(&ctx->program->match_scopes,
                                           ctx->match_scope);
        const TypedNode *value = typed_program_node(ctx->program, coerced[i]);
        if (scope == NULL || value == NULL) goto invalid;
        temporary.type_index = value->type_index;
        temporary.next = *scope;
        temporaries[i] = ctx->program->match_temporaries.length;
        reference.kind = TYPED_MATCH_TEMPORARY;
        reference.type_index = temporary.type_index;
        reference.name_index = TYPED_NO_NODE;
        reference.pos = value->pos;
        reference.left_child = TYPED_NO_NODE;
        reference.right_child = TYPED_NO_NODE;
        reference.integer_value_u = temporaries[i];
        if (!list_push(&ctx->program->match_temporaries, &temporary) ||
            !typed_program_push(ctx->program, &reference, &values[i])) {
            error_report_fatal(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
            return false;
        }
        *scope = temporaries[i];
    }
    start = ctx->program->children.length;
    for (i = 0; i < node->children_count; i++) {
        if (!typed_program_push_child(ctx->program, values[i])) {
            error_report_fatal(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
            return false;
        }
    }
    typed.kind = TYPED_RANGE_CREATE;
    typed.type_index = result;
    typed.pos = node->pos;
    typed.name_index = TYPED_NO_NODE;
    typed.left_child = TYPED_NO_NODE;
    typed.right_child = TYPED_NO_NODE;
    typed.children_start = start;
    typed.children_count = node->children_count;
    if (!typed_program_push(ctx->program, &typed, &result)) {
        error_report_fatal(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
        return false;
    }
    for (i = node->children_count; i > 0; i--) {
        TypedNode binding = {0};
        binding.kind = TYPED_MATCH_BIND_EXPRESSION;
        binding.type_index = typed.type_index;
        binding.pos = node->pos;
        binding.name_index = TYPED_NO_NODE;
        binding.left_child = coerced[i - 1];
        binding.right_child = result;
        binding.integer_value_u = temporaries[i - 1];
        if (!typed_program_push(ctx->program, &binding, &result)) {
            error_report_fatal(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
            return false;
        }
    }
    *mapping_slot(ctx->mapping, ast_index) = result;
    return true;
invalid:
    if (!ctx->errors->reported)
        error_report_at(ctx->errors, node->pos, MESSAGE_CHECKER_RANGE_ARGUMENTS);
    return false;
}

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

        parameter_type = *(const size_t *)list_get(
            &ctx->program->signature_parameters, first_parameter + i);
        if (!check_expression_expected(ctx, child, parameter_type, &value)) {
            goto done;
        }
        value_node = typed_program_node(ctx->program, value);
        if (value_node == NULL ||
            !inject_into(ctx, value, parameter_type, &coerced)) {
            error_report_at(ctx->errors, node->pos,
                            MESSAGE_CHECKER_FUNCTION_ARG_TYPE, i + 1, name);
            goto done;
        }
        value = coerced;
        if (!list_push(&values, &value)) {
            error_report_fatal(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
            goto done;
        }
    }

    /* Append the argument run only now, after nested calls have appended
       theirs, so this call's children stay contiguous. */
    children_start = ctx->program->children.length;
    for (i = 0; i < values.length; i++) {
        size_t value = *(const size_t *)list_get(&values, i);

        if (!typed_program_push_child(ctx->program, value)) {
            error_report_fatal(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
            goto done;
        }
    }

    typed.kind = TYPED_FUNCTION_CALL;
    typed.type_index = return_type;
    typed.pos = node->pos;
    typed.name_index = typed_program_copy_symbol(ctx->program, name,
                                                 strlen(name));
    if (typed.name_index == TYPED_NO_NODE) {
        error_report_fatal(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_COPYING_NAME);
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


static bool check_const_method_receiver(CheckContext *ctx, size_t ast_index)
{
    const AstNode *root = ast_get_node(ctx->ast, ast_index);
    const char *name;
    const VariableEntry *variable;

    while (root != NULL && root->kind == AST_MEMBER_ACCESS) {
        root = ast_get_node(ctx->ast, root->left_child);
    }
    if (root == NULL || root->kind != AST_IDENTIFIER) {
        return true;
    }
    name = ast_symbol_text(ctx->ast, root->name_index);
    if (name == NULL) {
        return true;
    }
    variable = find_variable(ctx->locals, name);
    if (variable == NULL && !ctx->in_function) {
        variable = find_variable(ctx->globals, name);
    }
    if (variable != NULL && variable->is_const) {
        error_report_at(ctx->errors, root->pos,
                        MESSAGE_CHECKER_CONST_ASSIGN, name);
        return false;
    }
    return true;
}


bool check_associated_function_call(CheckContext *ctx, const AstNode *node,
                                    const FunctionEntry *function,
                                    size_t ast_index, bool is_method)
{
    FunctionEntry target;
    const TypedType *callee_type;
    const TypedFunctionSignature *signature;
    size_t parameter_count;
    size_t first_parameter;
    size_t return_type;
    size_t explicit_parameters;
    size_t children_start;
    size_t i;
    List values;
    TypedNode typed = {0};
    bool ok = false;

    if (function == NULL || function->generic) {
        error_report_at(ctx->errors, node->pos,
                        MESSAGE_CHECKER_UNSUPPORTED_AST_NODE);
        return false;
    }
    target = *function;
    callee_type = typed_program_type(ctx->program, target.type_index);
    signature = callee_type == NULL || callee_type->kind != NIPPY_TYPE_FUNCTION
                    ? NULL
                    : typed_program_signature(ctx->program,
                                              callee_type->signature_index);
    if (signature == NULL ||
        (is_method && signature->parameter_count == 0)) {
        error_report_at(ctx->errors, node->pos,
                        MESSAGE_CHECKER_UNSUPPORTED_AST_NODE);
        return false;
    }
    parameter_count = signature->parameter_count;
    first_parameter = signature->first_parameter;
    return_type = signature->return_type;
    explicit_parameters = parameter_count - (is_method ? 1u : 0u);
    if (node->children_count != explicit_parameters) {
        error_report_at(ctx->errors, node->pos,
                        MESSAGE_CHECKER_FUNCTION_ARG_COUNT,
                        target.method_name, explicit_parameters,
                        node->children_count);
        return false;
    }

    list_init(&values, sizeof(size_t));
    if (is_method) {
        const AstNode *receiver_ast = ast_get_node(ctx->ast,
                                                  node->left_child);
        size_t receiver;
        const TypedNode *receiver_node;
        size_t expected_type = *(const size_t *)list_get(
            &ctx->program->signature_parameters, first_parameter);

        if (receiver_ast == NULL ||
            !field_is_lvalue(ctx->ast, node->left_child)) {
            error_report_at(ctx->errors, node->pos,
                            MESSAGE_CHECKER_METHOD_RECEIVER,
                            target.method_name);
            goto done;
        }
        if (!check_inline_const_mutation(ctx, node->left_child)) {
            goto done;
        }
        if (!check_const_method_receiver(ctx, node->left_child)) {
            goto done;
        }
        if (!check_expression(ctx, node->left_child, &receiver)) {
            goto done;
        }
        receiver_node = typed_program_node(ctx->program, receiver);
        if (receiver_node == NULL ||
            !same_type(ctx->program, receiver_node->type_index,
                       expected_type)) {
            error_report_at(ctx->errors, node->pos,
                            MESSAGE_CHECKER_METHOD_RECEIVER,
                            target.method_name);
            goto done;
        }
        if (!list_push(&values, &receiver)) {
            error_report_fatal(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
            goto done;
        }
    }

    for (i = 0; i < node->children_count; i++) {
        size_t child = *(const size_t *)list_get(&ctx->ast->children,
                                                 node->children_start + i);
        const AstNode *argument = ast_get_node(ctx->ast, child);
        size_t value;
        size_t parameter_type;
        size_t coerced;
        size_t parameter_offset = first_parameter +
                                  (is_method ? i + 1 : i);

        if (argument == NULL || argument->kind == AST_NAMED_ARG) {
            error_report_at(ctx->errors,
                            argument == NULL ? node->pos : argument->pos,
                            MESSAGE_CHECKER_NAMED_FUNCTION_ARG);
            goto done;
        }
        parameter_type = *(const size_t *)list_get(
            &ctx->program->signature_parameters, parameter_offset);
        if (!check_expression_expected(ctx, child, parameter_type, &value)) {
            goto done;
        }
        if (!inject_into(ctx, value, parameter_type, &coerced)) {
            error_report_at(ctx->errors, node->pos,
                            MESSAGE_CHECKER_FUNCTION_ARG_TYPE, i + 1,
                            target.method_name);
            goto done;
        }
        if (!list_push(&values, &coerced)) {
            error_report_fatal(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
            goto done;
        }
    }

    children_start = ctx->program->children.length;
    for (i = 0; i < values.length; i++) {
        size_t value = *(const size_t *)list_get(&values, i);

        if (!typed_program_push_child(ctx->program, value)) {
            error_report_fatal(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
            goto done;
        }
    }
    typed.kind = TYPED_FUNCTION_CALL;
    typed.type_index = return_type;
    typed.pos = node->pos;
    typed.name_index = typed_program_copy_symbol(ctx->program, target.name,
                                                 strlen(target.name));
    if (typed.name_index == TYPED_NO_NODE) {
        error_report_fatal(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_COPYING_NAME);
        goto done;
    }
    typed.left_child = TYPED_NO_NODE;
    typed.right_child = TYPED_NO_NODE;
    typed.children_start = children_start;
    typed.children_count = values.length;
    typed.is_method_call = is_method;
    ok = emit_node(ctx, &typed, ast_index, &(size_t){0});

done:
    list_free(&values);
    return ok;
}

bool check_numeric_conversion(CheckContext *ctx, const AstNode *node,
                                     const char *name, NippyType target_kind,
                                     size_t ast_index)
{
    size_t argument_ast;
    size_t argument;
    const TypedNode *argument_node;
    const TypedType *argument_type;
    size_t target_index;
    TypedNode typed = {0};

    if (node->right_child != AST_NO_NODE || node->children_count != 1) {
        error_report_at(ctx->errors, node->pos,
                        MESSAGE_CHECKER_CONVERSION_ARGS);
        return false;
    }
    argument_ast = *(const size_t *)list_get(&ctx->ast->children,
                                             node->children_start);
    if (!check_expression(ctx, argument_ast, &argument)) {
        return false;
    }
    argument_node = typed_program_node(ctx->program, argument);
    argument_type = argument_node == NULL
                        ? NULL
                        : typed_program_type(ctx->program,
                                             argument_node->type_index);
    if (argument_type == NULL ||
        !nippy_type_is_numeric(argument_type->kind)) {
        error_report_at(ctx->errors, node->pos,
                        MESSAGE_CHECKER_CONVERSION_TYPE);
        return false;
    }
    if (!add_builtin_type(ctx, target_kind, &target_index)) {
        return false;
    }
    typed.kind = TYPED_NUMERIC_CONVERSION;
    typed.type_index = target_index;
    typed.auxiliary_type = argument_node->type_index;
    typed.pos = node->pos;
    {
        /* A constant converts at compile time and must fit. The folded node
           names its own target as the source type, which is how later stages
           tell it needs no runtime check; it also keeps the explicit type, so
           context never retypes it. */
        NumericConstant source;
        NumericConstant folded;

        if (numeric_constant_read(ctx->program, argument, true, &source)) {
            if (!numeric_constant_convert(&source, target_kind, &folded)) {
                error_report_at(ctx->errors, node->pos,
                                MESSAGE_CHECKER_CONVERSION_RANGE);
                return false;
            }
            if (!numeric_constant_emit(ctx, &folded, target_kind,
                                       target_index, node->pos, &argument)) {
                return false;
            }
            typed.auxiliary_type = target_index;
        }
    }
    typed.name_index = typed_program_copy_symbol(ctx->program, name,
                                                 strlen(name));
    if (typed.name_index == TYPED_NO_NODE) {
        error_report_fatal(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_COPYING_NAME);
        return false;
    }
    typed.left_child = argument;
    typed.right_child = TYPED_NO_NODE;
    typed.children_start = 0;
    typed.children_count = 0;
    return emit_node(ctx, &typed, ast_index, &(size_t){0});
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
    if (node->is_static) {
        const char *owner = ast_symbol_text(ctx->ast,
                                            node->second_name_index);
        if (node->owner_type_index != AST_NO_NODE &&
            !resolve_type_in_env(ctx, node->owner_type_index, ctx->env,
                                 owner, &(size_t){0})) {
            return false;
        }
        if (owner != NULL && strcmp(owner, "Error") == 0 &&
            strcmp(name, "throw") == 0) {
            return check_error_throw(ctx, node, ast_index);
        }
        const FunctionEntry *associated =
            find_associated_function(ctx->functions, owner, name);
        char full_name[NIPPY_IDENTIFIER_MAX * 2 + 4];

        if (associated == NULL || !associated->is_static) {
            snprintf(full_name, sizeof(full_name), "%s::%s",
                     owner == NULL ? "<missing>" : owner, name);
            error_report_at(ctx->errors, node->pos,
                            MESSAGE_CHECKER_UNKNOWN_CALL, full_name);
            return false;
        }
        if (associated->generic) {
            size_t index = find_function_index(ctx,
                                               associated->name);

            return check_generic_call(ctx, node, index, ast_index);
        }
        return check_associated_function_call(ctx, node, associated,
                                              ast_index, false);
    }
    if (strcmp(name, "print") == 0) {
        return check_print(ctx, node, ast_index);
    }
    if (strcmp(name, "base64") == 0) {
        return check_base64(ctx, node, ast_index);
    }
    if (strcmp(name, "range") == 0) {
        return check_range_call(ctx, node, ast_index);
    }
    {
        NippyType target_kind;

        if (numeric_type_from_name(name, &target_kind)) {
            return check_numeric_conversion(ctx, node, name, target_kind,
                                            ast_index);
        }
    }
    if (strcmp(name, "Error") == 0) {
        return check_error_constructor(ctx, node, ast_index);
    }
    if (strcmp(name, "String") == 0 ||
        strcmp(name, "Text") == 0 ||
        strcmp(name, "TextBuilder") == 0 ||
        strcmp(name, "List") == 0 ||
        strcmp(name, "Array") == 0 ||
        strcmp(name, "Slice") == 0 || strcmp(name, "Range") == 0) {
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
        size_t function_index = find_function_index(ctx, name);

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
    if (strcmp(name, "Bool") == 0 || strcmp(name, "Nil") == 0) {
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
    size_t previous_match_scope = ctx->match_scope;
    List fresh_mapping;
    List locals;
    List parameter_nodes;
    size_t children_start;
    size_t typed_body;
    size_t typed_index;
    size_t i;
    bool ok = false;

    if (declaration == NULL ||
        !check_type_value_size(ctx, return_type, declaration->pos)) {
        return false;
    }
    list_init(&fresh_mapping, sizeof(size_t));
    list_init(&locals, sizeof(VariableEntry));
    list_init(&parameter_nodes, sizeof(size_t));
    {
        size_t empty = TYPED_NO_NODE;
        ctx->match_scope = ctx->program->match_scopes.length;
        if (!list_push(&ctx->program->match_scopes, &empty)) {
            error_report_fatal(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
            goto done;
        }
    }
    for (i = 0; i < ctx->ast->nodes.length; i++) {
        size_t none = TYPED_NO_NODE;

        if (!list_push(&fresh_mapping, &none)) {
            error_report_fatal(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
            goto done;
        }
    }
    ctx->mapping = &fresh_mapping;
    ctx->env = env;
    ctx->in_function = true;
    ctx->locals = &locals;
    ctx->loop_depth = 0;
    ctx->function_name = entry->is_method || entry->is_static
                             ? entry->method_name : entry->name;
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
        if (is_builtin_function_name(parameter_name)) {
            error_report_at(ctx->errors, parameter->pos,
                            MESSAGE_CHECKER_BUILTIN_RESERVED, parameter_name);
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
        if (!check_type_value_size(ctx, parameter_type, parameter->pos)) {
            goto done;
        }
        typed_parameter.kind = TYPED_PARAMETER;
        typed_parameter.type_index = parameter_type;
        typed_parameter.pos = parameter->pos;
        typed_parameter.name_index = typed_program_copy_symbol(
            ctx->program, parameter_name, strlen(parameter_name));
        if (typed_parameter.name_index == TYPED_NO_NODE) {
            error_report_fatal(ctx->errors,
                         MESSAGE_CHECKER_OUT_OF_MEMORY_COPYING_NAME);
            goto done;
        }
        typed_parameter.left_child = TYPED_NO_NODE;
        typed_parameter.right_child = TYPED_NO_NODE;
        typed_parameter.is_self = parameter->is_self;
        if (!typed_program_push(ctx->program, &typed_parameter,
                                &parameter_index) ||
            !list_push(&parameter_nodes, &parameter_index)) {
            error_report_fatal(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_PROGRAM);
            goto done;
        }
        memset(&local, 0, sizeof(local));
        memcpy(local.name, parameter_name, strlen(parameter_name) + 1);
        local.type_index = parameter_type;
        local.is_self_pointer = parameter->is_self;
        if (!list_push(&locals, &local)) {
            error_report_fatal(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
            goto done;
        }
    }

    if (!check_block(ctx, declaration->left_child, return_type, &typed_body)) {
        goto done;
    }
    if (declaration->right_child != AST_NO_NODE &&
        !typed_block_returns(ctx->program, typed_body)) {
        error_report_at(ctx->errors, declaration->pos,
                        MESSAGE_CHECKER_FUNCTION_MAY_NOT_RETURN,
                        ctx->function_name);
        goto done;
    }

    children_start = ctx->program->children.length;
    for (i = 0; i < parameter_nodes.length; i++) {
        size_t parameter_index = *(const size_t *)list_get(&parameter_nodes, i);

        if (!typed_program_push_child(ctx->program, parameter_index)) {
            error_report_fatal(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_PROGRAM);
            goto done;
        }
    }
    {
        TypedNode typed = {0};

        typed.kind = TYPED_FUNCTION_DECLARATION;
        typed.integer_value_u = ctx->match_scope;
        typed.type_index = function_type;
        typed.pos = declaration->pos;
        typed.name_index = name_index;
        typed.left_child = typed_body;
        typed.right_child = TYPED_NO_NODE;
        typed.is_method = declaration->is_method;
        typed.children_start = children_start;
        typed.children_count = declaration->children_count;
        if (!typed_program_push(ctx->program, &typed, &typed_index)) {
            error_report_fatal(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_PROGRAM);
            goto done;
        }
        if (!list_push(&ctx->program->roots, &typed_index)) {
            error_report_fatal(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
            goto done;
        }
    }
    ok = true;

done:
    ctx->match_scope = previous_match_scope;
    ctx->mapping = previous_mapping;
    ctx->env = previous_env;
    ctx->locals = previous_locals;
    ctx->in_function = previous_in_function;
    ctx->loop_depth = previous_loop;
    ctx->function_name = previous_name;
    ctx->narrowings->length = previous_narrowings;
    list_free(&fresh_mapping);
    list_free(&locals);
    list_free(&parameter_nodes);
    return ok;
}

bool check_generic_call(CheckContext *ctx, const AstNode *node,
                               size_t function_index, size_t ast_index)
{
    const FunctionEntry *entry =
        (const FunctionEntry *)list_get(ctx->functions, function_index);
    const AstNode *declaration = ast_get_node(ctx->ast, entry->ast_index);
    const char *display_name = entry->is_method || entry->is_static
                                   ? entry->method_name : entry->name;
    InferEnv infer = {0};
    TypeEnv env = {0};
    List values;
    List parameter_types;
    size_t return_type;
    size_t function_type;
    FunctionInstantiation *record;
    size_t i;
    bool is_method = entry->is_method;
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
    if (node->children_count + (is_method ? 1u : 0u) !=
        declaration->children_count) {
        error_report_at(ctx->errors, node->pos,
                        MESSAGE_CHECKER_FUNCTION_ARG_COUNT, display_name,
                        declaration->children_count - (is_method ? 1u : 0u),
                        node->children_count);
        return false;
    }
    if (is_method && !field_is_lvalue(ctx->ast, node->left_child)) {
        error_report_at(ctx->errors, node->pos,
                        MESSAGE_CHECKER_METHOD_RECEIVER, entry->method_name);
        return false;
    }
    if (is_method &&
        (!check_inline_const_mutation(ctx, node->left_child) ||
         !check_const_method_receiver(ctx, node->left_child))) {
        return false;
    }

    for (i = 0; i < entry->parameter_count; i++) {
        infer.names[i] = entry->parameters[i];
        infer.types[i] = TYPED_NO_NODE;
    }
    infer.count = entry->parameter_count;

    if (entry->is_static && entry->owner_parameter_count > 0) {
        size_t owner_type_index;
        const TypedType *owner_type;

        if (node->owner_type_index == AST_NO_NODE) {
            error_report_at(ctx->errors, node->pos,
                            MESSAGE_CHECKER_MISSING_TYPE_ARGUMENTS,
                            entry->owner_name);
            return false;
        }
        if (!resolve_type_in_env(ctx, node->owner_type_index, ctx->env,
                                 entry->owner_name, &owner_type_index)) {
            return false;
        }
        owner_type = typed_program_type(ctx->program, owner_type_index);
        if (owner_type == NULL || owner_type->kind != NIPPY_TYPE_STRUCT ||
            owner_type->child_count != entry->owner_parameter_count) {
            error_report_at(ctx->errors, node->pos,
                            MESSAGE_CHECKER_UNSUPPORTED_AST_NODE);
            return false;
        }
        for (i = 0; i < entry->owner_parameter_count; i++) {
            infer.types[i] = *(const size_t *)list_get(
                &ctx->program->type_children, owner_type->first_child + i);
        }
    }

    list_init(&values, sizeof(size_t));
    list_init(&parameter_types, sizeof(size_t));
    for (i = 0; i < declaration->children_count; i++) {
        size_t argument_ast = is_method && i == 0
                                  ? node->left_child
                                  : *(const size_t *)list_get(
                                        &ctx->ast->children,
                                        node->children_start + i -
                                            (is_method ? 1u : 0u));
        size_t parameter_ast = *(const size_t *)list_get(
            &ctx->ast->children, declaration->children_start + i);
        const AstNode *parameter = ast_get_node(ctx->ast, parameter_ast);
        size_t value;
        const TypedNode *value_node;
        const AstNode *parameter_type_node;
        size_t bound_parameter = TYPED_NO_NODE;
        bool defer_string_inference = false;

        if (parameter == NULL || parameter->kind != AST_PARAMETER) {
            error_report(ctx->errors, MESSAGE_CHECKER_MISSING_AST_NODE,
                         parameter_ast);
            goto done;
        }
        parameter_type_node = ast_get_node(ctx->ast, parameter->left_child);
        if (parameter_type_node != NULL &&
            parameter_type_node->kind == AST_TYPE_NAME) {
            const char *type_name = ast_symbol_text(
                ctx->ast, parameter_type_node->name_index);

            if (type_name != NULL) {
                bound_parameter = infer_find(&infer, type_name);
            }
        }
        if (!check_expression_expected(
                ctx, argument_ast,
                bound_parameter == TYPED_NO_NODE
                    ? TYPED_NO_NODE : infer.types[bound_parameter],
                &value)) {
            goto done;
        }
        if (bound_parameter != TYPED_NO_NODE &&
            infer.types[bound_parameter] == TYPED_NO_NODE &&
            !check_default_numeric(ctx, value)) {
            goto done;
        }
        value_node = typed_program_node(ctx->program, value);
        if (value_node != NULL && value_node->kind == TYPED_STRING_LITERAL &&
            !value_node->fixed_type && bound_parameter != TYPED_NO_NODE &&
            infer.types[bound_parameter] == TYPED_NO_NODE) {
            const AstNode *result_type_node = ast_get_node(
                ctx->ast, declaration->right_child);
            const TypedType *expected = ctx->expected_ast == ast_index
                ? typed_program_type(ctx->program, ctx->expected_type) : NULL;

            if (result_type_node != NULL &&
                result_type_node->kind == AST_TYPE_NAME &&
                result_type_node->name_index == parameter_type_node->name_index &&
                expected != NULL &&
                (expected->kind == NIPPY_TYPE_STRING ||
                 expected->kind == NIPPY_TYPE_DYNAMIC_STRING)) {
                infer.types[bound_parameter] = ctx->expected_type;
            } else {
                defer_string_inference = true;
            }
        }
        if (value_node == NULL ||
            (!defer_string_inference &&
             ast_type_has_unbound(ctx, parameter->left_child, &infer,
                                  NIPPY_TYPE_DEPTH) &&
             !infer_ast_type(ctx, parameter->left_child,
                             value_node->type_index, &infer, display_name))) {
            error_report_at(ctx->errors, node->pos,
                            MESSAGE_CHECKER_CANNOT_INFER_TYPE,
                            infer_first_unbound(&infer), display_name);
            goto done;
        }
        if (!list_push(&values, &value)) {
            error_report_fatal(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
            goto done;
        }
    }
    if (!infer_finish(ctx, &infer, &env, display_name)) {
        goto done;
    }

    for (i = 0; i < declaration->children_count; i++) {
        size_t parameter_ast = *(const size_t *)list_get(
            &ctx->ast->children, declaration->children_start + i);
        const AstNode *parameter = ast_get_node(ctx->ast, parameter_ast);
        size_t parameter_type;

        if (!resolve_type_in_env(ctx, parameter->left_child, &env,
                                 display_name, &parameter_type) ||
            !list_push(&parameter_types, &parameter_type)) {
            if (!ctx->errors->reported) {
                error_report_fatal(ctx->errors,
                             MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
            }
            goto done;
        }
    }
    if (!(declaration->right_child == AST_NO_NODE
              ? builtin_type(ctx, NIPPY_TYPE_NIL, &return_type)
              : resolve_type_in_env(ctx, declaration->right_child, &env,
                                    display_name, &return_type))) {
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

        if (ctx->function_instantiations->length >=
            NIPPY_MAX_INSTANTIATIONS) {
            error_report(ctx->errors, MESSAGE_CHECKER_TOO_MANY_INSTANTIATIONS,
                         display_name, NIPPY_MAX_INSTANTIATIONS);
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
            error_report_fatal(ctx->errors,
                         MESSAGE_CHECKER_OUT_OF_MEMORY_COPYING_NAME);
            goto done;
        }
        if (!list_push(ctx->function_instantiations, &fresh)) {
            error_report_fatal(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
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
                    if (is_method && i == 0) {
                        error_report_at(ctx->errors, node->pos,
                                        MESSAGE_CHECKER_METHOD_RECEIVER,
                                        display_name);
                    } else {
                        error_report_at(ctx->errors, node->pos,
                                        MESSAGE_CHECKER_FUNCTION_ARG_TYPE,
                                        i + 1 - (is_method ? 1u : 0u),
                                        display_name);
                    }
                }
                list_free(&coerced);
                goto done;
            }
        }
        children_start = ctx->program->children.length;
        for (i = 0; i < coerced.length; i++) {
            size_t value = *(const size_t *)list_get(&coerced, i);

            if (!typed_program_push_child(ctx->program, value)) {
                error_report_fatal(ctx->errors,
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
            typed.is_method_call = is_method;
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

