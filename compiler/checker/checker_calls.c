#include "checker_internal.h"

typedef struct {
    size_t value;
    size_t temporary;
} CallBinding;

static bool emit_sequenced_expression_slot(const List *order, size_t i, size_t first, TypedNode *call, CheckContext *ctx, size_t **child, const TypedNode **value, size_t **scope, TypedNode *reference, size_t *slot)
{
    (*slot) = order == NULL ? i : *(const size_t *)list_get(order, i - first);
    
    if ((*slot) < first || (*slot) >= call->children_count) {
        error_report(ctx->errors, MESSAGE_CHECKER_UNSUPPORTED_AST_NODE);
        return true;
    }
    (*child) = (size_t *)list_get(&ctx->program->children,
                               call->children_start + (*slot));
    (*value) = (*child) == NULL ? NULL :
                             typed_program_node(ctx->program, *(*child));
    (*scope) = (size_t *)list_get(&ctx->program->match_scopes,
                                      ctx->match_scope);
    
    (*reference) = (TypedNode){0};
    
    

    if ((*value) == NULL || (*scope) == NULL) {
        error_report(ctx->errors, MESSAGE_CHECKER_UNSUPPORTED_AST_NODE);
        return true;
    }
    return false;
}

static bool emit_sequenced_expression_ok(size_t first, TypedNode *call, const List *order, CheckContext *ctx, size_t ast_index, bool *ok, bool *outcome)
{
    (*ok) = false;

    if (first > call->children_count ||
        (order != NULL && order->length != call->children_count - first)) {
        error_report(ctx->errors, MESSAGE_CHECKER_UNSUPPORTED_AST_NODE);
        do { *outcome = (false); return true; } while (0);
    }
    if (call->children_count - first < 2)
        do { *outcome = (emit_node(ctx, call, ast_index, &(size_t){0})); return true; } while (0);
    return false;
}

static bool emit_sequenced_expression_sequenced_expression_slot(const List *order, size_t i, size_t first, TypedNode *call, CheckContext *ctx, CallBinding *binding, size_t **child, TypedMatchTemporary *temporary, const TypedNode **value, size_t **scope, TypedNode *reference, size_t *reference_index, size_t *slot, List *bindings)
{
    if (emit_sequenced_expression_slot(order, i, first, call, ctx, child, value, scope, reference, slot)) {
    return true;
}
    (*binding).value = *(*child);
    (*binding).temporary = ctx->program->match_temporaries.length;
    (*temporary).type_index = (*value)->type_index;
    (*temporary).next = *(*scope);
    (*reference).kind = TYPED_MATCH_TEMPORARY;
    (*reference).type_index = (*value)->type_index;
    (*reference).pos = (*value)->pos;
    (*reference).name_index = TYPED_NO_NODE;
    (*reference).left_child = TYPED_NO_NODE;
    (*reference).right_child = TYPED_NO_NODE;
    (*reference).integer_value_u = (*binding).temporary;
    checker_ok(ctx->errors, list_push(&ctx->program->match_temporaries, temporary), MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
    if (!checker_push_node(ctx, reference, reference_index, MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING)) {
        return true;
    }
    checker_ok(ctx->errors, list_push(bindings, binding), MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
    return false;
}

/* Bind values in source order before emitting the expression. A method
   receiver stays an lvalue for mutation. An optional order maps source order
   to the expression's field-ordered child positions. */
bool emit_sequenced_expression(CheckContext *ctx, TypedNode *call,
                               size_t ast_index, const List *order)
{
    List bindings;
    size_t first = call->is_method_call ? 1 : 0;
    size_t result, i;
    bool ok = {0};
    {
        bool outcome;

        if (emit_sequenced_expression_ok(first, call, order, ctx, ast_index, &ok, &outcome)) {
            return outcome;
        }
    }
    list_init(&bindings, sizeof(CallBinding));
    for (i = first; i < call->children_count; i++) {
        CallBinding binding = {0};
        size_t *child = {0};
        TypedMatchTemporary temporary = {0};
        const TypedNode *value = {0};
        size_t *scope = {0};
        TypedNode reference = {0};
        size_t reference_index = {0};
        size_t slot = {0};
        if (emit_sequenced_expression_sequenced_expression_slot(order, i, first, call, ctx, &binding, &child, &temporary, &value, &scope, &reference, &reference_index, &slot, &bindings)) {
        goto done;
    }
        *scope = binding.temporary;
        *(size_t *)list_get(&ctx->program->children, call->children_start + slot) =
            reference_index;
    }
    if (!checker_push_node(ctx, call, &result, MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING)) {
        goto done;
    }
    for (i = bindings.length; i > 0; i--) {
        const CallBinding *binding = list_get(&bindings, i - 1);
        TypedNode wrapper = {0};
        wrapper.kind = TYPED_MATCH_BIND_EXPRESSION;
        wrapper.type_index = call->type_index;
        wrapper.pos = call->pos;
        wrapper.name_index = TYPED_NO_NODE;
        wrapper.left_child = binding->value;
        wrapper.right_child = result;
        wrapper.integer_value_u = binding->temporary;
        if (!checker_push_node(ctx, &wrapper, &result, MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING)) {
            goto done;
        }
    }
    *mapping_slot(ctx->mapping, ast_index) = result;
    ok = true;
done:
    list_free(&bindings);
    return ok;
}

/* range(a, b) and range(a, b, step): integers only. The arguments are checked,
   coerced to one element type, and bound to temporaries so each is evaluated
   once, in source order. */
typedef enum {
    RANGE_OK,
    RANGE_INVALID, /* wrong shape: say so unless something else was said */
    RANGE_FAILED   /* already reported */
} RangeStep;

typedef struct {
    size_t values[3];
    size_t coerced[3];
    size_t temporaries[3];
    size_t element;
    size_t type;  /* the Range type */
    size_t start; /* where the arguments' children begin */
} RangeArgs;

typedef RangeStep (*RangeStepFn)(CheckContext *ctx, const AstNode *node,
                                 RangeArgs *range);

static RangeStep range_arguments(CheckContext *ctx, const AstNode *node,
                                 RangeArgs *range)
{
    size_t i;

    if (node->children_count < 2 || node->children_count > 3) {
        return RANGE_INVALID;
    }
    for (i = 0; i < node->children_count; i++) {
        size_t child = *(const size_t *)list_get(&ctx->ast->children,
                                                 node->children_start + i);
        const AstNode *argument = ast_get_node(ctx->ast, child);

        if (argument == NULL || argument->kind == AST_NAMED_ARG) {
            return RANGE_INVALID;
        }
        if (!check_expression(ctx, child, &range->values[i])) {
            return RANGE_FAILED;
        }
    }
    return RANGE_OK;
}

/* The elements have the type of the arguments, taking it from the one that is
   not a bare literal. */
static RangeStep range_element(CheckContext *ctx, const AstNode *node,
                               RangeArgs *range)
{
    const TypedNode *first = typed_program_node(ctx->program, range->values[0]);
    const TypedNode *second = typed_program_node(ctx->program, range->values[1]);
    NumericConstant constant;
    bool first_literal;
    bool second_literal;

    (void)node;
    if (first == NULL || second == NULL) {
        return RANGE_INVALID;
    }
    first_literal =
        numeric_constant_read(ctx->program, range->values[0], false, &constant);
    second_literal =
        numeric_constant_read(ctx->program, range->values[1], false, &constant);
    range->element = first_literal && !second_literal ? second->type_index
                                                      : first->type_index;
    return RANGE_OK;
}

/* The step is an Int64 that is not zero. */
static RangeStep range_step(CheckContext *ctx, const AstNode *node,
                            RangeArgs *range, size_t step_type)
{
    NumericConstant step;

    if (node->children_count != 3) {
        return RANGE_OK;
    }
    if (numeric_constant_read(ctx->program, range->values[2], true, &step) &&
        !step.is_float && step.magnitude == 0) {
        error_report_at(ctx->errors, node->pos, MESSAGE_CHECKER_RANGE_ZERO_STEP);
        return RANGE_FAILED;
    }
    if (!inject_into(ctx, range->values[2], step_type, &range->coerced[2])) {
        return RANGE_INVALID;
    }
    return check_default_numeric(ctx, range->coerced[2]) ? RANGE_OK
                                                         : RANGE_FAILED;
}

static RangeStep range_coerce(CheckContext *ctx, const AstNode *node,
                              RangeArgs *range)
{
    const TypedType *type = typed_program_type(ctx->program, range->element);
    NyxType element_kind;
    size_t step_type;

    if (type == NULL || !nyx_type_is_integer(type->kind)) {
        return RANGE_INVALID;
    }
    element_kind = type->kind;
    if (!inject_into(ctx, range->values[0], range->element,
                     &range->coerced[0]) ||
        !inject_into(ctx, range->values[1], range->element,
                     &range->coerced[1]) ||
        !builtin_type(ctx, NYX_TYPE_INT64, &step_type)) {
        return RANGE_INVALID;
    }
    if (element_kind == NYX_TYPE_INT64 &&
        (!check_default_numeric(ctx, range->coerced[0]) ||
         !check_default_numeric(ctx, range->coerced[1]))) {
        return RANGE_FAILED;
    }
    return range_step(ctx, node, range, step_type);
}

/* Hold each argument in a temporary of the enclosing scope, and make the
   references the Range node will read. */
static RangeStep range_bind(CheckContext *ctx, const AstNode *node,
                            RangeArgs *range)
{
    size_t i;

    if (!range_type(ctx, range->element, &range->type)) {
        return RANGE_FAILED;
    }
    for (i = 0; i < node->children_count; i++) {
        size_t *scope = (size_t *)list_get(&ctx->program->match_scopes,
                                           ctx->match_scope);
        const TypedNode *value =
            typed_program_node(ctx->program, range->coerced[i]);
        TypedMatchTemporary temporary;
        TypedNode reference = {0};

        if (scope == NULL || value == NULL) {
            return RANGE_INVALID;
        }
        temporary.type_index = value->type_index;
        temporary.next = *scope;
        range->temporaries[i] = ctx->program->match_temporaries.length;
        reference.kind = TYPED_MATCH_TEMPORARY;
        reference.type_index = temporary.type_index;
        reference.name_index = TYPED_NO_NODE;
        reference.pos = value->pos;
        reference.left_child = TYPED_NO_NODE;
        reference.right_child = TYPED_NO_NODE;
        reference.integer_value_u = range->temporaries[i];
        checker_ok(ctx->errors,
                   list_push(&ctx->program->match_temporaries, &temporary),
                   MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
        if (!checker_push_node(ctx, &reference, &range->values[i],
                               MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING)) {
            return RANGE_FAILED;
        }
        *scope = range->temporaries[i];
    }
    range->start = ctx->program->children.length;
    for (i = 0; i < node->children_count; i++) {
        checker_ok(ctx->errors,
                   typed_program_push_child(ctx->program, range->values[i]),
                   MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
    }
    return RANGE_OK;
}

static const RangeStepFn range_steps[] = {
    range_arguments, range_element, range_coerce, range_bind,
};

static bool check_range_call(CheckContext *ctx, const AstNode *node,
                             size_t ast_index)
{
    RangeArgs range = {0};
    TypedNode typed = {0};
    size_t result;
    size_t i;

    for (i = 0; i < NYX_COUNT_OF(range_steps); i++) {
        RangeStep step = range_steps[i](ctx, node, &range);

        if (step == RANGE_INVALID && !ctx->errors->reported) {
            error_report_at(ctx->errors, node->pos,
                            MESSAGE_CHECKER_RANGE_ARGUMENTS);
        }
        if (step != RANGE_OK) {
            return false;
        }
    }
    typed.kind = TYPED_RANGE_CREATE;
    typed.type_index = range.type;
    typed.pos = node->pos;
    typed.name_index = TYPED_NO_NODE;
    typed.left_child = TYPED_NO_NODE;
    typed.right_child = TYPED_NO_NODE;
    typed.children_start = range.start;
    typed.children_count = node->children_count;
    if (!checker_push_node(ctx, &typed, &result,
                           MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING)) {
        return false;
    }
    for (i = node->children_count; i > 0; i--) {
        TypedNode binding = {0};

        binding.kind = TYPED_MATCH_BIND_EXPRESSION;
        binding.type_index = range.type;
        binding.pos = node->pos;
        binding.name_index = TYPED_NO_NODE;
        binding.left_child = range.coerced[i - 1];
        binding.right_child = result;
        binding.integer_value_u = range.temporaries[i - 1];
        if (!checker_push_node(ctx, &binding, &result,
                               MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING)) {
            return false;
        }
    }
    *mapping_slot(ctx->mapping, ast_index) = result;
    return true;
}

/* now() reads the monotonic clock in whole milliseconds. */
static bool check_now_call(CheckContext *ctx, const AstNode *node,
                           size_t ast_index)
{
    TypedNode typed = {0};
    size_t int64_type;

    if (node->children_count != 0) {
        error_report_at(ctx->errors, node->pos, MESSAGE_CHECKER_NOW_ARGUMENTS);
        return false;
    }
    if (!builtin_type(ctx, NYX_TYPE_INT64, &int64_type)) return false;
    typed.kind = TYPED_CLOCK_NOW;
    typed.type_index = int64_type;
    typed.pos = node->pos;
    typed.name_index = TYPED_NO_NODE;
    typed.left_child = TYPED_NO_NODE;
    typed.right_child = TYPED_NO_NODE;
    return emit_node(ctx, &typed, ast_index, &(size_t){0});
}

/* sleep(ms) blocks the calling thread for a whole number of milliseconds. A
   negative literal is rejected here and a negative runtime value traps. */
static bool sleep_invalid(CheckContext *ctx, const AstNode *node)
{
    if (!ctx->errors->reported) {
        error_report_at(ctx->errors, node->pos, MESSAGE_CHECKER_SLEEP_ARGUMENTS);
    }
    return false;
}

/* sleep takes one positional argument. */
static bool sleep_argument(CheckContext *ctx, const AstNode *node, size_t *child)
{
    const AstNode *argument;

    if (node->children_count != 1) {
        return false;
    }
    *child = *(const size_t *)list_get(&ctx->ast->children, node->children_start);
    argument = ast_get_node(ctx->ast, *child);
    return argument != NULL && argument->kind != AST_NAMED_ARG;
}

static bool is_negative_literal(CheckContext *ctx, size_t value)
{
    NumericConstant constant;

    return numeric_constant_read(ctx->program, value, true, &constant) &&
           !constant.is_float && constant.negative && constant.magnitude != 0;
}

/* sleep(ms) blocks the calling thread for a whole number of milliseconds. A
   negative literal is rejected here and a negative runtime value traps. */
static bool check_sleep_call(CheckContext *ctx, const AstNode *node,
                             size_t ast_index)
{
    TypedNode typed = {0};
    size_t child;
    size_t value;
    size_t coerced;
    size_t int64_type;
    size_t nil_type;

    if (!sleep_argument(ctx, node, &child)) {
        return sleep_invalid(ctx, node);
    }
    if (!check_expression(ctx, child, &value) ||
        !builtin_type(ctx, NYX_TYPE_INT64, &int64_type) ||
        !builtin_type(ctx, NYX_TYPE_NIL, &nil_type)) {
        return false;
    }
    if (is_negative_literal(ctx, value)) {
        error_report_at(ctx->errors, node->pos, MESSAGE_CHECKER_SLEEP_NEGATIVE);
        return false;
    }
    if (!inject_into(ctx, value, int64_type, &coerced)) {
        return sleep_invalid(ctx, node);
    }
    if (!check_default_numeric(ctx, coerced)) {
        return false;
    }
    typed.kind = TYPED_SLEEP;
    typed.type_index = nil_type;
    typed.pos = node->pos;
    typed.name_index = TYPED_NO_NODE;
    typed.left_child = coerced;
    typed.right_child = TYPED_NO_NODE;
    return emit_node(ctx, &typed, ast_index, &(size_t){0});
}

/* Check the arguments against the parameters starting at `first_parameter`
   of the signature, coercing each, and keep them in `values`. */
static bool check_positional_arguments(CheckContext *ctx, const AstNode *node,
                                       size_t first_parameter, const char *name,
                                       List *values)
{
    size_t i;

    for (i = 0; i < node->children_count; i++) {
        size_t child = *(const size_t *)list_get(&ctx->ast->children,
                                                 node->children_start + i);
        const AstNode *argument = ast_get_node(ctx->ast, child);
        size_t parameter_type;
        size_t value;
        size_t coerced;

        if (argument == NULL || argument->kind == AST_NAMED_ARG) {
            error_report_at(ctx->errors,
                            argument == NULL ? node->pos : argument->pos,
                            MESSAGE_CHECKER_NAMED_FUNCTION_ARG);
            return false;
        }
        parameter_type = *(const size_t *)list_get(
            &ctx->program->signature_parameters, first_parameter + i);
        if (!check_expression_expected(ctx, child, parameter_type, &value)) {
            return false;
        }
        if (!inject_into(ctx, value, parameter_type, &coerced)) {
            error_report_at(ctx->errors, node->pos,
                            MESSAGE_CHECKER_FUNCTION_ARG_TYPE, i + 1, name);
            return false;
        }
        checker_ok(ctx->errors, list_push(values, &coerced),
                   MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
    }
    return true;
}

/* The call node itself. The argument run is appended only now, after nested
   calls have appended theirs, so this call's children stay contiguous. */
bool emit_call_to(CheckContext *ctx, const AstNode *node, size_t name_index,
                  size_t ast_index, size_t return_type, const List *values,
                  bool is_method)
{
    TypedNode typed = {0};
    size_t i;

    typed.children_start = ctx->program->children.length;
    for (i = 0; i < values->length; i++) {
        checker_ok(ctx->errors,
                   typed_program_push_child(
                       ctx->program, *(const size_t *)list_get(values, i)),
                   MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
    }
    typed.kind = TYPED_FUNCTION_CALL;
    typed.type_index = return_type;
    typed.pos = node->pos;
    typed.name_index = name_index;
    typed.left_child = TYPED_NO_NODE;
    typed.right_child = TYPED_NO_NODE;
    typed.children_count = values->length;
    typed.is_method_call = is_method;
    return emit_sequenced_expression(ctx, &typed, ast_index, NULL);
}

static bool emit_call(CheckContext *ctx, const AstNode *node, const char *name,
                      size_t ast_index, size_t return_type, const List *values,
                      bool is_method)
{
    return emit_call_to(ctx, node, checker_name(ctx, name), ast_index,
                        return_type, values, is_method);
}

/* The signature of a callee, or NULL after saying it is not callable. */
static const TypedFunctionSignature *callee_signature(CheckContext *ctx,
                                                      const AstNode *node,
                                                      const char *name,
                                                      size_t type_index)
{
    const TypedType *callee = typed_program_type(ctx->program, type_index);
    const TypedFunctionSignature *signature;

    if (callee == NULL || callee->kind != NYX_TYPE_FUNCTION) {
        error_report_at(ctx->errors, node->pos, MESSAGE_CHECKER_NOT_CALLABLE,
                        name);
        return NULL;
    }
    signature = typed_program_signature(ctx->program, callee->signature_index);
    if (signature == NULL) {
        error_report_at(ctx->errors, node->pos,
                        MESSAGE_CHECKER_UNSUPPORTED_AST_NODE);
    }
    return signature;
}

/* Arguments to a function value are positional. */
static bool check_plain_arguments(CheckContext *ctx, const AstNode *node)
{
    size_t i;

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
    return true;
}

bool check_function_call(CheckContext *ctx, const AstNode *node,
                         const char *name, size_t type_index, size_t ast_index)
{
    const TypedFunctionSignature *signature =
        callee_signature(ctx, node, name, type_index);
    size_t first_parameter;
    size_t return_type;
    List values;
    bool ok;

    if (signature == NULL || !check_plain_arguments(ctx, node)) {
        return false;
    }
    if (node->children_count != signature->parameter_count) {
        error_report_at(ctx->errors, node->pos,
                        MESSAGE_CHECKER_FUNCTION_ARG_COUNT, name,
                        signature->parameter_count, node->children_count);
        return false;
    }
    first_parameter = signature->first_parameter;
    return_type = signature->return_type;
    list_init(&values, sizeof(size_t));
    ok = check_positional_arguments(ctx, node, first_parameter, name, &values) &&
         emit_call(ctx, node, name, ast_index, return_type, &values, false);
    list_free(&values);
    return ok;
}


bool check_const_method_receiver(CheckContext *ctx, size_t ast_index)
{
    const AstNode *root = ast_get_node(ctx->ast, ast_index);
    const char *name;
    const VariableEntry *variable;

    while (root != NULL && root->kind == AST_MEMBER_ACCESS) {
        root = ast_get_node(ctx->ast, root->left_child);
    }
    name = root != NULL && root->kind == AST_IDENTIFIER
               ? ast_symbol_text(ctx->ast, root->name_index)
               : NULL;
    variable = name == NULL ? NULL : lookup_variable(ctx, name);
    if (variable != NULL && variable->is_const) {
        error_report_at(ctx->errors, root->pos, MESSAGE_CHECKER_CONST_ASSIGN,
                        name);
        return false;
    }
    return true;
}


/* The method receiver must be something a method can change in place, of the
   type the method was declared on. */
static bool check_method_receiver(CheckContext *ctx, const AstNode *node,
                                  const FunctionEntry *target,
                                  size_t first_parameter, List *values)
{
    const AstNode *receiver_ast = ast_get_node(ctx->ast, node->left_child);
    size_t expected_type = *(const size_t *)list_get(
        &ctx->program->signature_parameters, first_parameter);
    const TypedNode *receiver_node;
    size_t receiver;

    if (receiver_ast == NULL || !field_is_lvalue(ctx->ast, node->left_child)) {
        error_report_at(ctx->errors, node->pos, MESSAGE_CHECKER_METHOD_RECEIVER,
                        target->method_name);
        return false;
    }
    if (!check_inline_const_mutation(ctx, node->left_child) ||
        !check_const_method_receiver(ctx, node->left_child) ||
        !check_expression(ctx, node->left_child, &receiver)) {
        return false;
    }
    receiver_node = typed_program_node(ctx->program, receiver);
    if (receiver_node == NULL ||
        !same_type(ctx->program, receiver_node->type_index, expected_type)) {
        error_report_at(ctx->errors, node->pos, MESSAGE_CHECKER_METHOD_RECEIVER,
                        target->method_name);
        return false;
    }
    checker_ok(ctx->errors, list_push(values, &receiver),
               MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
    return true;
}

/* The signature of a non-generic function declared in a type, with room for
   the receiver if it is a method; NULL after saying why not. */
/* The signature of a non-generic function declared in a type, with room for
   the receiver if it is a method; NULL after saying why not. */
/* The signature of a non-generic function declared in a type, with room for
   the receiver if it is a method; NULL after saying how many arguments it
   takes. */
static const TypedFunctionSignature *associated_signature(
    CheckContext *ctx, const AstNode *node, const FunctionEntry *function,
    bool is_method)
{
    const TypedType *callee_type =
        typed_program_type(ctx->program, function->type_index);
    const TypedFunctionSignature *signature =
        typed_program_signature(ctx->program, callee_type->signature_index);
    size_t explicit_parameters = signature->parameter_count - (is_method ? 1u : 0u);

    if (node->children_count != explicit_parameters) {
        error_report_at(ctx->errors, node->pos,
                        MESSAGE_CHECKER_FUNCTION_ARG_COUNT,
                        function->method_name, explicit_parameters,
                        node->children_count);
        return NULL;
    }
    return signature;
}

bool check_associated_function_call(CheckContext *ctx, const AstNode *node,
                                    const FunctionEntry *function,
                                    size_t ast_index, bool is_method)
{
    const TypedFunctionSignature *signature =
        associated_signature(ctx, node, function, is_method);
    FunctionEntry target;
    size_t first_parameter;
    size_t return_type;
    List values;
    bool ok;

    if (signature == NULL) {
        return false;
    }
    target = *function;
    first_parameter = signature->first_parameter;
    return_type = signature->return_type;
    list_init(&values, sizeof(size_t));
    ok = (!is_method || check_method_receiver(ctx, node, &target, first_parameter,
                                              &values)) &&
         check_positional_arguments(ctx, node,
                                    first_parameter + (is_method ? 1u : 0u),
                                    target.method_name, &values) &&
         emit_call(ctx, node, target.name, ast_index, return_type, &values,
                   is_method);
    list_free(&values);
    return ok;
}

/* A constant converts at compile time and must fit. The folded node names its
   own target as the source type, which is how later stages tell it needs no
   runtime check; it also keeps the explicit type, so context never retypes it. */
static bool fold_conversion(CheckContext *ctx, const AstNode *node,
                            NyxType target_kind, size_t target_index,
                            size_t *argument, TypedNode *typed)
{
    NumericConstant source;
    NumericConstant folded;

    if (!numeric_constant_read(ctx->program, *argument, true, &source)) {
        return true;
    }
    if (!numeric_constant_convert(&source, target_kind, &folded)) {
        error_report_at(ctx->errors, node->pos, MESSAGE_CHECKER_CONVERSION_RANGE);
        return false;
    }
    if (!numeric_constant_emit(ctx, &folded, target_kind, target_index,
                               node->pos, argument)) {
        return false;
    }
    typed->auxiliary_type = target_index;
    return true;
}

bool check_numeric_conversion(CheckContext *ctx, const AstNode *node,
                              const char *name, NyxType target_kind,
                              size_t ast_index)
{
    size_t argument;
    const TypedNode *argument_node;
    const TypedType *argument_type;
    size_t target_index;
    TypedNode typed = {0};

    if (node->right_child != AST_NO_NODE || node->children_count != 1) {
        error_report_at(ctx->errors, node->pos, MESSAGE_CHECKER_CONVERSION_ARGS);
        return false;
    }
    if (!check_expression(ctx, *(const size_t *)list_get(
                                   &ctx->ast->children, node->children_start),
                          &argument)) {
        return false;
    }
    argument_node = typed_program_node(ctx->program, argument);
    argument_type = argument_node == NULL
                        ? NULL
                        : typed_program_type(ctx->program,
                                             argument_node->type_index);
    if (argument_type == NULL || !nyx_type_is_numeric(argument_type->kind)) {
        error_report_at(ctx->errors, node->pos, MESSAGE_CHECKER_CONVERSION_TYPE);
        return false;
    }
    if (!add_builtin_type(ctx, target_kind, &target_index)) {
        return false;
    }
    typed.kind = TYPED_NUMERIC_CONVERSION;
    typed.type_index = target_index;
    typed.auxiliary_type = argument_node->type_index;
    typed.pos = node->pos;
    if (!fold_conversion(ctx, node, target_kind, target_index, &argument,
                         &typed)) {
        return false;
    }
    typed.name_index = checker_name(ctx, name);
    typed.left_child = argument;
    typed.right_child = TYPED_NO_NODE;
    return emit_node(ctx, &typed, ast_index, &(size_t){0});
}


/* A call is decided by the first rule that recognises it. A rule declines
   (the next one looks), rejects (the call is wrong, and has said why) or
   accepts (the call is checked and emitted). */
typedef struct {
    CheckContext *ctx;
    const AstNode *node;
    const char *name;
    size_t ast_index;
} CallSite;

typedef enum {
    CALL_NEXT,
    CALL_REJECTED,
    CALL_ACCEPTED
} CallStep;

typedef CallStep (*CallRule)(const CallSite *call);

static CallStep call_step(bool ok)
{
    return ok ? CALL_ACCEPTED : CALL_REJECTED;
}

static CallStep call_needs_name(const CallSite *call)
{
    if (call->name != NULL) {
        return CALL_NEXT;
    }
    error_report_at(call->ctx->errors, call->node->pos,
                    MESSAGE_CHECKER_MISSING_NAME_OR_TYPE);
    return CALL_REJECTED;
}

/* Type::function(...) calls a function declared in a type, builds a case of a
   sum type, or throws an Error. */
typedef CallStep (*StaticRule)(const CallSite *call, const char *owner);

static CallStep static_error_throw(const CallSite *call, const char *owner)
{
    if (owner == NULL || strcmp(owner, "Error") != 0 ||
        strcmp(call->name, "throw") != 0) {
        return CALL_NEXT;
    }
    return call_step(check_error_throw(call->ctx, call->node, call->ast_index));
}

/* A sum type's own name is a case, unless it also has a static function of
   that name. Anything else called on a sum type is a case that does not exist. */
static CallStep static_case(const CallSite *call, const char *owner)
{
    CheckContext *ctx = call->ctx;
    const FunctionEntry *associated;

    if (owner == NULL || !is_sum_type_name(ctx, owner)) {
        return CALL_NEXT;
    }
    if (find_case_index(ctx->types, owner, call->name) != TYPED_NO_NODE) {
        return call_step(
            check_case_call(ctx, call->node, owner, call->ast_index));
    }
    associated = find_associated_function(ctx->functions, owner, call->name);
    if (associated != NULL && associated->is_static) {
        return CALL_NEXT;
    }
    error_report_at(ctx->errors, call->node->pos, MESSAGE_CHECKER_UNKNOWN_CASE,
                    owner, call->name);
    return CALL_REJECTED;
}

static CallStep static_function(const CallSite *call, const char *owner)
{
    CheckContext *ctx = call->ctx;
    const FunctionEntry *associated =
        find_associated_function(ctx->functions, owner, call->name);
    char full_name[NYX_IDENTIFIER_MAX * 2 + 4];

    if (associated == NULL || !associated->is_static) {
        snprintf(full_name, sizeof(full_name), "%s::%s",
                 owner == NULL ? MESSAGE_FRAGMENT_MISSING_NAME : owner,
                 call->name);
        error_report_at(ctx->errors, call->node->pos,
                        MESSAGE_CHECKER_UNKNOWN_CALL, full_name);
        return CALL_REJECTED;
    }
    if (associated->generic) {
        return call_step(check_generic_call(
            ctx, call->node, find_function_index(ctx, associated->name),
            call->ast_index));
    }
    return call_step(check_associated_function_call(
        ctx, call->node, associated, call->ast_index, false));
}

static const StaticRule static_rules[] = {
    static_error_throw, static_case, static_function,
};

static bool check_static_call(const CallSite *call, const char *owner)
{
    size_t i;

    for (i = 0; i < NYX_COUNT_OF(static_rules); i++) {
        CallStep step = static_rules[i](call, owner);

        if (step != CALL_NEXT) {
            return step == CALL_ACCEPTED;
        }
    }
    return false;
}

static CallStep call_static(const CallSite *call)
{
    const char *owner;

    if (!call->node->is_static) {
        return CALL_NEXT;
    }
    owner = ast_symbol_text(call->ctx->ast, call->node->second_name_index);
    if (call->node->owner_type_index != AST_NO_NODE &&
        !resolve_type_in_env(call->ctx, call->node->owner_type_index,
                             call->ctx->env, owner, &(size_t){0})) {
        return CALL_REJECTED;
    }
    return call_step(check_static_call(call, owner));
}

/* Functions the language provides. */
static const struct {
    const char *name;
    NodeCheck check;
} builtin_calls[] = {
    {"print", check_print},   {"base64", check_base64},
    {"range", check_range_call}, {"cores", check_cores_call},
    {"now", check_now_call},  {"sleep", check_sleep_call},
};

static CallStep call_builtin(const CallSite *call)
{
    size_t i;

    for (i = 0; i < NYX_COUNT_OF(builtin_calls); i++) {
        if (strcmp(call->name, builtin_calls[i].name) == 0) {
            return call_step(builtin_calls[i].check(call->ctx, call->node,
                                                    call->ast_index));
        }
    }
    return CALL_NEXT;
}

static CallStep call_numeric_conversion(const CallSite *call)
{
    NyxType target_kind;

    if (!numeric_type_from_name(call->name, &target_kind)) {
        return CALL_NEXT;
    }
    return call_step(check_numeric_conversion(call->ctx, call->node, call->name,
                                              target_kind, call->ast_index));
}

static CallStep call_error_constructor(const CallSite *call)
{
    if (strcmp(call->name, "Error") != 0) {
        return CALL_NEXT;
    }
    return call_step(
        check_error_constructor(call->ctx, call->node, call->ast_index));
}

/* Built-in types that are not structs have no constructor call, and a Store
   is made by declaration, not by a call. */
static const char *const not_constructible[] = {
    "String", "Text", "TextBuilder", "List", "Array", "Slice", "Range",
};

static CallStep call_not_constructible(const CallSite *call)
{
    size_t i;

    for (i = 0; i < NYX_COUNT_OF(not_constructible); i++) {
        if (strcmp(call->name, not_constructible[i]) == 0) {
            error_report_at(call->ctx->errors, call->node->pos,
                            MESSAGE_CHECKER_CONSTRUCTOR_NOT_STRUCT, call->name);
            return CALL_REJECTED;
        }
    }
    if (strcmp(call->name, "Store") == 0 ||
        find_store(call->ctx->stores, call->name) != NULL) {
        error_report_at(call->ctx->errors, call->node->pos,
                        MESSAGE_CHECKER_STORE_TYPE);
        return CALL_REJECTED;
    }
    return CALL_NEXT;
}

/* Calling a variable that holds a function. */
static CallStep call_variable(const CallSite *call)
{
    const VariableEntry *variable = lookup_variable(call->ctx, call->name);

    if (variable == NULL) {
        return CALL_NEXT;
    }
    return call_step(check_function_call(call->ctx, call->node, call->name,
                                         variable->type_index, call->ast_index));
}

static CallStep call_function(const CallSite *call)
{
    size_t index = find_function_index(call->ctx, call->name);
    const FunctionEntry *function;

    if (index == TYPED_NO_NODE) {
        return CALL_NEXT;
    }
    function = list_get(call->ctx->functions, index);
    return call_step(function->generic
                         ? check_generic_call(call->ctx, call->node, index,
                                              call->ast_index)
                         : check_function_call(call->ctx, call->node, call->name,
                                               function->type_index,
                                               call->ast_index));
}

static CallStep call_global_in_function(const CallSite *call)
{
    if (!call->ctx->in_function ||
        find_variable(call->ctx->globals, call->name) == NULL) {
        return CALL_NEXT;
    }
    error_report_at(call->ctx->errors, call->node->pos,
                    MESSAGE_CHECKER_GLOBAL_IN_FUNCTION, call->name);
    return CALL_REJECTED;
}

/* Name(...) of a declared type constructs a value of it. */
static CallStep call_type_constructor(const CallSite *call)
{
    CheckContext *ctx = call->ctx;
    size_t index = find_type_index(ctx->types, call->name);
    const TypeEntry *entry;
    const TypedType *constructed;

    if (index == TYPED_NO_NODE) {
        return CALL_NEXT;
    }
    entry = (const TypeEntry *)list_get(ctx->types, index);
    if (entry->kind == TYPE_ENTRY_ADT) {
        error_report_at(ctx->errors, call->node->pos,
                        MESSAGE_CHECKER_CONSTRUCTOR_NOT_STRUCT, call->name);
        return CALL_REJECTED;
    }
    if (entry->parameter_count > 0) {
        return call_step(
            check_generic_constructor(ctx, call->node, index, call->ast_index));
    }
    constructed = typed_program_type(ctx->program, entry->type_index);
    if (constructed == NULL || constructed->kind != NYX_TYPE_STRUCT) {
        error_report_at(ctx->errors, call->node->pos,
                        MESSAGE_CHECKER_CONSTRUCTOR_NOT_STRUCT, call->name);
        return CALL_REJECTED;
    }
    return call_step(check_constructor(ctx, call->node, entry, call->ast_index));
}

static CallStep call_reserved_type(const CallSite *call)
{
    if (strcmp(call->name, "Bool") != 0 && strcmp(call->name, "Nil") != 0) {
        return CALL_NEXT;
    }
    error_report_at(call->ctx->errors, call->node->pos,
                    MESSAGE_CHECKER_CONSTRUCTOR_NOT_STRUCT, call->name);
    return CALL_REJECTED;
}

static CallStep call_unknown(const CallSite *call)
{
    error_report_at(call->ctx->errors, call->node->pos,
                    MESSAGE_CHECKER_UNKNOWN_CALL, call->name);
    return CALL_REJECTED;
}

static const CallRule call_rules[] = {
    call_needs_name, call_static, call_builtin, call_numeric_conversion,
    call_error_constructor, call_not_constructible, call_variable,
    call_function, call_global_in_function, call_type_constructor,
    call_reserved_type, call_unknown,
};

bool check_call(CheckContext *ctx, const AstNode *node, size_t ast_index)
{
    CallSite call = {ctx, node, ast_symbol_text(ctx->ast, node->name_index),
                     ast_index};
    size_t i;

    for (i = 0; i < NYX_COUNT_OF(call_rules); i++) {
        CallStep step = call_rules[i](&call);

        if (step != CALL_NEXT) {
            return step == CALL_ACCEPTED;
        }
    }
    return false;
}
