#include "checker_internal.h"


/* Why `name` cannot be assigned to when no variable of that name is visible. */
static const char *missing_target_message(CheckContext *ctx, const char *name)
{
    if (ctx->in_function && find_variable(ctx->globals, name) != NULL) {
        return MESSAGE_CHECKER_GLOBAL_IN_FUNCTION;
    }
    if (name_is_resource(ctx, name) || find_function(ctx, name) != NULL ||
        find_type(ctx->types, name) != NULL || is_builtin_type_name(name)) {
        return MESSAGE_CHECKER_ASSIGN_TARGET;
    }
    return MESSAGE_CHECKER_UNKNOWN_NAME;
}

/* The variable an assignment writes, or NULL after saying why it cannot. */
static const VariableEntry *assignment_target(CheckContext *ctx,
                                              const AstNode *node,
                                              const char *name)
{
    const VariableEntry *variable = lookup_variable(ctx, name);

    if (variable == NULL) {
        error_report_at(ctx->errors, node->pos,
                        missing_target_message(ctx, name), name);
        return NULL;
    }
    if (variable->is_self_pointer) {
        error_report_at(ctx->errors, node->pos, MESSAGE_CHECKER_SELF_ASSIGN);
        return NULL;
    }
    if (variable->is_const) {
        error_report_at(ctx->errors, node->pos, MESSAGE_CHECKER_CONST_ASSIGN,
                        name);
        return NULL;
    }
    return variable;
}

bool check_assignment(CheckContext *ctx, const AstNode *node, size_t ast_index,
                      size_t *out_typed)
{
    const char *name = ast_symbol_text(ctx->ast, node->name_index);
    const VariableEntry *variable;
    size_t target_type;
    size_t value;
    size_t coerced;
    TypedNode typed = {0};

    if (name == NULL) {
        error_report_at(ctx->errors, node->pos,
                        MESSAGE_CHECKER_MISSING_NAME_OR_TYPE);
        return false;
    }
    variable = assignment_target(ctx, node, name);
    if (variable == NULL) {
        return false;
    }
    target_type = variable->type_index;
    if (!check_expression_expected(ctx, node->left_child, target_type, &value)) {
        return false;
    }
    if (!inject_into(ctx, value, target_type, &coerced)) {
        error_report_at(ctx->errors, node->pos, MESSAGE_CHECKER_ASSIGN_TYPE,
                        name);
        return false;
    }
    typed.kind = TYPED_ASSIGNMENT;
    typed.type_index = target_type;
    typed.pos = node->pos;
    typed.name_index = checker_name(ctx, name);
    typed.left_child = coerced;
    typed.right_child = TYPED_NO_NODE;
    typed.auxiliary_type = TYPED_NO_NODE;
    typed.store_id = TYPED_NO_NODE;
    /* The right-hand side has been checked under the old narrowing. Any later
       use of the target must see its declared union type again. */
    invalidate_narrowing(ctx, name);
    return emit_node(ctx, &typed, ast_index, out_typed);
}


/* The call of an expression statement, checked as one so that a call
   returning nothing is allowed here and nowhere else. */
static bool check_statement_call(CheckContext *ctx, size_t call,
                                 size_t *expression)
{
    bool previous = ctx->in_expression_statement;
    size_t previous_call = ctx->statement_call_ast;
    bool ok;

    ctx->in_expression_statement = true;
    ctx->statement_call_ast = call;
    ok = check_expression(ctx, call, expression);
    ctx->in_expression_statement = previous;
    ctx->statement_call_ast = previous_call;
    return ok;
}

bool check_expression_statement(CheckContext *ctx, size_t ast_index,
                                size_t *out_typed)
{
    const AstNode *node = ast_get_node(ctx->ast, ast_index);
    const AstNode *value = ast_get_node(ctx->ast, node->left_child);
    const TypedNode *expression_node;
    size_t expression;
    TypedNode typed;

    if (value == NULL ||
        (value->kind != AST_CALL && value->kind != AST_MEMBER_CALL)) {
        error_report_at(ctx->errors, value == NULL ? node->pos : value->pos,
                        MESSAGE_CHECKER_EXPRESSION_STATEMENT);
        return false;
    }
    if (!check_statement_call(ctx, node->left_child, &expression)) {
        return false;
    }
    expression_node = typed_program_node(ctx->program, expression);
    if (type_contains_error(ctx->program, expression_node->type_index)) {
        error_report_at(ctx->errors, node->pos, MESSAGE_CHECKER_DISCARDED_ERROR);
        return false;
    }
    typed = new_node(TYPED_EXPRESSION_STATEMENT, expression_node->type_index,
                     node->pos, expression, TYPED_NO_NODE);
    return emit_node(ctx, &typed, ast_index, out_typed);
}


bool check_for_binding(CheckContext *ctx, const char *name, size_t type_index,
                       SourcePos pos)
{
    VariableEntry entry = {0};

    if (name == NULL || name_is_taken(ctx, name, false)) {
        error_report_at(ctx->errors, pos, MESSAGE_CHECKER_DUPLICATE_NAME,
                        name == NULL ? "" : name);
        return false;
    }
    if (!check_reserved_name(ctx, pos, name) ||
        !check_type_value_size(ctx, type_index, pos)) {
        return false;
    }
    memcpy(entry.name, name, strlen(name) + 1);
    entry.type_index = type_index;
    checker_ok(ctx->errors, list_push(ctx->locals, &entry),
               MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
    return true;
}


/* `list.push(...)` or `list.pop()` on the list a loop is iterating. */
static bool is_list_mutation(const Ast *ast, const AstNode *node,
                             size_t symbol_index)
{
    const AstNode *receiver;
    const char *method;

    if (node->kind != AST_MEMBER_CALL) {
        return false;
    }
    receiver = ast_get_node(ast, node->left_child);
    method = ast_symbol_text(ast, node->name_index);
    return receiver != NULL && receiver->kind == AST_IDENTIFIER &&
           receiver->name_index == symbol_index && method != NULL &&
           (strcmp(method, "push") == 0 || strcmp(method, "pop") == 0);
}

static bool mutates_iterated_list(const Ast *ast, size_t ast_index,
                                  size_t symbol_index)
{
    const AstNode *node = ast_get_node(ast, ast_index);
    size_t i;

    if (node == NULL) {
        return false;
    }
    if (is_list_mutation(ast, node, symbol_index) ||
        mutates_iterated_list(ast, node->left_child, symbol_index) ||
        mutates_iterated_list(ast, node->right_child, symbol_index)) {
        return true;
    }
    for (i = 0; i < node->children_count; i++) {
        size_t child = *(const size_t *)list_get(&ast->children,
                                                 node->children_start + i);

        if (mutates_iterated_list(ast, child, symbol_index)) {
            return true;
        }
    }
    return false;
}


/* What a `for` can iterate and how: a record or table gives key and value, a
   list, array, slice or range gives elements, a string gives bytes. */
typedef enum {
    ITER_MAP,
    ITER_ELEMENTS,
    ITER_TEXT
} IterForm;

static const struct {
    NyxType kind;
    IterForm form;
    bool is_list;
} iterables[] = {
    {NYX_TYPE_RECORD, ITER_MAP, false},
    {NYX_TYPE_TABLE, ITER_MAP, false},
    {NYX_TYPE_LIST, ITER_ELEMENTS, true},
    {NYX_TYPE_FIXED_LIST, ITER_ELEMENTS, true},
    {NYX_TYPE_SLICE, ITER_ELEMENTS, false},
    {NYX_TYPE_RANGE, ITER_ELEMENTS, false},
    {NYX_TYPE_STRING, ITER_TEXT, false},
    {NYX_TYPE_DYNAMIC_STRING, ITER_TEXT, false},
};

typedef struct {
    bool is_map;
    bool is_list;
    size_t first_type;  /* key, element or byte */
    size_t second_type; /* value, for a map */
} Iteration;

static bool classify_iterable(CheckContext *ctx, const AstNode *node,
                              const TypedNode *iterable_node, Iteration *shape)
{
    const TypedType *type =
        iterable_node == NULL
            ? NULL
            : typed_program_type(ctx->program, iterable_node->type_index);
    size_t i;

    for (i = 0; type != NULL && i < NYX_COUNT_OF(iterables); i++) {
        size_t children;

        if (iterables[i].kind != type->kind) {
            continue;
        }
        shape->is_map = iterables[i].form == ITER_MAP;
        shape->is_list = iterables[i].is_list;
        if (iterables[i].form == ITER_TEXT) {
            return builtin_type(ctx, NYX_TYPE_BYTE, &shape->first_type);
        }
        children = shape->is_map ? 2 : 1;
        if (type->child_count != children) {
            break;
        }
        shape->first_type = *(const size_t *)list_get(
            &ctx->program->type_children, type->first_child);
        shape->second_type = shape->is_map
                                 ? *(const size_t *)list_get(
                                       &ctx->program->type_children,
                                       type->first_child + 1)
                                 : TYPED_NO_NODE;
        return true;
    }
    error_report_at(ctx->errors, node->pos, MESSAGE_CHECKER_FOR_ITERABLE);
    return false;
}

/* A loop over a list may not push to or pop from it. */
static bool loop_mutates_list(CheckContext *ctx, const AstNode *node,
                              const Iteration *shape)
{
    const AstNode *iterable = ast_get_node(ctx->ast, node->left_child);

    if (shape->is_list && iterable != NULL && iterable->kind == AST_IDENTIFIER &&
        mutates_iterated_list(ctx->ast, node->right_child, iterable->name_index)) {
        error_report_at(ctx->errors, node->pos, MESSAGE_CHECKER_FOR_MUTATION);
        return true;
    }
    return false;
}

/* A name a loop binds, with the type it gets and the symbol it is stored as. */
typedef struct {
    const char *name;
    size_t type;
    size_t symbol;
} LoopName;

static bool bind_loop_names(CheckContext *ctx, const AstNode *node,
                            LoopName *names, size_t count, size_t scope_start)
{
    size_t i;

    for (i = 0; i < count; i++) {
        if (!check_for_binding(ctx, names[i].name, names[i].type, node->pos)) {
            ctx->locals->length = scope_start;
            return false;
        }
    }
    for (i = 0; i < count; i++) {
        names[i].symbol = checker_name(ctx, names[i].name);
    }
    if (ctx->errors->fatal) {
        ctx->locals->length = scope_start;
    }
    return !ctx->errors->fatal;
}

static bool check_for_body(CheckContext *ctx, const AstNode *node,
                           size_t return_type, size_t scope_start, size_t *body)
{
    bool ok;

    ctx->loop_depth++;
    ok = check_block(ctx, node->right_child, return_type, body);
    ctx->loop_depth--;
    ctx->locals->length = scope_start;
    return ok;
}

/* What a `for` loops over, and the types its names will have. */
typedef struct {
    size_t iterable;
    Iteration shape;
    size_t index_type;
    size_t nil_type;
} ForHead;

static bool check_for_head(CheckContext *ctx, const AstNode *node,
                           const char *first, ForHead *head)
{
    return first != NULL &&
           check_expression(ctx, node->left_child, &head->iterable) &&
           classify_iterable(ctx, node,
                             typed_program_node(ctx->program, head->iterable),
                             &head->shape) &&
           builtin_type(ctx, NYX_TYPE_INT32, &head->index_type) &&
           builtin_type(ctx, NYX_TYPE_NIL, &head->nil_type) &&
           !loop_mutates_list(ctx, node, &head->shape);
}

/* Over a map the names are key and value; over anything else a single name is
   the element and two names are the index then the element. */
static void fill_loop_names(LoopName *names, const char *first,
                            const char *second, bool indexed, const ForHead *head)
{
    names[0] = (LoopName){first,
                          indexed ? head->index_type : head->shape.first_type,
                          TYPED_NO_NODE};
    names[1] = (LoopName){second,
                          indexed ? head->shape.first_type
                                  : head->shape.second_type,
                          TYPED_NO_NODE};
}

/* The loop's name and, if there are two, its second. With an index and an
   element the element is the loop's own name. */
static void set_loop_symbols(TypedNode *typed, const LoopName *names,
                             size_t count, bool indexed)
{
    typed->name_index = names[indexed ? 1 : 0].symbol;
    typed->second_name_index =
        count == 2 ? names[indexed ? 0 : 1].symbol : TYPED_NO_NODE;
}

bool check_for(CheckContext *ctx, const AstNode *node, size_t ast_index,
               size_t return_type, size_t *out_typed)
{
    const char *first = ast_symbol_text(ctx->ast, node->name_index);
    const char *second = node->second_name_index == AST_NO_NODE
                             ? NULL
                             : ast_symbol_text(ctx->ast, node->second_name_index);
    size_t scope_start = ctx->locals->length;
    size_t count = second == NULL ? 1 : 2;
    ForHead head = {0};
    LoopName names[2];
    bool indexed;
    size_t body;
    TypedNode typed;

    if (!check_for_head(ctx, node, first, &head)) {
        return false;
    }
    indexed = !head.shape.is_map && second != NULL;
    fill_loop_names(names, first, second, indexed, &head);
    if (!bind_loop_names(ctx, node, names, count, scope_start) ||
        !check_for_body(ctx, node, return_type, scope_start, &body)) {
        return false;
    }
    typed = new_node(TYPED_FOR, head.nil_type, node->pos, head.iterable, body);
    set_loop_symbols(&typed, names, count, indexed);
    typed.auxiliary_type = head.shape.first_type;
    typed.store_id = TYPED_NO_NODE;
    return emit_node(ctx, &typed, ast_index, out_typed);
}


/* The type a local declaration says it has, if it says: the name must be free
   and there must be a value to start from. */
static bool check_local_header(CheckContext *ctx, const AstNode *node,
                               const char *name, size_t *declared_type)
{
    const TypedType *annotated;

    if (name == NULL) {
        error_report_at(ctx->errors, node->pos,
                        MESSAGE_CHECKER_MISSING_NAME_OR_TYPE);
        return false;
    }
    if (!check_reserved_name(ctx, node->pos, name)) {
        return false;
    }
    if (name_is_taken(ctx, name, false)) {
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
        !resolve_type_node(ctx, node->right_child, name, declared_type)) {
        return false;
    }
    annotated = typed_program_type(ctx->program, *declared_type);
    if (annotated != NULL && annotated->kind == NYX_TYPE_STORE) {
        error_report_at(ctx->errors, node->pos, MESSAGE_CHECKER_LOCAL_STORE);
        return false;
    }
    return true;
}

/* Initialisers that say nothing about their own type, so a declaration
   without an annotation cannot take its type from them. */
static const struct {
    TypedNodeKind node_kind; /* TYPED_NODE_KIND_COUNT for any */
    bool untyped;            /* only while the node has no type yet */
    NyxType type_kind;       /* NYX_TYPE_UNKNOWN for any */
    const char *message;
} uninferable[] = {
    {TYPED_LIST_LITERAL, true, NYX_TYPE_UNKNOWN, MESSAGE_CHECKER_LIST_INFER},
    {TYPED_RECORD_LITERAL, true, NYX_TYPE_UNKNOWN,
     MESSAGE_CHECKER_RECORD_LITERAL_CONTEXT},
    {TYPED_NODE_KIND_COUNT, false, NYX_TYPE_STRING_LITERAL,
     MESSAGE_CHECKER_STRING_LITERAL_CONTEXT},
    {TYPED_NODE_KIND_COUNT, false, NYX_TYPE_STORE, NULL},
};

/* `store_message` is what a Store initialiser is told, since a local and a
   global say it differently. */
static bool uninferable_matches(size_t row, const TypedNode *initialiser,
                                const TypedType *type)
{
    bool kind_ok = uninferable[row].node_kind == TYPED_NODE_KIND_COUNT ||
                   initialiser->kind == uninferable[row].node_kind;
    bool typed_ok = !uninferable[row].untyped ||
                    initialiser->type_index == TYPED_NO_NODE;
    bool type_ok = uninferable[row].type_kind == NYX_TYPE_UNKNOWN ||
                   (type != NULL && type->kind == uninferable[row].type_kind);

    return kind_ok && typed_ok && type_ok;
}

/* `store_message` is what a Store initialiser is told, since a local and a
   global say it differently. */
static bool check_inferable(CheckContext *ctx, const AstNode *node,
                            const char *name, const TypedNode *initialiser,
                            const char *store_message)
{
    const TypedType *type =
        typed_program_type(ctx->program, initialiser->type_index);
    size_t i;

    for (i = 0; i < NYX_COUNT_OF(uninferable); i++) {
        if (uninferable_matches(i, initialiser, type)) {
            error_report_at(ctx->errors, node->pos,
                            uninferable[i].message == NULL
                                ? store_message
                                : uninferable[i].message,
                            name);
            return false;
        }
    }
    return true;
}

/* A declared type takes the initialiser coerced to it; otherwise the type is
   the initialiser's own. */
static bool check_local_type(CheckContext *ctx, const AstNode *node,
                             const char *name, size_t declared_type,
                             size_t *initialiser, size_t *variable_type,
                             const char *store_message)
{
    const TypedNode *initialiser_node;
    size_t coerced;
    char declared_text[128];

    if (declared_type == TYPED_NO_NODE) {
        if (!check_default_numeric(ctx, *initialiser)) {
            return false;
        }
        initialiser_node = typed_program_node(ctx->program, *initialiser);
        if (!check_inferable(ctx, node, name, initialiser_node, store_message)) {
            return false;
        }
        *variable_type = initialiser_node->type_index;
    } else if (!inject_into(ctx, *initialiser, declared_type, &coerced)) {
        if (!ctx->errors->reported) {
            type_ast_text(ctx->ast, node->right_child, declared_text,
                          sizeof(declared_text));
            error_report_at(ctx->errors, node->pos,
                            MESSAGE_CHECKER_INITIALISER_TYPE_MISMATCH, name,
                            declared_text);
        }
        return false;
    } else {
        *initialiser = coerced;
        *variable_type = declared_type;
    }
    return check_type_value_size(ctx, *variable_type, node->pos);
}

/* Nil is no type for a variable, and a union cannot start as a bare nil. */
static bool check_local_initial_value(CheckContext *ctx, const AstNode *node,
                                      size_t declared_type,
                                      const TypedNode *initialiser)
{
    const TypedType *type = typed_program_type(
        ctx->program, declared_type == TYPED_NO_NODE ? initialiser->type_index
                                                     : declared_type);
    const AstNode *initial_ast = ast_get_node(ctx->ast, node->left_child);

    if (type != NULL && type->kind == NYX_TYPE_NIL) {
        error_report_at(ctx->errors, node->pos, MESSAGE_CHECKER_NIL_TYPE);
        return false;
    }
    if (type != NULL && type->kind == NYX_TYPE_UNION && initial_ast != NULL &&
        initial_ast->kind == AST_NIL_LITERAL) {
        error_report_at(ctx->errors, initial_ast->pos,
                        MESSAGE_CHECKER_NIL_INITIALISER);
        return false;
    }
    return true;
}

/* The checked initialiser of a local, coerced to its declared type, and the
   type the variable ends up with. */
bool check_initialiser(CheckContext *ctx, const AstNode *node, const char *name,
                       size_t declared_type, size_t *initialiser,
                       size_t *variable_type, const char *store_message)
{
    const TypedNode *initialiser_node;

    if (!check_expression_expected(ctx, node->left_child, declared_type,
                                   initialiser)) {
        return false;
    }
    initialiser_node = typed_program_node(ctx->program, *initialiser);
    if (initialiser_node == NULL) {
        error_report_at(ctx->errors, node->pos, MESSAGE_CHECKER_MISSING_AST_NODE,
                        node->left_child);
        return false;
    }
    return check_local_initial_value(ctx, node, declared_type,
                                     initialiser_node) &&
           check_local_type(ctx, node, name, declared_type, initialiser,
                            variable_type, store_message);
}

/* A const Int32 local with a constant initialiser can be used where a
   constant is required. */
bool record_constant_value(CheckContext *ctx, const AstNode *node,
                           VariableEntry *entry)
{
    const TypedType *type = typed_program_type(ctx->program, entry->type_index);
    bool is_constant = false;
    int value;

    if (!node->is_const || type == NULL || type->kind != NYX_TYPE_INT32) {
        return true;
    }
    if (!const_int_eval(ctx, node->left_child, &value, &is_constant)) {
        return false;
    }
    entry->has_constant = is_constant;
    entry->constant_value = is_constant ? value : entry->constant_value;
    return true;
}

bool check_local_declaration(CheckContext *ctx, const AstNode *node,
                             size_t ast_index, size_t *out_typed)
{
    const char *name = ast_symbol_text(ctx->ast, node->name_index);
    size_t declared_type = TYPED_NO_NODE;
    size_t initialiser;
    size_t variable_type;
    TypedNode typed = {0};
    VariableEntry entry = {0};

    if (!check_local_header(ctx, node, name, &declared_type) ||
        !check_initialiser(ctx, node, name, declared_type, &initialiser,
                           &variable_type, MESSAGE_CHECKER_LOCAL_STORE)) {
        return false;
    }
    typed.kind = TYPED_LOCAL_DECLARATION;
    typed.type_index = variable_type;
    typed.pos = node->pos;
    typed.name_index = checker_name(ctx, name);
    typed.left_child = initialiser;
    typed.right_child = TYPED_NO_NODE;
    if (!emit_node(ctx, &typed, ast_index, out_typed)) {
        return false;
    }
    memcpy(entry.name, name, strlen(name) + 1);
    entry.type_index = variable_type;
    entry.is_const = node->is_const;
    if (!record_constant_value(ctx, node, &entry)) {
        return false;
    }
    checker_ok(ctx->errors, list_push(ctx->locals, &entry),
               MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
    return true;
}


/* A statement that always leaves the block makes what follows it dead. */
static bool check_reachable(CheckContext *ctx, const AstNode *node, size_t i,
                            size_t statement)
{
    const AstNode *next;

    if (i + 1 >= node->children_count ||
        !typed_statement_terminates(ctx->program, statement)) {
        return true;
    }
    next = ast_get_node(ctx->ast, *(const size_t *)list_get(
                                      &ctx->ast->children,
                                      node->children_start + i + 1));
    error_report_at(ctx->errors, next == NULL ? node->pos : next->pos,
                    MESSAGE_CHECKER_UNREACHABLE_CODE);
    return false;
}

/* After `if x is T then return end`, the code that follows knows x is not T. */
static bool narrow_after_early_exit(CheckContext *ctx, size_t child,
                                    size_t statement)
{
    const AstNode *original = ast_get_node(ctx->ast, child);
    const TypedNode *checked = typed_program_node(ctx->program, statement);
    size_t then_block;

    if (original == NULL || original->kind != AST_IF ||
        original->children_count != 1 || checked == NULL ||
        checked->kind != TYPED_IF || checked->children_count != 1) {
        return true;
    }
    then_block = *(const size_t *)list_get(&ctx->program->children,
                                           checked->children_start);
    return !typed_statement_terminates(ctx->program, then_block) ||
           push_else_narrowing(ctx, original->left_child);
}

/* Returns false when the rest of the block cannot be checked. */
static bool check_block_next_child(size_t i, const AstNode *node,
                                   CheckContext *ctx, size_t statement,
                                   size_t child, List *statements)
{
    if (!check_reachable(ctx, node, i, statement) ||
        !narrow_after_early_exit(ctx, child, statement)) {
        return false;
    }
    return checker_ok(ctx->errors, list_push(statements, &statement),
                      MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
}

/* Check each statement. A bad statement is recorded and the rest are still
   checked, so one pass reports every bad statement; running out of memory or
   room ends the check. Returns false when the block cannot go on. */
static bool check_block_statements(CheckContext *ctx, const AstNode *node,
                                   size_t return_type, List *statements,
                                   bool *had_errors)
{
    size_t i;

    for (i = 0; i < node->children_count; i++) {
        size_t child = *(const size_t *)list_get(&ctx->ast->children,
                                                 node->children_start + i);
        Checkpoint checkpoint = checker_checkpoint(ctx);
        size_t statement;

        ctx->errors->reported = false;
        if (check_statement(ctx, child, return_type, &statement)) {
            if (!check_block_next_child(i, node, ctx, statement, child,
                                        statements)) {
                return false;
            }
            continue;
        }
        if (!ctx->errors->reported) {
            error_unknown_if_none(ctx->errors);
        }
        if (checker_cannot_recover(ctx->errors)) {
            return false;
        }
        checker_restore(ctx, checkpoint);
        *had_errors = true;
    }
    return true;
}

static bool emit_block(CheckContext *ctx, const AstNode *node, size_t ast_index,
                       const List *statements, size_t *out_typed)
{
    TypedNode typed = {0};
    size_t i;

    typed.kind = TYPED_BLOCK;
    typed.type_index = TYPED_NO_NODE;
    typed.name_index = TYPED_NO_NODE;
    typed.pos = node->pos;
    typed.left_child = TYPED_NO_NODE;
    typed.right_child = TYPED_NO_NODE;
    typed.children_start = ctx->program->children.length;
    typed.children_count = statements->length;
    for (i = 0; i < statements->length; i++) {
        checker_ok(ctx->errors,
                   typed_program_push_child(
                       ctx->program, *(const size_t *)list_get(statements, i)),
                   MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
    }
    return emit_node(ctx, &typed, ast_index, out_typed);
}

bool check_block(CheckContext *ctx, size_t ast_index, size_t return_type,
                 size_t *out_typed)
{
    const AstNode *node = ast_get_node(ctx->ast, ast_index);
    size_t scope_start = ctx->locals->length;
    size_t narrowing_start = ctx->narrowings->length;
    bool had_errors = false;
    List statements;
    bool ok;

    if (node == NULL || node->kind != AST_BLOCK) {
        error_report_at(ctx->errors,
                        node == NULL ? (SourcePos){0, 1, 1} : node->pos,
                        MESSAGE_CHECKER_UNSUPPORTED_AST_NODE);
        return false;
    }
    list_init(&statements, sizeof(size_t));
    ok = check_block_statements(ctx, node, return_type, &statements,
                                &had_errors);
    if (ok && had_errors) {
        ctx->errors->reported = true;
    }
    ok = ok && !had_errors &&
         emit_block(ctx, node, ast_index, &statements, out_typed);
    ctx->narrowings->length = narrowing_start;
    ctx->locals->length = scope_start;
    list_free(&statements);
    return ok;
}


static bool ast_contains_call_to(const Ast *ast, size_t ast_index,
                                 size_t symbol_index)
{
    const AstNode *node = ast_get_node(ast, ast_index);
    size_t i;

    if (node == NULL) {
        return false;
    }
    if ((node->kind == AST_CALL && node->name_index == symbol_index) ||
        ast_contains_call_to(ast, node->left_child, symbol_index) ||
        ast_contains_call_to(ast, node->right_child, symbol_index)) {
        return true;
    }
    for (i = 0; i < node->children_count; i++) {
        size_t child = *(const size_t *)list_get(&ast->children,
                                                 node->children_start + i);

        if (ast_contains_call_to(ast, child, symbol_index)) {
            return true;
        }
    }
    return false;
}


static void ast_scan_returns(const Ast *ast, size_t ast_index,
                             size_t symbol_index, bool *has_return,
                             bool *has_safe_return)
{
    const AstNode *node = ast_get_node(ast, ast_index);
    size_t i;

    if (node == NULL) {
        return;
    }
    if (node->kind == AST_RETURN) {
        *has_return = true;
        if (!ast_contains_call_to(ast, node->left_child, symbol_index)) {
            *has_safe_return = true;
        }
    }
    if (node->left_child != AST_NO_NODE) {
        ast_scan_returns(ast, node->left_child, symbol_index, has_return,
                         has_safe_return);
    }
    if (node->right_child != AST_NO_NODE) {
        ast_scan_returns(ast, node->right_child, symbol_index, has_return,
                         has_safe_return);
    }
    for (i = 0; i < node->children_count; i++) {
        size_t child = *(const size_t *)list_get(
            &ast->children, node->children_start + i);

        ast_scan_returns(ast, child, symbol_index, has_return,
                         has_safe_return);
    }
}


/* ---- generic function bodies ---- */

/* The names a generic body binds and the names it uses. */
typedef struct {
    List pending; /* syntax nodes still to visit */
    List names;   /* symbols bound by parameters and local declarations */
    List uses;    /* identifier and call nodes */
} GenericScan;

static void remember(CheckContext *ctx, List *list, const void *item,
                     bool wanted)
{
    if (wanted) {
        checker_ok(ctx->errors, list_push(list, item),
                   MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
    }
}

static void scan_node(CheckContext *ctx, GenericScan *scan, size_t index,
                      const AstNode *node)
{
    bool binds = node->kind == AST_DECLARATION || node->kind == AST_FOR ||
                 node->kind == AST_SCOPE;
    bool second = node->kind == AST_FOR &&
                  node->second_name_index != AST_NO_NODE;
    bool uses = node->kind == AST_IDENTIFIER ||
                (node->kind == AST_CALL && !node->is_static);
    size_t i;

    remember(ctx, &scan->names, &node->name_index, binds);
    remember(ctx, &scan->names, &node->second_name_index, second);
    remember(ctx, &scan->uses, &index, uses);
    remember(ctx, &scan->pending, &node->left_child,
             node->left_child != AST_NO_NODE);
    remember(ctx, &scan->pending, &node->right_child,
             node->right_child != AST_NO_NODE);
    for (i = 0; i < node->children_count; i++) {
        remember(ctx, &scan->pending,
                 list_get(&ctx->ast->children, node->children_start + i), true);
    }
}

/* Collect every name the body binds and every name it uses. */
static bool scan_generic_body(CheckContext *ctx, const AstNode *function,
                              GenericScan *scan)
{
    size_t i;

    for (i = 0; i < function->children_count; i++) {
        size_t index = *(const size_t *)list_get(
            &ctx->ast->children, function->children_start + i);
        const AstNode *parameter = ast_get_node(ctx->ast, index);

        if (parameter == NULL) {
            error_report(ctx->errors, MESSAGE_CHECKER_MISSING_AST_NODE, index);
            return false;
        }
        remember(ctx, &scan->names, &parameter->name_index, true);
    }
    remember(ctx, &scan->pending, &function->left_child, true);
    while (scan->pending.length != 0 && !ctx->errors->fatal) {
        size_t index = *(const size_t *)list_get(&scan->pending,
                                                 scan->pending.length - 1);
        const AstNode *node = ast_get_node(ctx->ast, index);

        scan->pending.length--;
        if (node == NULL) {
            return false;
        }
        scan_node(ctx, scan, index, node);
    }
    return !ctx->errors->fatal;
}

static bool list_has_symbol(const List *list, size_t symbol)
{
    size_t i;

    for (i = 0; i < list->length; i++) {
        if (*(const size_t *)list_get(list, i) == symbol) {
            return true;
        }
    }
    return false;
}

/* A name that is not a local may still be a function declared later: a
   template can be instantiated after a later function is declared, so its
   availability at that call is left to the checker. */
static bool is_declared_function(const CheckContext *ctx, const AstNode *use)
{
    size_t j;

    for (j = 0; j < ctx->ast->roots.length; j++) {
        const AstNode *candidate = ast_get_node(
            ctx->ast, *(const size_t *)list_get(&ctx->ast->roots, j));

        if (candidate != NULL && candidate->kind == AST_FUNCTION_DECLARATION &&
            !candidate->is_method && !candidate->is_static &&
            candidate->name_index == use->name_index) {
            return true;
        }
    }
    return false;
}

static bool names_a_known_thing(CheckContext *ctx, const char *name)
{
    return name != NULL &&
           (find_function(ctx, name) != NULL ||
            find_type(ctx->types, name) != NULL || name_is_resource(ctx, name) ||
            is_builtin_type_name(name) || is_builtin_function_name(name));
}

static void report_unknown_use(CheckContext *ctx, const AstNode *use,
                               const char *name)
{
    const char *shown = name == NULL ? "" : name;

    if (name != NULL && find_variable(ctx->globals, name) != NULL) {
        error_report_at(ctx->errors, use->pos,
                        MESSAGE_CHECKER_GLOBAL_IN_FUNCTION, name);
    } else {
        error_report_at(ctx->errors, use->pos,
                        use->kind == AST_CALL ? MESSAGE_CHECKER_UNKNOWN_CALL
                                              : MESSAGE_CHECKER_UNKNOWN_NAME,
                        shown);
    }
}

/* A generic body is type-checked on instantiation, but a name absent from
   every possible local binding is already known to be invalid. */
static bool check_generic_names(CheckContext *ctx, const AstNode *function)
{
    GenericScan scan;
    bool ok;
    size_t i;

    list_init(&scan.pending, sizeof(size_t));
    list_init(&scan.names, sizeof(size_t));
    list_init(&scan.uses, sizeof(size_t));
    ok = scan_generic_body(ctx, function, &scan);
    for (i = 0; ok && i < scan.uses.length; i++) {
        const AstNode *use = ast_get_node(
            ctx->ast, *(const size_t *)list_get(&scan.uses, i));
        const char *name = ast_symbol_text(ctx->ast, use->name_index);

        ok = list_has_symbol(&scan.names, use->name_index) ||
             names_a_known_thing(ctx, name) || is_declared_function(ctx, use);
        if (!ok) {
            report_unknown_use(ctx, use, name);
        }
    }
    list_free(&scan.uses);
    list_free(&scan.names);
    list_free(&scan.pending);
    return ok;
}

/* ---- function declarations ---- */

/* What a declaration says about the function before its body is looked at. */
typedef struct {
    const char *name;
    const char *owner_name;   /* NULL for a plain function */
    const char *backend_name; /* the C name: the name, or a generated one */
    char generated_name[NYX_IDENTIFIER_MAX];
    bool associated;          /* a method or a static function of a type */
    TypeEntry owner;
} FunctionHead;

/* A method of a generic type repeats the type's parameters, in order. */
/* A method's repeated owner parameter is a bare name equal to the owner's. */
static bool owner_parameter_matches(const CheckContext *ctx, size_t child,
                                    const char *expected)
{
    const AstNode *parameter = ast_get_node(ctx->ast, child);
    const char *name = parameter == NULL
                           ? NULL
                           : ast_symbol_text(ctx->ast, parameter->name_index);

    return parameter != NULL && parameter->kind == AST_TYPE_NAME &&
           name != NULL && strcmp(name, expected) == 0;
}

static bool owner_parameters_match(CheckContext *ctx, const AstNode *node,
                                   const TypeEntry *owner,
                                   const char *owner_name)
{
    const AstNode *owner_type = ast_get_node(ctx->ast, node->owner_type_index);
    size_t count = owner_type != NULL && owner_type->kind == AST_TYPE_APPLICATION
                       ? owner_type->children_count
                       : 0;
    size_t i;
    bool match = count == owner->parameter_count;

    for (i = 0; match && i < count; i++) {
        match = owner_parameter_matches(
            ctx,
            *(const size_t *)list_get(&ctx->ast->children,
                                      owner_type->children_start + i),
            owner->parameters[i]);
    }
    if (!match) {
        error_report_at(ctx->errors, node->pos,
                        MESSAGE_CHECKER_METHOD_OWNER_PARAMETERS, owner_name);
    }
    return match;
}

static bool check_method_owner(CheckContext *ctx, const AstNode *node,
                               FunctionHead *head)
{
    const TypeEntry *owner = head->owner_name == NULL
                                 ? NULL
                                 : find_type(ctx->types, head->owner_name);

    if (owner == NULL || owner->kind != TYPE_ENTRY_STRUCT) {
        error_report_at(ctx->errors, node->pos, MESSAGE_CHECKER_METHOD_OWNER,
                        head->owner_name == NULL ? MESSAGE_FRAGMENT_MISSING_NAME
                                                 : head->owner_name);
        return false;
    }
    head->owner = *owner;
    if (!owner_parameters_match(ctx, node, &head->owner, head->owner_name)) {
        return false;
    }
    if (find_associated_function(ctx->functions, head->owner_name,
                                 head->name) != NULL) {
        error_report_at(ctx->errors, node->pos, MESSAGE_CHECKER_DUPLICATE_METHOD,
                        head->owner_name, head->name);
        return false;
    }
    return true;
}

/* Methods get a generated C name, since two types may share a method name. */
static bool name_method(CheckContext *ctx, FunctionHead *head)
{
    int written = snprintf(head->generated_name, sizeof(head->generated_name),
                           "nyx_method_%zu", ctx->generated_names++);

    if (written < 0 || strlen(head->generated_name) >= sizeof(head->generated_name)) {
        error_report_fatal(ctx->errors,
                           MESSAGE_CHECKER_OUT_OF_MEMORY_COPYING_NAME);
        return false;
    }
    head->backend_name = head->generated_name;
    return true;
}

static bool function_name_taken(const CheckContext *ctx, const char *name)
{
    return find_type(ctx->types, name) != NULL ||
           find_function(ctx, name) != NULL ||
           find_variable(ctx->globals, name) != NULL ||
           name_is_resource(ctx, name) || is_builtin_type_name(name);
}

/* A method's owner and generated name, or a plain function's free name; says
   so if the name is taken. */
static bool check_head_name(CheckContext *ctx, const AstNode *node,
                            FunctionHead *head)
{
    bool ok = head->associated
                  ? check_method_owner(ctx, node, head) && name_method(ctx, head)
                  : !function_name_taken(ctx, head->name);

    if (!ok && !ctx->errors->reported) {
        error_report_at(ctx->errors, node->pos, MESSAGE_CHECKER_DUPLICATE_NAME,
                        head->name);
    }
    return ok;
}

static bool check_function_head(CheckContext *ctx, const AstNode *node,
                                FunctionHead *head)
{
    *head = (FunctionHead){0};
    head->name = ast_symbol_text(ctx->ast, node->name_index);
    head->backend_name = head->name;
    head->associated = node->is_method || node->is_static;
    head->owner_name = node->second_name_index == AST_NO_NODE
                           ? NULL
                           : ast_symbol_text(ctx->ast, node->second_name_index);
    if (head->name == NULL) {
        error_report_at(ctx->errors, node->pos,
                        MESSAGE_CHECKER_MISSING_NAME_OR_TYPE);
        return false;
    }
    if (!check_reserved_name(ctx, node->pos, head->name) ||
        !check_head_name(ctx, node, head)) {
        return false;
    }
    if (is_builtin_function_name(head->name)) {
        error_report_at(ctx->errors, node->pos,
                        MESSAGE_CHECKER_BUILTIN_RESERVED, head->name);
        return false;
    }
    return true;
}

static void copy_text(char *to, const char *from)
{
    memcpy(to, from, strlen(from) + 1);
}

/* A generic function is a template: it is recorded, its body is not checked
   until a call instantiates it. */
static bool register_generic(CheckContext *ctx, const AstNode *node,
                             const FunctionHead *head, size_t ast_index)
{
    FunctionEntry generic = {0};
    size_t i;

    if (node->type_parameters_count + head->owner.parameter_count >
        NYX_MAX_TYPE_PARAMETERS) {
        error_report_at(ctx->errors, node->pos,
                        MESSAGE_CHECKER_TOO_MANY_TYPE_PARAMETERS, head->name,
                        (size_t)NYX_MAX_TYPE_PARAMETERS);
        return false;
    }
    copy_text(generic.name, head->backend_name);
    if (head->associated) {
        copy_text(generic.owner_name, head->owner_name);
        copy_text(generic.method_name, head->name);
        generic.is_static = node->is_static;
        generic.is_method = node->is_method;
    }
    generic.type_index = TYPED_NO_NODE;
    generic.generic = true;
    generic.ast_index = ast_index;
    generic.owner_parameter_count = head->owner.parameter_count;
    for (i = 0; i < head->owner.parameter_count; i++) {
        copy_text(generic.parameters[i], head->owner.parameters[i]);
        generic.parameter_count++;
    }
    for (i = 0; i < node->type_parameters_count; i++) {
        if (!add_type_parameter_name(
                ctx,
                *(const size_t *)list_get(&ctx->ast->type_parameters,
                                          node->type_parameters_start + i),
                generic.parameters, &generic.parameter_count)) {
            return false;
        }
    }
    checker_ok(ctx->errors, list_push(ctx->functions, &generic),
               MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
    checker_ok(ctx->errors,
               name_index_insert(&ctx->function_index, ctx->functions,
                                 ctx->functions->length - 1),
               MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
    return check_generic_names(ctx, node);
}

/* A parameter's name must be free and not reserved. */
static bool check_parameter_name(CheckContext *ctx, const AstNode *parameter,
                                 const char *name)
{
    if (name == NULL) {
        error_report_at(ctx->errors, parameter->pos,
                        MESSAGE_CHECKER_MISSING_NAME_OR_TYPE);
        return false;
    }
    if (!check_reserved_name(ctx, parameter->pos, name)) {
        return false;
    }
    if (is_builtin_function_name(name)) {
        error_report_at(ctx->errors, parameter->pos,
                        MESSAGE_CHECKER_BUILTIN_RESERVED, name);
        return false;
    }
    if (find_variable(ctx->locals, name) != NULL || name_is_resource(ctx, name)) {
        error_report_at(ctx->errors, parameter->pos,
                        MESSAGE_CHECKER_DUPLICATE_PARAMETER, name);
        return false;
    }
    return true;
}

/* A parameter's declared type: a store may be passed, any other value must
   fit the by-value size cap. */
static bool check_parameter_type(CheckContext *ctx, const AstNode *parameter,
                                 const char *name, size_t *type)
{
    const TypedType *resolved;

    if (!resolve_type_node(ctx, parameter->left_child, name, type)) {
        return false;
    }
    resolved = typed_program_type(ctx->program, *type);
    return resolved != NULL &&
           (resolved->kind == NYX_TYPE_STORE ||
            check_type_value_size(ctx, *type, parameter->pos));
}

/* Declare one parameter: its typed node, its place in the signature, and the
   local it introduces. */
static bool declare_parameter(CheckContext *ctx, size_t child,
                              List *parameter_nodes, List *parameter_types)
{
    const AstNode *parameter = ast_get_node(ctx->ast, child);
    const char *name;
    size_t type;
    size_t index;
    TypedNode typed = {0};
    VariableEntry entry = {0};

    if (parameter == NULL || parameter->kind != AST_PARAMETER) {
        error_report(ctx->errors, MESSAGE_CHECKER_MISSING_AST_NODE, child);
        return false;
    }
    name = ast_symbol_text(ctx->ast, parameter->name_index);
    if (!check_parameter_name(ctx, parameter, name) ||
        !check_parameter_type(ctx, parameter, name, &type)) {
        return false;
    }
    typed.kind = TYPED_PARAMETER;
    typed.type_index = type;
    typed.pos = parameter->pos;
    typed.name_index = checker_name(ctx, name);
    typed.left_child = TYPED_NO_NODE;
    typed.right_child = TYPED_NO_NODE;
    typed.is_self = parameter->is_self;
    if (!checker_push_node(ctx, &typed, &index,
                           MESSAGE_CHECKER_OUT_OF_MEMORY_PROGRAM)) {
        return false;
    }
    checker_ok(ctx->errors, list_push(parameter_nodes, &index),
               MESSAGE_CHECKER_OUT_OF_MEMORY_PROGRAM);
    if (parameter_types != NULL) {
        checker_ok(ctx->errors, list_push(parameter_types, &type),
                   MESSAGE_CHECKER_OUT_OF_MEMORY_PROGRAM);
    }
    copy_text(entry.name, name);
    entry.type_index = type;
    entry.is_const = typed_program_type(ctx->program, type)->kind == NYX_TYPE_STORE;
    entry.is_self_pointer = parameter->is_self;
    checker_ok(ctx->errors, list_push(ctx->locals, &entry),
               MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
    return true;
}

bool declare_parameters(CheckContext *ctx, const AstNode *node,
                               List *parameter_nodes, List *parameter_types)
{
    size_t i;

    for (i = 0; i < node->children_count; i++) {
        size_t child = *(const size_t *)list_get(&ctx->ast->children,
                                                 node->children_start + i);

        if (!declare_parameter(ctx, child, parameter_nodes, parameter_types)) {
            return false;
        }
    }
    return true;
}

/* Types a function may not return. */
static const struct {
    NyxType kind;
    const char *message;
} unreturnable[] = {
    {NYX_TYPE_STORE, MESSAGE_CHECKER_STORE_TYPE},
    {NYX_TYPE_CHANNEL, MESSAGE_CHECKER_CHANNEL_USE},
};

const char *resource_type_message(const TypedType *type)
{
    size_t i;

    for (i = 0; type != NULL && i < NYX_COUNT_OF(unreturnable); i++) {
        if (type->kind == unreturnable[i].kind) {
            return unreturnable[i].message;
        }
    }
    return NULL;
}

static bool check_return_type(CheckContext *ctx, const AstNode *node,
                              const char *name, size_t *return_type)
{
    const char *message;

    if (!(node->right_child == AST_NO_NODE
              ? builtin_type(ctx, NYX_TYPE_NIL, return_type)
              : resolve_type_node(ctx, node->right_child, name, return_type))) {
        return false;
    }
    message = resource_type_message(typed_program_type(ctx->program, *return_type));
    if (message != NULL) {
        error_report_at(ctx->errors, node->pos, message);
        return false;
    }
    return check_type_value_size(ctx, *return_type, node->pos);
}

static void register_function(CheckContext *ctx, const AstNode *node,
                              const FunctionHead *head, size_t ast_index,
                              size_t function_type)
{
    FunctionEntry entry = {0};

    copy_text(entry.name, head->backend_name);
    if (head->associated) {
        copy_text(entry.owner_name, head->owner_name);
        copy_text(entry.method_name, head->name);
    }
    entry.type_index = function_type;
    entry.is_method = node->is_method;
    entry.is_static = node->is_static;
    entry.ast_index = ast_index;
    checker_ok(ctx->errors, list_push(ctx->functions, &entry),
               MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
    checker_ok(ctx->errors,
               name_index_insert(&ctx->function_index, ctx->functions,
                                 ctx->functions->length - 1),
               MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
}

/* Whether the body returns a value on every path, and does not simply
   recurse forever. */
static bool check_function_flow(CheckContext *ctx, const AstNode *node,
                                const FunctionHead *head, size_t typed_body)
{
    bool has_return = false;
    bool has_safe_return = false;

    if (node->right_child != AST_NO_NODE &&
        !typed_block_returns(ctx->program, typed_body)) {
        error_report_at(ctx->errors, node->pos,
                        MESSAGE_CHECKER_FUNCTION_MAY_NOT_RETURN, head->name);
        return false;
    }
    if (!head->associated) {
        ast_scan_returns(ctx->ast, node->left_child, node->name_index,
                         &has_return, &has_safe_return);
    }
    if (has_return && !has_safe_return) {
        error_report_at(ctx->errors, node->pos,
                        MESSAGE_CHECKER_ALWAYS_RECURSIVE, head->name);
        return false;
    }
    return true;
}

static bool check_function_body(CheckContext *ctx, const AstNode *node,
                                const FunctionHead *head, size_t return_type,
                                size_t *typed_body)
{
    if (node->left_child == AST_NO_NODE) {
        error_report_at(ctx->errors, node->pos, MESSAGE_CHECKER_MISSING_RETURN,
                        head->name);
        return false;
    }
    ctx->loop_depth = 0;
    ctx->defer_depth = 0;
    ctx->function_name = head->name;
    return check_block(ctx, node->left_child, return_type, typed_body) &&
           check_function_flow(ctx, node, head, *typed_body);
}

/* Check a function's signature and body and emit it. */
static bool emit_function(CheckContext *ctx, const AstNode *node,
                          size_t ast_index, const FunctionHead *head,
                          const List *parameter_nodes, List *parameter_types)
{
    size_t return_type;
    size_t function_type;
    size_t typed_body;
    size_t typed_index;
    size_t i;
    TypedNode typed = {0};

    if (!check_return_type(ctx, node, head->name, &return_type) ||
        !add_function_type(ctx, parameter_types, return_type, &function_type)) {
        return false;
    }
    register_function(ctx, node, head, ast_index, function_type);
    if (!check_function_body(ctx, node, head, return_type, &typed_body)) {
        return false;
    }
    typed.children_start = ctx->program->children.length;
    for (i = 0; i < parameter_nodes->length; i++) {
        checker_ok(ctx->errors,
                   typed_program_push_child(
                       ctx->program,
                       *(const size_t *)list_get(parameter_nodes, i)),
                   MESSAGE_CHECKER_OUT_OF_MEMORY_PROGRAM);
    }
    typed.kind = TYPED_FUNCTION_DECLARATION;
    typed.integer_value_u = ctx->match_scope;
    typed.type_index = function_type;
    typed.pos = node->pos;
    typed.name_index = checker_name(ctx, head->backend_name);
    typed.left_child = typed_body;
    typed.right_child = TYPED_NO_NODE;
    typed.is_method = node->is_method;
    typed.children_count = node->children_count;
    return emit_node(ctx, &typed, ast_index, &typed_index);
}

/* A function body is checked in its own frame of locals, loop depth and
   match temporaries; none of it outlives the function. */
static bool check_concrete_function(CheckContext *ctx, const AstNode *node,
                                    size_t ast_index, const FunctionHead *head)
{
    size_t previous_match_scope = ctx->match_scope;
    size_t empty = TYPED_NO_NODE;
    List parameter_types;
    List parameter_nodes;
    bool ok;

    list_init(&parameter_types, sizeof(size_t));
    list_init(&parameter_nodes, sizeof(size_t));
    ctx->match_scope = ctx->program->match_scopes.length;
    checker_ok(ctx->errors, list_push(&ctx->program->match_scopes, &empty),
               MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
    ctx->in_function = true;
    ctx->locals->length = 0;
    ok = declare_parameters(ctx, node, &parameter_nodes, &parameter_types) &&
         emit_function(ctx, node, ast_index, head, &parameter_nodes,
                       &parameter_types);
    ctx->match_scope = previous_match_scope;
    ctx->in_function = false;
    ctx->loop_depth = 0;
    ctx->defer_depth = 0;
    ctx->function_name = NULL;
    ctx->locals->length = 0;
    list_free(&parameter_types);
    list_free(&parameter_nodes);
    return ok;
}

bool check_function_declaration(CheckContext *ctx, const AstNode *node,
                                size_t ast_index)
{
    FunctionHead head;

    if (!check_function_head(ctx, node, &head)) {
        return false;
    }
    if (node->type_parameters_count > 0 ||
        (head.associated && head.owner.parameter_count > 0)) {
        return register_generic(ctx, node, &head, ast_index);
    }
    return check_concrete_function(ctx, node, ast_index, &head);
}
