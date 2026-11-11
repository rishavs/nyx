#include "typed_program.h"

#include <string.h>

void typed_program_init(TypedProgram *program)
{
    list_init(&program->nodes, sizeof(TypedNode));
    list_init(&program->symbols, sizeof(TypedSymbol));
    list_init(&program->roots, sizeof(size_t));
    list_init(&program->children, sizeof(size_t));
    list_init(&program->types, sizeof(TypedType));
    list_init(&program->signatures, sizeof(TypedFunctionSignature));
    list_init(&program->signature_parameters, sizeof(size_t));
    list_init(&program->type_children, sizeof(size_t));
}

void typed_program_free(TypedProgram *program)
{
    if (program == NULL) {
        return;
    }
    list_free(&program->nodes);
    list_free(&program->symbols);
    list_free(&program->roots);
    list_free(&program->children);
    list_free(&program->types);
    list_free(&program->signatures);
    list_free(&program->signature_parameters);
    list_free(&program->type_children);
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

    for (i = 0; i < program->symbols.length; i++) {
        const TypedSymbol *existing = list_get(&program->symbols, i);
        if (existing != NULL && strlen(existing->text) == length &&
            memcmp(existing->text, text, length) == 0) {
            return i;
        }
    }

    {
        TypedSymbol symbol;
        memset(&symbol, 0, sizeof(symbol));
        memcpy(symbol.text, text, length);
        if (!list_push(&program->symbols, &symbol)) {
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

bool typed_program_push_child(TypedProgram *program, size_t index)
{
    return list_push(&program->children, &index);
}
