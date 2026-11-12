#include "checker_internal.h"



bool check_nil_literal(CheckContext *ctx, const AstNode *node,
                              size_t ast_index)
{
    size_t nil_type;
    TypedNode typed = {0};

    if (!builtin_type(ctx, NIPPY_TYPE_NIL, &nil_type)) {
        return false;
    }
    typed.kind = TYPED_NIL_LITERAL;
    typed.type_index = nil_type;
    typed.pos = node->pos;
    typed.name_index = TYPED_NO_NODE;
    typed.left_child = TYPED_NO_NODE;
    typed.right_child = TYPED_NO_NODE;
    return emit_node(ctx, &typed, ast_index, &(size_t){0});
}


bool check_record_literal(CheckContext *ctx, const AstNode *node,
                                   size_t ast_index)
{
    TypedNode typed = {0};
    List children;
    size_t children_start;
    size_t i;

    if (node->children_count % 2 != 0) {
        error_report_at(ctx->errors, node->pos,
                        MESSAGE_CHECKER_RECORD_LITERAL_CONTEXT);
        return false;
    }
    list_init(&children, sizeof(size_t));
    for (i = 0; i < node->children_count; i += 2) {
        size_t key_ast = *(const size_t *)list_get(
            &ctx->ast->children, node->children_start + i);
        size_t value_ast = *(const size_t *)list_get(
            &ctx->ast->children, node->children_start + i + 1);
        size_t key;
        size_t value;

        if (!check_expression(ctx, key_ast, &key) ||
            !check_expression(ctx, value_ast, &value) ||
            !list_push(&children, &key) ||
            !list_push(&children, &value)) {
            list_free(&children);
            return false;
        }
    }
    children_start = ctx->program->children.length;
    for (i = 0; i < children.length; i++) {
        size_t child = *(const size_t *)list_get(&children, i);

        if (!typed_program_push_child(ctx->program, child)) {
            error_report(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
            list_free(&children);
            return false;
        }
    }
    typed.kind = TYPED_RECORD_LITERAL;
    typed.type_index = TYPED_NO_NODE;
    typed.pos = node->pos;
    typed.name_index = TYPED_NO_NODE;
    typed.left_child = TYPED_NO_NODE;
    typed.right_child = TYPED_NO_NODE;
    typed.auxiliary_type = TYPED_NO_NODE;
    typed.store_id = TYPED_NO_NODE;
    typed.children_start = children_start;
    typed.children_count = children.length;
    list_free(&children);
    return emit_node(ctx, &typed, ast_index, &(size_t){0});
}


bool check_list_literal(CheckContext *ctx, const AstNode *node,
                                size_t ast_index)
{
    TypedNode typed = {0};
    List checked;
    size_t candidate = TYPED_NO_NODE;
    size_t list_index;
    size_t children_start;
    bool mixed = false;
    bool has_strings = false;
    size_t i;

    list_init(&checked, sizeof(size_t));
    for (i = 0; i < node->children_count; i++) {
        size_t child_ast = *(const size_t *)list_get(
            &ctx->ast->children, node->children_start + i);
        size_t child;

        if (!check_expression(ctx, child_ast, &child) ||
            !list_push(&checked, &child)) {
            if (!ctx->errors->reported) {
                error_report(ctx->errors,
                             MESSAGE_CHECKER_OUT_OF_MEMORY_PROGRAM);
            }
            list_free(&checked);
            return false;
        }
    }
    if (checked.length == 0) {
        typed.kind = TYPED_LIST_LITERAL;
        typed.type_index = TYPED_NO_NODE;
        typed.pos = node->pos;
        typed.name_index = TYPED_NO_NODE;
        typed.left_child = TYPED_NO_NODE;
        typed.right_child = TYPED_NO_NODE;
        typed.auxiliary_type = TYPED_NO_NODE;
        typed.store_id = TYPED_NO_NODE;
        typed.children_start = ctx->program->children.length;
        typed.children_count = 0;
        list_free(&checked);
        return emit_node(ctx, &typed, ast_index, &(size_t){0});
    }
    for (i = 0; i < checked.length; i++) {
        size_t child = *(const size_t *)list_get(&checked, i);
        const TypedNode *child_node = typed_program_node(ctx->program, child);
        const TypedType *child_type =
            child_node == NULL
                ? NULL
                : typed_program_type(ctx->program, child_node->type_index);

        if (child_type == NULL) {
            error_report_at(ctx->errors, node->pos,
                            MESSAGE_CHECKER_LIST_INFER);
            list_free(&checked);
            return false;
        }
        if (child_type->kind == NIPPY_TYPE_STRING_LITERAL) {
            has_strings = true;
            continue;
        }
        if (candidate == TYPED_NO_NODE) {
            candidate = child_node->type_index;
        } else if (!same_type(ctx->program, candidate,
                              child_node->type_index)) {
            mixed = true;
        }
    }
    if (candidate == TYPED_NO_NODE) {
        if (!has_strings ||
            !builtin_type(ctx, NIPPY_TYPE_DYNAMIC_STRING, &candidate)) {
            if (!has_strings) {
                list_free(&checked);
                return false;
            }
            list_free(&checked);
            return false;
        }
    } else if (has_strings) {
        const TypedType *candidate_type = typed_program_type(
            ctx->program, candidate);

        if (candidate_type == NULL ||
            (candidate_type->kind != NIPPY_TYPE_STRING &&
             candidate_type->kind != NIPPY_TYPE_DYNAMIC_STRING)) {
            mixed = true;
        }
    }
    if (mixed) {
        children_start = ctx->program->children.length;
        for (i = 0; i < checked.length; i++) {
            size_t child = *(const size_t *)list_get(&checked, i);

            if (!typed_program_push_child(ctx->program, child)) {
                error_report(ctx->errors,
                             MESSAGE_CHECKER_OUT_OF_MEMORY_PROGRAM);
                list_free(&checked);
                return false;
            }
        }
        list_free(&checked);
        typed.kind = TYPED_LIST_LITERAL;
        typed.type_index = TYPED_NO_NODE;
        typed.pos = node->pos;
        typed.name_index = TYPED_NO_NODE;
        typed.left_child = TYPED_NO_NODE;
        typed.right_child = TYPED_NO_NODE;
        typed.auxiliary_type = TYPED_NO_NODE;
        typed.store_id = TYPED_NO_NODE;
        typed.children_start = children_start;
        typed.children_count = node->children_count;
        return emit_node(ctx, &typed, ast_index, &(size_t){0});
    }
    if (!list_type(ctx, candidate, true, checked.length, &list_index)) {
        list_free(&checked);
        return false;
    }
    children_start = ctx->program->children.length;
    for (i = 0; i < checked.length; i++) {
        size_t child = *(const size_t *)list_get(&checked, i);
        size_t coerced;

        if (!inject_into(ctx, child, candidate, &coerced) ||
            !typed_program_push_child(ctx->program, coerced)) {
            if (!ctx->errors->reported) {
                error_report_at(ctx->errors, node->pos,
                                MESSAGE_CHECKER_LIST_INFER);
            }
            list_free(&checked);
            return false;
        }
    }
    list_free(&checked);
    typed.kind = TYPED_LIST_LITERAL;
    typed.type_index = list_index;
    typed.pos = node->pos;
    typed.name_index = TYPED_NO_NODE;
    typed.left_child = TYPED_NO_NODE;
    typed.right_child = TYPED_NO_NODE;
    typed.auxiliary_type = TYPED_NO_NODE;
    typed.store_id = TYPED_NO_NODE;
    typed.children_start = children_start;
    typed.children_count = node->children_count;
    return emit_node(ctx, &typed, ast_index, &(size_t){0});
}


bool check_string_literal(CheckContext *ctx, const AstNode *node,
                                 size_t ast_index)
{
    size_t literal_type;
    size_t offset = ctx->program->bytes.length;
    size_t i;
    TypedNode typed = {0};

    for (i = 0; i < node->string_length; i++) {
        const unsigned char *byte = list_get(
            &ctx->ast->bytes, node->string_offset + i);

        if (byte == NULL ||
            !typed_program_push_byte(ctx->program, *byte)) {
            error_report(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
            return false;
        }
    }
    if (!string_literal_type(ctx, &literal_type)) {
        return false;
    }
    typed.kind = TYPED_STRING_LITERAL;
    typed.type_index = literal_type;
    typed.pos = node->pos;
    typed.name_index = TYPED_NO_NODE;
    typed.left_child = TYPED_NO_NODE;
    typed.right_child = TYPED_NO_NODE;
    typed.auxiliary_type = TYPED_NO_NODE;
    typed.store_id = TYPED_NO_NODE;
    typed.string_offset = offset;
    typed.string_length = node->string_length;
    return emit_node(ctx, &typed, ast_index, &(size_t){0});
}


bool check_is(CheckContext *ctx, const AstNode *node, size_t ast_index)
{
    size_t value;
    size_t tested;
    const TypedNode *value_node;
    const TypedType *value_type;
    size_t tested_type;
    size_t bool_type;
    int result;
    TypedNode typed = {0};

    if (!check_expression(ctx, node->left_child, &value)) {
        return false;
    }
    if (!resolve_type_in_env(ctx, node->right_child, ctx->env, "is", &tested)) {
        return false;
    }
    tested_type = tested;
    {
        const TypedType *target = typed_program_type(ctx->program, tested_type);

        if (target != NULL && target->kind == NIPPY_TYPE_UNION) {
            error_report_at(ctx->errors, node->pos,
                            MESSAGE_CHECKER_IS_TYPE_IS_UNION);
            return false;
        }
    }
    value_node = typed_program_node(ctx->program, value);
    value_type = value_node == NULL
                     ? NULL
                     : typed_program_type(ctx->program, value_node->type_index);
    if (value_type == NULL) {
        error_report_at(ctx->errors, node->pos,
                        MESSAGE_CHECKER_UNSUPPORTED_AST_NODE);
        return false;
    }
    if (value_type->kind == NIPPY_TYPE_STRING_LITERAL) {
        error_report_at(ctx->errors, node->pos,
                        MESSAGE_CHECKER_STRING_LITERAL_CONTEXT);
        return false;
    }

    if (same_type(ctx->program, value_node->type_index, tested_type)) {
        result = 1;
    } else if (value_type->kind == NIPPY_TYPE_UNION) {
        size_t tag;

        if (union_has_alternative(ctx->program, value_type, tested_type, &tag)) {
            result = (int)tag;
        } else {
            result = 0;
        }
    } else {
        result = 0;
    }

    if (!builtin_type(ctx, NIPPY_TYPE_BOOL, &bool_type)) {
        return false;
    }
    typed.kind = TYPED_IS;
    typed.type_index = bool_type;
    typed.auxiliary_type = tested_type;
    typed.pos = node->pos;
    typed.integer_value = result;
    typed.name_index = TYPED_NO_NODE;
    typed.left_child = value;
    typed.right_child = TYPED_NO_NODE;
    return emit_node(ctx, &typed, ast_index, &(size_t){0});
}


bool check_literal(CheckContext *ctx, const AstNode *node,
                          size_t ast_index)
{
    size_t int32_type;
    TypedNode typed = {0};

    if (!add_type_record(ctx, NIPPY_TYPE_INT32, TYPED_NO_NODE, TYPED_NO_NODE,
                         &int32_type)) {
        return false;
    }

    typed.kind = TYPED_INTEGER_LITERAL;
    typed.type_index = int32_type;
    typed.pos = node->pos;
    typed.integer_value = node->integer_value;
    typed.name_index = TYPED_NO_NODE;
    typed.left_child = TYPED_NO_NODE;
    typed.right_child = TYPED_NO_NODE;
    typed.children_start = 0;
    typed.children_count = 0;

    return emit_node(ctx, &typed, ast_index, &(size_t){0});
}


bool check_identifier(CheckContext *ctx, const AstNode *node,
                             size_t ast_index)
{
    const char *name = ast_symbol_text(ctx->ast, node->name_index);
    const VariableEntry *variable;
    const FunctionEntry *function;
    size_t type_index;
    TypedNode typed = {0};

    if (name == NULL) {
        error_report_at(ctx->errors, node->pos,
                        MESSAGE_CHECKER_MISSING_NAME_OR_TYPE);
        return false;
    }
    if (find_store(ctx->stores, name) != NULL) {
        error_report_at(ctx->errors, node->pos,
                        MESSAGE_CHECKER_STORE_NOT_VALUE, name);
        return false;
    }

    variable = find_variable(ctx->locals, name);
    if (variable == NULL && !ctx->in_function) {
        variable = find_variable(ctx->globals, name);
    }
    if (variable != NULL) {
        type_index = variable->type_index;
    } else {
        function = find_function(ctx->functions, name);
        if (function != NULL) {
            if (function->generic) {
                error_report_at(ctx->errors, node->pos,
                                MESSAGE_CHECKER_GENERIC_AS_VALUE, name);
                return false;
            }
            type_index = function->type_index;
        } else if (ctx->in_function && find_variable(ctx->globals, name)) {
            error_report_at(ctx->errors, node->pos,
                            MESSAGE_CHECKER_GLOBAL_IN_FUNCTION, name);
            return false;
        } else if (find_type(ctx->types, name) != NULL ||
                   strcmp(name, "Int32") == 0 || strcmp(name, "Bool") == 0 ||
                   strcmp(name, "Nil") == 0 ||
                   strcmp(name, "String") == 0 ||
                   strcmp(name, "Text") == 0 ||
                   strcmp(name, "TextBuilder") == 0 ||
                   strcmp(name, "List") == 0 ||
                   strcmp(name, "Array") == 0 ||
                   strcmp(name, "Slice") == 0 ||
                   strcmp(name, "Error") == 0) {
            error_report_at(ctx->errors, node->pos,
                            MESSAGE_CHECKER_TYPE_AS_VALUE, name);
            return false;
        } else {
            error_report_at(ctx->errors, node->pos,
                            MESSAGE_CHECKER_UNKNOWN_NAME, name);
            return false;
        }
    }

    if (variable != NULL) {
        size_t i;

        for (i = 0; i < ctx->narrowings->length; i++) {
            const Narrowing *narrowing =
                (const Narrowing *)list_get(ctx->narrowings, i);
            const TypedType *declared;

            if (narrowing == NULL || !narrowing->active ||
                strcmp(narrowing->name, name) != 0) {
                continue;
            }
            declared = typed_program_type(ctx->program, variable->type_index);
            if (declared == NULL || declared->kind != NIPPY_TYPE_UNION) {
                continue;
            }
            {
                TypedNode base = {0};
                TypedNode payload = {0};
                size_t base_index;

                base.kind = TYPED_IDENTIFIER;
                base.type_index = variable->type_index;
                base.pos = node->pos;
                base.name_index = typed_program_copy_symbol(
                    ctx->program, name, strlen(name));
                if (base.name_index == TYPED_NO_NODE) {
                    error_report(ctx->errors,
                                 MESSAGE_CHECKER_OUT_OF_MEMORY_COPYING_NAME);
                    return false;
                }
                base.left_child = TYPED_NO_NODE;
                base.right_child = TYPED_NO_NODE;
                if (!typed_program_push(ctx->program, &base, &base_index)) {
                    error_report(ctx->errors,
                                 MESSAGE_CHECKER_OUT_OF_MEMORY_PROGRAM);
                    return false;
                }
                payload.kind = TYPED_SUM_PAYLOAD;
                payload.type_index = narrowing->type_index;
                payload.pos = node->pos;
                payload.name_index = TYPED_NO_NODE;
                payload.integer_value = (int)narrowing->tag;
                payload.left_child = base_index;
                payload.right_child = TYPED_NO_NODE;
                return emit_node(ctx, &payload, ast_index, &(size_t){0});
            }
        }
    }

    typed.kind = TYPED_IDENTIFIER;
    typed.type_index = type_index;
    typed.pos = node->pos;
    typed.name_index = typed_program_copy_symbol(ctx->program, name,
                                                 strlen(name));
    if (typed.name_index == TYPED_NO_NODE) {
        error_report(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_COPYING_NAME);
        return false;
    }
    typed.left_child = TYPED_NO_NODE;
    typed.right_child = TYPED_NO_NODE;
    typed.children_start = 0;
    typed.children_count = 0;

    return emit_node(ctx, &typed, ast_index, &(size_t){0});
}


const char *operator_text(NippyOperator op)
{
    switch (op) {
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
    case NIPPY_OPERATOR_AND:
        return "&&";
    case NIPPY_OPERATOR_OR:
        return "||";
    case NIPPY_OPERATOR_NOT:
        return "!";
    default:
        return "?";
    }
}


bool check_bool_literal(CheckContext *ctx, const AstNode *node,
                               size_t ast_index)
{
    size_t bool_type;
    TypedNode typed = {0};

    if (!add_builtin_type(ctx, NIPPY_TYPE_BOOL, &bool_type)) {
        return false;
    }
    typed.kind = TYPED_BOOL_LITERAL;
    typed.type_index = bool_type;
    typed.pos = node->pos;
    typed.integer_value = node->integer_value;
    typed.name_index = TYPED_NO_NODE;
    typed.left_child = TYPED_NO_NODE;
    typed.right_child = TYPED_NO_NODE;
    typed.children_start = 0;
    typed.children_count = 0;
    return emit_node(ctx, &typed, ast_index, &(size_t){0});
}


bool check_binary(CheckContext *ctx, const AstNode *node,
                         size_t ast_index)
{
    size_t left;
    size_t right;
    size_t result_type;
    const TypedNode *left_node;
    const TypedNode *right_node;
    const TypedType *left_type;
    const TypedType *right_type;
    bool operands_ok = false;
    TypedNode typed = {0};

    if (!check_expression(ctx, node->left_child, &left) ||
        !check_expression(ctx, node->right_child, &right)) {
        return false;
    }
    left_node = typed_program_node(ctx->program, left);
    right_node = typed_program_node(ctx->program, right);
    if (left_node == NULL || right_node == NULL) {
        error_report(ctx->errors, MESSAGE_CHECKER_MISSING_AST_NODE,
                     node->left_child);
        return false;
    }
    left_type = typed_program_type(ctx->program, left_node->type_index);
    right_type = typed_program_type(ctx->program, right_node->type_index);
    if (left_type == NULL || right_type == NULL) {
        error_report_at(ctx->errors, node->pos,
                        MESSAGE_CHECKER_UNSUPPORTED_AST_NODE);
        return false;
    }

    if ((node->op == NIPPY_OPERATOR_DIV || node->op == NIPPY_OPERATOR_MOD) &&
        right_node->kind == TYPED_INTEGER_LITERAL &&
        right_node->integer_value == 0) {
        error_report_at(ctx->errors, node->pos,
                        MESSAGE_CHECKER_ZERO_DIVISOR,
                        operator_text(node->op));
        return false;
    }

    /* A Byte can be compared with an integer literal: the literal is typed as
       a Byte in that context. A Byte variable is never implicitly compared with
       an Int32 variable. */
    if (node->op == NIPPY_OPERATOR_EQ || node->op == NIPPY_OPERATOR_NE) {
        if (left_type->kind == NIPPY_TYPE_BYTE &&
            right_node->kind == TYPED_INTEGER_LITERAL) {
            size_t coerced;

            if (!inject_into(ctx, right, left_node->type_index, &coerced)) {
                error_report_at(ctx->errors, node->pos,
                                MESSAGE_CHECKER_OPERATOR_TYPE,
                                operator_text(node->op));
                return false;
            }
            right = coerced;
            right_node = typed_program_node(ctx->program, right);
            right_type = typed_program_type(ctx->program,
                                            right_node->type_index);
        } else if (right_type->kind == NIPPY_TYPE_BYTE &&
                   left_node->kind == TYPED_INTEGER_LITERAL) {
            size_t coerced;

            if (!inject_into(ctx, left, right_node->type_index, &coerced)) {
                error_report_at(ctx->errors, node->pos,
                                MESSAGE_CHECKER_OPERATOR_TYPE,
                                operator_text(node->op));
                return false;
            }
            left = coerced;
            left_node = typed_program_node(ctx->program, left);
            left_type = typed_program_type(ctx->program,
                                           left_node->type_index);
        } else if ((left_type->kind == NIPPY_TYPE_STRING ||
                    left_type->kind == NIPPY_TYPE_DYNAMIC_STRING) &&
                   right_node->kind == TYPED_STRING_LITERAL) {
            size_t coerced;

            if (!inject_into(ctx, right, left_node->type_index, &coerced)) {
                return false;
            }
            right = coerced;
            right_node = typed_program_node(ctx->program, right);
            right_type = typed_program_type(ctx->program,
                                            right_node->type_index);
        } else if ((right_type->kind == NIPPY_TYPE_STRING ||
                    right_type->kind == NIPPY_TYPE_DYNAMIC_STRING) &&
                   left_node->kind == TYPED_STRING_LITERAL) {
            size_t coerced;

            if (!inject_into(ctx, left, right_node->type_index, &coerced)) {
                return false;
            }
            left = coerced;
            left_node = typed_program_node(ctx->program, left);
            left_type = typed_program_type(ctx->program,
                                           left_node->type_index);
        } else if (left_type->kind == NIPPY_TYPE_STRING_LITERAL ||
                   right_type->kind == NIPPY_TYPE_STRING_LITERAL) {
            error_report_at(ctx->errors, node->pos,
                            MESSAGE_CHECKER_STRING_COMPARE_CONTEXT);
            return false;
        }
    }

    switch (node->op) {
    case NIPPY_OPERATOR_ADD:
    case NIPPY_OPERATOR_SUB:
    case NIPPY_OPERATOR_MUL:
    case NIPPY_OPERATOR_DIV:
    case NIPPY_OPERATOR_MOD:
    case NIPPY_OPERATOR_BIT_AND:
    case NIPPY_OPERATOR_BIT_OR:
    case NIPPY_OPERATOR_BIT_XOR:
    case NIPPY_OPERATOR_SHL:
    case NIPPY_OPERATOR_SHR:
        operands_ok = left_type->kind == NIPPY_TYPE_INT32 &&
                      right_type->kind == NIPPY_TYPE_INT32;
        result_type = NIPPY_TYPE_INT32;
        break;
    case NIPPY_OPERATOR_LT:
    case NIPPY_OPERATOR_LE:
    case NIPPY_OPERATOR_GT:
    case NIPPY_OPERATOR_GE:
        operands_ok = left_type->kind == NIPPY_TYPE_INT32 &&
                      right_type->kind == NIPPY_TYPE_INT32;
        result_type = NIPPY_TYPE_BOOL;
        break;
    case NIPPY_OPERATOR_EQ:
    case NIPPY_OPERATOR_NE:
        operands_ok = same_type(ctx->program, left_node->type_index,
                                right_node->type_index) &&
                      (left_type->kind == NIPPY_TYPE_INT32 ||
                       left_type->kind == NIPPY_TYPE_BOOL ||
                       left_type->kind == NIPPY_TYPE_BYTE ||
                       left_type->kind == NIPPY_TYPE_STRING ||
                       left_type->kind == NIPPY_TYPE_DYNAMIC_STRING);
        result_type = NIPPY_TYPE_BOOL;
        break;
    case NIPPY_OPERATOR_AND:
    case NIPPY_OPERATOR_OR:
        operands_ok = left_type->kind == NIPPY_TYPE_BOOL &&
                      right_type->kind == NIPPY_TYPE_BOOL;
        result_type = NIPPY_TYPE_BOOL;
        break;
    default:
        operands_ok = false;
        result_type = NIPPY_TYPE_UNKNOWN;
        break;
    }
    if (!operands_ok) {
        error_report_at(ctx->errors, node->pos, MESSAGE_CHECKER_OPERATOR_TYPE,
                        operator_text(node->op));
        return false;
    }

    {
        size_t result_index;

        if (!add_builtin_type(ctx, result_type, &result_index)) {
            return false;
        }
        typed.kind = TYPED_BINARY;
        typed.type_index = result_index;
        typed.pos = node->pos;
        typed.op = node->op;
        typed.name_index = TYPED_NO_NODE;
        typed.left_child = left;
        typed.right_child = right;
        typed.children_start = 0;
        typed.children_count = 0;
    }

    return emit_node(ctx, &typed, ast_index, &(size_t){0});
}


bool check_unary(CheckContext *ctx, const AstNode *node,
                        size_t ast_index)
{
    size_t operand;
    const TypedNode *operand_node;
    const TypedType *operand_type;
    size_t result_type;
    TypedNode typed = {0};

    if (node->op != NIPPY_OPERATOR_NOT && node->op != NIPPY_OPERATOR_NEG &&
        node->op != NIPPY_OPERATOR_BIT_NOT) {
        error_report_at(ctx->errors, node->pos,
                        MESSAGE_CHECKER_UNSUPPORTED_AST_NODE);
        return false;
    }
    if (!check_expression(ctx, node->left_child, &operand)) {
        return false;
    }
    operand_node = typed_program_node(ctx->program, operand);
    operand_type = operand_node == NULL
                       ? NULL
                       : typed_program_type(ctx->program,
                                            operand_node->type_index);
    if (operand_type == NULL ||
        (node->op == NIPPY_OPERATOR_NOT
             ? operand_type->kind != NIPPY_TYPE_BOOL
             : operand_type->kind != NIPPY_TYPE_INT32)) {
        error_report_at(ctx->errors, node->pos, MESSAGE_CHECKER_OPERATOR_TYPE,
                        operator_text(node->op));
        return false;
    }
    if (!add_builtin_type(ctx,
                          node->op == NIPPY_OPERATOR_NOT ? NIPPY_TYPE_BOOL
                                                         : NIPPY_TYPE_INT32,
                          &result_type)) {
        return false;
    }
    typed.kind = TYPED_UNARY;
    typed.type_index = result_type;
    typed.pos = node->pos;
    typed.op = node->op;
    typed.name_index = TYPED_NO_NODE;
    typed.left_child = operand;
    typed.right_child = TYPED_NO_NODE;
    typed.children_start = 0;
    typed.children_count = 0;
    return emit_node(ctx, &typed, ast_index, &(size_t){0});
}
