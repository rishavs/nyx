#include "checker_internal.h"

/* Reading and writing a field, an element or an entry: `a.f`, `a[i]`,
   `a.f = v` and `a[i] = v`. */

/* The built-in Error's fields, in the order its type holds them. */
static const char *const error_fields[] = {
    "header", "description", "filename", "line", "col", "pos",
};

static bool lookup_struct_field(CheckContext *ctx, const TypedType *type,
                                const char *name, size_t *out_type)
{
    const TypedNode *declaration =
        typed_program_node(ctx->program, type->declaration_node);
    size_t i;

    for (i = 0; declaration != NULL && i < declaration->children_count; i++) {
        const TypedNode *field = typed_program_node(
            ctx->program,
            *(const size_t *)list_get(&ctx->program->children,
                                      declaration->children_start + i));
        const char *field_name =
            field == NULL || field->kind != TYPED_FIELD
                ? NULL
                : typed_program_symbol_text(ctx->program, field->name_index);

        if (field_name != NULL && strcmp(field_name, name) == 0) {
            *out_type = field->type_index;
            return true;
        }
    }
    return false;
}

static bool lookup_error_field(CheckContext *ctx, const TypedType *type,
                               const char *name, size_t *out_type)
{
    size_t i;

    for (i = 0; i < NYX_COUNT_OF(error_fields) && i < type->child_count; i++) {
        if (strcmp(error_fields[i], name) == 0) {
            *out_type = *(const size_t *)list_get(&ctx->program->type_children,
                                                  type->first_child + i);
            return true;
        }
    }
    return false;
}

bool lookup_field(CheckContext *ctx, size_t receiver_type, const char *name,
                  size_t *out_type, bool *out_error)
{
    const TypedType *type = typed_program_type(ctx->program, receiver_type);

    *out_error = type != NULL && type->kind == NYX_TYPE_ERROR;
    if (type == NULL) {
        return false;
    }
    return type->kind == NYX_TYPE_STRUCT
               ? lookup_struct_field(ctx, type, name, out_type)
               : type->kind == NYX_TYPE_ERROR &&
                     lookup_error_field(ctx, type, name, out_type);
}

bool field_is_lvalue(const Ast *ast, size_t ast_index)
{
    const AstNode *node = ast_get_node(ast, ast_index);

    return node != NULL &&
           (node->kind == AST_IDENTIFIER ||
            (node->kind == AST_MEMBER_ACCESS &&
             field_is_lvalue(ast, node->left_child)));
}

bool check_member_access(CheckContext *ctx, const AstNode *node, size_t ast_index)
{
    const char *name = ast_symbol_text(ctx->ast, node->name_index);
    const TypedNode *receiver_node;
    size_t receiver;
    size_t field_type;
    bool is_error;
    TypedNode typed;

    if (name == NULL || !check_expression(ctx, node->left_child, &receiver)) {
        return false;
    }
    receiver_node = typed_program_node(ctx->program, receiver);
    if (receiver_node == NULL ||
        !lookup_field(ctx, receiver_node->type_index, name, &field_type,
                      &is_error)) {
        error_report_at(ctx->errors, node->pos, MESSAGE_CHECKER_UNKNOWN_FIELD,
                        name);
        return false;
    }
    typed = new_node(TYPED_MEMBER_ACCESS, field_type, node->pos, receiver,
                     TYPED_NO_NODE);
    typed.name_index = checker_name(ctx, name);
    typed.auxiliary_type = TYPED_NO_NODE;
    typed.store_id = TYPED_NO_NODE;
    return emit_node(ctx, &typed, ast_index, &(size_t){0});
}

/* Types whose contents change in place when written through a name. */
static bool changes_in_place(NyxType kind)
{
    return kind == NYX_TYPE_STRUCT || kind == NYX_TYPE_FIXED_LIST ||
           kind == NYX_TYPE_RECORD || kind == NYX_TYPE_STRING;
}

/* The name at the root of a chain of field and element reads, or NULL when
   the chain starts from something else. */
const AstNode *access_root_node(const Ast *ast, size_t ast_index)
{
    const AstNode *node = ast_get_node(ast, ast_index);

    while (node != NULL &&
           (node->kind == AST_MEMBER_ACCESS || node->kind == AST_INDEX)) {
        node = ast_get_node(ast, node->left_child);
    }
    return node != NULL && node->kind == AST_IDENTIFIER ? node : NULL;
}

/* A const variable's contents may not be written through a field or element. */
bool check_inline_const_mutation(CheckContext *ctx, size_t ast_index)
{
    const AstNode *root = access_root_node(ctx->ast, ast_index);
    const char *name =
        root == NULL ? NULL : ast_symbol_text(ctx->ast, root->name_index);
    const VariableEntry *variable = name == NULL ? NULL : lookup_variable(ctx, name);
    const TypedType *type =
        variable == NULL
            ? NULL
            : typed_program_type(ctx->program, variable->type_index);

    if (variable == NULL || !variable->is_const || type == NULL ||
        !changes_in_place(type->kind)) {
        return true;
    }
    error_report_at(ctx->errors, root->pos, MESSAGE_CHECKER_CONST_ASSIGN, name);
    return false;
}

/* The target of an assignment, checked as an expression that is being written
   rather than read. */
static bool check_assignment_target(CheckContext *ctx, size_t target_ast,
                                    size_t *typed)
{
    size_t previous = ctx->assignment_target_ast;
    bool ok;

    ctx->assignment_target_ast = target_ast;
    ok = check_expression(ctx, target_ast, typed);
    ctx->assignment_target_ast = previous;
    return ok;
}

/* The target of `a.f = v`, checked as a write: a field of a struct held in a
   variable. Returns the typed target. */
static bool member_target(CheckContext *ctx, const AstNode *node,
                          size_t *target_typed, const TypedNode **target_node)
{
    const AstNode *target = ast_get_node(ctx->ast, node->left_child);

    if (target == NULL || target->kind != AST_MEMBER_ACCESS ||
        !field_is_lvalue(ctx->ast, node->left_child) ||
        !check_assignment_target(ctx, node->left_child, target_typed)) {
        error_report_at(ctx->errors, node->pos, MESSAGE_CHECKER_FIELD_LVALUE);
        return false;
    }
    *target_node = typed_program_node(ctx->program, *target_typed);
    if (*target_node == NULL || (*target_node)->kind != TYPED_MEMBER_ACCESS) {
        error_report_at(ctx->errors, node->pos, MESSAGE_CHECKER_FIELD_LVALUE);
        return false;
    }
    return true;
}

/* An Error's fields are read-only. */
static bool member_receiver_writable(CheckContext *ctx, const AstNode *node,
                                     const TypedNode *target_node)
{
    const TypedNode *receiver =
        typed_program_node(ctx->program, target_node->left_child);
    const TypedType *receiver_type =
        receiver == NULL
            ? NULL
            : typed_program_type(ctx->program, receiver->type_index);

    if (receiver_type == NULL || receiver_type->kind == NYX_TYPE_ERROR) {
        error_report_at(ctx->errors, node->pos, MESSAGE_CHECKER_FIELD_LVALUE);
        return false;
    }
    return true;
}

bool check_member_assignment(CheckContext *ctx, const AstNode *node,
                             size_t ast_index, size_t *out_typed)
{
    const TypedNode *target_node;
    size_t target_typed;
    size_t target_type_index;
    size_t value;
    size_t coerced;
    size_t nil_type;
    TypedNode typed;

    if (!member_target(ctx, node, &target_typed, &target_node) ||
        !check_inline_const_mutation(ctx, node->left_child)) {
        return false;
    }
    target_type_index = target_node->type_index;
    if (!member_receiver_writable(ctx, node, target_node)) {
        return false;
    }
    if (!check_expression_expected(ctx, node->right_child, target_type_index,
                                   &value) ||
        !inject_into(ctx, value, target_type_index, &coerced) ||
        !builtin_type(ctx, NYX_TYPE_NIL, &nil_type)) {
        if (!ctx->errors->reported) {
            error_report(ctx->errors, MESSAGE_CHECKER_INITIALISER_TYPE_MISMATCH,
                         "field", "field");
        }
        return false;
    }
    typed = new_node(TYPED_MEMBER_WRITE, nil_type, node->pos, target_typed,
                     coerced);
    typed.auxiliary_type = TYPED_NO_NODE;
    typed.store_id = TYPED_NO_NODE;
    invalidate_access_narrowing(ctx, node->left_child);
    return emit_node(ctx, &typed, ast_index, out_typed);
}

bool check_fixed_index_bounds(CheckContext *ctx, size_t index_ast,
                              size_t receiver_type_index)
{
    const TypedType *type =
        typed_program_type(ctx->program, receiver_type_index);
    const AstNode *index_node = ast_get_node(ctx->ast, index_ast);
    int value;

    if (type == NULL || index_node == NULL ||
        (type->kind != NYX_TYPE_FIXED_LIST && type->kind != NYX_TYPE_STRING) ||
        !literal_int_value(ctx->ast, index_ast, &value)) {
        return true;
    }
    if (value < 0 || (size_t)value >= type->fixed_length) {
        error_report_at(ctx->errors, index_node->pos,
                        MESSAGE_CHECKER_INDEX_BOUNDS, value, type->fixed_length);
        return false;
    }
    return true;
}

/* ---- reading an element ---- */

/* The shape of an indexed value's type, read before anything that adds a type
   can move the array. */
typedef struct {
    size_t type_index;
    NyxType kind;
    size_t child_count;
    size_t first_child;
} Indexed;

static Indexed indexed_type(const CheckContext *ctx, size_t type_index)
{
    const TypedType *type = type_index == TYPED_NO_NODE
                                ? NULL
                                : typed_program_type(ctx->program, type_index);
    Indexed shape = {type_index, NYX_TYPE_UNKNOWN, 0, 0};

    if (type != NULL) {
        shape.kind = type->kind;
        shape.child_count = type->child_count;
        shape.first_child = type->first_child;
    }
    return shape;
}

static size_t indexed_part(const CheckContext *ctx, const Indexed *shape,
                           size_t i)
{
    return *(const size_t *)list_get(&ctx->program->type_children,
                                     shape->first_child + i);
}

static bool is_map_kind(NyxType kind)
{
    return kind == NYX_TYPE_RECORD || kind == NYX_TYPE_TABLE;
}

static bool is_element_kind(NyxType kind)
{
    return kind == NYX_TYPE_LIST || kind == NYX_TYPE_FIXED_LIST ||
           kind == NYX_TYPE_SLICE;
}

/* map[key]: the key is coerced to the map's key type. */
static bool index_map(CheckContext *ctx, const AstNode *node,
                      const Indexed *shape, TypedNode *typed, size_t receiver)
{
    size_t key;
    size_t coerced;

    if (shape->child_count != 2) {
        error_report_at(ctx->errors, node->pos, MESSAGE_CHECKER_MEMBER_RECEIVER,
                        "index");
        return false;
    }
    if (!check_expression(ctx, node->right_child, &key) ||
        !inject_into(ctx, key, indexed_part(ctx, shape, 0), &coerced)) {
        if (!ctx->errors->reported) {
            error_report_at(ctx->errors, node->pos, MESSAGE_CHECKER_MAP_KEY);
        }
        return false;
    }
    *typed = new_node(shape->kind == NYX_TYPE_RECORD ? TYPED_RECORD_GET
                                                     : TYPED_TABLE_GET,
                      indexed_part(ctx, shape, 1), node->pos, receiver, coerced);
    return true;
}

/* list[i]: the index is an Int32, within an array's bounds when constant. */
static bool index_element(CheckContext *ctx, const AstNode *node,
                          const Indexed *shape, TypedNode *typed,
                          size_t receiver)
{
    size_t index;
    Indexed after;

    if (!is_element_kind(shape->kind) || shape->child_count != 1) {
        error_report_at(ctx->errors, node->pos,
                        shape->kind == NYX_TYPE_STRING ||
                                shape->kind == NYX_TYPE_DYNAMIC_STRING
                            ? MESSAGE_CHECKER_TEXT_INDEX
                            : MESSAGE_CHECKER_MEMBER_RECEIVER,
                        "index");
        return false;
    }
    if (!check_fixed_index_bounds(ctx, node->right_child, shape->type_index)) {
        return false;
    }
    if (!check_int32_expression(ctx, node->right_child, &index)) {
        error_report_at(ctx->errors, node->pos, MESSAGE_CHECKER_LIST_INDEX);
        return false;
    }
    /* Checking the index can append types, growing the list and moving its
       storage, so the shape is read again. */
    after = indexed_type(ctx, shape->type_index);
    if (after.kind == NYX_TYPE_UNKNOWN) {
        error_report_at(ctx->errors, node->pos, MESSAGE_CHECKER_MEMBER_RECEIVER,
                        "index");
        return false;
    }
    *typed = new_node(after.kind == NYX_TYPE_SLICE ? TYPED_SLICE_GET
                                                   : TYPED_LIST_GET,
                      indexed_part(ctx, &after, 0), node->pos, receiver, index);
    return true;
}

bool check_index_expression(CheckContext *ctx, const AstNode *node,
                            size_t ast_index)
{
    const TypedNode *receiver_node;
    size_t receiver;
    Indexed shape;
    TypedNode typed;

    if (!check_expression(ctx, node->left_child, &receiver)) {
        return false;
    }
    receiver_node = typed_program_node(ctx->program, receiver);
    shape = indexed_type(ctx, receiver_node == NULL ? TYPED_NO_NODE
                                                    : receiver_node->type_index);
    if (!(is_map_kind(shape.kind)
              ? index_map(ctx, node, &shape, &typed, receiver)
              : index_element(ctx, node, &shape, &typed, receiver))) {
        return false;
    }
    typed.auxiliary_type = TYPED_NO_NODE;
    typed.store_id = TYPED_NO_NODE;
    return emit_node(ctx, &typed, ast_index, &(size_t){0});
}

/* ---- writing an element ---- */

/* The checked target of `a[i] = v`: the read it would be, and what it is a
   read of. */
typedef struct {
    size_t left;     /* the typed receiver */
    size_t right;    /* the typed index or key */
    size_t name;     /* the receiver's name: it must be a variable */
    Indexed shape;
} IndexTarget;

/* Kinds of read that name a place that can be written. */
static bool is_place_read(TypedNodeKind kind)
{
    return kind == TYPED_LIST_GET || kind == TYPED_SLICE_GET ||
           kind == TYPED_RECORD_GET || kind == TYPED_TABLE_GET;
}

/* The syntax of an indexed assignment target is a variable's element. */
static bool index_target_syntax(const CheckContext *ctx, const AstNode *node)
{
    const AstNode *syntax = ast_get_node(ctx->ast, node->left_child);
    const AstNode *receiver =
        syntax == NULL ? NULL : ast_get_node(ctx->ast, syntax->left_child);

    return syntax != NULL && syntax->kind == AST_INDEX && receiver != NULL &&
           receiver->kind == AST_IDENTIFIER;
}

static bool index_target(CheckContext *ctx, const AstNode *node,
                         IndexTarget *target)
{
    const TypedNode *target_node = NULL;
    const TypedNode *receiver;
    size_t typed;
    bool ok = index_target_syntax(ctx, node);

    if (ok && (!check_inline_const_mutation(ctx, node->left_child) ||
               !check_assignment_target(ctx, node->left_child, &typed))) {
        return false;
    }
    if (ok) {
        target_node = typed_program_node(ctx->program, typed);
        ok = target_node != NULL && is_place_read(target_node->kind);
    }
    if (!ok) {
        error_report_at(ctx->errors, node->pos, MESSAGE_CHECKER_LIST_LVALUE);
        return false;
    }
    target->left = target_node->left_child;
    target->right = target_node->right_child;
    receiver = typed_program_node(ctx->program, target->left);
    target->name = receiver == NULL ? TYPED_NO_NODE : receiver->name_index;
    target->shape = indexed_type(ctx, receiver == NULL ? TYPED_NO_NODE
                                                       : receiver->type_index);
    return true;
}

/* map[key] = value. */
static bool assign_map_entry(CheckContext *ctx, const AstNode *node,
                             const IndexTarget *target, size_t ast_index,
                             size_t *out_typed)
{
    size_t key_type = indexed_part(ctx, &target->shape, 0);
    size_t value_type = indexed_part(ctx, &target->shape, 1);
    size_t coerced_key;
    size_t value;
    size_t coerced_value;
    size_t nil_type;
    TypedNode typed;

    if (target->shape.child_count != 2 || target->name == TYPED_NO_NODE) {
        error_report_at(ctx->errors, node->pos, MESSAGE_CHECKER_LIST_LVALUE);
        return false;
    }
    if (!inject_into(ctx, target->right, key_type, &coerced_key) ||
        !check_expression_expected(ctx, node->right_child, value_type, &value) ||
        !inject_into(ctx, value, value_type, &coerced_value) ||
        !builtin_type(ctx, NYX_TYPE_NIL, &nil_type)) {
        if (!ctx->errors->reported) {
            error_report_at(ctx->errors, node->pos, MESSAGE_CHECKER_MAP_VALUE);
        }
        return false;
    }
    typed = new_node(target->shape.kind == NYX_TYPE_RECORD ? TYPED_RECORD_SET
                                                           : TYPED_TABLE_SET,
                     nil_type, node->pos, target->left, TYPED_NO_NODE);
    typed.name_index = target->name;
    typed.auxiliary_type = value_type;
    typed.store_id = TYPED_NO_NODE;
    typed.children_start = ctx->program->children.length;
    typed.children_count = 2;
    checker_ok(ctx->errors, typed_program_push_child(ctx->program, coerced_key),
               MESSAGE_CHECKER_OUT_OF_MEMORY_PROGRAM);
    checker_ok(ctx->errors, typed_program_push_child(ctx->program, coerced_value),
               MESSAGE_CHECKER_OUT_OF_MEMORY_PROGRAM);
    return emit_node(ctx, &typed, ast_index, out_typed);
}

/* The target of `list[i] = v` is an array or list held in a variable; a slice
   cannot be written through. */
static bool list_target_writable(CheckContext *ctx, const AstNode *node,
                                 const IndexTarget *target)
{
    if (target->shape.kind == NYX_TYPE_UNKNOWN ||
        target->shape.child_count != 1 || target->name == TYPED_NO_NODE) {
        error_report_at(ctx->errors, node->pos, MESSAGE_CHECKER_LIST_LVALUE);
        return false;
    }
    if (target->shape.kind == NYX_TYPE_SLICE) {
        error_report_at(ctx->errors, node->pos, MESSAGE_CHECKER_SLICE_READONLY);
        return false;
    }
    return true;
}

/* list[i] = value. */
static bool assign_list_element(CheckContext *ctx, const AstNode *node,
                                const IndexTarget *target, size_t ast_index,
                                size_t *out_typed)
{
    const AstNode *syntax = ast_get_node(ctx->ast, node->left_child);
    const TypedNode *receiver = typed_program_node(ctx->program, target->left);
    size_t element;
    size_t value;
    size_t coerced;
    size_t nil_type;
    TypedNode typed;

    /* The bounds check reads the index as a syntax node, so it needs the
       target's syntax child; `target->right` is a typed-node index and numbers
       a different array. */
    if (!list_target_writable(ctx, node, target) ||
        !check_fixed_index_bounds(ctx, syntax->right_child,
                                  receiver->type_index)) {
        return false;
    }
    element = indexed_part(ctx, &target->shape, 0);
    if (!check_expression_expected(ctx, node->right_child, element, &value) ||
        !inject_into(ctx, value, element, &coerced) ||
        !builtin_type(ctx, NYX_TYPE_NIL, &nil_type)) {
        if (!ctx->errors->reported) {
            error_report(ctx->errors, MESSAGE_CHECKER_LIST_VALUE);
        }
        return false;
    }
    typed = new_node(TYPED_LIST_SET, nil_type, node->pos, target->left,
                     TYPED_NO_NODE);
    typed.name_index = target->name;
    typed.auxiliary_type = element;
    typed.store_id = TYPED_NO_NODE;
    typed.children_start = ctx->program->children.length;
    typed.children_count = 2;
    checker_ok(ctx->errors, typed_program_push_child(ctx->program, target->right),
               MESSAGE_CHECKER_OUT_OF_MEMORY_PROGRAM);
    checker_ok(ctx->errors, typed_program_push_child(ctx->program, coerced),
               MESSAGE_CHECKER_OUT_OF_MEMORY_PROGRAM);
    invalidate_access_narrowing(ctx, node->left_child);
    invalidate_aliased_narrowings(ctx);
    return emit_node(ctx, &typed, ast_index, out_typed);
}

bool check_index_assignment(CheckContext *ctx, const AstNode *node,
                            size_t ast_index, size_t *out_typed)
{
    IndexTarget target;

    if (!index_target(ctx, node, &target)) {
        return false;
    }
    return is_map_kind(target.shape.kind)
               ? assign_map_entry(ctx, node, &target, ast_index, out_typed)
               : assign_list_element(ctx, node, &target, ast_index, out_typed);
}
