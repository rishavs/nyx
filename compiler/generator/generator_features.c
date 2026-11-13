#include "generator_internal.h"


size_t count_store_declarations(const TypedProgram *program)
{
    size_t count = 0;
    size_t i;

    for (i = 0; i < program->roots.length; i++) {
        size_t root = *(const size_t *)list_get(&program->roots, i);
        const TypedNode *node = typed_program_node(program, root);

        if (node != NULL && node->kind == TYPED_STORE_DECLARATION) {
            count++;
        }
    }
    return count;
}


bool validate_store_declarations(const TypedProgram *program,
                                        size_t store_count,
                                        ErrorState *errors)
{
    size_t i;

    for (i = 0; i < program->roots.length; i++) {
        size_t root = *(const size_t *)list_get(&program->roots, i);
        const TypedNode *node = typed_program_node(program, root);
        const TypedType *type;

        if (node == NULL || node->kind != TYPED_STORE_DECLARATION) {
            continue;
        }
        type = typed_program_type(program, node->type_index);
        if (type == NULL || type->kind != NIPPY_TYPE_STORE ||
            node->store_id >= store_count || node->integer_value < 0 ||
            find_store_declaration(program, node->store_id) != root) {
            error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_ROOT);
            return false;
        }
    }
    return true;
}



bool program_uses_string_builder(const TypedProgram *program)
{
    size_t i;

    for (i = 0; i < program->types.length; i++) {
        const TypedType *type = typed_program_type(program, i);

        if (type != NULL && type->kind == NIPPY_TYPE_TEXT_BUILDER) {
            return true;
        }
    }
    return false;
}


bool program_uses_list(const TypedProgram *program)
{
    size_t i;

    for (i = 0; i < program->types.length; i++) {
        const TypedType *type = typed_program_type(program, i);

        if (type != NULL &&
            (type->kind == NIPPY_TYPE_LIST ||
             type->kind == NIPPY_TYPE_FIXED_LIST)) {
            return true;
        }
    }
    return false;
}


bool program_uses_dynamic_list(const TypedProgram *program)
{
    size_t i;

    for (i = 0; i < program->types.length; i++) {
        const TypedType *type = typed_program_type(program, i);

        if (type != NULL && type->kind == NIPPY_TYPE_LIST) {
            return true;
        }
    }
    return false;
}


bool program_uses_slice(const TypedProgram *program)
{
    size_t i;

    for (i = 0; i < program->types.length; i++) {
        const TypedType *type = typed_program_type(program, i);

        if (type != NULL && type->kind == NIPPY_TYPE_SLICE) {
            return true;
        }
    }
    return false;
}


bool program_uses_dynamic_string(const TypedProgram *program)
{
    size_t i;

    for (i = 0; i < program->types.length; i++) {
        const TypedType *type = typed_program_type(program, i);

        if (type != NULL && type->kind == NIPPY_TYPE_DYNAMIC_STRING) {
            return true;
        }
    }
    return false;
}


bool program_uses_string(const TypedProgram *program)
{
    size_t i;

    for (i = 0; i < program->types.length; i++) {
        const TypedType *type = typed_program_type(program, i);

        if (type != NULL &&
            (type->kind == NIPPY_TYPE_STRING ||
             type->kind == NIPPY_TYPE_ERROR)) {
            return true;
        }
    }
    for (i = 0; i < program->nodes.length; i++) {
        const TypedNode *node = typed_program_node(program, i);

        if (node != NULL && node->kind == TYPED_STRING_LITERAL) {
            return true;
        }
    }
    return false;
}

bool program_uses_handle_or_store_type(const TypedProgram *program)
{
    size_t i;

    for (i = 0; i < program->types.length; i++) {
        const TypedType *type = typed_program_type(program, i);

        if (type != NULL && (type->kind == NIPPY_TYPE_STORE ||
                             type->kind == NIPPY_TYPE_HANDLE)) {
            return true;
        }
    }
    return false;
}



bool program_uses_binary_operator(const TypedProgram *program,
                                        NippyOperator op)
{
    size_t i;

    for (i = 0; i < program->nodes.length; i++) {
        const TypedNode *node = typed_program_node(program, i);

        if (node != NULL && node->kind == TYPED_BINARY && node->op == op) {
            return true;
        }
    }
    return false;
}


bool program_uses_print(const TypedProgram *program)
{
    size_t i;

    for (i = 0; i < program->nodes.length; i++) {
        const TypedNode *node = typed_program_node(program, i);

        if (node != NULL && node->kind == TYPED_PRINT) {
            return true;
        }
    }
    return false;
}

bool program_uses_recursion(const TypedProgram *program)
{
    size_t i;

    for (i = 0; i < program->nodes.length; i++) {
        const TypedNode *node = typed_program_node(program, i);

        if (node != NULL && node->kind == TYPED_FUNCTION_DECLARATION &&
            typed_calls_name(program, node->left_child, node->name_index)) {
            return true;
        }
    }
    return false;
}

bool program_uses_numeric_conversion(const TypedProgram *program)
{
    size_t i;

    for (i = 0; i < program->nodes.length; i++) {
        const TypedNode *node = typed_program_node(program, i);

        if (node != NULL && node->kind == TYPED_NUMERIC_CONVERSION) {
            const TypedType *target = typed_program_type(program,
                                                         node->type_index);
            const TypedType *source = typed_program_type(
                program, node->auxiliary_type);

            /* A folded constant, or a conversion to the same type, emits no
               helper call. */
            if (target != NULL && source != NULL &&
                target->kind != source->kind) {
                return true;
            }
        }
    }
    return false;
}
