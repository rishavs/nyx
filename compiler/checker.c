#include "checker.h"

#include "messages.h"

#include <string.h>

/* A declared struct type, tracked so later declarations can resolve it by
   name. Entries are stored inline in a List, so the name is a fixed-size
   array rather than a pointer. */
typedef struct {
    char name[NIPPY_IDENTIFIER_MAX];
    size_t node_index; /* typed TYPED_TYPE_DECLARATION node */
    size_t type_index; /* record in TypedProgram.types */
} TypeEntry;

typedef struct {
    char name[NIPPY_IDENTIFIER_MAX];
    size_t type_index; /* record in TypedProgram.types */
} FunctionEntry;

typedef struct {
    char name[NIPPY_IDENTIFIER_MAX];
    size_t type_index; /* record in TypedProgram.types */
} VariableEntry;

typedef struct {
    const Ast *ast;
    TypedProgram *program;
    ErrorState *errors;
    List *mapping;
    List *types;     /* TypeEntry */
    List *functions; /* FunctionEntry */
    List *globals;   /* VariableEntry, program-level names */
    List *locals;    /* VariableEntry, current function's parameters */
    bool in_function;
    size_t loop_depth;
    const char *function_name;
} CheckContext;

static bool check_expression(CheckContext *ctx, size_t ast_index,
                             size_t *out_typed);
static bool check_block(CheckContext *ctx, size_t ast_index,
                        size_t return_type, size_t *out_typed);

static size_t *mapping_slot(List *mapping, size_t index)
{
    return (size_t *)list_get(mapping, index);
}

static const TypeEntry *find_type(const List *types, const char *name)
{
    size_t i;

    for (i = 0; i < types->length; i++) {
        const TypeEntry *entry = list_get(types, i);

        if (entry != NULL && strcmp(entry->name, name) == 0) {
            return entry;
        }
    }
    return NULL;
}

static const FunctionEntry *find_function(const List *functions,
                                          const char *name)
{
    size_t i;

    for (i = 0; i < functions->length; i++) {
        const FunctionEntry *entry = list_get(functions, i);

        if (entry != NULL && strcmp(entry->name, name) == 0) {
            return entry;
        }
    }
    return NULL;
}

static const VariableEntry *find_variable(const List *variables,
                                          const char *name)
{
    size_t i;

    for (i = 0; i < variables->length; i++) {
        const VariableEntry *entry = list_get(variables, i);

        if (entry != NULL && strcmp(entry->name, name) == 0) {
            return entry;
        }
    }
    return NULL;
}

static bool add_type_record(CheckContext *ctx, NippyType kind,
                            size_t name_index, size_t signature_index,
                            size_t *out_index)
{
    TypedType type = {0};

    type.kind = kind;
    type.name_index = name_index;
    type.signature_index = signature_index;
    if (!typed_program_add_type(ctx->program, &type, out_index)) {
        error_report(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
        return false;
    }
    return true;
}

static bool add_function_type(CheckContext *ctx, const List *parameter_types,
                              size_t return_type, size_t *out_index)
{
    TypedFunctionSignature signature = {0};
    size_t signature_index;
    size_t i;

    signature.first_parameter = ctx->program->signature_parameters.length;
    signature.parameter_count = parameter_types->length;
    signature.return_type = return_type;

    for (i = 0; i < parameter_types->length; i++) {
        size_t parameter_type = *(const size_t *)list_get(parameter_types, i);

        if (!typed_program_add_signature_parameter(ctx->program,
                                                   parameter_type)) {
            error_report(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
            return false;
        }
    }
    if (!typed_program_add_signature(ctx->program, &signature,
                                     &signature_index)) {
        error_report(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
        return false;
    }
    return add_type_record(ctx, NIPPY_TYPE_FUNCTION, TYPED_NO_NODE,
                           signature_index, out_index);
}

static bool same_type(const TypedProgram *program, size_t a, size_t b)
{
    const TypedType *left;
    const TypedType *right;

    if (a == b) {
        return true;
    }
    left = typed_program_type(program, a);
    right = typed_program_type(program, b);
    if (left == NULL || right == NULL || left->kind != right->kind) {
        return false;
    }
    if (left->kind == NIPPY_TYPE_INT32 || left->kind == NIPPY_TYPE_BOOL) {
        return true;
    }
    if (left->kind == NIPPY_TYPE_STRUCT) {
        return left->name_index == right->name_index;
    }
    if (left->kind == NIPPY_TYPE_FUNCTION) {
        const TypedFunctionSignature *left_signature =
            typed_program_signature(program, left->signature_index);
        const TypedFunctionSignature *right_signature =
            typed_program_signature(program, right->signature_index);
        size_t i;

        if (left_signature == NULL || right_signature == NULL ||
            left_signature->parameter_count !=
                right_signature->parameter_count) {
            return false;
        }
        if (!same_type(program, left_signature->return_type,
                       right_signature->return_type)) {
            return false;
        }
        for (i = 0; i < left_signature->parameter_count; i++) {
            size_t left_parameter = *(const size_t *)list_get(
                &program->signature_parameters,
                left_signature->first_parameter + i);
            size_t right_parameter = *(const size_t *)list_get(
                &program->signature_parameters,
                right_signature->first_parameter + i);

            if (!same_type(program, left_parameter, right_parameter)) {
                return false;
            }
        }
        return true;
    }
    return false;
}

static const char *ast_type_text(const Ast *ast, size_t ast_type_index)
{
    const AstNode *node = ast_get_node(ast, ast_type_index);

    if (node == NULL) {
        return "value";
    }
    if (node->kind == AST_TYPE_FUNCTION) {
        return "Fun";
    }
    if (node->kind == AST_TYPE_NAME) {
        return ast_symbol_text(ast, node->name_index);
    }
    return "value";
}

/* Resolve an AST type node to a record in TypedProgram.types. Nested function
   types are resolved before the signature that contains them, so generated C
   can emit their typedefs in order. `owner` names the parameter, variable, or
   function for diagnostics. */
static bool resolve_type_node(CheckContext *ctx, size_t ast_index,
                              const char *owner, size_t *out_index)
{
    const AstNode *node = ast_get_node(ctx->ast, ast_index);
    size_t i;

    if (node == NULL) {
        error_report(ctx->errors, MESSAGE_CHECKER_MISSING_AST_NODE, ast_index);
        return false;
    }

    if (node->kind == AST_TYPE_NAME) {
        const char *name = ast_symbol_text(ctx->ast, node->name_index);

        if (name == NULL) {
            error_report_at(ctx->errors, node->pos,
                            MESSAGE_CHECKER_MISSING_NAME_OR_TYPE);
            return false;
        }
        if (strcmp(name, "Int32") == 0) {
            return add_type_record(ctx, NIPPY_TYPE_INT32, TYPED_NO_NODE,
                                   TYPED_NO_NODE, out_index);
        }
        if (strcmp(name, "Bool") == 0) {
            return add_type_record(ctx, NIPPY_TYPE_BOOL, TYPED_NO_NODE,
                                   TYPED_NO_NODE, out_index);
        }
        {
            const TypeEntry *entry = find_type(ctx->types, name);

            if (entry == NULL) {
                error_report_at(ctx->errors, node->pos,
                                MESSAGE_CHECKER_UNKNOWN_TYPE, name,
                                owner == NULL ? "value" : owner);
                return false;
            }
            *out_index = entry->type_index;
            return true;
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

            if (!resolve_type_node(ctx, child, owner, &parameter_type)) {
                goto function_type_done;
            }
            if (!list_push(&parameter_types, &parameter_type)) {
                error_report(ctx->errors,
                             MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
                goto function_type_done;
            }
        }
        if (!resolve_type_node(ctx, node->right_child, owner, &return_type)) {
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

static bool emit_node(CheckContext *ctx, const TypedNode *node,
                      size_t ast_index, size_t *out_index)
{
    if (!typed_program_push(ctx->program, node, out_index)) {
        error_report(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_PROGRAM);
        return false;
    }
    *mapping_slot(ctx->mapping, ast_index) = *out_index;
    return true;
}

static bool check_literal(CheckContext *ctx, const AstNode *node,
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

static bool check_identifier(CheckContext *ctx, const AstNode *node,
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

    variable = find_variable(ctx->locals, name);
    if (variable == NULL && !ctx->in_function) {
        variable = find_variable(ctx->globals, name);
    }
    if (variable != NULL) {
        type_index = variable->type_index;
    } else {
        function = find_function(ctx->functions, name);
        if (function != NULL) {
            type_index = function->type_index;
        } else if (ctx->in_function && find_variable(ctx->globals, name)) {
            error_report_at(ctx->errors, node->pos,
                            MESSAGE_CHECKER_GLOBAL_IN_FUNCTION, name);
            return false;
        } else if (find_type(ctx->types, name) != NULL ||
                   strcmp(name, "Int32") == 0 || strcmp(name, "Bool") == 0) {
            error_report_at(ctx->errors, node->pos,
                            MESSAGE_CHECKER_TYPE_AS_VALUE, name);
            return false;
        } else {
            error_report_at(ctx->errors, node->pos,
                            MESSAGE_CHECKER_UNKNOWN_NAME, name);
            return false;
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

static const char *operator_text(NippyOperator op)
{
    switch (op) {
    case NIPPY_OPERATOR_ADD:
        return "+";
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

static bool add_builtin_type(CheckContext *ctx, NippyType kind,
                             size_t *out_index)
{
    return add_type_record(ctx, kind, TYPED_NO_NODE, TYPED_NO_NODE, out_index);
}

static bool check_bool_literal(CheckContext *ctx, const AstNode *node,
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

static bool check_binary(CheckContext *ctx, const AstNode *node,
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

    switch (node->op) {
    case NIPPY_OPERATOR_ADD:
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
                       left_type->kind == NIPPY_TYPE_BOOL);
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

static bool check_unary(CheckContext *ctx, const AstNode *node,
                        size_t ast_index)
{
    size_t operand;
    const TypedNode *operand_node;
    const TypedType *operand_type;
    size_t bool_type;
    TypedNode typed = {0};

    if (node->op != NIPPY_OPERATOR_NOT) {
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
    if (operand_type == NULL || operand_type->kind != NIPPY_TYPE_BOOL) {
        error_report_at(ctx->errors, node->pos, MESSAGE_CHECKER_OPERATOR_TYPE,
                        operator_text(node->op));
        return false;
    }
    if (!add_builtin_type(ctx, NIPPY_TYPE_BOOL, &bool_type)) {
        return false;
    }

    typed.kind = TYPED_UNARY;
    typed.type_index = bool_type;
    typed.pos = node->pos;
    typed.op = node->op;
    typed.name_index = TYPED_NO_NODE;
    typed.left_child = operand;
    typed.right_child = TYPED_NO_NODE;
    typed.children_start = 0;
    typed.children_count = 0;
    return emit_node(ctx, &typed, ast_index, &(size_t){0});
}

static const char *struct_field_name(const TypedProgram *program,
                                     const TypedNode *type_decl,
                                     size_t field_index)
{
    size_t child = *(const size_t *)list_get(
        &program->children, type_decl->children_start + field_index);
    const TypedNode *field = typed_program_node(program, child);

    return field == NULL ? NULL
                         : typed_program_symbol_text(program, field->name_index);
}

static size_t struct_field_type(const TypedProgram *program,
                                const TypedNode *type_decl,
                                size_t field_index)
{
    size_t child = *(const size_t *)list_get(
        &program->children, type_decl->children_start + field_index);
    const TypedNode *field = typed_program_node(program, child);

    return field == NULL ? TYPED_NO_NODE : field->type_index;
}

static bool constructor_value_fits(CheckContext *ctx, size_t value,
                                   const TypeEntry *entry, size_t field_index,
                                   size_t argument_index, SourcePos pos)
{
    const TypedNode *value_node = typed_program_node(ctx->program, value);
    const TypedType *value_type = value_node == NULL
                                      ? NULL
                                      : typed_program_type(ctx->program,
                                                           value_node->type_index);
    size_t field_type_index = struct_field_type(ctx->program,
                                                typed_program_node(
                                                    ctx->program,
                                                    entry->node_index),
                                                field_index);
    const TypedType *field_type = typed_program_type(ctx->program,
                                                     field_type_index);

    if (value_type == NULL || field_type == NULL ||
        !same_type(ctx->program, value_node->type_index, field_type_index)) {
        error_report_at(ctx->errors, pos,
                        MESSAGE_CHECKER_CONSTRUCTOR_ARG_TYPE,
                        argument_index, entry->name);
        return false;
    }
    return true;
}

static bool check_constructor(CheckContext *ctx, const AstNode *node,
                              const TypeEntry *entry, size_t ast_index)
{
    const TypedNode *type_decl = typed_program_node(ctx->program,
                                                    entry->node_index);
    size_t field_count;
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
        if (node->children_count != field_count) {
            error_report_at(ctx->errors, node->pos,
                            MESSAGE_CHECKER_CONSTRUCTOR_ARG_COUNT,
                            entry->name, field_count, node->children_count);
            goto done;
        }
        for (i = 0; i < field_count; i++) {
            size_t child = *(const size_t *)list_get(
                &ctx->ast->children, node->children_start + i);
            size_t value;

            if (!check_expression(ctx, child, &value)) {
                goto done;
            }
            if (!constructor_value_fits(ctx, value, entry, i, i + 1,
                                         node->pos)) {
                goto done;
            }
            if (!list_push(&values, &value)) {
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
                const char *field_name = struct_field_name(ctx->program,
                                                           type_decl, j);

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
            const char *field_name = struct_field_name(ctx->program,
                                                       type_decl, i);
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
                error_report_at(ctx->errors, node->pos,
                                MESSAGE_CHECKER_MISSING_CONSTRUCTOR_ARG,
                                field_name);
                goto done;
            }
            {
                size_t value;

                if (!check_expression(ctx, found->left_child, &value)) {
                    goto done;
                }
                if (!constructor_value_fits(ctx, value, entry, i, i + 1,
                                            found->pos)) {
                    goto done;
                }
                if (!list_push(&values, &value)) {
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

static bool check_function_call(CheckContext *ctx, const AstNode *node,
                                const char *name, size_t type_index,
                                size_t ast_index)
{
    const TypedType *callee_type = typed_program_type(ctx->program, type_index);
    const TypedFunctionSignature *signature;
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

    if (node->children_count != signature->parameter_count) {
        error_report_at(ctx->errors, node->pos,
                        MESSAGE_CHECKER_FUNCTION_ARG_COUNT, name,
                        signature->parameter_count, node->children_count);
        return false;
    }

    list_init(&values, sizeof(size_t));
    for (i = 0; i < node->children_count; i++) {
        size_t child = *(const size_t *)list_get(&ctx->ast->children,
                                                 node->children_start + i);
        size_t value;
        size_t parameter_type;
        const TypedNode *value_node;

        if (!check_expression(ctx, child, &value)) {
            goto done;
        }
        value_node = typed_program_node(ctx->program, value);
        parameter_type = *(const size_t *)list_get(
            &ctx->program->signature_parameters,
            signature->first_parameter + i);
        if (value_node == NULL ||
            !same_type(ctx->program, value_node->type_index, parameter_type)) {
            error_report_at(ctx->errors, node->pos,
                            MESSAGE_CHECKER_FUNCTION_ARG_TYPE, i + 1, name);
            goto done;
        }
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
    typed.type_index = signature->return_type;
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

static bool check_call(CheckContext *ctx, const AstNode *node,
                       size_t ast_index)
{
    const char *name = ast_symbol_text(ctx->ast, node->name_index);
    const VariableEntry *variable;
    const FunctionEntry *function;
    const TypeEntry *type_entry;

    if (name == NULL) {
        error_report_at(ctx->errors, node->pos,
                        MESSAGE_CHECKER_MISSING_NAME_OR_TYPE);
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

    function = find_function(ctx->functions, name);
    if (function != NULL) {
        return check_function_call(ctx, node, name, function->type_index,
                                   ast_index);
    }

    if (ctx->in_function && find_variable(ctx->globals, name) != NULL) {
        error_report_at(ctx->errors, node->pos,
                        MESSAGE_CHECKER_GLOBAL_IN_FUNCTION, name);
        return false;
    }

    type_entry = find_type(ctx->types, name);
    if (type_entry != NULL) {
        return check_constructor(ctx, node, type_entry, ast_index);
    }
    if (strcmp(name, "Int32") == 0 || strcmp(name, "Bool") == 0) {
        error_report_at(ctx->errors, node->pos,
                        MESSAGE_CHECKER_CONSTRUCTOR_NOT_STRUCT, name);
        return false;
    }
    error_report_at(ctx->errors, node->pos, MESSAGE_CHECKER_UNKNOWN_CALL,
                    name);
    return false;
}

static bool check_expression(CheckContext *ctx, size_t ast_index,
                             size_t *out_typed)
{
    const AstNode *node = ast_get_node(ctx->ast, ast_index);
    size_t *slot;

    if (node == NULL) {
        error_report(ctx->errors, MESSAGE_CHECKER_MISSING_AST_NODE, ast_index);
        return false;
    }
    slot = mapping_slot(ctx->mapping, ast_index);
    if (*slot != TYPED_NO_NODE) {
        *out_typed = *slot;
        return true;
    }

    {
        bool ok;

        switch (node->kind) {
        case AST_INTEGER_LITERAL:
            ok = check_literal(ctx, node, ast_index);
            break;
        case AST_BOOL_LITERAL:
            ok = check_bool_literal(ctx, node, ast_index);
            break;
        case AST_UNARY:
            ok = check_unary(ctx, node, ast_index);
            break;
        case AST_IDENTIFIER:
            ok = check_identifier(ctx, node, ast_index);
            break;
        case AST_BINARY:
            ok = check_binary(ctx, node, ast_index);
            break;
        case AST_CALL:
            ok = check_call(ctx, node, ast_index);
            break;
        default:
            error_report_at(ctx->errors, node->pos,
                            MESSAGE_CHECKER_UNSUPPORTED_AST_NODE);
            return false;
        }
        if (ok) {
            *out_typed = *slot;
        }
        return ok;
    }
}

static bool check_var_declaration(CheckContext *ctx, const AstNode *node,
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
    if (find_variable(ctx->globals, name) != NULL ||
        find_function(ctx->functions, name) != NULL ||
        find_type(ctx->types, name) != NULL ||
        strcmp(name, "Int32") == 0 || strcmp(name, "Bool") == 0) {
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
    if (!check_expression(ctx, node->left_child, &initialiser)) {
        return false;
    }
    initialiser_node = typed_program_node(ctx->program, initialiser);
    if (initialiser_node == NULL) {
        error_report_at(ctx->errors, node->pos,
                        MESSAGE_CHECKER_MISSING_AST_NODE, node->left_child);
        return false;
    }

    if (declared_type != TYPED_NO_NODE) {
        if (!same_type(ctx->program, declared_type,
                       initialiser_node->type_index)) {
            error_report_at(ctx->errors, node->pos,
                            MESSAGE_CHECKER_INITIALISER_TYPE_MISMATCH, name,
                            ast_type_text(ctx->ast, node->right_child));
            return false;
        }
        variable_type = declared_type;
    } else {
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
        if (!list_push(ctx->globals, &entry)) {
            error_report(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
            return false;
        }
    }
    return true;
}

static bool check_field(CheckContext *ctx, const AstNode *node)
{
    const char *name = ast_symbol_text(ctx->ast, node->name_index);
    const AstNode *type_node = ast_get_node(ctx->ast, node->left_child);
    size_t field_type_index;
    const TypedType *field_type;
    size_t field_index;
    TypedNode typed = {0};

    if (name == NULL || type_node == NULL) {
        error_report_at(ctx->errors, node->pos,
                        MESSAGE_CHECKER_MISSING_NAME_OR_TYPE);
        return false;
    }
    if (!resolve_type_node(ctx, node->left_child, name, &field_type_index)) {
        return false;
    }
    field_type = typed_program_type(ctx->program, field_type_index);
    if (field_type == NULL ||
        (field_type->kind != NIPPY_TYPE_INT32 &&
         field_type->kind != NIPPY_TYPE_BOOL)) {
        error_report_at(ctx->errors, node->pos, MESSAGE_CHECKER_FIELD_TYPE,
                        name);
        return false;
    }

    typed.kind = TYPED_FIELD;
    typed.type_index = field_type_index;
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

    if (!typed_program_push(ctx->program, &typed, &field_index)) {
        error_report(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_PROGRAM);
        return false;
    }
    if (!typed_program_push_child(ctx->program, field_index)) {
        error_report(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
        return false;
    }
    return true;
}

static bool check_type_declaration(CheckContext *ctx, const AstNode *node,
                                   size_t ast_index)
{
    const char *name = ast_symbol_text(ctx->ast, node->name_index);
    size_t children_start = ctx->program->children.length;
    size_t typed_index;
    size_t struct_type;
    size_t i;
    TypedNode typed = {0};

    if (name == NULL) {
        error_report_at(ctx->errors, node->pos,
                        MESSAGE_CHECKER_MISSING_NAME_OR_TYPE);
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
    if (find_type(ctx->types, name) != NULL) {
        error_report_at(ctx->errors, node->pos, MESSAGE_CHECKER_DUPLICATE_TYPE,
                        name);
        return false;
    }
    if (find_variable(ctx->globals, name) != NULL ||
        find_function(ctx->functions, name) != NULL) {
        error_report_at(ctx->errors, node->pos, MESSAGE_CHECKER_DUPLICATE_NAME,
                        name);
        return false;
    }
    if (node->children_count == 0) {
        error_report_at(ctx->errors, node->pos,
                        MESSAGE_CHECKER_TYPE_HAS_NO_FIELDS);
        return false;
    }

    for (i = 0; i < node->children_count; i++) {
        size_t child = *(const size_t *)list_get(&ctx->ast->children,
                                                 node->children_start + i);
        const AstNode *field = ast_get_node(ctx->ast, child);

        if (field == NULL) {
            error_report(ctx->errors, MESSAGE_CHECKER_MISSING_AST_NODE, child);
            return false;
        }
        if (!check_field(ctx, field)) {
            return false;
        }
    }

    typed.kind = TYPED_TYPE_DECLARATION;
    typed.type_index = TYPED_NO_NODE;
    typed.pos = node->pos;
    typed.name_index = typed_program_copy_symbol(ctx->program, name,
                                                 strlen(name));
    if (typed.name_index == TYPED_NO_NODE) {
        error_report(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_COPYING_NAME);
        return false;
    }
    typed.left_child = TYPED_NO_NODE;
    typed.right_child = TYPED_NO_NODE;
    typed.children_start = children_start;
    typed.children_count = node->children_count;

    if (!emit_node(ctx, &typed, ast_index, &typed_index)) {
        return false;
    }
    if (!add_type_record(ctx, NIPPY_TYPE_STRUCT, typed.name_index,
                         TYPED_NO_NODE, &struct_type)) {
        return false;
    }

    {
        TypeEntry entry;

        memset(&entry, 0, sizeof(entry));
        memcpy(entry.name, name, strlen(name) + 1);
        entry.node_index = typed_index;
        entry.type_index = struct_type;
        if (!list_push(ctx->types, &entry)) {
            error_report(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
            return false;
        }
    }
    return true;
}

static bool check_condition(CheckContext *ctx, size_t ast_index,
                            size_t *out_typed)
{
    size_t expression;
    const TypedNode *node;
    const TypedType *type;

    if (!check_expression(ctx, ast_index, &expression)) {
        return false;
    }
    node = typed_program_node(ctx->program, expression);
    type = node == NULL ? NULL
                        : typed_program_type(ctx->program, node->type_index);
    if (type == NULL || type->kind != NIPPY_TYPE_BOOL) {
        const AstNode *ast_node = ast_get_node(ctx->ast, ast_index);

        error_report_at(ctx->errors,
                        ast_node == NULL ? (SourcePos){0, 1, 1} : ast_node->pos,
                        MESSAGE_CHECKER_CONDITION_TYPE);
        return false;
    }
    *out_typed = expression;
    return true;
}

static bool typed_block_returns(const TypedProgram *program, size_t index);
static bool typed_statement_returns(const TypedProgram *program, size_t index);
static bool typed_statement_terminates(const TypedProgram *program,
                                       size_t index);

static bool typed_statement_returns(const TypedProgram *program, size_t index)
{
    const TypedNode *node = typed_program_node(program, index);

    if (node == NULL) {
        return false;
    }
    if (node->kind == TYPED_RETURN) {
        return true;
    }
    if (node->kind == TYPED_BLOCK) {
        return typed_block_returns(program, index);
    }
    if (node->kind == TYPED_IF) {
        size_t then_block;
        size_t else_block;

        if (node->children_count != 2) {
            return false;
        }
        then_block = *(const size_t *)list_get(&program->children,
                                               node->children_start);
        else_block = *(const size_t *)list_get(&program->children,
                                               node->children_start + 1);
        return typed_statement_returns(program, then_block) &&
               typed_statement_returns(program, else_block);
    }
    return false;
}

static bool typed_statement_terminates(const TypedProgram *program,
                                       size_t index)
{
    const TypedNode *node = typed_program_node(program, index);

    if (node == NULL) {
        return false;
    }
    if (node->kind == TYPED_RETURN || node->kind == TYPED_BREAK ||
        node->kind == TYPED_CONTINUE) {
        return true;
    }
    if (node->kind == TYPED_BLOCK) {
        size_t i;

        for (i = 0; i < node->children_count; i++) {
            size_t child = *(const size_t *)list_get(&program->children,
                                                     node->children_start + i);

            if (typed_statement_terminates(program, child)) {
                return true;
            }
        }
        return false;
    }
    if (node->kind == TYPED_IF) {
        size_t then_block;
        size_t else_block;

        if (node->children_count != 2) {
            return false;
        }
        then_block = *(const size_t *)list_get(&program->children,
                                               node->children_start);
        else_block = *(const size_t *)list_get(&program->children,
                                               node->children_start + 1);
        return typed_statement_terminates(program, then_block) &&
               typed_statement_terminates(program, else_block);
    }
    return false;
}

static bool typed_block_returns(const TypedProgram *program, size_t index)
{
    const TypedNode *block = typed_program_node(program, index);
    size_t i;

    if (block == NULL || block->kind != TYPED_BLOCK) {
        return false;
    }
    for (i = 0; i < block->children_count; i++) {
        size_t child = *(const size_t *)list_get(&program->children,
                                                 block->children_start + i);

        if (typed_statement_returns(program, child)) {
            return true;
        }
        if (typed_statement_terminates(program, child)) {
            return false;
        }
    }
    return false;
}

static bool check_statement(CheckContext *ctx, size_t ast_index,
                            size_t return_type, size_t *out_typed)
{
    const AstNode *node = ast_get_node(ctx->ast, ast_index);
    TypedNode typed = {0};

    if (node == NULL) {
        error_report(ctx->errors, MESSAGE_CHECKER_MISSING_AST_NODE, ast_index);
        return false;
    }

    if (node->kind == AST_RETURN) {
        size_t expression;
        const TypedNode *expression_node;

        if (!check_expression(ctx, node->left_child, &expression)) {
            return false;
        }
        expression_node = typed_program_node(ctx->program, expression);
        if (expression_node == NULL ||
            !same_type(ctx->program, expression_node->type_index,
                       return_type)) {
            error_report_at(ctx->errors, node->pos,
                            MESSAGE_CHECKER_RETURN_TYPE_MISMATCH,
                            ctx->function_name == NULL ? "function"
                                                       : ctx->function_name);
            return false;
        }
        typed.kind = TYPED_RETURN;
        typed.type_index = return_type;
        typed.pos = node->pos;
        typed.left_child = expression;
        typed.right_child = TYPED_NO_NODE;
        return emit_node(ctx, &typed, ast_index, out_typed);
    }

    if (node->kind == AST_IF) {
        size_t condition;
        size_t then_block;
        size_t else_block = TYPED_NO_NODE;
        size_t children_start;

        if (!check_condition(ctx, node->left_child, &condition)) {
            return false;
        }
        if (node->children_count == 0) {
            error_report_at(ctx->errors, node->pos,
                            MESSAGE_CHECKER_UNSUPPORTED_AST_NODE);
            return false;
        }
        {
            size_t then_ast = *(const size_t *)list_get(
                &ctx->ast->children, node->children_start);

            if (!check_block(ctx, then_ast, return_type, &then_block)) {
                return false;
            }
        }
        if (node->children_count > 1) {
            size_t else_ast = *(const size_t *)list_get(
                &ctx->ast->children, node->children_start + 1);

            if (!check_block(ctx, else_ast, return_type, &else_block)) {
                return false;
            }
        }

        children_start = ctx->program->children.length;
        if (!typed_program_push_child(ctx->program, then_block)) {
            error_report(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
            return false;
        }
        if (else_block != TYPED_NO_NODE &&
            !typed_program_push_child(ctx->program, else_block)) {
            error_report(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
            return false;
        }

        typed.kind = TYPED_IF;
        typed.type_index = TYPED_NO_NODE;
        typed.pos = node->pos;
        typed.left_child = condition;
        typed.right_child = TYPED_NO_NODE;
        typed.children_start = children_start;
        typed.children_count = node->children_count > 1 ? 2 : 1;
        return emit_node(ctx, &typed, ast_index, out_typed);
    }

    if (node->kind == AST_WHILE) {
        size_t condition;
        size_t body;

        if (!check_condition(ctx, node->left_child, &condition)) {
            return false;
        }
        ctx->loop_depth++;
        if (!check_block(ctx, node->right_child, return_type, &body)) {
            ctx->loop_depth--;
            return false;
        }
        ctx->loop_depth--;

        typed.kind = TYPED_WHILE;
        typed.type_index = TYPED_NO_NODE;
        typed.pos = node->pos;
        typed.left_child = condition;
        typed.right_child = body;
        return emit_node(ctx, &typed, ast_index, out_typed);
    }

    if (node->kind == AST_BREAK || node->kind == AST_CONTINUE) {
        if (ctx->loop_depth == 0) {
            error_report_at(ctx->errors, node->pos,
                            node->kind == AST_BREAK
                                ? MESSAGE_CHECKER_BREAK_OUTSIDE_LOOP
                                : MESSAGE_CHECKER_CONTINUE_OUTSIDE_LOOP);
            return false;
        }
        typed.kind = node->kind == AST_BREAK ? TYPED_BREAK : TYPED_CONTINUE;
        typed.type_index = TYPED_NO_NODE;
        typed.pos = node->pos;
        typed.left_child = TYPED_NO_NODE;
        typed.right_child = TYPED_NO_NODE;
        return emit_node(ctx, &typed, ast_index, out_typed);
    }

    error_report_at(ctx->errors, node->pos,
                    MESSAGE_CHECKER_UNSUPPORTED_AST_NODE);
    return false;
}

static bool check_block(CheckContext *ctx, size_t ast_index,
                        size_t return_type, size_t *out_typed)
{
    const AstNode *node = ast_get_node(ctx->ast, ast_index);
    List statements;
    size_t children_start;
    size_t i;
    TypedNode typed = {0};
    bool ok = false;

    if (node == NULL || node->kind != AST_BLOCK) {
        error_report_at(ctx->errors,
                        node == NULL ? (SourcePos){0, 1, 1} : node->pos,
                        MESSAGE_CHECKER_UNSUPPORTED_AST_NODE);
        return false;
    }

    list_init(&statements, sizeof(size_t));
    for (i = 0; i < node->children_count; i++) {
        size_t child = *(const size_t *)list_get(&ctx->ast->children,
                                                 node->children_start + i);
        size_t statement;

        if (!check_statement(ctx, child, return_type, &statement)) {
            goto done;
        }
        if (!list_push(&statements, &statement)) {
            error_report(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
            goto done;
        }
    }

    children_start = ctx->program->children.length;
    for (i = 0; i < statements.length; i++) {
        size_t statement = *(const size_t *)list_get(&statements, i);

        if (!typed_program_push_child(ctx->program, statement)) {
            error_report(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
            goto done;
        }
    }

    typed.kind = TYPED_BLOCK;
    typed.type_index = TYPED_NO_NODE;
    typed.pos = node->pos;
    typed.left_child = TYPED_NO_NODE;
    typed.right_child = TYPED_NO_NODE;
    typed.children_start = children_start;
    typed.children_count = statements.length;
    ok = emit_node(ctx, &typed, ast_index, out_typed);

done:
    list_free(&statements);
    return ok;
}

static bool check_function_declaration(CheckContext *ctx, const AstNode *node,
                                       size_t ast_index)
{
    const char *name = ast_symbol_text(ctx->ast, node->name_index);
    List parameter_types;
    size_t children_start = ctx->program->children.length;
    size_t return_type;
    size_t function_type;
    size_t typed_body;
    size_t typed_index;
    size_t i;
    TypedNode typed = {0};
    bool ok = false;

    if (name == NULL) {
        error_report_at(ctx->errors, node->pos,
                        MESSAGE_CHECKER_MISSING_NAME_OR_TYPE);
        return false;
    }
    if (find_type(ctx->types, name) != NULL ||
        find_function(ctx->functions, name) != NULL ||
        find_variable(ctx->globals, name) != NULL ||
        strcmp(name, "Int32") == 0 || strcmp(name, "Bool") == 0) {
        error_report_at(ctx->errors, node->pos, MESSAGE_CHECKER_DUPLICATE_NAME,
                        name);
        return false;
    }
    if (node->right_child == AST_NO_NODE) {
        error_report_at(ctx->errors, node->pos,
                        MESSAGE_CHECKER_MISSING_NAME_OR_TYPE);
        return false;
    }

    list_init(&parameter_types, sizeof(size_t));
    ctx->in_function = true;
    ctx->locals->length = 0;

    for (i = 0; i < node->children_count; i++) {
        size_t child = *(const size_t *)list_get(&ctx->ast->children,
                                                 node->children_start + i);
        const AstNode *parameter = ast_get_node(ctx->ast, child);
        const char *parameter_name;
        size_t parameter_type;
        size_t parameter_index;
        VariableEntry entry;
        TypedNode typed_parameter = {0};

        if (parameter == NULL || parameter->kind != AST_PARAMETER) {
            error_report(ctx->errors, MESSAGE_CHECKER_MISSING_AST_NODE, child);
            goto done;
        }
        parameter_name = ast_symbol_text(ctx->ast, parameter->name_index);
        if (parameter_name == NULL) {
            error_report_at(ctx->errors, parameter->pos,
                            MESSAGE_CHECKER_MISSING_NAME_OR_TYPE);
            goto done;
        }
        if (find_variable(ctx->locals, parameter_name) != NULL) {
            error_report_at(ctx->errors, parameter->pos,
                            MESSAGE_CHECKER_DUPLICATE_PARAMETER,
                            parameter_name);
            goto done;
        }
        if (!resolve_type_node(ctx, parameter->left_child, parameter_name,
                               &parameter_type)) {
            goto done;
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
        typed_parameter.children_start = 0;
        typed_parameter.children_count = 0;
        if (!typed_program_push(ctx->program, &typed_parameter,
                                &parameter_index) ||
            !typed_program_push_child(ctx->program, parameter_index) ||
            !list_push(&parameter_types, &parameter_type)) {
            error_report(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_PROGRAM);
            goto done;
        }

        memset(&entry, 0, sizeof(entry));
        memcpy(entry.name, parameter_name, strlen(parameter_name) + 1);
        entry.type_index = parameter_type;
        if (!list_push(ctx->locals, &entry)) {
            error_report(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
            goto done;
        }
    }

    if (!resolve_type_node(ctx, node->right_child, name, &return_type)) {
        goto done;
    }
    if (!add_function_type(ctx, &parameter_types, return_type, &function_type)) {
        goto done;
    }

    {
        FunctionEntry entry;

        memset(&entry, 0, sizeof(entry));
        memcpy(entry.name, name, strlen(name) + 1);
        entry.type_index = function_type;
        if (!list_push(ctx->functions, &entry)) {
            error_report(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
            goto done;
        }
    }

    if (node->left_child == AST_NO_NODE) {
        error_report_at(ctx->errors, node->pos, MESSAGE_CHECKER_MISSING_RETURN,
                        name);
        goto done;
    }
    ctx->loop_depth = 0;
    ctx->function_name = name;
    if (!check_block(ctx, node->left_child, return_type, &typed_body)) {
        goto done;
    }
    if (!typed_block_returns(ctx->program, typed_body)) {
        error_report_at(ctx->errors, node->pos,
                        MESSAGE_CHECKER_FUNCTION_MAY_NOT_RETURN, name);
        goto done;
    }

    typed.kind = TYPED_FUNCTION_DECLARATION;
    typed.type_index = function_type;
    typed.pos = node->pos;
    typed.name_index = typed_program_copy_symbol(ctx->program, name,
                                                 strlen(name));
    if (typed.name_index == TYPED_NO_NODE) {
        error_report(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_COPYING_NAME);
        goto done;
    }
    typed.left_child = typed_body;
    typed.right_child = TYPED_NO_NODE;
    typed.children_start = children_start;
    typed.children_count = node->children_count;

    if (!emit_node(ctx, &typed, ast_index, &typed_index)) {
        goto done;
    }
    ok = true;

done:
    ctx->in_function = false;
    ctx->loop_depth = 0;
    ctx->function_name = NULL;
    ctx->locals->length = 0;
    list_free(&parameter_types);
    return ok;
}

bool checker_check(const Ast *ast, TypedProgram *program, ErrorState *errors)
{
    CheckContext ctx;
    List mapping;
    List types;
    List functions;
    List globals;
    List locals;
    size_t i;
    bool ok = false;

    if (ast->roots.length == 0) {
        error_report(errors, MESSAGE_CHECKER_MISSING_AST_ROOT);
        return false;
    }

    list_init(&mapping, sizeof(size_t));
    list_init(&types, sizeof(TypeEntry));
    list_init(&functions, sizeof(FunctionEntry));
    list_init(&globals, sizeof(VariableEntry));
    list_init(&locals, sizeof(VariableEntry));

    ctx.ast = ast;
    ctx.program = program;
    ctx.errors = errors;
    ctx.mapping = &mapping;
    ctx.types = &types;
    ctx.functions = &functions;
    ctx.globals = &globals;
    ctx.locals = &locals;
    ctx.in_function = false;
    ctx.loop_depth = 0;
    ctx.function_name = NULL;

    for (i = 0; i < ast->nodes.length; i++) {
        size_t none = TYPED_NO_NODE;

        if (!list_push(&mapping, &none)) {
            error_report(errors, MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
            goto done;
        }
    }

    for (i = 0; i < ast->roots.length; i++) {
        size_t ast_root = *(const size_t *)list_get(&ast->roots, i);
        const AstNode *node = ast_get_node(ast, ast_root);
        size_t typed_root;
        bool checked = false;

        if (node == NULL) {
            error_report(errors, MESSAGE_CHECKER_MISSING_AST_NODE, ast_root);
            goto done;
        }

        switch (node->kind) {
        case AST_DECLARATION:
            checked = check_var_declaration(&ctx, node, ast_root);
            break;
        case AST_TYPE_DECLARATION:
            checked = check_type_declaration(&ctx, node, ast_root);
            break;
        case AST_FUNCTION_DECLARATION:
            checked = check_function_declaration(&ctx, node, ast_root);
            break;
        default:
            error_report_at(errors, node->pos,
                            MESSAGE_CHECKER_UNSUPPORTED_AST_NODE);
            goto done;
        }
        if (!checked) {
            goto done;
        }

        typed_root = *mapping_slot(&mapping, ast_root);
        if (!list_push(&program->roots, &typed_root)) {
            error_report(errors, MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
            goto done;
        }
    }
    ok = true;

done:
    list_free(&mapping);
    list_free(&types);
    list_free(&functions);
    list_free(&globals);
    list_free(&locals);
    return ok;
}
