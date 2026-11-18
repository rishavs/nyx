#include "typed_program.h"

#include "macros.h"

#include <string.h>

void typed_program_init(TypedProgram *program)
{
    list_init(&program->nodes, sizeof(TypedNode));
    list_init(&program->symbols, sizeof(TypedSymbol));
    name_index_init(&program->symbol_index);
    list_init(&program->roots, sizeof(size_t));
    list_init(&program->children, sizeof(size_t));
    list_init(&program->types, sizeof(TypedType));
    list_init(&program->signatures, sizeof(TypedFunctionSignature));
    list_init(&program->signature_parameters, sizeof(size_t));
    list_init(&program->type_children, sizeof(size_t));
    list_init(&program->bytes, sizeof(unsigned char));
    list_init(&program->match_temporaries, sizeof(TypedMatchTemporary));
    list_init(&program->match_scopes, sizeof(size_t));
    program->error_header_capacity = NYX_DEFAULT_ERROR_HEADER_CAPACITY;
    program->error_description_capacity =
        NYX_DEFAULT_ERROR_DESCRIPTION_CAPACITY;
    program->error_filename_capacity = NYX_DEFAULT_ERROR_FILENAME_CAPACITY;
    program->max_string_capacity = NYX_DEFAULT_MAX_STRING_CAPACITY;
    program->max_list_capacity = NYX_DEFAULT_MAX_LIST_CAPACITY;
    program->max_value_bytes = NYX_MAX_VALUE_BYTES;
    program->max_print_depth = NYX_DEFAULT_MAX_PRINT_DEPTH;
}

void typed_program_free(TypedProgram *program)
{
    if (program == NULL) {
        return;
    }
    list_free(&program->nodes);
    list_free(&program->symbols);
    name_index_free(&program->symbol_index);
    list_free(&program->roots);
    list_free(&program->children);
    list_free(&program->types);
    list_free(&program->signatures);
    list_free(&program->signature_parameters);
    list_free(&program->type_children);
    list_free(&program->bytes);
    list_free(&program->match_temporaries);
    list_free(&program->match_scopes);
}

const TypedNode *typed_program_node(const TypedProgram *program, size_t index)
{
    return list_get(&program->nodes, index);
}

bool typed_program_push(TypedProgram *program, const TypedNode *node,
                        size_t *out_index)
{
    if (node == NULL) {
        return false;
    }
    *out_index = program->nodes.length;
    return list_push(&program->nodes, node);
}

const char *typed_program_symbol_text(const TypedProgram *program, size_t index)
{
    const TypedSymbol *symbol = list_get(&program->symbols, index);

    return symbol == NULL ? NULL : symbol->text;
}

size_t typed_program_copy_symbol(TypedProgram *program, const char *text,
                                 size_t length)
{
    size_t i;

    if (text == NULL || length >= NYX_IDENTIFIER_MAX) {
        return TYPED_NO_NODE;
    }

    i = name_index_find(&program->symbol_index, &program->symbols, text, length);
    if (i != TYPED_NO_NODE) {
        return i;
    }

    {
        TypedSymbol symbol;
        memset(&symbol, 0, sizeof(symbol));
        memcpy(symbol.text, text, length);
        if (!list_push(&program->symbols, &symbol)) {
            return TYPED_NO_NODE;
        }
        if (!name_index_insert(&program->symbol_index, &program->symbols,
                               program->symbols.length - 1)) {
            program->symbols.length--;
            return TYPED_NO_NODE;
        }
        return program->symbols.length - 1;
    }
}

const TypedType *typed_program_type(const TypedProgram *program, size_t index)
{
    return list_get(&program->types, index);
}

NyxType typed_program_node_kind(const TypedProgram *program, size_t node_index)
{
    const TypedNode *node = typed_program_node(program, node_index);
    const TypedType *type =
        typed_program_type(program, node == NULL ? TYPED_NO_NODE : node->type_index);

    return type == NULL ? NYX_TYPE_KIND_COUNT : type->kind;
}

bool typed_program_add_type(TypedProgram *program, const TypedType *type,
                            size_t *out_index)
{
    if (type == NULL) {
        return false;
    }
    *out_index = program->types.length;
    return list_push(&program->types, type);
}

/* The parts of a type are the types it is made of: a struct's field types, and
   for every other kind its recorded children. A struct without a declaration is
   malformed; it counts as one part that cannot be read. */
size_t typed_type_part_count(const TypedProgram *program, const TypedType *type)
{
    const TypedNode *declaration;

    if (type->kind != NYX_TYPE_STRUCT) {
        return type->child_count;
    }
    declaration = typed_program_node(program, type->declaration_node);
    return declaration == NULL ? 1 : declaration->children_count;
}

bool typed_type_part(const TypedProgram *program, const TypedType *type,
                     size_t index, size_t *out_type)
{
    const TypedNode *declaration;
    const TypedNode *field;

    if (type->kind != NYX_TYPE_STRUCT) {
        *out_type = *(const size_t *)list_get(&program->type_children,
                                              type->first_child + index);
        return true;
    }
    declaration = typed_program_node(program, type->declaration_node);
    if (declaration == NULL) {
        return false;
    }
    field = typed_program_node(
        program, *(const size_t *)list_get(&program->children,
                                           declaration->children_start + index));
    if (field == NULL || field->kind != TYPED_FIELD) {
        return false;
    }
    *out_type = field->type_index;
    return true;
}

const TypedFunctionSignature *typed_program_signature(
    const TypedProgram *program, size_t index)
{
    return list_get(&program->signatures, index);
}

bool typed_program_add_signature(TypedProgram *program,
                                 const TypedFunctionSignature *signature,
                                 size_t *out_index)
{
    if (signature == NULL) {
        return false;
    }
    *out_index = program->signatures.length;
    return list_push(&program->signatures, signature);
}

bool typed_program_add_signature_parameter(TypedProgram *program,
                                           size_t type_index)
{
    return list_push(&program->signature_parameters, &type_index);
}

bool typed_program_add_type_child(TypedProgram *program, size_t type_index)
{
    return list_push(&program->type_children, &type_index);
}

bool typed_program_push_byte(TypedProgram *program, unsigned char byte)
{
    return list_push(&program->bytes, &byte);
}

bool typed_program_push_child(TypedProgram *program, size_t index)
{
    return list_push(&program->children, &index);
}

bool nyx_type_is_integer(NyxType kind)
{
    return kind == NYX_TYPE_INT8 || kind == NYX_TYPE_INT16 ||
           kind == NYX_TYPE_INT32 || kind == NYX_TYPE_INT64 ||
           kind == NYX_TYPE_BYTE || kind == NYX_TYPE_UINT16 ||
           kind == NYX_TYPE_UINT32 || kind == NYX_TYPE_UINT64;
}

bool nyx_type_is_signed_integer(NyxType kind)
{
    return kind == NYX_TYPE_INT8 || kind == NYX_TYPE_INT16 ||
           kind == NYX_TYPE_INT32 || kind == NYX_TYPE_INT64;
}

bool nyx_type_is_unsigned_integer(NyxType kind)
{
    return kind == NYX_TYPE_BYTE || kind == NYX_TYPE_UINT16 ||
           kind == NYX_TYPE_UINT32 || kind == NYX_TYPE_UINT64;
}

bool nyx_type_is_float(NyxType kind)
{
    return kind == NYX_TYPE_FLOAT32 || kind == NYX_TYPE_FLOAT64;
}

bool nyx_type_is_numeric(NyxType kind)
{
    return nyx_type_is_integer(kind) || nyx_type_is_float(kind);
}

unsigned nyx_type_bit_width(NyxType kind)
{
    switch (kind) {
    case NYX_TYPE_INT8:
    case NYX_TYPE_BYTE:
        return 8;
    case NYX_TYPE_INT16:
    case NYX_TYPE_UINT16:
        return 16;
    case NYX_TYPE_INT32:
    case NYX_TYPE_UINT32:
    case NYX_TYPE_FLOAT32:
        return 32;
    case NYX_TYPE_INT64:
    case NYX_TYPE_UINT64:
    case NYX_TYPE_FLOAT64:
        return 64;
    default:
        return 0;
    }
}

/* Two types are equivalent when they would be the same C type: structs by
   name, the rest by structure down to `depth` levels. Each kind with parts has
   a rule; a kind with none is equivalent once the kinds match. */
typedef bool (*EquivalenceRule)(const TypedProgram *program,
                                const TypedType *left, const TypedType *right,
                                unsigned depth);

static bool equivalent_run(const TypedProgram *program, const List *items,
                           size_t left_first, size_t right_first, size_t count,
                           unsigned depth)
{
    size_t i;

    for (i = 0; i < count; i++) {
        if (!typed_types_equivalent(
                program,
                *(const size_t *)list_get(items, left_first + i),
                *(const size_t *)list_get(items, right_first + i), depth - 1)) {
            return false;
        }
    }
    return true;
}

static bool equivalent_always(const TypedProgram *program,
                              const TypedType *left, const TypedType *right,
                              unsigned depth)
{
    (void)program;
    (void)left;
    (void)right;
    (void)depth;
    return true;
}

static bool equivalent_struct(const TypedProgram *program,
                              const TypedType *left, const TypedType *right,
                              unsigned depth)
{
    (void)program;
    (void)depth;
    return left->name_index == right->name_index;
}

static bool equivalent_handle(const TypedProgram *program,
                              const TypedType *left, const TypedType *right,
                              unsigned depth)
{
    return depth != 0 && left->child_count == 1 && right->child_count == 1 &&
           equivalent_run(program, &program->type_children, left->first_child,
                          right->first_child, 1, depth);
}

static bool equivalent_union(const TypedProgram *program,
                             const TypedType *left, const TypedType *right,
                             unsigned depth)
{
    return depth != 0 && left->child_count == right->child_count &&
           equivalent_run(program, &program->type_children, left->first_child,
                          right->first_child, left->child_count, depth);
}

static bool equivalent_function(const TypedProgram *program,
                                const TypedType *left, const TypedType *right,
                                unsigned depth)
{
    const TypedFunctionSignature *ls =
        typed_program_signature(program, left->signature_index);
    const TypedFunctionSignature *rs =
        typed_program_signature(program, right->signature_index);

    return depth != 0 && ls != NULL && rs != NULL &&
           ls->parameter_count == rs->parameter_count &&
           typed_types_equivalent(program, ls->return_type, rs->return_type,
                            depth - 1) &&
           equivalent_run(program, &program->signature_parameters,
                          ls->first_parameter, rs->first_parameter,
                          ls->parameter_count, depth);
}

static const EquivalenceRule equivalence_rules[] = {
    [NYX_TYPE_BOOL] = equivalent_always,
    [NYX_TYPE_NIL] = equivalent_always,
    [NYX_TYPE_STORE] = equivalent_always,
    [NYX_TYPE_STRUCT] = equivalent_struct,
    [NYX_TYPE_HANDLE] = equivalent_handle,
    [NYX_TYPE_UNION] = equivalent_union,
    [NYX_TYPE_FUNCTION] = equivalent_function,
};

bool typed_types_equivalent(const TypedProgram *program, size_t first,
                      size_t second, unsigned depth)
{
    const TypedType *left;
    const TypedType *right;

    if (first == second) {
        return true;
    }
    if (first == TYPED_NO_NODE || second == TYPED_NO_NODE) {
        return false;
    }
    left = typed_program_type(program, first);
    right = typed_program_type(program, second);
    if (left == NULL || right == NULL || left->kind != right->kind) {
        return false;
    }
    if (nyx_type_is_numeric(left->kind)) {
        return true;
    }
    return (size_t)left->kind < NYX_COUNT_OF(equivalence_rules) &&
           equivalence_rules[left->kind] != NULL &&
           equivalence_rules[left->kind](program, left, right, depth);
}
