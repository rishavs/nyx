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
    size_t key_type = TYPED_NO_NODE;
    size_t value_type = TYPED_NO_NODE;

    if (ctx->expected_ast == ast_index) {
        const TypedType *expected = typed_program_type(ctx->program,
                                                       ctx->expected_type);

        if (expected != NULL && expected->kind == NIPPY_TYPE_RECORD &&
            expected->child_count == 2) {
            key_type = *(const size_t *)list_get(&ctx->program->type_children,
                                                  expected->first_child);
            value_type = *(const size_t *)list_get(
                &ctx->program->type_children, expected->first_child + 1);
        }
    }

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

        if (!check_expression_expected(ctx, key_ast, key_type, &key) ||
            !check_expression_expected(ctx, value_ast, value_type, &value) ||
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
            error_report_fatal(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
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
    size_t expected_element = TYPED_NO_NODE;
    size_t i;

    if (ctx->expected_ast == ast_index) {
        const TypedType *expected = typed_program_type(ctx->program,
                                                       ctx->expected_type);

        if (expected != NULL && expected->kind == NIPPY_TYPE_FIXED_LIST &&
            expected->child_count == 1) {
            expected_element = *(const size_t *)list_get(
                &ctx->program->type_children, expected->first_child);
        }
    }
    list_init(&checked, sizeof(size_t));
    for (i = 0; i < node->children_count; i++) {
        size_t child_ast = *(const size_t *)list_get(
            &ctx->ast->children, node->children_start + i);
        size_t child;

        if (!check_expression_expected(ctx, child_ast, expected_element, &child) ||
            (expected_element == TYPED_NO_NODE &&
             !check_default_numeric(ctx, child)) ||
            !list_push(&checked, &child)) {
            if (!ctx->errors->reported) {
                error_report_fatal(ctx->errors,
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
                error_report_fatal(ctx->errors,
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
            error_report_fatal(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
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
    NippyType kind = NIPPY_TYPE_INT64;
    size_t literal_type;
    TypedNode typed = {0};

    if (node->kind == AST_FLOAT_LITERAL) {
        if (!add_type_record(ctx, NIPPY_TYPE_FLOAT64, TYPED_NO_NODE,
                             TYPED_NO_NODE, &literal_type)) {
            return false;
        }
        typed.kind = TYPED_FLOAT_LITERAL;
        typed.type_index = literal_type;
        typed.pos = node->pos;
        typed.float_value = node->float_value;
        typed.name_index = TYPED_NO_NODE;
        typed.left_child = TYPED_NO_NODE;
        typed.right_child = TYPED_NO_NODE;
        typed.children_start = 0;
        typed.children_count = 0;
        return emit_node(ctx, &typed, ast_index, &(size_t){0});
    }
    if (node->integer_value_u > (uint64_t)INT64_MAX) {
        kind = NIPPY_TYPE_UINT64;
    }
    if (!add_type_record(ctx, kind, TYPED_NO_NODE, TYPED_NO_NODE,
                         &literal_type)) {
        return false;
    }

    typed.kind = TYPED_INTEGER_LITERAL;
    typed.type_index = literal_type;
    typed.pos = node->pos;
    typed.integer_value = node->integer_value;
    typed.integer_value_u = node->integer_value_u;
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
        function = find_function(ctx, name);
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
                   strcmp(name, "Slice") == 0 || strcmp(name, "Range") == 0 ||
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

        for (i = ctx->narrowings->length; i > 0; i--) {
            const Narrowing *narrowing =
                (const Narrowing *)list_get(ctx->narrowings, i - 1);
            const TypedType *declared;

            if (narrowing == NULL || !narrowing->active ||
                narrowing->path_ast != AST_NO_NODE ||
                strcmp(narrowing->name, name) != 0) {
                continue;
            }
            declared = typed_program_type(ctx->program, variable->type_index);
            if (declared == NULL || declared->kind != NIPPY_TYPE_UNION) {
                continue;
            }
            {
                TypedNode base = {0};
                size_t base_index, narrowed;

                base.kind = TYPED_IDENTIFIER;
                base.type_index = variable->type_index;
                base.pos = node->pos;
                base.name_index = typed_program_copy_symbol(
                    ctx->program, name, strlen(name));
                if (base.name_index == TYPED_NO_NODE) {
                    error_report_fatal(ctx->errors,
                                 MESSAGE_CHECKER_OUT_OF_MEMORY_COPYING_NAME);
                    return false;
                }
                base.left_child = TYPED_NO_NODE;
                base.right_child = TYPED_NO_NODE;
                base.is_self = variable->is_self_pointer;
                if (!typed_program_push(ctx->program, &base, &base_index)) {
                    error_report_fatal(ctx->errors,
                                 MESSAGE_CHECKER_OUT_OF_MEMORY_PROGRAM);
                    return false;
                }
                if (!emit_narrowed_value(ctx, base_index, narrowing, &narrowed))
                    return false;
                *mapping_slot(ctx->mapping, ast_index) = narrowed;
                return true;
            }
        }
    }

    typed.kind = TYPED_IDENTIFIER;
    typed.type_index = type_index;
    typed.pos = node->pos;
    typed.name_index = typed_program_copy_symbol(ctx->program, name,
                                                 strlen(name));
    if (typed.name_index == TYPED_NO_NODE) {
        error_report_fatal(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_COPYING_NAME);
        return false;
    }
    typed.left_child = TYPED_NO_NODE;
    typed.right_child = TYPED_NO_NODE;
    typed.children_start = 0;
    typed.children_count = 0;
    typed.is_self = variable != NULL && variable->is_self_pointer;

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
    case NIPPY_OPERATOR_STRICT_EQ:
        return "===";
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

static void diagnostic_type_name_depth(const TypedProgram *program, size_t index,
                                       char *out, size_t size, unsigned depth)
{
    const TypedType *type = typed_program_type(program, index);
    const char *name = "value";

    if (type != NULL && depth > 0 &&
        (type->kind == NIPPY_TYPE_UNION || type->kind == NIPPY_TYPE_HANDLE ||
         type->kind == NIPPY_TYPE_FIXED_LIST || type->kind == NIPPY_TYPE_LIST ||
         type->kind == NIPPY_TYPE_SLICE || type->kind == NIPPY_TYPE_RANGE ||
         type->kind == NIPPY_TYPE_RECORD ||
         type->kind == NIPPY_TYPE_TABLE ||
         (type->kind == NIPPY_TYPE_STRUCT && type->child_count != 0))) {
        size_t used = 0;
        size_t i;
        char child_text[128];

        out[0] = '\0';
        switch (type->kind) {
        case NIPPY_TYPE_HANDLE: name = "Handle"; break;
        case NIPPY_TYPE_FIXED_LIST: name = "Array"; break;
        case NIPPY_TYPE_LIST: name = "List"; break;
        case NIPPY_TYPE_SLICE: name = "Slice"; break;
        case NIPPY_TYPE_RANGE: name = "Range"; break;
        case NIPPY_TYPE_RECORD: name = "Record"; break;
        case NIPPY_TYPE_TABLE: name = "Table"; break;
        case NIPPY_TYPE_STRUCT:
            name = typed_program_symbol_text(program, type->source_name_index);
            if (name == NULL) name = "struct";
            break;
        default: name = ""; break;
        }
        type_ast_append(name, out, size, &used);
        if (type->kind != NIPPY_TYPE_UNION) type_ast_append("<", out, size, &used);
        for (i = 0; i < type->child_count; i++) {
            size_t child = *(const size_t *)list_get(&program->type_children,
                                                      type->first_child + i);
            if (i != 0) type_ast_append(type->kind == NIPPY_TYPE_UNION
                                           ? " | " : ", ", out, size, &used);
            diagnostic_type_name_depth(program, child, child_text,
                                       sizeof(child_text), depth - 1);
            type_ast_append(child_text, out, size, &used);
        }
        if (type->kind == NIPPY_TYPE_FIXED_LIST || type->kind == NIPPY_TYPE_RECORD) {
            char number[32];
            snprintf(number, sizeof(number), ", %zu", type->fixed_length);
            type_ast_append(number, out, size, &used);
        }
        if (type->kind != NIPPY_TYPE_UNION) type_ast_append(">", out, size, &used);
        return;
    }
    if (type != NULL) {
        switch (type->kind) {
        case NIPPY_TYPE_INT8: name = "Int8"; break;
        case NIPPY_TYPE_INT16: name = "Int16"; break;
        case NIPPY_TYPE_INT32: name = "Int32"; break;
        case NIPPY_TYPE_INT64: name = "Int64"; break;
        case NIPPY_TYPE_BYTE: name = "Byte"; break;
        case NIPPY_TYPE_UINT16: name = "Uint16"; break;
        case NIPPY_TYPE_UINT32: name = "Uint32"; break;
        case NIPPY_TYPE_UINT64: name = "Uint64"; break;
        case NIPPY_TYPE_FLOAT32: name = "Float32"; break;
        case NIPPY_TYPE_FLOAT64: name = "Float64"; break;
        case NIPPY_TYPE_BOOL: name = "Bool"; break;
        case NIPPY_TYPE_NIL: name = "Nil"; break;
        case NIPPY_TYPE_DYNAMIC_STRING: name = "Text"; break;
        case NIPPY_TYPE_STRING_LITERAL: name = "string literal"; break;
        case NIPPY_TYPE_HANDLE: name = "Handle"; break;
        case NIPPY_TYPE_FIXED_LIST: name = "Array"; break;
        case NIPPY_TYPE_LIST: name = "List"; break;
        case NIPPY_TYPE_SLICE: name = "Slice"; break;
        case NIPPY_TYPE_RANGE: name = "Range"; break;
        case NIPPY_TYPE_RECORD: name = "Record"; break;
        case NIPPY_TYPE_TABLE: name = "Table"; break;
        case NIPPY_TYPE_UNION: name = "union"; break;
        case NIPPY_TYPE_FUNCTION: name = "function"; break;
        case NIPPY_TYPE_ERROR: name = "Error"; break;
        case NIPPY_TYPE_STRUCT:
            name = typed_program_symbol_text(program, type->source_name_index);
            if (name == NULL) name = "struct";
            break;
        case NIPPY_TYPE_STRING:
            snprintf(out, size, "String<%zu>", type->fixed_length);
            return;
        default: break;
        }
    }
    snprintf(out, size, "%s", name);
}

static void diagnostic_type_name(const TypedProgram *program, size_t index,
                                 char *out, size_t size)
{
    diagnostic_type_name_depth(program, index, out, size, NIPPY_TYPE_DEPTH);
}

static void report_binary_operator_type(CheckContext *ctx, const AstNode *node,
                                        size_t left, size_t right)
{
    const TypedNode *a = typed_program_node(ctx->program, left);
    const TypedNode *b = typed_program_node(ctx->program, right);
    char left_name[128];
    char right_name[128];

    diagnostic_type_name(ctx->program, a == NULL ? TYPED_NO_NODE : a->type_index,
                         left_name, sizeof(left_name));
    diagnostic_type_name(ctx->program, b == NULL ? TYPED_NO_NODE : b->type_index,
                         right_name, sizeof(right_name));
    error_report_at(ctx->errors, node->pos, MESSAGE_CHECKER_OPERATOR_TYPES,
                    operator_text(node->op), left_name, right_name);
}

static void report_unary_operator_type(CheckContext *ctx, const AstNode *node,
                                       size_t operand)
{
    const TypedNode *value = typed_program_node(ctx->program, operand);
    char name[128];

    diagnostic_type_name(ctx->program,
                         value == NULL ? TYPED_NO_NODE : value->type_index,
                         name, sizeof(name));
    error_report_at(ctx->errors, node->pos, MESSAGE_CHECKER_UNARY_OPERAND,
                    operator_text(node->op), name);
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

bool equality_type_supported(const TypedProgram *program,
                             size_t type_index, unsigned depth)
{
    const TypedType *type = typed_program_type(program, type_index);
    size_t i;

    if (type == NULL || depth == 0) {
        return false;
    }
    if (nippy_type_is_numeric(type->kind) || type->kind == NIPPY_TYPE_BOOL ||
        type->kind == NIPPY_TYPE_STRING ||
        type->kind == NIPPY_TYPE_DYNAMIC_STRING ||
        type->kind == NIPPY_TYPE_HANDLE) {
        return true;
    }
    if (type->kind == NIPPY_TYPE_STRUCT) {
        const TypedNode *declaration = typed_program_node(program,
                                                           type->declaration_node);
        if (declaration == NULL) {
            return false;
        }
        for (i = 0; i < declaration->children_count; i++) {
            size_t child = *(const size_t *)list_get(
                &program->children, declaration->children_start + i);
            const TypedNode *field = typed_program_node(program, child);
            if (field == NULL || !equality_type_supported(
                    program, field->type_index, depth - 1)) {
                return false;
            }
        }
        return true;
    }
    if (type->kind == NIPPY_TYPE_FIXED_LIST || type->kind == NIPPY_TYPE_LIST ||
        type->kind == NIPPY_TYPE_RECORD || type->kind == NIPPY_TYPE_TABLE) {
        size_t expected = (type->kind == NIPPY_TYPE_RECORD ||
                           type->kind == NIPPY_TYPE_TABLE) ? 2 : 1;
        if (type->child_count != expected) {
            return false;
        }
        for (i = 0; i < expected; i++) {
            size_t child = *(const size_t *)list_get(
                &program->type_children, type->first_child + i);
            if (!equality_type_supported(program, child, depth - 1)) {
                return false;
            }
        }
        return true;
    }
    return false;
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
    size_t operand_context = TYPED_NO_NODE;
    TypedNode typed = {0};

    if (ctx->expected_ast == ast_index &&
        node->op != NIPPY_OPERATOR_EQ && node->op != NIPPY_OPERATOR_NE &&
        node->op != NIPPY_OPERATOR_STRICT_EQ &&
        node->op != NIPPY_OPERATOR_LT && node->op != NIPPY_OPERATOR_LE &&
        node->op != NIPPY_OPERATOR_GT && node->op != NIPPY_OPERATOR_GE &&
        node->op != NIPPY_OPERATOR_AND && node->op != NIPPY_OPERATOR_OR) {
        const TypedType *expected = typed_program_type(ctx->program,
                                                       ctx->expected_type);
        if (expected != NULL && nippy_type_is_numeric(expected->kind)) {
            operand_context = ctx->expected_type;
        }
    }
    if (!check_expression_expected(ctx, node->left_child, operand_context, &left)) {
        return false;
    }
    if (node->op == NIPPY_OPERATOR_AND || node->op == NIPPY_OPERATOR_OR) {
        size_t narrowing_start = ctx->narrowings->length;
        bool ok = (node->op == NIPPY_OPERATOR_AND
                       ? push_if_narrowing(ctx, node->left_child)
                       : push_else_narrowing(ctx, node->left_child)) &&
                  check_expression_expected(ctx, node->right_child,
                                            operand_context, &right);

        ctx->narrowings->length = narrowing_start;
        if (!ok) return false;
    } else if (!check_expression_expected(ctx, node->right_child,
                                           operand_context, &right)) {
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

    if ((node->op == NIPPY_OPERATOR_EQ || node->op == NIPPY_OPERATOR_NE ||
         node->op == NIPPY_OPERATOR_STRICT_EQ) &&
        nippy_type_is_numeric(left_type->kind) &&
        nippy_type_is_numeric(right_type->kind)) {
        NumericConstant constant;
        bool left_literal = numeric_constant_read(ctx->program, left, false,
                                                  &constant);
        bool right_literal = numeric_constant_read(ctx->program, right, false,
                                                   &constant);

        if ((right_type->kind != NIPPY_TYPE_UINT64 || right_literal) &&
            !check_default_numeric(ctx, left)) {
            return false;
        }
        if ((left_type->kind != NIPPY_TYPE_UINT64 || left_literal) &&
            !check_default_numeric(ctx, right)) {
            return false;
        }
    }
    if (node->op == NIPPY_OPERATOR_STRICT_EQ) {
        if (nippy_type_is_numeric(left_type->kind) &&
            !nippy_type_is_numeric(right_type->kind) &&
            !check_default_numeric(ctx, left)) {
            return false;
        }
        if (nippy_type_is_numeric(right_type->kind) &&
            !nippy_type_is_numeric(left_type->kind) &&
            !check_default_numeric(ctx, right)) {
            return false;
        }
    }

    /* Numeric literals are typed by context: an untyped literal adopts the
       type of the other numeric operand. Two integer literals use the widest
       default that can hold them. */
    if (node->op != NIPPY_OPERATOR_EQ && node->op != NIPPY_OPERATOR_NE &&
        node->op != NIPPY_OPERATOR_STRICT_EQ &&
        (nippy_type_is_numeric(left_type->kind) ||
         nippy_type_is_numeric(right_type->kind))) {
        /* A negated literal is still a literal: `x + -1` types the 1 from x. */
        NumericConstant left_constant;
        NumericConstant right_constant_value;
        bool left_literal = numeric_constant_read(ctx->program, left, false,
                                                  &left_constant);
        bool right_literal = numeric_constant_read(
            ctx->program, right, false, &right_constant_value);

        if (left_literal && right_literal && !left_constant.is_float &&
            !right_constant_value.is_float) {
            NippyType common = NIPPY_TYPE_INT32;
            size_t common_type;
            size_t coerced_left;
            size_t coerced_right;

            if ((ctx->expected_ast != ast_index ||
                 ctx->expected_type == TYPED_NO_NODE ||
                 typed_program_type(ctx->program, ctx->expected_type) == NULL ||
                 !nippy_type_is_integer(typed_program_type(
                     ctx->program, ctx->expected_type)->kind)) &&
                (!check_default_numeric(ctx, left) ||
                 !check_default_numeric(ctx, right))) {
                return false;
            }

            if (left_type->kind == NIPPY_TYPE_UINT64 ||
                right_type->kind == NIPPY_TYPE_UINT64) {
                common = NIPPY_TYPE_UINT64;
            } else if (left_type->kind == NIPPY_TYPE_INT64 ||
                       right_type->kind == NIPPY_TYPE_INT64) {
                common = NIPPY_TYPE_INT64;
            }
            if (ctx->expected_ast == ast_index) {
                const TypedType *expected = typed_program_type(ctx->program,
                                                               ctx->expected_type);
                if (expected != NULL && nippy_type_is_integer(expected->kind) &&
                    node->op != NIPPY_OPERATOR_EQ &&
                    node->op != NIPPY_OPERATOR_NE &&
                    node->op != NIPPY_OPERATOR_LT &&
                    node->op != NIPPY_OPERATOR_LE &&
                    node->op != NIPPY_OPERATOR_GT &&
                    node->op != NIPPY_OPERATOR_GE) {
                    common = expected->kind;
                }
            }
            if (!add_builtin_type(ctx, common, &common_type) ||
                !inject_into(ctx, left, common_type, &coerced_left) ||
                !inject_into(ctx, right, common_type, &coerced_right)) {
                if (!ctx->errors->reported) {
                    report_binary_operator_type(ctx, node, left, right);
                }
                return false;
            }
            left = coerced_left;
            right = coerced_right;
        } else if (left_literal && right_literal && left_constant.is_float &&
                   right_constant_value.is_float &&
                   operand_context != TYPED_NO_NODE &&
                   nippy_type_is_float(typed_program_type(
                       ctx->program, operand_context)->kind)) {
            size_t coerced_left;
            size_t coerced_right;

            if (!inject_into(ctx, left, operand_context, &coerced_left) ||
                !inject_into(ctx, right, operand_context, &coerced_right)) {
                return false;
            }
            left = coerced_left;
            right = coerced_right;
        } else if (left_literal && !right_literal &&
                   nippy_type_is_numeric(right_type->kind)) {
            size_t coerced;
            size_t reported = ctx->errors->length;

            if (!inject_into(ctx, left, right_node->type_index, &coerced)) {
                /* A range error is already reported; a kind mismatch such as
                   an integer variable beside a float literal is not. */
                if (ctx->errors->length == reported) {
                    report_binary_operator_type(ctx, node, left, right);
                }
                return false;
            }
            left = coerced;
        } else if (right_literal && !left_literal &&
                   nippy_type_is_numeric(left_type->kind)) {
            size_t coerced;
            size_t reported = ctx->errors->length;

            if (!inject_into(ctx, right, left_node->type_index, &coerced)) {
                if (ctx->errors->length == reported) {
                    report_binary_operator_type(ctx, node, left, right);
                }
                return false;
            }
            right = coerced;
        }
        left_node = typed_program_node(ctx->program, left);
        right_node = typed_program_node(ctx->program, right);
        left_type = typed_program_type(ctx->program, left_node->type_index);
        right_type = typed_program_type(ctx->program,
                                        right_node->type_index);
    }

    if (node->op == NIPPY_OPERATOR_STRICT_EQ &&
        nippy_type_is_numeric(left_type->kind) &&
        nippy_type_is_numeric(right_type->kind)) {
        NumericConstant lc;
        NumericConstant rc;
        bool l_literal = numeric_constant_read(ctx->program, left, false, &lc);
        bool r_literal = numeric_constant_read(ctx->program, right, false, &rc);
        size_t target = TYPED_NO_NODE;
        size_t literal = TYPED_NO_NODE;
        size_t coerced;

        if (l_literal && !r_literal &&
            ((lc.is_float && nippy_type_is_float(right_type->kind) &&
              float_constant_fits(right_type->kind, lc.real)) ||
             (!lc.is_float && nippy_type_is_integer(right_type->kind) &&
              integer_constant_fits(right_type->kind, lc.negative, lc.magnitude)))) {
            literal = left;
            target = right_node->type_index;
        } else if (r_literal && !l_literal &&
                   ((rc.is_float && nippy_type_is_float(left_type->kind) &&
                     float_constant_fits(left_type->kind, rc.real)) ||
                    (!rc.is_float && nippy_type_is_integer(left_type->kind) &&
                     integer_constant_fits(left_type->kind, rc.negative,
                                           rc.magnitude)))) {
            literal = right;
            target = left_node->type_index;
        }
        if (literal != TYPED_NO_NODE) {
            if (!inject_into(ctx, literal, target, &coerced)) {
                return false;
            }
            if (literal == left) {
                left = coerced;
            } else {
                right = coerced;
            }
            left_node = typed_program_node(ctx->program, left);
            right_node = typed_program_node(ctx->program, right);
            left_type = typed_program_type(ctx->program, left_node->type_index);
            right_type = typed_program_type(ctx->program, right_node->type_index);
        }
    }
    if (node->op == NIPPY_OPERATOR_EQ || node->op == NIPPY_OPERATOR_NE ||
        node->op == NIPPY_OPERATOR_STRICT_EQ) {
        if (left_type->kind == NIPPY_TYPE_STRING_LITERAL && !left_node->fixed_type) {
            size_t target = TYPED_NO_NODE;
            size_t coerced;

            if ((right_type->kind == NIPPY_TYPE_STRING &&
                 left_node->string_length <= right_type->fixed_length) ||
                right_type->kind == NIPPY_TYPE_DYNAMIC_STRING) {
                target = right_node->type_index;
            } else if (!builtin_type(ctx, NIPPY_TYPE_DYNAMIC_STRING, &target)) {
                return false;
            }
            if (!inject_into(ctx, left, target, &coerced)) {
                return false;
            }
            left = coerced;
            left_node = typed_program_node(ctx->program, left);
            right_node = typed_program_node(ctx->program, right);
            left_type = typed_program_type(ctx->program, left_node->type_index);
            right_type = typed_program_type(ctx->program, right_node->type_index);
        }
        if (right_type->kind == NIPPY_TYPE_STRING_LITERAL && !right_node->fixed_type) {
            size_t target = TYPED_NO_NODE;
            size_t coerced;

            if ((left_type->kind == NIPPY_TYPE_STRING &&
                 right_node->string_length <= left_type->fixed_length) ||
                left_type->kind == NIPPY_TYPE_DYNAMIC_STRING) {
                target = left_node->type_index;
            } else if (!builtin_type(ctx, NIPPY_TYPE_DYNAMIC_STRING, &target)) {
                return false;
            }
            if (!inject_into(ctx, right, target, &coerced)) {
                return false;
            }
            right = coerced;
            left_node = typed_program_node(ctx->program, left);
            right_node = typed_program_node(ctx->program, right);
            left_type = typed_program_type(ctx->program, left_node->type_index);
            right_type = typed_program_type(ctx->program, right_node->type_index);
        }
    }

    switch (node->op) {
    case NIPPY_OPERATOR_ADD:
    case NIPPY_OPERATOR_SUB:
    case NIPPY_OPERATOR_MUL:
    case NIPPY_OPERATOR_DIV:
        operands_ok = same_type(ctx->program, left_node->type_index,
                                right_node->type_index) &&
                      nippy_type_is_numeric(left_type->kind);
        result_type = operands_ok ? left_type->kind : NIPPY_TYPE_UNKNOWN;
        break;
    case NIPPY_OPERATOR_MOD:
    case NIPPY_OPERATOR_BIT_AND:
    case NIPPY_OPERATOR_BIT_OR:
    case NIPPY_OPERATOR_BIT_XOR:
    case NIPPY_OPERATOR_SHL:
    case NIPPY_OPERATOR_SHR:
        operands_ok = same_type(ctx->program, left_node->type_index,
                                right_node->type_index) &&
                      nippy_type_is_integer(left_type->kind);
        result_type = operands_ok ? left_type->kind : NIPPY_TYPE_UNKNOWN;
        break;
    case NIPPY_OPERATOR_LT:
    case NIPPY_OPERATOR_LE:
    case NIPPY_OPERATOR_GT:
    case NIPPY_OPERATOR_GE:
        operands_ok = same_type(ctx->program, left_node->type_index,
                                right_node->type_index) &&
                      nippy_type_is_numeric(left_type->kind);
        result_type = NIPPY_TYPE_BOOL;
        break;
    case NIPPY_OPERATOR_EQ:
    case NIPPY_OPERATOR_NE:
        operands_ok = (nippy_type_is_numeric(left_type->kind) &&
                       nippy_type_is_numeric(right_type->kind)) ||
                      ((left_type->kind == NIPPY_TYPE_STRING ||
                        left_type->kind == NIPPY_TYPE_DYNAMIC_STRING) &&
                       (right_type->kind == NIPPY_TYPE_STRING ||
                        right_type->kind == NIPPY_TYPE_DYNAMIC_STRING)) ||
                      (same_type(ctx->program, left_node->type_index,
                                 right_node->type_index) &&
                       equality_type_supported(ctx->program,
                                               left_node->type_index, 64));
        result_type = NIPPY_TYPE_BOOL;
        break;
    case NIPPY_OPERATOR_STRICT_EQ:
        operands_ok = equality_type_supported(ctx->program,
                                               left_node->type_index, 64) &&
                      equality_type_supported(ctx->program,
                                               right_node->type_index, 64);
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
        report_binary_operator_type(ctx, node, left, right);
        return false;
    }
    {
        bool right_constant = false;
        int right_value = 0;

        if (!const_int_eval(ctx, node->right_child, &right_value,
                            &right_constant)) {
            return false;
        }
        if (right_constant &&
            (node->op == NIPPY_OPERATOR_SHL ||
             node->op == NIPPY_OPERATOR_SHR) &&
            nippy_type_is_integer(left_type->kind)) {
            unsigned width = nippy_type_bit_width(left_type->kind);

            if (right_value < 0 || (unsigned)right_value >= width) {
                error_report_at(ctx->errors, node->pos,
                                MESSAGE_CHECKER_SHIFT_RANGE);
                return false;
            }
        }
        if (right_constant &&
            (node->op == NIPPY_OPERATOR_DIV ||
             node->op == NIPPY_OPERATOR_MOD) &&
            right_value == 0) {
            error_report_at(ctx->errors, node->pos,
                            MESSAGE_CHECKER_ZERO_DIVISOR,
                            operator_text(node->op));
            return false;
        }
    }
    if (result_type == NIPPY_TYPE_INT32) {
        bool constant = false;
        int value;

        if (!const_int_eval(ctx, ast_index, &value, &constant)) {
            return false;
        }
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
    NippyType result_kind;
    size_t result_type;
    TypedNode typed = {0};

    if (node->op != NIPPY_OPERATOR_NOT && node->op != NIPPY_OPERATOR_NEG &&
        node->op != NIPPY_OPERATOR_BIT_NOT) {
        error_report_at(ctx->errors, node->pos,
                        MESSAGE_CHECKER_UNSUPPORTED_AST_NODE);
        return false;
    }
    if (!check_expression_expected(
            ctx, node->left_child,
            ctx->expected_ast == ast_index ? ctx->expected_type : TYPED_NO_NODE,
            &operand)) {
        return false;
    }
    operand_node = typed_program_node(ctx->program, operand);
    operand_type = operand_node == NULL
                       ? NULL
                       : typed_program_type(ctx->program,
                                            operand_node->type_index);
    if (operand_type == NULL) {
        report_unary_operator_type(ctx, node, operand);
        return false;
    }
    if (node->op == NIPPY_OPERATOR_NOT) {
        if (operand_type->kind != NIPPY_TYPE_BOOL) {
            report_unary_operator_type(ctx, node, operand);
            return false;
        }
        result_kind = NIPPY_TYPE_BOOL;
    } else if (node->op == NIPPY_OPERATOR_BIT_NOT) {
        if (!nippy_type_is_integer(operand_type->kind)) {
            report_unary_operator_type(ctx, node, operand);
            return false;
        }
        if ((ctx->expected_ast != ast_index ||
             ctx->expected_type == TYPED_NO_NODE) &&
            !check_default_numeric(ctx, operand)) {
            return false;
        }
        result_kind = operand_type->kind;
    } else {
        if (nippy_type_is_signed_integer(operand_type->kind) ||
            nippy_type_is_float(operand_type->kind)) {
            result_kind = operand_type->kind;
        } else if (operand_node->kind == TYPED_INTEGER_LITERAL &&
                   operand_type->kind == NIPPY_TYPE_UINT64 &&
                   operand_node->integer_value_u <= (1ULL << 63)) {
            result_kind = NIPPY_TYPE_INT64;
        } else {
            report_unary_operator_type(ctx, node, operand);
            return false;
        }
    }
    if (!add_builtin_type(ctx, result_kind, &result_type)) {
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
