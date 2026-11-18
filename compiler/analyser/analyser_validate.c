#include "analyser.h"
#include "analyser_internal.h"

#include "messages.h"

#include <string.h>

/* Validation of the typed program's own structure: every index and run points
   inside its table, and every node and type has the shape its kind promises.

   Each invariant is a rule: a function that looks at one node (or type) and
   returns the diagnostic for what is wrong with it, or nothing. A table says
   which rules apply to which kind, so a new kind adds rows. The engine reports
   the first violation. */

typedef struct {
    const char *message; /* NULL when nothing is wrong */
    size_t argument;     /* what the message names */
} Violation;

static Violation bad(const char *message, size_t argument)
{
    return (Violation){message, argument};
}

static Violation holds(bool invariant, const char *message, size_t argument)
{
    return invariant ? (Violation){NULL, 0} : bad(message, argument);
}

static bool valid_run(size_t start, size_t count, size_t length)
{
    return start <= length && count <= length - start;
}

static bool index_in(size_t index, size_t limit)
{
    return index == TYPED_NO_NODE || index < limit;
}

static bool is_kind(const TypedNode *node, TypedNodeKind kind)
{
    return node != NULL && node->kind == kind;
}

static bool type_is(const TypedProgram *program, size_t index, NyxType kind)
{
    const TypedType *type = typed_program_type(program, index);

    return type != NULL && type->kind == kind;
}

static bool node_type_is(const TypedProgram *program, size_t node_index,
                         NyxType kind)
{
    const TypedNode *node = typed_program_node(program, node_index);

    return node != NULL && type_is(program, node->type_index, kind);
}

static bool is_block_or_none(const TypedProgram *program, size_t index)
{
    return index == TYPED_NO_NODE ||
           is_kind(typed_program_node(program, index), TYPED_BLOCK);
}

static bool atomic_element(NyxType kind)
{
    return kind == NYX_TYPE_BOOL || kind == NYX_TYPE_BYTE ||
           nyx_type_is_integer(kind);
}

/* Every child of the node is a node of the given kind. */
static bool children_are(const TypedProgram *program, const TypedNode *node,
                         TypedNodeKind kind)
{
    size_t i;

    for (i = 0; i < node->children_count; i++) {
        if (!is_kind(typed_program_node(
                         program, *(const size_t *)list_get(
                                      &program->children,
                                      node->children_start + i)),
                     kind)) {
            return false;
        }
    }
    return true;
}

/* A string literal's bytes lie inside the program's bytes. */
static bool bytes_in_range(const TypedProgram *program, const TypedNode *node)
{
    return node->string_offset <= program->bytes.length &&
           node->string_length <= program->bytes.length - node->string_offset;
}

typedef struct {
    const TypedProgram *program;
    const TypedNode *node;
    size_t index;
    size_t store_count;
} NodeCheck;

typedef Violation (*NodeRule)(const NodeCheck *c);

/* ---- links ---- */

static bool children_are_nodes(const TypedProgram *program,
                               const TypedNode *node)
{
    size_t j;

    for (j = 0; j < node->children_count; j++) {
        if (*(const size_t *)list_get(&program->children,
                                      node->children_start + j) >=
            program->nodes.length) {
            return false;
        }
    }
    return true;
}

static bool kinds_in_range(const TypedNode *n)
{
    return (size_t)n->kind < TYPED_NODE_KIND_COUNT &&
           (size_t)n->op < NYX_OPERATOR_COUNT;
}

static bool links_in_range(const TypedProgram *p, const TypedNode *n)
{
    return index_in(n->left_child, p->nodes.length) &&
           index_in(n->right_child, p->nodes.length) &&
           (n->children_count == 0 ||
            valid_run(n->children_start, n->children_count, p->children.length));
}

static Violation node_links(const NodeCheck *c)
{
    const TypedProgram *p = c->program;
    const TypedNode *n = c->node;

    if (!kinds_in_range(n) || !links_in_range(p, n)) {
        return bad(MESSAGE_ANALYSER_INVALID_NODE_LINK, c->index);
    }
    if (!children_are_nodes(p, n)) {
        return bad(MESSAGE_ANALYSER_INVALID_CHILD_INDEX, c->index);
    }
    if (!index_in(n->name_index, p->symbols.length)) {
        return bad(MESSAGE_ANALYSER_INVALID_NAME_INDEX, c->index);
    }
    if (!index_in(n->type_index, p->types.length)) {
        return bad(MESSAGE_ANALYSER_INVALID_TYPE_INDEX, c->index);
    }
    return holds(index_in(n->auxiliary_type, p->types.length),
                 MESSAGE_ANALYSER_INVALID_AUXILIARY_TYPE, c->index);
}

/* A binary operation has two operands that have types. */
static Violation binary_operands(const NodeCheck *c)
{
    const TypedProgram *p = c->program;

    return holds(typed_program_node_kind(p, c->node->left_child) !=
                         NYX_TYPE_KIND_COUNT &&
                     typed_program_node_kind(p, c->node->right_child) !=
                         NYX_TYPE_KIND_COUNT,
                 MESSAGE_ANALYSER_INVALID_NODE_LINK, c->index);
}

/* A match temporary, and the binding of one, agree with the temporary table. */
static Violation match_binding(const NodeCheck *c)
{
    const TypedProgram *p = c->program;
    const TypedNode *n = c->node;
    const TypedMatchTemporary *temporary =
        list_get(&p->match_temporaries, (size_t)n->integer_value_u);
    bool reference = n->kind == TYPED_MATCH_TEMPORARY;
    const TypedNode *value =
        reference ? n : typed_program_node(p, n->left_child);

    return holds(temporary != NULL && value != NULL &&
                     temporary->type_index == value->type_index &&
                     (reference ||
                      typed_program_node(p, n->right_child) != NULL),
                 MESSAGE_ANALYSER_INVALID_NODE_LINK, c->index);
}

/* A scope names a Store, a capacity (an Int32) and a body block. */
static Violation scope_shape(const NodeCheck *c)
{
    const TypedNode *n = c->node;
    const TypedProgram *p = c->program;

    return holds(n->name_index != TYPED_NO_NODE &&
                     type_is(p, n->type_index, NYX_TYPE_STORE) &&
                     node_type_is(p, n->left_child, NYX_TYPE_INT32) &&
                     is_kind(typed_program_node(p, n->right_child), TYPED_BLOCK),
                 MESSAGE_ANALYSER_INVALID_NODE_LINK, c->index);
}

static Violation if_expression_shape(const NodeCheck *c)
{
    return holds(c->node->children_count == 1 &&
                     c->node->left_child != TYPED_NO_NODE &&
                     c->node->right_child != TYPED_NO_NODE,
                 MESSAGE_ANALYSER_INVALID_NODE_LINK, c->index);
}

/* Slicing and element writes carry two children and a receiver. */
static Violation two_children(const NodeCheck *c)
{
    return holds(c->node->children_count == 2 &&
                     c->node->left_child != TYPED_NO_NODE,
                 MESSAGE_ANALYSER_INVALID_NODE_LINK, c->index);
}

/* ---- Stores ---- */

/* The nodes that take a Store name it (or, for a declaration, own an id). */
static Violation store_binding(const NodeCheck *c)
{
    const TypedNode *n = c->node;
    bool names_store = n->kind != TYPED_HANDLE_READ &&
                       n->kind != TYPED_HANDLE_WRITE;

    if (n->store_id != TYPED_NO_NODE ? n->store_id >= c->store_count
                                     : names_store &&
                                           (n->kind == TYPED_STORE_DECLARATION ||
                                            n->name_index == TYPED_NO_NODE)) {
        return bad(MESSAGE_ANALYSER_INVALID_STORE_ID, c->index);
    }
    return holds(typed_program_type(c->program, n->type_index) != NULL,
                 MESSAGE_ANALYSER_INVALID_TYPE_INDEX, c->index);
}

/* ---- strings, errors, numbers ---- */

static Violation string_literal(const NodeCheck *c)
{
    const TypedNode *n = c->node;
    const TypedType *type = typed_program_type(c->program, n->type_index);

    if (!bytes_in_range(c->program, n)) {
        return bad(MESSAGE_ANALYSER_INVALID_STRING_LITERAL, c->index);
    }
    if (type == NULL || (type->kind != NYX_TYPE_STRING &&
                         type->kind != NYX_TYPE_DYNAMIC_STRING &&
                         type->kind != NYX_TYPE_STRING_LITERAL)) {
        return bad(MESSAGE_ANALYSER_INVALID_TYPE_INDEX, c->index);
    }
    /* A literal whose type is fixed (base64) is always a Text. */
    return holds((type->kind != NYX_TYPE_STRING ||
                  n->string_length <= type->fixed_length) &&
                     (!n->fixed_type || type->kind == NYX_TYPE_DYNAMIC_STRING),
                 MESSAGE_ANALYSER_INVALID_STRING_LITERAL, c->index);
}

static Violation error_construct(const NodeCheck *c)
{
    return holds(type_is(c->program, c->node->type_index, NYX_TYPE_ERROR) &&
                     c->node->children_count == 6,
                 MESSAGE_ANALYSER_INVALID_ERROR, c->index);
}

static Violation error_throw(const NodeCheck *c)
{
    const TypedNode *n = c->node;

    return holds(type_is(c->program, n->type_index, NYX_TYPE_ERROR) &&
                     n->children_count == 2 &&
                     type_is(c->program, n->auxiliary_type, NYX_TYPE_STRING) &&
                     bytes_in_range(c->program, n),
                 MESSAGE_ANALYSER_INVALID_ERROR, c->index);
}

static Violation error_field(const NodeCheck *c)
{
    return holds(c->node->integer_value >= 0 && c->node->integer_value <= 5,
                 MESSAGE_ANALYSER_INVALID_ERROR, c->index);
}

/* The element type of a range type, or NYX_TYPE_KIND_COUNT when it is not one
   range of a single element. */
static NyxType range_element_kind(const TypedProgram *p, const TypedType *range)
{
    bool shaped = range != NULL && range->kind == NYX_TYPE_RANGE &&
                  range->child_count == 1 &&
                  range->first_child < p->type_children.length;
    const TypedType *element =
        shaped ? typed_program_type(
                     p, *(const size_t *)list_get(&p->type_children,
                                                  range->first_child))
               : NULL;

    return element == NULL ? NYX_TYPE_KIND_COUNT : element->kind;
}

/* The bounds are of the element type; the optional step is an Int64. */
static bool range_argument_ok(const TypedProgram *p, const TypedNode *n,
                              size_t j, NyxType element)
{
    NyxType kind = typed_program_node_kind(
        p, *(const size_t *)list_get(&p->children, n->children_start + j));

    return kind == (j < 2 ? element : NYX_TYPE_INT64);
}

/* A range holds two bounds of its element type and an optional Int64 step. */
static Violation range_create(const NodeCheck *c)
{
    const TypedProgram *p = c->program;
    const TypedNode *n = c->node;
    NyxType element =
        range_element_kind(p, typed_program_type(p, n->type_index));
    size_t j;
    bool ok = element != NYX_TYPE_KIND_COUNT &&
              (n->children_count == 2 || n->children_count == 3);

    for (j = 0; ok && j < n->children_count; j++) {
        ok = range_argument_ok(p, n, j, element);
    }
    return holds(ok, MESSAGE_ANALYSER_INVALID_TYPE_CHILD, n->type_index);
}

/* A literal has a numeric type its value can be stored in. A conversion
   converts a numeric child, whose type is the recorded source type, to a
   numeric target; a folded constant records its own target as the source. A
   negated literal carries its magnitude in the unsigned counterpart of its
   type, so a literal's magnitude is checked against that width. */
static bool integer_literal_fits(const TypedType *type, const TypedNode *node)
{
    unsigned width = nyx_type_bit_width(type->kind);

    return nyx_type_is_integer(type->kind) &&
           (width == 64 || node->integer_value_u < (1ULL << width));
}

static bool float_literal_fits(const TypedType *type, const TypedNode *node)
{
    return nyx_type_is_float(type->kind) && node->float_value == node->float_value &&
           node->float_value <= 1.7976931348623157e308 &&
           node->float_value >= -1.7976931348623157e308;
}

static bool conversion_is_valid(const TypedProgram *program,
                                const TypedNode *node, const TypedType *type)
{
    const TypedType *source = typed_program_type(program, node->auxiliary_type);
    const TypedNode *child = typed_program_node(program, node->left_child);
    const TypedType *child_type =
        child == NULL ? NULL : typed_program_type(program, child->type_index);

    return nyx_type_is_numeric(type->kind) && source != NULL &&
           nyx_type_is_numeric(source->kind) && child_type != NULL &&
           child_type->kind == source->kind;
}

static Violation numeric_node(const NodeCheck *c)
{
    const TypedNode *n = c->node;
    const TypedType *type = typed_program_type(c->program, n->type_index);
    bool ok = type != NULL;

    if (ok && n->kind == TYPED_INTEGER_LITERAL) {
        ok = integer_literal_fits(type, n);
    } else if (ok && n->kind == TYPED_FLOAT_LITERAL) {
        ok = float_literal_fits(type, n);
    } else if (ok) {
        ok = conversion_is_valid(c->program, n, type);
    }
    return holds(ok, MESSAGE_ANALYSER_INVALID_NUMERIC, c->index);
}

/* A sum injection or payload names an alternative of a union. */
static Violation sum_node(const NodeCheck *c)
{
    const TypedNode *n = c->node;
    const TypedType *type = typed_program_type(c->program, n->type_index);

    if (type == NULL || n->integer_value < 0) {
        return bad(MESSAGE_ANALYSER_INVALID_TYPE_INDEX, c->index);
    }
    return holds(n->kind != TYPED_SUM_INJECT ||
                     (type->kind == NYX_TYPE_UNION &&
                      (size_t)n->integer_value < type->child_count),
                 MESSAGE_ANALYSER_INVALID_TYPE_CHILD, n->type_index);
}

/* ---- clock, defer, atomics ---- */

/* now() is an Int64 with no operands; sleep(ms) is Nil with one Int64
   operand. */
static Violation clock_node(const NodeCheck *c)
{
    const TypedNode *n = c->node;
    bool sleep = n->kind == TYPED_SLEEP;

    if (!type_is(c->program, n->type_index, sleep ? NYX_TYPE_NIL : NYX_TYPE_INT64)) {
        return bad(MESSAGE_ANALYSER_INVALID_TYPE_INDEX, c->index);
    }
    return holds(n->right_child == TYPED_NO_NODE && n->children_count == 0 &&
                     (sleep ? node_type_is(c->program, n->left_child,
                                           NYX_TYPE_INT64)
                            : n->left_child == TYPED_NO_NODE),
                 MESSAGE_ANALYSER_INVALID_NODE_LINK, c->index);
}

/* A defer holds exactly one block and nothing else. */
static Violation defer_node(const NodeCheck *c)
{
    const TypedNode *n = c->node;

    return holds(is_kind(typed_program_node(c->program, n->left_child),
                         TYPED_BLOCK) &&
                     n->right_child == TYPED_NO_NODE && n->children_count == 0,
                 MESSAGE_ANALYSER_INVALID_NODE_LINK, c->index);
}

/* An Atomic cell holds a Bool or an integer; load takes nothing, store and add
   take one value of the cell's own type. */
static bool atomic_type_ok(const TypedNode *n, const TypedType *type)
{
    if (type == NULL) {
        return false;
    }
    if (n->kind == TYPED_ATOMIC_STORE) {
        return type->kind == NYX_TYPE_NIL;
    }
    return atomic_element(type->kind) &&
           (n->kind != TYPED_ATOMIC_ADD || type->kind != NYX_TYPE_BOOL);
}

static bool atomic_value_ok(const TypedProgram *p, const TypedNode *n,
                            const TypedType *type)
{
    const TypedNode *value = typed_program_node(p, n->left_child);
    const TypedType *value_type =
        value == NULL ? NULL : typed_program_type(p, value->type_index);

    if (value_type == NULL) {
        return false;
    }
    return n->kind == TYPED_ATOMIC_ADD ? value_type->kind == type->kind
                                       : atomic_element(value_type->kind);
}

static Violation atomic_node(const NodeCheck *c)
{
    const TypedNode *n = c->node;
    const TypedType *type = typed_program_type(c->program, n->type_index);
    bool takes_value = n->kind == TYPED_ATOMIC_STORE ||
                       n->kind == TYPED_ATOMIC_ADD;

    if (!atomic_type_ok(n, type)) {
        return bad(MESSAGE_ANALYSER_INVALID_TYPE_INDEX, c->index);
    }
    if (n->right_child != TYPED_NO_NODE || n->children_count != 0 ||
        (!takes_value && n->left_child != TYPED_NO_NODE)) {
        return bad(MESSAGE_ANALYSER_INVALID_NODE_LINK, c->index);
    }
    return holds(!takes_value || atomic_value_ok(c->program, n, type),
                 MESSAGE_ANALYSER_INVALID_NODE_LINK, c->index);
}

/* ---- parallel ---- */

/* The call a parallel arm makes, under the temporaries that order its
   arguments. */
const TypedNode *analyser_parallel_arm_call(const TypedProgram *program,
                                            const TypedNode *arm)
{
    const TypedNode *node = typed_program_node(program, arm->left_child);

    while (is_kind(node, TYPED_MATCH_BIND_EXPRESSION)) {
        node = typed_program_node(program, node->right_child);
    }
    return is_kind(node, TYPED_FUNCTION_CALL) ? node : NULL;
}

static Violation cores_node(const NodeCheck *c)
{
    return holds(type_is(c->program, c->node->type_index, NYX_TYPE_INT32),
                 MESSAGE_ANALYSER_INVALID_TYPE_INDEX, c->index);
}

/* A result names an arm that declares it. */
static Violation result_arm(const NodeCheck *c, TypedNodeKind kind, bool named)
{
    const TypedNode *arm =
        typed_program_node(c->program, (size_t)c->node->integer_value_u);

    return holds(is_kind(arm, kind) &&
                     (named ? arm->name_index != TYPED_NO_NODE
                            : arm->integer_value == 2),
                 MESSAGE_ANALYSER_INVALID_NODE_LINK, c->index);
}

static Violation parallel_result(const NodeCheck *c)
{
    return result_arm(c, TYPED_PARALLEL_ARM, false);
}

static Violation when_result(const NodeCheck *c)
{
    return result_arm(c, TYPED_WHEN_ARM, true);
}

/* An arm holds one call and, when repeated, two bounds and a lane name. */
static Violation parallel_arm(const NodeCheck *c)
{
    const TypedNode *n = c->node;
    bool lanes = n->children_count != 0;

    return holds(analyser_parallel_arm_call(c->program, n) != NULL &&
                     n->integer_value >= 0 && n->integer_value <= 2 &&
                     (!lanes || (n->children_count == 2 &&
                                 n->second_name_index != TYPED_NO_NODE &&
                                 n->integer_value != 2)) &&
                     (n->integer_value == 0 || n->name_index != TYPED_NO_NODE),
                 MESSAGE_ANALYSER_INVALID_NODE_LINK, c->index);
}

/* A parallel block holds arms, a pool when it names one, and optional setup and
   then blocks. */
static Violation parallel_node(const NodeCheck *c)
{
    const TypedNode *n = c->node;
    bool pooled = n->integer_value == 1;

    return holds(n->children_count != 0 &&
                     pooled == (n->left_child != TYPED_NO_NODE) &&
                     (!pooled || n->name_index != TYPED_NO_NODE) &&
                     is_block_or_none(c->program, n->setup_block) &&
                     is_block_or_none(c->program, n->right_child) &&
                     children_are(c->program, n, TYPED_PARALLEL_ARM),
                 MESSAGE_ANALYSER_INVALID_NODE_LINK, c->index);
}

/* ---- channels ---- */

/* The declaration of the global channel with that id. */
static const TypedNode *channel_declaration_of(const TypedProgram *program,
                                               size_t id)
{
    size_t i;

    for (i = 0; i < program->roots.length; i++) {
        const TypedNode *node = typed_program_node(
            program, *(const size_t *)list_get(&program->roots, i));

        if (is_kind(node, TYPED_CHANNEL_DECLARATION) && node->store_id == id) {
            return node;
        }
    }
    return NULL;
}

/* A channel's receive type is `element | Nil`: a union of two. */
static bool receive_type_ok(const TypedProgram *p, size_t index)
{
    const TypedType *optional = typed_program_type(p, index);

    return optional != NULL && optional->kind == NYX_TYPE_UNION &&
           optional->child_count == 2;
}

static bool element_type_ok(const TypedProgram *p, const TypedNode *n)
{
    const TypedType *type = typed_program_type(p, n->type_index);

    return type != NULL && type->kind != NYX_TYPE_NIL &&
           type->kind != NYX_TYPE_UNION &&
           (n->integer_value_u == 0 || type->kind == NYX_TYPE_INT64);
}

/* A channel declaration names a plain element and its `element | Nil` receive
   type. */
static Violation channel_declaration_node(const NodeCheck *c)
{
    const TypedNode *n = c->node;

    return holds(element_type_ok(c->program, n) &&
                     receive_type_ok(c->program, n->auxiliary_type) &&
                     n->integer_value >= 0 && n->left_child == TYPED_NO_NODE &&
                     n->right_child == TYPED_NO_NODE,
                 MESSAGE_ANALYSER_INVALID_TYPE_INDEX, c->index);
}

static Violation channel_create_node(const NodeCheck *c)
{
    const TypedProgram *p = c->program;
    const TypedNode *n = c->node;
    const TypedType *type = typed_program_type(p, n->type_index);

    return holds(type != NULL && type->kind == NYX_TYPE_CHANNEL &&
                     type->child_count == 1 &&
                     type_is(p, (size_t)n->integer_value_u, NYX_TYPE_UNION) &&
                     typed_program_node(p, n->left_child) != NULL,
                 MESSAGE_ANALYSER_INVALID_TYPE_INDEX, c->index);
}

/* What each operation yields, and whether it takes a value. */
static const struct {
    NyxType result;
    bool takes_value;
} channel_shapes[] = {
    [CHANNEL_OP_SEND] = {NYX_TYPE_BOOL, true},
    [CHANNEL_OP_RECV] = {NYX_TYPE_UNION, false},
    [CHANNEL_OP_TRY_SEND] = {NYX_TYPE_BOOL, true},
    [CHANNEL_OP_TRY_RECV] = {NYX_TYPE_UNION, false},
    [CHANNEL_OP_CLOSE] = {NYX_TYPE_NIL, false},
    [CHANNEL_OP_CLOSED] = {NYX_TYPE_BOOL, false},
};

/* A global channel names its declaration; a Channel value is the expression in
   right_child. */
static bool channel_operand_linked(const TypedProgram *p, const TypedNode *n)
{
    const TypedNode *declaration = channel_declaration_of(p, n->store_id);

    if (declaration != NULL) {
        return n->right_child == TYPED_NO_NODE;
    }
    return n->store_id == TYPED_NO_NODE &&
           node_type_is(p, n->right_child, NYX_TYPE_CHANNEL);
}

static Violation channel_op_node(const NodeCheck *c)
{
    const TypedProgram *p = c->program;
    const TypedNode *n = c->node;
    const TypedType *type = typed_program_type(p, n->type_index);

    if (!channel_operand_linked(p, n) || type == NULL || n->integer_value < 0 ||
        n->integer_value > CHANNEL_OP_CLOSED || n->children_count != 0) {
        return bad(MESSAGE_ANALYSER_INVALID_NODE_LINK, c->index);
    }
    return holds(type->kind == channel_shapes[n->integer_value].result &&
                     channel_shapes[n->integer_value].takes_value ==
                         (n->left_child != TYPED_NO_NODE) &&
                     (!channel_shapes[n->integer_value].takes_value ||
                      typed_program_node(p, n->left_child) != NULL),
                 MESSAGE_ANALYSER_INVALID_NODE_LINK, c->index);
}

/* A `when` holds arms (a send or receive and a block) and an optional else
   block. */
static Violation when_arm(const NodeCheck *c)
{
    const TypedNode *n = c->node;
    const TypedNode *operation = typed_program_node(c->program, n->left_child);

    return holds(is_kind(operation, TYPED_CHANNEL_OP) &&
                     (operation->integer_value == CHANNEL_OP_SEND ||
                      operation->integer_value == CHANNEL_OP_RECV) &&
                     is_kind(typed_program_node(c->program, n->right_child),
                             TYPED_BLOCK),
                 MESSAGE_ANALYSER_INVALID_NODE_LINK, c->index);
}

static Violation when_node(const NodeCheck *c)
{
    return holds(c->node->children_count != 0 &&
                     is_block_or_none(c->program, c->node->right_child) &&
                     children_are(c->program, c->node, TYPED_WHEN_ARM),
                 MESSAGE_ANALYSER_INVALID_NODE_LINK, c->index);
}

/* ---- the table ---- */

#define ANY_NODE TYPED_NODE_KIND_COUNT

static const struct {
    TypedNodeKind kind; /* ANY_NODE for every node */
    NodeRule rule;
} node_rules[] = {
    {ANY_NODE, node_links},
    {TYPED_BINARY, binary_operands},
    {TYPED_MATCH_TEMPORARY, match_binding},
    {TYPED_MATCH_BIND_EXPRESSION, match_binding},
    {TYPED_MATCH_BIND_STATEMENT, match_binding},
    {TYPED_SCOPE, scope_shape},
    {TYPED_IF_EXPRESSION, if_expression_shape},
    {TYPED_RECORD_SET, two_children},
    {TYPED_TABLE_SET, two_children},
    {TYPED_LIST_SET, two_children},
    {TYPED_SLICE_CREATE, two_children},
    {TYPED_STRING_SLICE, two_children},
    {TYPED_STORE_DECLARATION, store_binding},
    {TYPED_STORE_ALLOC, store_binding},
    {TYPED_HANDLE_READ, store_binding},
    {TYPED_HANDLE_WRITE, store_binding},
    {TYPED_STRING_CONCAT, store_binding},
    {TYPED_STRING_COPY, store_binding},
    {TYPED_STRING_FROM_FIXED, store_binding},
    {TYPED_BUILDER_CREATE, store_binding},
    {TYPED_LIST_CREATE, store_binding},
    {TYPED_CHANNEL_CREATE, store_binding},
    {TYPED_TABLE_CREATE, store_binding},
    {TYPED_STRING_LITERAL, string_literal},
    {TYPED_ERROR_CONSTRUCT, error_construct},
    {TYPED_ERROR_THROW, error_throw},
    {TYPED_ERROR_FIELD, error_field},
    {TYPED_RANGE_CREATE, range_create},
    {TYPED_INTEGER_LITERAL, numeric_node},
    {TYPED_FLOAT_LITERAL, numeric_node},
    {TYPED_NUMERIC_CONVERSION, numeric_node},
    {TYPED_SUM_INJECT, sum_node},
    {TYPED_SUM_PAYLOAD, sum_node},
    {TYPED_CLOCK_NOW, clock_node},
    {TYPED_SLEEP, clock_node},
    {TYPED_DEFER, defer_node},
    {TYPED_ATOMIC_DECLARATION, atomic_node},
    {TYPED_ATOMIC_LOAD, atomic_node},
    {TYPED_ATOMIC_STORE, atomic_node},
    {TYPED_ATOMIC_ADD, atomic_node},
    {TYPED_CORES, cores_node},
    {TYPED_PARALLEL_RESULT, parallel_result},
    {TYPED_PARALLEL_ARM, parallel_arm},
    {TYPED_PARALLEL, parallel_node},
    {TYPED_CHANNEL_DECLARATION, channel_declaration_node},
    {TYPED_CHANNEL_CREATE, channel_create_node},
    {TYPED_CHANNEL_OP, channel_op_node},
    {TYPED_WHEN_RESULT, when_result},
    {TYPED_WHEN_ARM, when_arm},
    {TYPED_WHEN, when_node},
};

static bool validate_node(const TypedProgram *program, ErrorState *errors,
                          size_t index, size_t store_count)
{
    const TypedNode *node = typed_program_node(program, index);
    NodeCheck check = {program, node, index, store_count};
    size_t i;

    if (node == NULL) {
        error_report(errors, MESSAGE_ANALYSER_INVALID_NODE_LINK, index);
        return false;
    }
    for (i = 0; i < NYX_COUNT_OF(node_rules); i++) {
        Violation violation;

        if (node_rules[i].kind != ANY_NODE && node_rules[i].kind != node->kind) {
            continue;
        }
        violation = node_rules[i].rule(&check);
        if (violation.message != NULL) {
            error_report_at(errors, node->pos, violation.message,
                            violation.argument);
            return false;
        }
    }
    return true;
}

/* ---- types ---- */

typedef struct {
    const TypedProgram *program;
    const TypedType *type;
    size_t index;
} TypeCheck;

typedef Violation (*TypeRule)(const TypeCheck *c);

/* Every part of the type is a type (or satisfies `ok`). */
static bool parts_are(const TypeCheck *c, bool (*ok)(const TypedProgram *, size_t))
{
    size_t j;

    for (j = 0; j < c->type->child_count; j++) {
        if (!ok(c->program, *(const size_t *)list_get(&c->program->type_children,
                                                      c->type->first_child + j))) {
            return false;
        }
    }
    return true;
}

static bool part_is_type(const TypedProgram *program, size_t part)
{
    return part < program->types.length;
}

static bool part_has_type(const TypedProgram *program, size_t part)
{
    return typed_program_type(program, part) != NULL;
}

static bool part_is_string(const TypedProgram *program, size_t part)
{
    return type_is(program, part, NYX_TYPE_STRING);
}

static bool part_is_integer(const TypedProgram *program, size_t part)
{
    const TypedType *type = typed_program_type(program, part);

    return type != NULL && nyx_type_is_integer(type->kind);
}

static bool run_in_range(const TypeCheck *c)
{
    return valid_run(c->type->first_child, c->type->child_count,
                     c->program->type_children.length);
}

static Violation string_type(const TypeCheck *c)
{
    return holds(c->type->fixed_length <=
                     (size_t)c->program->max_string_capacity,
                 MESSAGE_ANALYSER_INVALID_TYPE_CHILD, c->index);
}

/* Every field of a struct declaration has a type of the program. */
static bool fields_have_types(const TypedProgram *program,
                              const TypedNode *declaration)
{
    size_t i;

    for (i = 0; i < declaration->children_count; i++) {
        const TypedNode *field = typed_program_node(
            program, *(const size_t *)list_get(
                         &program->children, declaration->children_start + i));

        if (field->type_index >= program->types.length) {
            return false;
        }
    }
    return true;
}

static Violation struct_type(const TypeCheck *c)
{
    const TypedProgram *p = c->program;
    const TypedType *t = c->type;
    const TypedNode *declaration;

    if (t->name_index >= p->symbols.length ||
        !index_in(t->source_name_index, p->symbols.length)) {
        return bad(MESSAGE_ANALYSER_INVALID_NAME_INDEX, c->index);
    }
    declaration = t->declaration_node == TYPED_NO_NODE
                      ? NULL
                      : typed_program_node(p, t->declaration_node);
    if (!is_kind(declaration, TYPED_TYPE_DECLARATION)) {
        return bad(MESSAGE_ANALYSER_INVALID_DECLARATION, c->index);
    }
    return holds((t->child_count == 0 || run_in_range(c)) &&
                     parts_are(c, part_is_type) &&
                     children_are(p, declaration, TYPED_FIELD) &&
                     fields_have_types(p, declaration),
                 MESSAGE_ANALYSER_INVALID_TYPE_CHILD, c->index);
}

static Violation union_type(const TypeCheck *c)
{
    return holds(c->type->child_count >= 2 && run_in_range(c) &&
                     parts_are(c, part_is_type),
                 MESSAGE_ANALYSER_INVALID_TYPE_CHILD, c->index);
}

/* A list, array, slice or channel has one part; a record or table two. */
static Violation collection_type(const TypeCheck *c)
{
    size_t count = c->type->kind == NYX_TYPE_RECORD ||
                           c->type->kind == NYX_TYPE_TABLE
                       ? 2
                       : 1;

    return holds(c->type->child_count == count && run_in_range(c) &&
                     parts_are(c, part_has_type),
                 MESSAGE_ANALYSER_INVALID_TYPE_CHILD, c->index);
}

static Violation range_type_check(const TypeCheck *c)
{
    return holds(c->type->child_count == 1 && run_in_range(c) &&
                     parts_are(c, part_is_integer),
                 MESSAGE_ANALYSER_INVALID_TYPE_CHILD, c->index);
}

static Violation handle_type_check(const TypeCheck *c)
{
    return holds(c->type->child_count == 1 && run_in_range(c) &&
                     parts_are(c, part_is_type),
                 MESSAGE_ANALYSER_INVALID_HANDLE_TYPE, c->index);
}

/* An Error holds three strings: header, description and filename. */
static Violation error_type_check(const TypeCheck *c)
{
    return holds(c->type->child_count == 3 && run_in_range(c) &&
                     parts_are(c, part_is_string),
                 MESSAGE_ANALYSER_INVALID_TYPE_CHILD, c->index);
}

static Violation function_type(const TypeCheck *c)
{
    return holds(c->type->signature_index < c->program->signatures.length,
                 MESSAGE_ANALYSER_INVALID_SIGNATURE_INDEX, c->index);
}

static const struct {
    NyxType kind;
    TypeRule rule;
} type_rules[] = {
    {NYX_TYPE_STRING, string_type},
    {NYX_TYPE_STRUCT, struct_type},
    {NYX_TYPE_UNION, union_type},
    {NYX_TYPE_LIST, collection_type},
    {NYX_TYPE_FIXED_LIST, collection_type},
    {NYX_TYPE_SLICE, collection_type},
    {NYX_TYPE_RECORD, collection_type},
    {NYX_TYPE_CHANNEL, collection_type},
    {NYX_TYPE_TABLE, collection_type},
    {NYX_TYPE_RANGE, range_type_check},
    {NYX_TYPE_HANDLE, handle_type_check},
    {NYX_TYPE_ERROR, error_type_check},
    {NYX_TYPE_FUNCTION, function_type},
};

static bool validate_type(const TypedProgram *program, ErrorState *errors,
                          size_t index)
{
    const TypedType *type = typed_program_type(program, index);
    TypeCheck check = {program, type, index};
    size_t i;

    if (type == NULL || (size_t)type->kind >= NYX_TYPE_KIND_COUNT) {
        error_report(errors, MESSAGE_ANALYSER_INVALID_TYPE_INDEX, index);
        return false;
    }
    for (i = 0; i < NYX_COUNT_OF(type_rules); i++) {
        Violation violation;

        if (type_rules[i].kind != type->kind) {
            continue;
        }
        violation = type_rules[i].rule(&check);
        if (violation.message != NULL) {
            error_report(errors, violation.message, violation.argument);
            return false;
        }
    }
    return true;
}

/* ---- tables of the program ---- */

static bool validate_signature(const TypedProgram *program, ErrorState *errors,
                               size_t index)
{
    const TypedFunctionSignature *signature =
        typed_program_signature(program, index);
    size_t j;
    bool ok = signature != NULL &&
              signature->return_type < program->types.length &&
              valid_run(signature->first_parameter, signature->parameter_count,
                        program->signature_parameters.length);

    for (j = 0; ok && j < signature->parameter_count; j++) {
        ok = *(const size_t *)list_get(&program->signature_parameters,
                                       signature->first_parameter + j) <
             program->types.length;
    }
    if (!ok) {
        error_report(errors, MESSAGE_ANALYSER_INVALID_SIGNATURE_LINK, index);
    }
    return ok;
}

/* A match temporary chains only to an earlier one. */
static bool validate_match_temporaries(const TypedProgram *program,
                                       ErrorState *errors)
{
    size_t i;

    for (i = 0; i < program->match_temporaries.length; i++) {
        const TypedMatchTemporary *temporary =
            list_get(&program->match_temporaries, i);

        if (temporary == NULL ||
            typed_program_type(program, temporary->type_index) == NULL ||
            (temporary->next != TYPED_NO_NODE && temporary->next >= i)) {
            error_report(errors, MESSAGE_ANALYSER_INVALID_NODE_LINK, i);
            return false;
        }
    }
    return true;
}

static bool validate_match_scopes(const TypedProgram *program, ErrorState *errors)
{
    size_t i;

    for (i = 0; i < program->match_scopes.length; i++) {
        const size_t *head = list_get(&program->match_scopes, i);

        if (head == NULL ||
            (*head != TYPED_NO_NODE &&
             *head >= program->match_temporaries.length)) {
            error_report(errors, MESSAGE_ANALYSER_INVALID_NODE_LINK, i);
            return false;
        }
    }
    return true;
}

static bool validate_roots(const TypedProgram *program, ErrorState *errors)
{
    size_t i;

    for (i = 0; i < program->roots.length; i++) {
        size_t root = *(const size_t *)list_get(&program->roots, i);

        if (root >= program->nodes.length) {
            error_report(errors, MESSAGE_ANALYSER_ROOT_OUT_OF_RANGE, root);
            return false;
        }
    }
    return true;
}

static size_t count_store_roots(const TypedProgram *program)
{
    size_t count = 0;
    size_t i;

    for (i = 0; i < program->roots.length; i++) {
        count += is_kind(typed_program_node(
                             program, *(const size_t *)list_get(&program->roots, i)),
                         TYPED_STORE_DECLARATION);
    }
    return count;
}

static bool validate_all_nodes(const TypedProgram *program, ErrorState *errors,
                               size_t store_count)
{
    size_t i;

    for (i = 0; i < program->nodes.length; i++) {
        if (!validate_node(program, errors, i, store_count)) {
            return false;
        }
    }
    return true;
}

static bool validate_all_types(const TypedProgram *program, ErrorState *errors)
{
    size_t i;

    for (i = 0; i < program->types.length; i++) {
        if (!validate_type(program, errors, i)) {
            return false;
        }
    }
    return true;
}

static bool validate_all_signatures(const TypedProgram *program,
                                    ErrorState *errors)
{
    size_t i;

    for (i = 0; i < program->signatures.length; i++) {
        if (!validate_signature(program, errors, i)) {
            return false;
        }
    }
    return true;
}

bool analyser_validate(const TypedProgram *program, ErrorState *errors)
{
    size_t store_count = count_store_roots(program);

    return validate_match_temporaries(program, errors) &&
           validate_match_scopes(program, errors) &&
           validate_roots(program, errors) &&
           validate_all_nodes(program, errors, store_count) &&
           validate_all_types(program, errors) &&
           validate_all_signatures(program, errors) &&
           analyser_validate_stores(program, store_count, errors);
}
