#include "generator_internal.h"


bool emit_children_expressions(const TypedProgram *program,
                               const TypedNode *node, TextBuffer *buffer,
                               ErrorState *errors)
{
    size_t i;

    for (i = 0; i < node->children_count; i++) {
        if (i > 0) {
            buffer_append(buffer, ", ");
        }
        if (!emit_expression(program,
                             *(const size_t *)list_get(&program->children,
                                                       node->children_start + i),
                             buffer, errors)) {
            return false;
        }
    }
    return true;
}

const char *binary_operator_text(NyxOperator op)
{
    static const struct {
        NyxOperator op;
        const char *text;
    } spellings[] = {
        {NYX_OPERATOR_EQ, "=="}, {NYX_OPERATOR_STRICT_EQ, "=="},
        {NYX_OPERATOR_NE, "!="}, {NYX_OPERATOR_LT, "<"},
        {NYX_OPERATOR_LE, "<="}, {NYX_OPERATOR_GT, ">"},
        {NYX_OPERATOR_GE, ">="}, {NYX_OPERATOR_ADD, "+"},
        {NYX_OPERATOR_SUB, "-"}, {NYX_OPERATOR_MUL, "*"},
        {NYX_OPERATOR_DIV, "/"}, {NYX_OPERATOR_MOD, "%"},
        {NYX_OPERATOR_BIT_AND, "&"}, {NYX_OPERATOR_BIT_OR, "|"},
        {NYX_OPERATOR_BIT_XOR, "^"}, {NYX_OPERATOR_SHL, "<<"},
        {NYX_OPERATOR_SHR, ">>"}, {NYX_OPERATOR_NEG, "-"},
        {NYX_OPERATOR_BIT_NOT, "~"}, {NYX_OPERATOR_AND, "&&"},
        {NYX_OPERATOR_OR, "||"},
    };
    size_t i;

    for (i = 0; i < NYX_COUNT_OF(spellings); i++) {
        if (spellings[i].op == op) {
            return spellings[i].text;
        }
    }
    return NULL;
}

/* `open`, the two operands joined by `middle`, then `close`. */
bool emit_operands(const TypedProgram *program, const TypedNode *node,
                          TextBuffer *buffer, ErrorState *errors,
                          const char *open, const char *middle,
                          const char *close)
{
    bool ok;

    buffer_append(buffer, open);
    ok = emit_expression(program, node->left_child, buffer, errors);
    buffer_append(buffer, middle);
    ok = ok && emit_expression(program, node->right_child, buffer, errors);
    buffer_append(buffer, close);
    return ok;
}

/* `open`, the one operand, then `close`. */
bool emit_operand(const TypedProgram *program, const TypedNode *node,
                         TextBuffer *buffer, ErrorState *errors,
                         const char *open, const char *close)
{
    bool ok;

    buffer_append(buffer, open);
    ok = emit_expression(program, node->left_child, buffer, errors);
    buffer_append(buffer, close);
    return ok;
}

/* The source position the runtime reports a failed check at. */
void position_text(const TypedNode *node, char *text, size_t size)
{
    snprintf(text, size, ", __FILE__, %zu, %zu, %zu", node->pos.line,
             node->pos.column, node->pos.offset);
}

typedef bool (*ExprEmitter)(const TypedProgram *program,
                            const TypedNode *node, TextBuffer *buffer,
                            ErrorState *errors);

static bool emit_match_temporary(const TypedProgram *program,
                                 const TypedNode *node, TextBuffer *buffer,
                                 ErrorState *errors)
{
    (void)program;
    (void)errors;
    return buffer_appendf(buffer, "nyx_match_%zu",
                          (size_t)node->integer_value_u);
}

/* The temporaries that sequence a call's arguments, bound as written. */
static bool emit_match_bind(const TypedProgram *program, const TypedNode *node,
                            TextBuffer *buffer, ErrorState *errors)
{
    bool ok;

    buffer_appendf(buffer, "(nyx_match_%zu = ", (size_t)node->integer_value_u);
    ok = emit_expression(program, node->left_child, buffer, errors);
    buffer_append(buffer, ", ");
    ok = ok && emit_expression(program, node->right_child, buffer, errors);
    buffer_append(buffer, ")");
    return ok;
}

static bool emit_cores(const TypedProgram *program, const TypedNode *node,
                       TextBuffer *buffer, ErrorState *errors)
{
    (void)program;
    (void)node;
    (void)errors;
    return buffer_append(buffer, "nyx_cores()");
}

static bool emit_parallel_result(const TypedProgram *program,
                                 const TypedNode *node, TextBuffer *buffer,
                                 ErrorState *errors)
{
    (void)program;
    (void)errors;
    return buffer_appendf(buffer, "nyx_par_%zu_v[0].result",
                          (size_t)node->integer_value_u);
}

/* A range: both bounds, and a step that defaults to one. */
static bool emit_range(const TypedProgram *program, const TypedNode *node,
                       TextBuffer *buffer, ErrorState *errors)
{
    size_t first = *(const size_t *)list_get(&program->children,
                                             node->children_start);
    size_t last = *(const size_t *)list_get(&program->children,
                                            node->children_start + 1);
    bool ok;

    buffer_appendf(buffer, "((nyx_range_%zu){ ", node->type_index);
    ok = emit_expression(program, first, buffer, errors);
    buffer_append(buffer, ", ");
    ok = ok && emit_expression(program, last, buffer, errors);
    buffer_append(buffer, ", ");
    if (node->children_count == 3) {
        buffer_append(buffer, "nyx_range_step(");
        ok = ok && emit_expression(program,
                                   *(const size_t *)list_get(
                                       &program->children,
                                       node->children_start + 2),
                                   buffer, errors);
        buffer_appendf(buffer, ", __FILE__, %zu, %zu, %zu)", node->pos.line,
                       node->pos.column, node->pos.offset);
    } else {
        buffer_append(buffer, "INT64_C(1)");
    }
    buffer_append(buffer, " })");
    return ok;
}

/* now() and sleep(ms) call the helpers emitted next to the range helper. They
   carry the source position so a runtime failure can name it. */
static bool emit_clock_expression(const TypedProgram *program,
                                  const TypedNode *node, TextBuffer *buffer,
                                  ErrorState *errors)
{
    bool ok = true;

    if (node->kind == TYPED_CLOCK_NOW) {
        buffer_append(buffer, "nyx_clock_now(");
    } else {
        buffer_append(buffer, "nyx_sleep(");
        ok = emit_expression(program, node->left_child, buffer, errors);
        buffer_append(buffer, ", ");
    }
    buffer_appendf(buffer, "__FILE__, %zu, %zu, %zu)", node->pos.line,
                   node->pos.column, node->pos.offset);
    return ok;
}

/* Atomic cells are static, so an operation names its cell directly. */
static bool emit_atomic_expression(const TypedProgram *program,
                                   const TypedNode *node, TextBuffer *buffer,
                                   ErrorState *errors)
{
    char type[NYX_IDENTIFIER_MAX + 32];
    bool store = node->kind == TYPED_ATOMIC_STORE;
    bool ok;

    if (node->kind == TYPED_ATOMIC_LOAD) {
        return buffer_appendf(buffer, "nyx_atomic_load(&nyx_atomic_%zu)",
                              node->store_id);
    }
    if (!format_c_type(program,
                       store ? typed_program_node(program, node->left_child)
                                   ->type_index
                             : node->type_index,
                       type, sizeof(type), errors)) {
        return false;
    }
    buffer_appendf(buffer,
                   store ? "(nyx_atomic_store(&nyx_atomic_%zu, (%s)("
                         : "nyx_atomic_add(&nyx_atomic_%zu, (%s)(",
                   node->store_id, type);
    ok = emit_expression(program, node->left_child, buffer, errors);
    buffer_append(buffer, store ? ")), (nyx_nil)0)" : "))");
    return ok;
}

static const struct {
    TypedNodeKind kind;
    ExprEmitter emit;
} direct_emitters[] = {
    {TYPED_MATCH_TEMPORARY, emit_match_temporary},
    {TYPED_MATCH_BIND_EXPRESSION, emit_match_bind},
    {TYPED_WHEN_RESULT, emit_when_result},
    {TYPED_CHANNEL_OP, emit_channel_op_expression},
    {TYPED_CHANNEL_CREATE, emit_channel_create_expression},
    {TYPED_CORES, emit_cores},
    {TYPED_PARALLEL_RESULT, emit_parallel_result},
    {TYPED_ATOMIC_LOAD, emit_atomic_expression},
    {TYPED_ATOMIC_STORE, emit_atomic_expression},
    {TYPED_ATOMIC_ADD, emit_atomic_expression},
    {TYPED_CLOCK_NOW, emit_clock_expression},
    {TYPED_SLEEP, emit_clock_expression},
    {TYPED_RANGE_CREATE, emit_range},
};

/* The families of kinds other modules write. Each sets `handled` when the node
   is one of its own. */
typedef bool (*FamilyEmitter)(const TypedProgram *program, const TypedNode *node,
                              size_t node_index, TextBuffer *buffer,
                              ErrorState *errors, bool *handled);

static bool family_literals(const TypedProgram *program, const TypedNode *node,
                            size_t index, TextBuffer *buffer,
                            ErrorState *errors, bool *handled)
{
    (void)index;
    return emit_expr_literals(program, node, buffer, errors, handled);
}

static bool family_strings(const TypedProgram *program, const TypedNode *node,
                           size_t index, TextBuffer *buffer, ErrorState *errors,
                           bool *handled)
{
    return emit_expr_strings(program, node, index, buffer, errors, handled);
}

static bool family_collections(const TypedProgram *program,
                               const TypedNode *node, size_t index,
                               TextBuffer *buffer, ErrorState *errors,
                               bool *handled)
{
    return emit_expr_collections(program, node, index, buffer, errors, handled);
}

static bool family_access(const TypedProgram *program, const TypedNode *node,
                          size_t index, TextBuffer *buffer, ErrorState *errors,
                          bool *handled)
{
    (void)index;
    return emit_expr_access(program, node, buffer, errors, handled);
}

static bool family_operators(const TypedProgram *program,
                             const TypedNode *node, size_t index,
                             TextBuffer *buffer, ErrorState *errors,
                             bool *handled)
{
    (void)index;
    return emit_expr_operators(program, node, buffer, errors, handled);
}

static const FamilyEmitter families[] = {
    family_literals, family_strings, family_collections, family_access,
    family_operators,
};

bool emit_expression(const TypedProgram *program, size_t node_index,
                     TextBuffer *buffer, ErrorState *errors)
{
    const TypedNode *node = typed_program_node(program, node_index);
    size_t i;

    for (i = 0; node != NULL && i < NYX_COUNT_OF(direct_emitters); i++) {
        if (direct_emitters[i].kind == node->kind) {
            return direct_emitters[i].emit(program, node, buffer, errors);
        }
    }
    for (i = 0; node != NULL && i < NYX_COUNT_OF(families); i++) {
        bool handled = false;

        if (!families[i](program, node, node_index, buffer, errors, &handled)) {
            return false;
        }
        if (handled) {
            return true;
        }
    }
    error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_EXPRESSION);
    return false;
}
