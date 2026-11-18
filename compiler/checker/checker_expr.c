#include "checker_internal.h"



bool check_nil_literal(CheckContext *ctx, const AstNode *node,
                              size_t ast_index)
{
    size_t nil_type;
    TypedNode typed = {0};

    if (!builtin_type(ctx, NYX_TYPE_NIL, &nil_type)) {
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


/* The key and value types a record literal is expected to have. */
static void expected_record_types(const CheckContext *ctx, size_t ast_index,
                                  size_t *key_type, size_t *value_type)
{
    const TypedType *expected =
        ctx->expected_ast == ast_index
            ? typed_program_type(ctx->program, ctx->expected_type)
            : NULL;

    *key_type = TYPED_NO_NODE;
    *value_type = TYPED_NO_NODE;
    if (expected != NULL && expected->kind == NYX_TYPE_RECORD &&
        expected->child_count == 2) {
        *key_type = *(const size_t *)list_get(&ctx->program->type_children,
                                              expected->first_child);
        *value_type = *(const size_t *)list_get(&ctx->program->type_children,
                                                expected->first_child + 1);
    }
}

/* Append checked values to the typed program's children; returns where they
   start. */
size_t push_typed_children(CheckContext *ctx, const List *children)
{
    size_t start = ctx->program->children.length;
    size_t i;

    for (i = 0; i < children->length; i++) {
        checker_ok(ctx->errors,
                   typed_program_push_child(
                       ctx->program, *(const size_t *)list_get(children, i)),
                   MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
    }
    return start;
}

bool check_record_literal(CheckContext *ctx, const AstNode *node,
                          size_t ast_index)
{
    TypedNode typed;
    List children;
    size_t key_type;
    size_t value_type;
    size_t i;

    expected_record_types(ctx, ast_index, &key_type, &value_type);
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
            !check_expression_expected(ctx, value_ast, value_type, &value)) {
            list_free(&children);
            return false;
        }
        checker_ok(ctx->errors, list_push(&children, &key),
                   MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
        checker_ok(ctx->errors, list_push(&children, &value),
                   MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
    }
    typed = new_node(TYPED_RECORD_LITERAL, TYPED_NO_NODE, node->pos,
                     TYPED_NO_NODE, TYPED_NO_NODE);
    typed.auxiliary_type = TYPED_NO_NODE;
    typed.store_id = TYPED_NO_NODE;
    typed.children_start = push_typed_children(ctx, &children);
    typed.children_count = children.length;
    list_free(&children);
    return emit_node(ctx, &typed, ast_index, &(size_t){0});
}


/* An untyped list literal: its elements are coerced later, by the type the
   context gives it. */
static TypedNode list_literal_node(const AstNode *node, size_t type_index)
{
    TypedNode typed = new_node(TYPED_LIST_LITERAL, type_index, node->pos,
                               TYPED_NO_NODE, TYPED_NO_NODE);

    typed.auxiliary_type = TYPED_NO_NODE;
    typed.store_id = TYPED_NO_NODE;
    return typed;
}

/* What the elements of a list literal have in common. A string literal can
   become any string type, so it joins whatever string type the others have,
   and a list of nothing but string literals holds Text. Elements with
   nothing in common are `mixed`, and left for the context to coerce. */
typedef struct {
    size_t candidate;
    bool has_strings;
    bool mixed;
} ListElements;

static bool classify_list_element(CheckContext *ctx, const AstNode *node,
                                  size_t child, ListElements *elements)
{
    const TypedNode *child_node = typed_program_node(ctx->program, child);
    const TypedType *child_type =
        child_node == NULL
            ? NULL
            : typed_program_type(ctx->program, child_node->type_index);

    if (child_type == NULL) {
        error_report_at(ctx->errors, node->pos, MESSAGE_CHECKER_LIST_INFER);
        return false;
    }
    if (child_type->kind == NYX_TYPE_STRING_LITERAL) {
        elements->has_strings = true;
    } else if (elements->candidate == TYPED_NO_NODE) {
        elements->candidate = child_node->type_index;
    } else if (!same_type(ctx->program, elements->candidate,
                          child_node->type_index)) {
        elements->mixed = true;
    }
    return true;
}

static bool list_element_type(CheckContext *ctx, const AstNode *node,
                              const List *checked, ListElements *elements)
{
    const TypedType *type;
    size_t i;

    *elements = (ListElements){TYPED_NO_NODE, false, false};
    for (i = 0; i < checked->length; i++) {
        if (!classify_list_element(ctx, node, *(const size_t *)list_get(checked, i),
                                   elements)) {
            return false;
        }
    }
    if (elements->candidate == TYPED_NO_NODE) {
        return builtin_type(ctx, NYX_TYPE_DYNAMIC_STRING, &elements->candidate);
    }
    type = typed_program_type(ctx->program, elements->candidate);
    elements->mixed = elements->mixed ||
                      (elements->has_strings &&
                       (type == NULL || (type->kind != NYX_TYPE_STRING &&
                                         type->kind != NYX_TYPE_DYNAMIC_STRING)));
    return true;
}

/* The element type the context expects of a list literal, if it says. */
static size_t expected_element_type(const CheckContext *ctx, size_t ast_index)
{
    const TypedType *expected =
        ctx->expected_ast == ast_index
            ? typed_program_type(ctx->program, ctx->expected_type)
            : NULL;

    if (expected != NULL && expected->kind == NYX_TYPE_FIXED_LIST &&
        expected->child_count == 1) {
        return *(const size_t *)list_get(&ctx->program->type_children,
                                         expected->first_child);
    }
    return TYPED_NO_NODE;
}

static bool check_list_elements(CheckContext *ctx, const AstNode *node,
                                size_t expected_element, List *checked)
{
    size_t i;

    for (i = 0; i < node->children_count; i++) {
        size_t child_ast = *(const size_t *)list_get(
            &ctx->ast->children, node->children_start + i);
        size_t child;

        if (!check_expression_expected(ctx, child_ast, expected_element,
                                       &child) ||
            (expected_element == TYPED_NO_NODE &&
             !check_default_numeric(ctx, child))) {
            return false;
        }
        checker_ok(ctx->errors, list_push(checked, &child),
                   MESSAGE_CHECKER_OUT_OF_MEMORY_PROGRAM);
    }
    return true;
}

/* A typed list: each element coerced to the common type and appended. */
static bool push_coerced_elements(CheckContext *ctx, const AstNode *node,
                                  const List *checked, size_t candidate,
                                  size_t *children_start)
{
    size_t i;

    *children_start = ctx->program->children.length;
    for (i = 0; i < checked->length; i++) {
        size_t coerced;

        if (!inject_into(ctx, *(const size_t *)list_get(checked, i), candidate,
                         &coerced) ||
            !typed_program_push_child(ctx->program, coerced)) {
            if (!ctx->errors->reported) {
                error_report_at(ctx->errors, node->pos,
                                MESSAGE_CHECKER_LIST_INFER);
            }
            return false;
        }
    }
    return true;
}

/* The literal's children and type, by the kind of elements it holds: none, a
   mix left for the context to coerce, or one type they are all coerced to. */
static bool shape_list_literal(CheckContext *ctx, const AstNode *node,
                               const List *checked, TypedNode *typed)
{
    ListElements elements;
    size_t list_index;

    if (checked->length == 0) {
        typed->children_start = ctx->program->children.length;
        return true;
    }
    if (!list_element_type(ctx, node, checked, &elements)) {
        return false;
    }
    typed->children_count = node->children_count;
    if (elements.mixed) {
        typed->children_start = push_typed_children(ctx, checked);
        return true;
    }
    if (!list_type(ctx, elements.candidate, true, checked->length, &list_index)) {
        return false;
    }
    typed->type_index = list_index;
    return push_coerced_elements(ctx, node, checked, elements.candidate,
                                 &typed->children_start);
}

bool check_list_literal(CheckContext *ctx, const AstNode *node, size_t ast_index)
{
    TypedNode typed = list_literal_node(node, TYPED_NO_NODE);
    List checked;
    bool ok;

    list_init(&checked, sizeof(size_t));
    ok = check_list_elements(ctx, node, expected_element_type(ctx, ast_index),
                             &checked) &&
         shape_list_literal(ctx, node, &checked, &typed);
    list_free(&checked);
    return ok && emit_node(ctx, &typed, ast_index, &(size_t){0});
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


/* The test's value: 1 when the value has exactly the tested type, the
   alternative number when it is a union holding that type, otherwise 0. */
static int is_result(const CheckContext *ctx, const TypedNode *value_node,
                     const TypedType *value_type, size_t tested)
{
    size_t tag;

    if (same_type(ctx->program, value_node->type_index, tested)) {
        return 1;
    }
    if (value_type->kind == NYX_TYPE_UNION &&
        union_has_alternative(ctx->program, value_type, tested, &tag)) {
        return (int)tag;
    }
    return 0;
}

bool check_is(CheckContext *ctx, const AstNode *node, size_t ast_index)
{
    const TypedNode *value_node;
    const TypedType *value_type;
    const TypedType *target;
    size_t value;
    size_t tested;
    size_t bool_type;
    int result;
    TypedNode typed;

    if (!check_expression(ctx, node->left_child, &value) ||
        !resolve_type_in_env(ctx, node->right_child, ctx->env, "is", &tested)) {
        return false;
    }
    target = typed_program_type(ctx->program, tested);
    if (target != NULL && target->kind == NYX_TYPE_UNION) {
        error_report_at(ctx->errors, node->pos, MESSAGE_CHECKER_IS_TYPE_IS_UNION);
        return false;
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
    if (value_type->kind == NYX_TYPE_STRING_LITERAL) {
        error_report_at(ctx->errors, node->pos,
                        MESSAGE_CHECKER_STRING_LITERAL_CONTEXT);
        return false;
    }
    result = is_result(ctx, value_node, value_type, tested);
    if (!builtin_type(ctx, NYX_TYPE_BOOL, &bool_type)) {
        return false;
    }
    typed = new_node(TYPED_IS, bool_type, node->pos, value, TYPED_NO_NODE);
    typed.auxiliary_type = tested;
    typed.integer_value = result;
    return emit_node(ctx, &typed, ast_index, &(size_t){0});
}


bool check_literal(CheckContext *ctx, const AstNode *node,
                          size_t ast_index)
{
    NyxType kind = NYX_TYPE_INT64;
    size_t literal_type;
    TypedNode typed = {0};

    if (node->kind == AST_FLOAT_LITERAL) {
        if (!add_type_record(ctx, NYX_TYPE_FLOAT64, TYPED_NO_NODE,
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
        kind = NYX_TYPE_UINT64;
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


/* An identifier is decided by the first rule that recognises it. */
typedef struct {
    CheckContext *ctx;
    const AstNode *node;
    const char *name;
    size_t ast_index;
} Identifier;

typedef enum {
    ID_NEXT,
    ID_REJECTED,
    ID_ACCEPTED
} IdentifierStep;

typedef IdentifierStep (*IdentifierRule)(const Identifier *id);

static IdentifierStep identifier_step(bool ok)
{
    return ok ? ID_ACCEPTED : ID_REJECTED;
}

static IdentifierStep reject_identifier(const Identifier *id, const char *message)
{
    error_report_at(id->ctx->errors, id->node->pos, message, id->name);
    return ID_REJECTED;
}

static IdentifierStep identifier_needs_name(const Identifier *id)
{
    if (id->name != NULL) {
        return ID_NEXT;
    }
    error_report_at(id->ctx->errors, id->node->pos,
                    MESSAGE_CHECKER_MISSING_NAME_OR_TYPE);
    return ID_REJECTED;
}

/* A channel or an atomic cell is a resource, not a value. A local of the same
   name hides it. */
static IdentifierStep identifier_resource(const Identifier *id)
{
    const CheckContext *ctx = id->ctx;

    if (find_variable(ctx->locals, id->name) != NULL) {
        return ID_NEXT;
    }
    if (find_channel(ctx->channels, id->name) != NULL) {
        return reject_identifier(id, MESSAGE_CHECKER_CHANNEL_NOT_VALUE);
    }
    if (find_atomic(ctx->atomics, id->name) != NULL) {
        return reject_identifier(id, MESSAGE_CHECKER_ATOMIC_NOT_VALUE);
    }
    return ID_NEXT;
}

/* A Store capability (a global store, or a local that holds one) may be named
   only where a Store is expected, such as an argument for a Store parameter. */
static IdentifierStep identifier_store(const Identifier *id)
{
    CheckContext *ctx = id->ctx;
    const StoreEntry *store = find_store(ctx->stores, id->name);
    const VariableEntry *local = find_variable(ctx->locals, id->name);
    const TypedType *local_type =
        local == NULL ? NULL : typed_program_type(ctx->program, local->type_index);
    const TypedType *expected =
        ctx->expected_ast == id->ast_index
            ? typed_program_type(ctx->program, ctx->expected_type)
            : NULL;
    TypedNode typed;

    if (store == NULL && (local_type == NULL || local_type->kind != NYX_TYPE_STORE)) {
        return ID_NEXT;
    }
    if (expected == NULL || expected->kind != NYX_TYPE_STORE) {
        return reject_identifier(id, MESSAGE_CHECKER_STORE_NOT_VALUE);
    }
    typed = new_node(TYPED_IDENTIFIER, ctx->expected_type, id->node->pos,
                     TYPED_NO_NODE, TYPED_NO_NODE);
    typed.name_index = checker_name(ctx, id->name);
    typed.store_id = store == NULL ? TYPED_NO_NODE : store->id;
    return identifier_step(emit_node(ctx, &typed, id->ast_index, &(size_t){0}));
}

/* A union variable that an enclosing test narrowed reads as the narrower
   value. */
static bool emit_narrowed_identifier(const Identifier *id,
                                     const VariableEntry *variable,
                                     const Narrowing *narrowing)
{
    CheckContext *ctx = id->ctx;
    TypedNode base = new_node(TYPED_IDENTIFIER, variable->type_index,
                              id->node->pos, TYPED_NO_NODE, TYPED_NO_NODE);
    size_t base_index;
    size_t narrowed;

    base.name_index = checker_name(ctx, id->name);
    base.is_self = variable->is_self_pointer;
    if (!checker_push_node(ctx, &base, &base_index,
                           MESSAGE_CHECKER_OUT_OF_MEMORY_PROGRAM) ||
        !emit_narrowed_value(ctx, base_index, narrowing, &narrowed)) {
        return false;
    }
    *mapping_slot(ctx->mapping, id->ast_index) = narrowed;
    return true;
}

static IdentifierStep identifier_variable(const Identifier *id)
{
    CheckContext *ctx = id->ctx;
    const VariableEntry *variable = lookup_variable(ctx, id->name);
    const TypedType *declared;
    const Narrowing *narrowing;
    TypedNode typed;

    if (variable == NULL) {
        return ID_NEXT;
    }
    declared = typed_program_type(ctx->program, variable->type_index);
    narrowing = declared != NULL && declared->kind == NYX_TYPE_UNION
                    ? prior_narrowing(ctx, id->name, false, 0)
                    : NULL;
    if (narrowing != NULL) {
        return identifier_step(emit_narrowed_identifier(id, variable, narrowing));
    }
    typed = new_node(TYPED_IDENTIFIER, variable->type_index, id->node->pos,
                     TYPED_NO_NODE, TYPED_NO_NODE);
    typed.name_index = checker_name(ctx, id->name);
    typed.is_self = variable->is_self_pointer;
    return identifier_step(emit_node(ctx, &typed, id->ast_index, &(size_t){0}));
}

/* A function named as a value. */
static IdentifierStep identifier_function(const Identifier *id)
{
    CheckContext *ctx = id->ctx;
    const FunctionEntry *function = find_function(ctx, id->name);
    TypedNode typed;

    if (function == NULL) {
        return ID_NEXT;
    }
    if (function->generic) {
        return reject_identifier(id, MESSAGE_CHECKER_GENERIC_AS_VALUE);
    }
    typed = new_node(TYPED_IDENTIFIER, function->type_index, id->node->pos,
                     TYPED_NO_NODE, TYPED_NO_NODE);
    typed.name_index = checker_name(ctx, id->name);
    return identifier_step(emit_node(ctx, &typed, id->ast_index, &(size_t){0}));
}

static IdentifierStep identifier_global_in_function(const Identifier *id)
{
    if (!id->ctx->in_function ||
        find_variable(id->ctx->globals, id->name) == NULL) {
        return ID_NEXT;
    }
    return reject_identifier(id, MESSAGE_CHECKER_GLOBAL_IN_FUNCTION);
}

/* Type names that are not values. */
static const char *const type_value_names[] = {
    "Int32", "Bool", "Nil", "String", "Text", "TextBuilder", "List", "Array",
    "Slice", "Range", "Error",
};

static IdentifierStep identifier_type(const Identifier *id)
{
    size_t i;
    bool is_type = find_type(id->ctx->types, id->name) != NULL;

    for (i = 0; !is_type && i < NYX_COUNT_OF(type_value_names); i++) {
        is_type = strcmp(id->name, type_value_names[i]) == 0;
    }
    return is_type ? reject_identifier(id, MESSAGE_CHECKER_TYPE_AS_VALUE)
                   : ID_NEXT;
}

static IdentifierStep identifier_unknown(const Identifier *id)
{
    return reject_identifier(id, MESSAGE_CHECKER_UNKNOWN_NAME);
}

static const IdentifierRule identifier_rules[] = {
    identifier_needs_name, identifier_resource, identifier_store,
    identifier_variable, identifier_function, identifier_global_in_function,
    identifier_type, identifier_unknown,
};

bool check_identifier(CheckContext *ctx, const AstNode *node, size_t ast_index)
{
    Identifier id = {ctx, node, ast_symbol_text(ctx->ast, node->name_index),
                     ast_index};
    size_t i;

    for (i = 0; i < NYX_COUNT_OF(identifier_rules); i++) {
        IdentifierStep step = identifier_rules[i](&id);

        if (step != ID_NEXT) {
            return step == ID_ACCEPTED;
        }
    }
    return false;
}


typedef struct {
    NyxOperator op;
    const char *text;
} OperatorSpelling;

static const char *spelling_of(const OperatorSpelling *table, size_t count,
                               NyxOperator op, const char *fallback)
{
    size_t i;

    for (i = 0; i < count; i++) {
        if (table[i].op == op) {
            return table[i].text;
        }
    }
    return fallback;
}

const char *operator_text(NyxOperator op)
{
    static const OperatorSpelling spellings[] = {
        {NYX_OPERATOR_ADD, "+"}, {NYX_OPERATOR_SUB, "-"},
        {NYX_OPERATOR_MUL, "*"}, {NYX_OPERATOR_DIV, "/"},
        {NYX_OPERATOR_MOD, "%"}, {NYX_OPERATOR_BIT_AND, "&"},
        {NYX_OPERATOR_BIT_OR, "|"}, {NYX_OPERATOR_BIT_XOR, "^"},
        {NYX_OPERATOR_SHL, "<<"}, {NYX_OPERATOR_SHR, ">>"},
        {NYX_OPERATOR_NEG, "-"}, {NYX_OPERATOR_BIT_NOT, "~"},
        {NYX_OPERATOR_EQ, "=="}, {NYX_OPERATOR_STRICT_EQ, "==="},
        {NYX_OPERATOR_NE, "!="}, {NYX_OPERATOR_LT, "<"},
        {NYX_OPERATOR_LE, "<="}, {NYX_OPERATOR_GT, ">"},
        {NYX_OPERATOR_GE, ">="}, {NYX_OPERATOR_AND, "&&"},
        {NYX_OPERATOR_OR, "||"}, {NYX_OPERATOR_NOT, "!"},
    };

    return spelling_of(spellings, NYX_COUNT_OF(spellings), op, "?");
}

/* How a type is spelled in a message. A plain type is just its name; an applied
   type is Name<part, part>, with its length appended for Array and Record; a
   union lists its alternatives. */
typedef enum { FORM_PLAIN, FORM_APPLIED, FORM_ALTERNATIVES } NameForm;

typedef struct {
    const char *name;
    NameForm form;
    bool length;
} TypeName;

static const TypeName type_names[] = {
    [NYX_TYPE_INT8] = {"Int8"}, [NYX_TYPE_INT16] = {"Int16"},
    [NYX_TYPE_INT32] = {"Int32"}, [NYX_TYPE_INT64] = {"Int64"},
    [NYX_TYPE_BYTE] = {"Byte"}, [NYX_TYPE_UINT16] = {"Uint16"},
    [NYX_TYPE_UINT32] = {"Uint32"}, [NYX_TYPE_UINT64] = {"Uint64"},
    [NYX_TYPE_FLOAT32] = {"Float32"}, [NYX_TYPE_FLOAT64] = {"Float64"},
    [NYX_TYPE_BOOL] = {"Bool"}, [NYX_TYPE_NIL] = {"Nil"},
    [NYX_TYPE_DYNAMIC_STRING] = {"Text"},
    [NYX_TYPE_STRING_LITERAL] = {MESSAGE_FRAGMENT_STRING_LITERAL},
    [NYX_TYPE_FUNCTION] = {MESSAGE_FRAGMENT_FUNCTION}, [NYX_TYPE_ERROR] = {"Error"},
    [NYX_TYPE_HANDLE] = {"Handle", FORM_APPLIED},
    [NYX_TYPE_FIXED_LIST] = {"Array", FORM_APPLIED, true},
    [NYX_TYPE_LIST] = {"List", FORM_APPLIED},
    [NYX_TYPE_SLICE] = {"Slice", FORM_APPLIED},
    [NYX_TYPE_RANGE] = {"Range", FORM_APPLIED},
    [NYX_TYPE_RECORD] = {"Record", FORM_APPLIED, true},
    [NYX_TYPE_TABLE] = {"Table", FORM_APPLIED},
    [NYX_TYPE_UNION] = {MESSAGE_FRAGMENT_UNION, FORM_ALTERNATIVES},
    [NYX_TYPE_STRUCT] = {NULL, FORM_APPLIED},
};

static const TypeName *type_name_entry(const TypedType *type)
{
    static const TypeName unnamed = {MESSAGE_FRAGMENT_VALUE, FORM_PLAIN, false};

    if ((size_t)type->kind >= NYX_COUNT_OF(type_names) ||
        (type_names[type->kind].name == NULL &&
         type->kind != NYX_TYPE_STRUCT)) {
        return &unnamed;
    }
    return &type_names[type->kind];
}

static const char *type_head(const TypedProgram *program,
                             const TypedType *type, const TypeName *entry)
{
    const char *name;

    if (type->kind != NYX_TYPE_STRUCT) {
        return entry->name;
    }
    name = typed_program_symbol_text(program, type->source_name_index);
    return name == NULL ? MESSAGE_FRAGMENT_STRUCT : name;
}

static void write_type_name(const TypedProgram *program, size_t index,
                            char *out, size_t size, unsigned depth);

/* The parts of an applied type or the alternatives of a union. */
static void write_type_parts(const TypedProgram *program, const TypedType *type,
                             const TypeName *entry, char *out, size_t size,
                             size_t *used, unsigned depth)
{
    size_t i;

    for (i = 0; i < type->child_count; i++) {
        char part[128];

        if (i != 0) {
            type_ast_append(entry->form == FORM_APPLIED ? ", " : " | ", out,
                            size, used);
        }
        write_type_name(program,
                        *(const size_t *)list_get(&program->type_children,
                                                  type->first_child + i),
                        part, sizeof(part), depth - 1);
        type_ast_append(part, out, size, used);
    }
}

/* A type written as one word: its name, or String<n> for a fixed string. */
static void write_plain_type_name(const TypedProgram *program,
                                  const TypedType *type, char *out, size_t size)
{
    if (type->kind == NYX_TYPE_STRING) {
        snprintf(out, size, "String<%zu>", type->fixed_length);
        return;
    }
    snprintf(out, size, "%s", type_head(program, type, type_name_entry(type)));
}

/* Name<part, part> for an applied type, with its length appended for Array
   and Record; alternatives joined by | for a union. */
static void write_compound_type_name(const TypedProgram *program,
                                     const TypedType *type, const TypeName *entry,
                                     char *out, size_t size, unsigned depth)
{
    bool applied = entry->form == FORM_APPLIED;
    char length[32];
    size_t used = 0;

    out[0] = '\0';
    if (applied) {
        type_ast_append(type_head(program, type, entry), out, size, &used);
        type_ast_append("<", out, size, &used);
    }
    write_type_parts(program, type, entry, out, size, &used, depth);
    if (applied) {
        snprintf(length, sizeof(length), entry->length ? ", %zu>" : ">",
                 type->fixed_length);
        type_ast_append(length, out, size, &used);
    }
}

static void write_type_name(const TypedProgram *program, size_t index,
                            char *out, size_t size, unsigned depth)
{
    const TypedType *type = typed_program_type(program, index);
    const TypeName *entry;

    if (type == NULL) {
        snprintf(out, size, MESSAGE_FRAGMENT_VALUE);
        return;
    }
    entry = type_name_entry(type);
    if (type->kind == NYX_TYPE_STRING || entry->form == FORM_PLAIN ||
        depth == 0 || type->child_count == 0) {
        write_plain_type_name(program, type, out, size);
        return;
    }
    write_compound_type_name(program, type, entry, out, size, depth);
}

static void diagnostic_type_name(const TypedProgram *program, size_t index,
                                 char *out, size_t size)
{
    write_type_name(program, index, out, size, NYX_TYPE_DEPTH);
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

    if (!add_builtin_type(ctx, NYX_TYPE_BOOL, &bool_type)) {
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

/* A binary expression being checked. The operands may be replaced as literals
   adapt to their context, so the nodes and types are reloaded after each
   replacement rather than held across one. */
typedef struct {
    CheckContext *ctx;
    const AstNode *node;
    size_t ast_index;
    size_t operand_context; /* a numeric type the operands should adopt */
    size_t left;
    size_t right;
    const TypedNode *left_node;
    const TypedNode *right_node;
    const TypedType *left_type;
    const TypedType *right_type;
} Binary;

typedef bool (*OperandRule)(const Binary *binary);

/* Properties of an operator that the phases below consult. */
enum {
    OP_COMPARISON = 1, /* ==, !=, <, <=, >, >=: never widens its operands */
    OP_EQUALITY = 2,   /* ==, !=, ===: may compare strings and records */
    OP_ARITHMETIC = 4, /* passes the expected type down to its operands */
    OP_SHIFT = 8,      /* a constant count must be less than the width */
    OP_DIVISOR = 16    /* a constant divisor must not be zero */
};

typedef struct {
    NyxOperator op;
    OperandRule operands_ok;
    bool yields_bool; /* otherwise the result has the operand type */
    unsigned flags;
} BinaryOperator;

static bool binary_same_types(const Binary *binary)
{
    return same_type(binary->ctx->program, binary->left_node->type_index,
                     binary->right_node->type_index);
}

static bool operands_numeric(const Binary *binary)
{
    return binary_same_types(binary) &&
           nyx_type_is_numeric(binary->left_type->kind);
}

static bool operands_integer(const Binary *binary)
{
    return binary_same_types(binary) &&
           nyx_type_is_integer(binary->left_type->kind);
}

static bool is_text_kind(NyxType kind)
{
    return kind == NYX_TYPE_STRING || kind == NYX_TYPE_DYNAMIC_STRING;
}

static bool operands_comparable(const Binary *binary)
{
    return (nyx_type_is_numeric(binary->left_type->kind) &&
            nyx_type_is_numeric(binary->right_type->kind)) ||
           (is_text_kind(binary->left_type->kind) &&
            is_text_kind(binary->right_type->kind)) ||
           (binary_same_types(binary) &&
            equality_type_supported(binary->ctx->program,
                                    binary->left_node->type_index,
                                    NYX_TYPE_DEPTH));
}

static bool operands_strictly_comparable(const Binary *binary)
{
    return equality_type_supported(binary->ctx->program,
                                   binary->left_node->type_index,
                                   NYX_TYPE_DEPTH) &&
           equality_type_supported(binary->ctx->program,
                                   binary->right_node->type_index,
                                   NYX_TYPE_DEPTH);
}

static bool operands_bool(const Binary *binary)
{
    return binary->left_type->kind == NYX_TYPE_BOOL &&
           binary->right_type->kind == NYX_TYPE_BOOL;
}

static const BinaryOperator binary_operators[] = {
    {NYX_OPERATOR_ADD, operands_numeric, false, OP_ARITHMETIC},
    {NYX_OPERATOR_SUB, operands_numeric, false, OP_ARITHMETIC},
    {NYX_OPERATOR_MUL, operands_numeric, false, OP_ARITHMETIC},
    {NYX_OPERATOR_DIV, operands_numeric, false, OP_ARITHMETIC | OP_DIVISOR},
    {NYX_OPERATOR_MOD, operands_integer, false, OP_ARITHMETIC | OP_DIVISOR},
    {NYX_OPERATOR_BIT_AND, operands_integer, false, OP_ARITHMETIC},
    {NYX_OPERATOR_BIT_OR, operands_integer, false, OP_ARITHMETIC},
    {NYX_OPERATOR_BIT_XOR, operands_integer, false, OP_ARITHMETIC},
    {NYX_OPERATOR_SHL, operands_integer, false, OP_ARITHMETIC | OP_SHIFT},
    {NYX_OPERATOR_SHR, operands_integer, false, OP_ARITHMETIC | OP_SHIFT},
    {NYX_OPERATOR_LT, operands_numeric, true, OP_COMPARISON},
    {NYX_OPERATOR_LE, operands_numeric, true, OP_COMPARISON},
    {NYX_OPERATOR_GT, operands_numeric, true, OP_COMPARISON},
    {NYX_OPERATOR_GE, operands_numeric, true, OP_COMPARISON},
    {NYX_OPERATOR_EQ, operands_comparable, true,
     OP_COMPARISON | OP_EQUALITY},
    {NYX_OPERATOR_NE, operands_comparable, true,
     OP_COMPARISON | OP_EQUALITY},
    {NYX_OPERATOR_STRICT_EQ, operands_strictly_comparable, true,
     OP_EQUALITY},
    {NYX_OPERATOR_AND, operands_bool, true, 0},
    {NYX_OPERATOR_OR, operands_bool, true, 0},
};

static const BinaryOperator *binary_operator(NyxOperator op)
{
    size_t i;

    for (i = 0; i < NYX_COUNT_OF(binary_operators); i++) {
        if (binary_operators[i].op == op) {
            return &binary_operators[i];
        }
    }
    return NULL;
}

static bool operator_has(NyxOperator op, unsigned flag)
{
    const BinaryOperator *info = binary_operator(op);

    return info != NULL && (info->flags & flag) != 0;
}

static bool binary_reload(Binary *binary)
{
    const TypedProgram *program = binary->ctx->program;

    binary->left_node = typed_program_node(program, binary->left);
    binary->right_node = typed_program_node(program, binary->right);
    if (binary->left_node == NULL || binary->right_node == NULL) {
        error_report(binary->ctx->errors, MESSAGE_CHECKER_MISSING_AST_NODE,
                     binary->node->left_child);
        return false;
    }
    binary->left_type = typed_program_type(program,
                                           binary->left_node->type_index);
    binary->right_type = typed_program_type(program,
                                            binary->right_node->type_index);
    return true;
}

/* The numeric type an arithmetic operator's operands should adopt, taken from
   the type its result is expected to have. */
static size_t binary_expected_context(const Binary *binary)
{
    const CheckContext *ctx = binary->ctx;
    const TypedType *expected;

    if (ctx->expected_ast != binary->ast_index ||
        !operator_has(binary->node->op, OP_ARITHMETIC)) {
        return TYPED_NO_NODE;
    }
    expected = typed_program_type(ctx->program, ctx->expected_type);
    return expected != NULL && nyx_type_is_numeric(expected->kind)
               ? ctx->expected_type
               : TYPED_NO_NODE;
}

/* The integer kind the whole expression is expected to have, or
   NYX_TYPE_UNKNOWN. */
static NyxType binary_expected_integer(const Binary *binary)
{
    const CheckContext *ctx = binary->ctx;
    const TypedType *expected;

    if (ctx->expected_ast != binary->ast_index) {
        return NYX_TYPE_UNKNOWN;
    }
    expected = typed_program_type(ctx->program, ctx->expected_type);
    return expected != NULL && nyx_type_is_integer(expected->kind)
               ? expected->kind
               : NYX_TYPE_UNKNOWN;
}

/* The right operand of `and`/`or` is checked knowing what the left proved. */
static bool binary_check_right(Binary *binary)
{
    CheckContext *ctx = binary->ctx;
    const AstNode *node = binary->node;
    size_t narrowing_start = ctx->narrowings->length;
    bool ok;

    if (node->op != NYX_OPERATOR_AND && node->op != NYX_OPERATOR_OR) {
        return check_expression_expected(ctx, node->right_child,
                                         binary->operand_context,
                                         &binary->right);
    }
    ok = (node->op == NYX_OPERATOR_AND
              ? push_if_narrowing(ctx, node->left_child)
              : push_else_narrowing(ctx, node->left_child)) &&
         check_expression_expected(ctx, node->right_child,
                                   binary->operand_context, &binary->right);
    ctx->narrowings->length = narrowing_start;
    return ok;
}

/* Comparing numbers must not let an out-of-range literal slip through. */
/* Equality between numbers must not let an out-of-range literal slip through:
   each side is typed from a default unless the other is a full-range Uint64. */
static bool check_numeric_equality_defaults(Binary *binary)
{
    CheckContext *ctx = binary->ctx;
    NumericConstant ignored;
    bool left_literal =
        numeric_constant_read(ctx->program, binary->left, false, &ignored);
    bool right_literal =
        numeric_constant_read(ctx->program, binary->right, false, &ignored);

    return ((binary->right_type->kind == NYX_TYPE_UINT64 && !right_literal) ||
            check_default_numeric(ctx, binary->left)) &&
           ((binary->left_type->kind == NYX_TYPE_UINT64 && !left_literal) ||
            check_default_numeric(ctx, binary->right));
}

/* Comparing numbers must not let an out-of-range literal slip through. */
static bool binary_check_defaults(Binary *binary)
{
    CheckContext *ctx = binary->ctx;
    bool left_numeric = nyx_type_is_numeric(binary->left_type->kind);
    bool right_numeric = nyx_type_is_numeric(binary->right_type->kind);

    if (!operator_has(binary->node->op, OP_EQUALITY)) {
        return true;
    }
    if (left_numeric && right_numeric) {
        return check_numeric_equality_defaults(binary);
    }
    return binary->node->op != NYX_OPERATOR_STRICT_EQ ||
           ((!left_numeric || check_default_numeric(ctx, binary->left)) &&
            (!right_numeric || check_default_numeric(ctx, binary->right)));
}

static bool binary_operands(Binary *binary)
{
    CheckContext *ctx = binary->ctx;

    binary->operand_context = binary_expected_context(binary);
    if (!check_expression_expected(ctx, binary->node->left_child,
                                   binary->operand_context, &binary->left) ||
        !binary_check_right(binary) || !binary_reload(binary)) {
        return false;
    }
    if (binary->left_type == NULL || binary->right_type == NULL) {
        error_report_at(ctx->errors, binary->node->pos,
                        MESSAGE_CHECKER_UNSUPPORTED_AST_NODE);
        return false;
    }
    return binary_check_defaults(binary);
}

/* Which operands are numeric literals, and their values. */
typedef struct {
    NumericConstant left;
    NumericConstant right;
    bool left_literal;
    bool right_literal;
} Literals;

static Literals binary_literals(const Binary *binary)
{
    Literals found;

    found.left_literal = numeric_constant_read(
        binary->ctx->program, binary->left, false, &found.left);
    found.right_literal = numeric_constant_read(
        binary->ctx->program, binary->right, false, &found.right);
    return found;
}

typedef enum { ADAPT_SKIP, ADAPT_DONE, ADAPT_FAILED } Adapt;

typedef Adapt (*Adapter)(Binary *binary, const Literals *found);

static void report_unless_reported(Binary *binary, size_t before)
{
    if (binary->ctx->errors->length == before) {
        report_binary_operator_type(binary->ctx, binary->node, binary->left,
                                    binary->right);
    }
}

/* Two integer literals use the widest default that can hold them. */
/* Both operands are literals of one sort: integers, or floats. */
static bool both_literals(const Literals *found, bool floats)
{
    return found->left_literal && found->right_literal &&
           found->left.is_float == floats && found->right.is_float == floats;
}

/* The widest of the integer types the operands already have. */
static NyxType widest_integer_kind(const Binary *binary)
{
    NyxType left = binary->left_type->kind;
    NyxType right = binary->right_type->kind;

    if (left == NYX_TYPE_UINT64 || right == NYX_TYPE_UINT64) {
        return NYX_TYPE_UINT64;
    }
    return left == NYX_TYPE_INT64 || right == NYX_TYPE_INT64 ? NYX_TYPE_INT64
                                                              : NYX_TYPE_INT32;
}

/* Give both operands one type. */
static Adapt adapt_both_to(Binary *binary, NyxType kind)
{
    CheckContext *ctx = binary->ctx;
    size_t common_type;
    size_t left;
    size_t right;

    if (!add_builtin_type(ctx, kind, &common_type) ||
        !inject_into(ctx, binary->left, common_type, &left) ||
        !inject_into(ctx, binary->right, common_type, &right)) {
        if (!ctx->errors->reported) {
            report_binary_operator_type(ctx, binary->node, binary->left,
                                        binary->right);
        }
        return ADAPT_FAILED;
    }
    binary->left = left;
    binary->right = right;
    return ADAPT_DONE;
}

/* Two integer literals use the widest default that can hold them, or the
   type the context asks for. */
static Adapt adapt_integer_pair(Binary *binary, const Literals *found)
{
    CheckContext *ctx = binary->ctx;
    NyxType expected = binary_expected_integer(binary);
    bool context = expected != NYX_TYPE_UNKNOWN &&
                   !operator_has(binary->node->op, OP_COMPARISON);

    if (!both_literals(found, false)) {
        return ADAPT_SKIP;
    }
    if (expected == NYX_TYPE_UNKNOWN &&
        (!check_default_numeric(ctx, binary->left) ||
         !check_default_numeric(ctx, binary->right))) {
        return ADAPT_FAILED;
    }
    return adapt_both_to(binary, context ? expected
                                         : widest_integer_kind(binary));
}

/* Two float literals take the float type the context asks for. */
/* Two float literals take the float type the context asks for. */
static Adapt adapt_float_pair(Binary *binary, const Literals *found)
{
    const TypedType *context =
        binary->operand_context == TYPED_NO_NODE
            ? NULL
            : typed_program_type(binary->ctx->program, binary->operand_context);
    size_t left;
    size_t right;

    if (!both_literals(found, true) || context == NULL ||
        !nyx_type_is_float(context->kind)) {
        return ADAPT_SKIP;
    }
    if (!inject_into(binary->ctx, binary->left, binary->operand_context,
                     &left) ||
        !inject_into(binary->ctx, binary->right, binary->operand_context,
                     &right)) {
        return ADAPT_FAILED;
    }
    binary->left = left;
    binary->right = right;
    return ADAPT_DONE;
}

/* A literal beside a typed number adopts that number's type: `x + -1` types
   the 1 from x. */
/* Whether `left_side` is a literal beside something that is not one, and the
   other side is a number. */
static bool literal_beside_number(const Binary *binary, const Literals *found,
                                  bool left_side)
{
    bool here = left_side ? found->left_literal : found->right_literal;
    bool there = left_side ? found->right_literal : found->left_literal;
    const TypedType *other = left_side ? binary->right_type : binary->left_type;

    return here && !there && nyx_type_is_numeric(other->kind);
}

/* A literal beside a typed number adopts that number's type: `x + -1` types
   the 1 from x. */
static Adapt adapt_toward_other(Binary *binary, const Literals *found,
                                bool left_side)
{
    const TypedNode *other_node =
        left_side ? binary->right_node : binary->left_node;
    size_t *value = left_side ? &binary->left : &binary->right;
    size_t before = binary->ctx->errors->length;
    size_t coerced;

    if (!literal_beside_number(binary, found, left_side)) {
        return ADAPT_SKIP;
    }
    if (!inject_into(binary->ctx, *value, other_node->type_index, &coerced)) {
        /* A range error is already reported; a kind mismatch such as an
           integer variable beside a float literal is not. */
        report_unless_reported(binary, before);
        return ADAPT_FAILED;
    }
    *value = coerced;
    return ADAPT_DONE;
}

static Adapt adapt_left_literal(Binary *binary, const Literals *found)
{
    return adapt_toward_other(binary, found, true);
}

static Adapt adapt_right_literal(Binary *binary, const Literals *found)
{
    return adapt_toward_other(binary, found, false);
}

/* Numeric literals are typed by context. The first adapter that applies wins. */
static const Adapter literal_adapters[] = {
    adapt_integer_pair, adapt_float_pair, adapt_left_literal,
    adapt_right_literal,
};

static bool binary_adapt_numbers(Binary *binary)
{
    Literals found;
    size_t i;

    if (operator_has(binary->node->op, OP_EQUALITY) ||
        (!nyx_type_is_numeric(binary->left_type->kind) &&
         !nyx_type_is_numeric(binary->right_type->kind))) {
        return true;
    }
    found = binary_literals(binary);
    for (i = 0; i < NYX_COUNT_OF(literal_adapters); i++) {
        Adapt result = literal_adapters[i](binary, &found);

        if (result == ADAPT_FAILED) {
            return false;
        }
        if (result == ADAPT_DONE) {
            break;
        }
    }
    return binary_reload(binary);
}

static bool literal_fits(const NumericConstant *constant, NyxType kind)
{
    if (constant->is_float) {
        return nyx_type_is_float(kind) &&
               float_constant_fits(kind, constant->real);
    }
    return nyx_type_is_integer(kind) &&
           integer_constant_fits(kind, constant->negative,
                                 constant->magnitude);
}

/* `===` keeps the types apart, so a literal is typed by the other side only
   when its value fits there. */
/* The literal side of a `===` that the other side can type, and the type: a
   literal beside a non-literal whose type holds its value. */
static bool strict_literal_side(Binary *binary, size_t **literal, size_t *target)
{
    Literals found = binary_literals(binary);

    if (found.left_literal && !found.right_literal &&
        literal_fits(&found.left, binary->right_type->kind)) {
        *literal = &binary->left;
        *target = binary->right_node->type_index;
        return true;
    }
    if (found.right_literal && !found.left_literal &&
        literal_fits(&found.right, binary->left_type->kind)) {
        *literal = &binary->right;
        *target = binary->left_node->type_index;
        return true;
    }
    return false;
}

/* `===` keeps the types apart, so a literal is typed by the other side only
   when its value fits there. */
static bool binary_adapt_strict(Binary *binary)
{
    size_t *literal;
    size_t target;
    size_t coerced;

    if (binary->node->op != NYX_OPERATOR_STRICT_EQ ||
        !nyx_type_is_numeric(binary->left_type->kind) ||
        !nyx_type_is_numeric(binary->right_type->kind) ||
        !strict_literal_side(binary, &literal, &target)) {
        return true;
    }
    if (!inject_into(binary->ctx, *literal, target, &coerced)) {
        return false;
    }
    *literal = coerced;
    return binary_reload(binary);
}

/* A string literal compared with text becomes that text's type when it fits,
   and Text otherwise. */
/* The type a string literal takes beside `other`: that string type when the
   literal fits, otherwise Text. */
static bool string_literal_target(Binary *binary, const TypedNode *literal_node,
                                  const TypedType *other_type,
                                  const TypedNode *other_node, size_t *target)
{
    bool fits = other_type->kind == NYX_TYPE_DYNAMIC_STRING ||
                (other_type->kind == NYX_TYPE_STRING &&
                 literal_node->string_length <= other_type->fixed_length);

    if (fits) {
        *target = other_node->type_index;
        return true;
    }
    return builtin_type(binary->ctx, NYX_TYPE_DYNAMIC_STRING, target);
}

/* A string literal whose type is still open to its context. */
static bool is_contextual_literal(const TypedType *type, const TypedNode *node)
{
    return type->kind == NYX_TYPE_STRING_LITERAL && !node->fixed_type;
}

/* A string literal compared with text becomes that text's type when it fits,
   and Text otherwise. */
static bool adapt_string_literal(Binary *binary, bool left_side)
{
    const TypedType *literal_type =
        left_side ? binary->left_type : binary->right_type;
    const TypedNode *literal_node =
        left_side ? binary->left_node : binary->right_node;
    const TypedType *other_type =
        left_side ? binary->right_type : binary->left_type;
    const TypedNode *other_node =
        left_side ? binary->right_node : binary->left_node;
    size_t *value = left_side ? &binary->left : &binary->right;
    size_t target;
    size_t coerced;

    if (!is_contextual_literal(literal_type, literal_node)) {
        return true;
    }
    if (!string_literal_target(binary, literal_node, other_type, other_node,
                               &target) ||
        !inject_into(binary->ctx, *value, target, &coerced)) {
        return false;
    }
    *value = coerced;
    return binary_reload(binary);
}

static bool binary_adapt_strings(Binary *binary)
{
    return !operator_has(binary->node->op, OP_EQUALITY) ||
           (adapt_string_literal(binary, true) &&
            adapt_string_literal(binary, false));
}

/* A constant shift count or divisor is checked here rather than at run time. */
/* A constant shift count is less than the width of what it shifts. */
static bool check_shift_count(const Binary *binary, int64_t value)
{
    NyxType kind = binary->left_type->kind;

    if (nyx_type_is_integer(kind) &&
        (value < 0 || (uint64_t)value >= nyx_type_bit_width(kind))) {
        error_report_at(binary->ctx->errors, binary->node->pos,
                        MESSAGE_CHECKER_SHIFT_RANGE);
        return false;
    }
    return true;
}

/* A constant shift count or divisor is checked here rather than at run time. */
static bool binary_check_constant_right(const Binary *binary)
{
    const AstNode *node = binary->node;
    bool constant = false;
    int64_t value = 0;

    /* The count or divisor is folded in its own type, so a constant that is
       out of range for Int32 but not for its type is not an error. */
    if (!const_int_eval_in(binary->ctx, node->right_child,
                           binary->right_type->kind, &value, &constant)) {
        return false;
    }
    if (constant && operator_has(node->op, OP_SHIFT) &&
        !check_shift_count(binary, value)) {
        return false;
    }
    if (constant && operator_has(node->op, OP_DIVISOR) && value == 0) {
        error_report_at(binary->ctx->errors, node->pos,
                        MESSAGE_CHECKER_ZERO_DIVISOR, operator_text(node->op));
        return false;
    }
    return true;
}

/* Decides whether the operands suit the operator and what the result is. */
static bool binary_classify(const Binary *binary, NyxType *result_kind)
{
    const BinaryOperator *info = binary_operator(binary->node->op);
    bool ok = info != NULL && info->operands_ok(binary);
    bool evaluated = false;
    int value;

    if (!ok) {
        report_binary_operator_type(binary->ctx, binary->node, binary->left,
                                    binary->right);
        return false;
    }
    *result_kind = info->yields_bool ? NYX_TYPE_BOOL : binary->left_type->kind;
    if (!binary_check_constant_right(binary)) {
        return false;
    }
    /* Folding the whole expression reports an overflow in a constant. */
    return *result_kind != NYX_TYPE_INT32 ||
           const_int_eval(binary->ctx, binary->ast_index, &value, &evaluated);
}

static bool check_binary_operands_and_emit(CheckContext *ctx,
                                           const AstNode *node,
                                           size_t ast_index)
{
    Binary binary = {ctx, node, ast_index, TYPED_NO_NODE, 0, 0,
                     NULL, NULL, NULL, NULL};
    TypedNode typed = {0};
    NyxType result_kind;
    size_t result_index;

    if (!binary_operands(&binary) || !binary_adapt_numbers(&binary) ||
        !binary_adapt_strict(&binary) || !binary_adapt_strings(&binary) ||
        !binary_classify(&binary, &result_kind) ||
        !add_builtin_type(ctx, result_kind, &result_index)) {
        return false;
    }
    typed.kind = TYPED_BINARY;
    typed.type_index = result_index;
    typed.pos = node->pos;
    typed.op = node->op;
    typed.name_index = TYPED_NO_NODE;
    typed.left_child = binary.left;
    typed.right_child = binary.right;
    return emit_node(ctx, &typed, ast_index, &(size_t){0});
}

/* The outermost binary expression owns the fold memo: while it is being
   checked, folding a node twice costs one fold, and when it ends every entry
   is stale at once because the epoch moves on. */
bool check_binary(CheckContext *ctx, const AstNode *node, size_t ast_index)
{
    bool ok;

    ctx->binary_depth++;
    ok = check_binary_operands_and_emit(ctx, node, ast_index);
    if (--ctx->binary_depth == 0) {
        ctx->fold_epoch++;
    }
    return ok;
}


/* A unary operator: what operand it accepts and what it yields. */
typedef bool (*UnaryRule)(CheckContext *ctx, size_t ast_index, size_t operand,
                          const TypedNode *operand_node,
                          const TypedType *operand_type, NyxType *result_kind);

static bool unary_not(CheckContext *ctx, size_t ast_index, size_t operand,
                      const TypedNode *operand_node,
                      const TypedType *operand_type, NyxType *result_kind)
{
    (void)ctx;
    (void)ast_index;
    (void)operand;
    (void)operand_node;
    *result_kind = NYX_TYPE_BOOL;
    return operand_type->kind == NYX_TYPE_BOOL;
}

static bool unary_bit_not(CheckContext *ctx, size_t ast_index, size_t operand,
                          const TypedNode *operand_node,
                          const TypedType *operand_type, NyxType *result_kind)
{
    bool context = ctx->expected_ast == ast_index &&
                   ctx->expected_type != TYPED_NO_NODE;

    (void)operand_node;
    *result_kind = operand_type->kind;
    return nyx_type_is_integer(operand_type->kind) &&
           (context || check_default_numeric(ctx, operand));
}

/* Negation: a signed integer or a float, or the literal 2^63 read as an Int64
   so that the most negative Int64 can be written. */
static bool unary_neg(CheckContext *ctx, size_t ast_index, size_t operand,
                      const TypedNode *operand_node,
                      const TypedType *operand_type, NyxType *result_kind)
{
    (void)ctx;
    (void)ast_index;
    (void)operand;
    *result_kind = operand_type->kind;
    if (nyx_type_is_signed_integer(operand_type->kind) ||
        nyx_type_is_float(operand_type->kind)) {
        return true;
    }
    *result_kind = NYX_TYPE_INT64;
    return operand_node->kind == TYPED_INTEGER_LITERAL &&
           operand_type->kind == NYX_TYPE_UINT64 &&
           operand_node->integer_value_u <= (1ULL << 63);
}

static const struct {
    NyxOperator op;
    UnaryRule rule;
} unary_rules[] = {
    {NYX_OPERATOR_NOT, unary_not},
    {NYX_OPERATOR_BIT_NOT, unary_bit_not},
    {NYX_OPERATOR_NEG, unary_neg},
};

static UnaryRule unary_rule_for(NyxOperator op)
{
    size_t i;

    for (i = 0; i < NYX_COUNT_OF(unary_rules); i++) {
        if (unary_rules[i].op == op) {
            return unary_rules[i].rule;
        }
    }
    return NULL;
}

/* The operand of a unary operator, accepted by the operator's rule. */
static bool check_unary_operand(CheckContext *ctx, UnaryRule rule,
                                size_t ast_index, size_t operand,
                                NyxType *result_kind)
{
    const TypedNode *node = typed_program_node(ctx->program, operand);
    const TypedType *type =
        node == NULL ? NULL : typed_program_type(ctx->program, node->type_index);

    return type != NULL &&
           rule(ctx, ast_index, operand, node, type, result_kind);
}

bool check_unary(CheckContext *ctx, const AstNode *node, size_t ast_index)
{
    UnaryRule rule = unary_rule_for(node->op);
    NyxType result_kind;
    size_t operand;
    size_t result_type;
    TypedNode typed;

    if (rule == NULL) {
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
    if (!check_unary_operand(ctx, rule, ast_index, operand, &result_kind)) {
        if (!ctx->errors->reported) {
            report_unary_operator_type(ctx, node, operand);
        }
        return false;
    }
    if (!add_builtin_type(ctx, result_kind, &result_type)) {
        return false;
    }
    typed = new_node(TYPED_UNARY, result_type, node->pos, operand, TYPED_NO_NODE);
    typed.op = node->op;
    return emit_node(ctx, &typed, ast_index, &(size_t){0});
}
