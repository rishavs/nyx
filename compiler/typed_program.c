#include "typed_program.h"

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
    program->error_header_capacity = NIPPY_DEFAULT_ERROR_HEADER_CAPACITY;
    program->error_description_capacity =
        NIPPY_DEFAULT_ERROR_DESCRIPTION_CAPACITY;
    program->error_filename_capacity = NIPPY_DEFAULT_ERROR_FILENAME_CAPACITY;
    program->max_string_capacity = NIPPY_DEFAULT_MAX_STRING_CAPACITY;
    program->max_list_capacity = NIPPY_DEFAULT_MAX_LIST_CAPACITY;
    program->max_print_depth = NIPPY_DEFAULT_MAX_PRINT_DEPTH;
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

    if (text == NULL || length >= NIPPY_IDENTIFIER_MAX) {
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

bool typed_program_add_type(TypedProgram *program, const TypedType *type,
                            size_t *out_index)
{
    if (type == NULL) {
        return false;
    }
    *out_index = program->types.length;
    return list_push(&program->types, type);
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

bool nippy_type_is_integer(NippyType kind)
{
    return kind == NIPPY_TYPE_INT8 || kind == NIPPY_TYPE_INT16 ||
           kind == NIPPY_TYPE_INT32 || kind == NIPPY_TYPE_INT64 ||
           kind == NIPPY_TYPE_BYTE || kind == NIPPY_TYPE_UINT16 ||
           kind == NIPPY_TYPE_UINT32 || kind == NIPPY_TYPE_UINT64;
}

bool nippy_type_is_signed_integer(NippyType kind)
{
    return kind == NIPPY_TYPE_INT8 || kind == NIPPY_TYPE_INT16 ||
           kind == NIPPY_TYPE_INT32 || kind == NIPPY_TYPE_INT64;
}

bool nippy_type_is_unsigned_integer(NippyType kind)
{
    return kind == NIPPY_TYPE_BYTE || kind == NIPPY_TYPE_UINT16 ||
           kind == NIPPY_TYPE_UINT32 || kind == NIPPY_TYPE_UINT64;
}

bool nippy_type_is_float(NippyType kind)
{
    return kind == NIPPY_TYPE_FLOAT32 || kind == NIPPY_TYPE_FLOAT64;
}

bool nippy_type_is_numeric(NippyType kind)
{
    return nippy_type_is_integer(kind) || nippy_type_is_float(kind);
}

unsigned nippy_type_bit_width(NippyType kind)
{
    switch (kind) {
    case NIPPY_TYPE_INT8:
    case NIPPY_TYPE_BYTE:
        return 8;
    case NIPPY_TYPE_INT16:
    case NIPPY_TYPE_UINT16:
        return 16;
    case NIPPY_TYPE_INT32:
    case NIPPY_TYPE_UINT32:
    case NIPPY_TYPE_FLOAT32:
        return 32;
    case NIPPY_TYPE_INT64:
    case NIPPY_TYPE_UINT64:
    case NIPPY_TYPE_FLOAT64:
        return 64;
    default:
        return 0;
    }
}
