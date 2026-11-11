#include "generator.h"

#include "alloc.h"
#include "messages.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool valid_header_name(const char *name)
{
    const unsigned char *cursor = (const unsigned char *)name;

    if (name == NULL || *name == '\0') {
        return false;
    }
    while (*cursor != '\0') {
        if (!isalnum(*cursor) && *cursor != '_' && *cursor != '-' && *cursor != '.') {
            return false;
        }
        cursor++;
    }
    return true;
}

/* A #line filename becomes a C string literal, so it must be printable and
   contain no quote or backslash. Unlike a header name it may contain '/'
   because source paths do. */
static bool valid_line_name(const char *name)
{
    const unsigned char *cursor = (const unsigned char *)name;

    if (name == NULL || *name == '\0') {
        return false;
    }
    while (*cursor != '\0') {
        if (*cursor == '"' || *cursor == '\\' || *cursor < 32 ||
            *cursor == 127) {
            return false;
        }
        cursor++;
    }
    return true;
}

/* The checker only emits identifier-shaped names, but the generator does not
   trust an earlier stage: an unvalidated name would be pasted into C source. */
static bool valid_identifier(const char *name)
{
    const unsigned char *cursor = (const unsigned char *)name;

    if (name == NULL || *cursor == '\0') {
        return false;
    }
    if (!isalpha(*cursor) && *cursor != '_') {
        return false;
    }
    cursor++;
    while (*cursor != '\0') {
        if (!isalnum(*cursor) && *cursor != '_') {
            return false;
        }
        cursor++;
    }
    return true;
}

/* A growable string used to compose generated C with a variable number of
   declarations and fields. */
typedef struct {
    char *text;
    size_t length;
    size_t capacity;
} TextBuffer;

static void buffer_init(TextBuffer *buffer)
{
    buffer->text = NULL;
    buffer->length = 0;
    buffer->capacity = 0;
}

static void buffer_free(TextBuffer *buffer)
{
    free(buffer->text);
    buffer->text = NULL;
    buffer->length = 0;
    buffer->capacity = 0;
}

static bool buffer_reserve(TextBuffer *buffer, size_t required)
{
    size_t capacity;
    char *grown;

    if (required <= buffer->capacity) {
        return true;
    }
    capacity = buffer->capacity == 0 ? 64 : buffer->capacity;
    while (capacity < required) {
        if (capacity > (size_t)-1 / 2) {
            return false;
        }
        capacity *= 2;
    }
    grown = nippy_realloc(buffer->text, capacity);
    if (grown == NULL) {
        return false;
    }
    buffer->text = grown;
    buffer->capacity = capacity;
    return true;
}

static bool buffer_append(TextBuffer *buffer, const char *text)
{
    size_t length = strlen(text);

    if (!buffer_reserve(buffer, buffer->length + length + 1)) {
        return false;
    }
    memcpy(buffer->text + buffer->length, text, length);
    buffer->length += length;
    buffer->text[buffer->length] = '\0';
    return true;
}

static bool buffer_appendf(TextBuffer *buffer, const char *format, ...)
{
    va_list args;
    va_list measure;
    int needed;

    va_start(args, format);
    va_copy(measure, args);
    needed = vsnprintf(NULL, 0, format, measure);
    va_end(measure);
    if (needed < 0) {
        va_end(args);
        return false;
    }
    if (!buffer_reserve(buffer, buffer->length + (size_t)needed + 1)) {
        va_end(args);
        return false;
    }
    vsnprintf(buffer->text + buffer->length, buffer->capacity - buffer->length,
              format, args);
    va_end(args);
    buffer->length += (size_t)needed;
    return true;
}

/* Build the C spelling of a resolved type. Structs use their typedef name and
   function types use a generated function-pointer typedef, so every declaration
   is a simple `type name` pair. */
static bool format_c_type(const TypedProgram *program, size_t type_index,
                          char *out, size_t out_size, ErrorState *errors)
{
    const TypedType *type = typed_program_type(program, type_index);

    if (type == NULL) {
        error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_TYPE);
        return false;
    }
    if (type->kind == NIPPY_TYPE_INT32) {
        snprintf(out, out_size, "int32_t");
        return true;
    }
    if (type->kind == NIPPY_TYPE_BOOL) {
        snprintf(out, out_size, "bool");
        return true;
    }
    if (type->kind == NIPPY_TYPE_NIL) {
        snprintf(out, out_size, "nippy_nil");
        return true;
    }
    if (type->kind == NIPPY_TYPE_BYTE) {
        snprintf(out, out_size, "uint8_t");
        return true;
    }
    if (type->kind == NIPPY_TYPE_HANDLE) {
        snprintf(out, out_size, "nippy_handle");
        return true;
    }
    if (type->kind == NIPPY_TYPE_STRUCT) {
        const char *name = typed_program_symbol_text(program, type->name_index);

        if (!valid_identifier(name)) {
            error_report(errors, MESSAGE_GENERATOR_INVALID_IDENTIFIER);
            return false;
        }
        snprintf(out, out_size, "%s", name);
        return true;
    }
    if (type->kind == NIPPY_TYPE_FUNCTION) {
        snprintf(out, out_size, "nippy_fun_%zu", type->signature_index);
        return true;
    }
    if (type->kind == NIPPY_TYPE_UNION) {
        snprintf(out, out_size, "nippy_sum_%zu", type_index);
        return true;
    }
    error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_TYPE);
    return false;
}

/* Handle<T> records its element as the single entry in the handle type's
   child run. */
static bool handle_element_type(const TypedProgram *program,
                                size_t handle_type_index, size_t *out_element)
{
    const TypedType *handle = typed_program_type(program, handle_type_index);
    const size_t *element;

    if (handle == NULL || handle->kind != NIPPY_TYPE_HANDLE ||
        handle->child_count != 1) {
        return false;
    }
    element = list_get(&program->type_children, handle->first_child);
    if (element == NULL) {
        return false;
    }
    *out_element = *element;
    return true;
}

/* Unions and struct instantiations are interned by the checker, but a function
   type is not guaranteed to share an index with a structurally equal one, so
   `.is<T>` matches alternatives structurally. The depth bound keeps a function
   type that mentions itself from recursing forever. */
static bool types_equivalent(const TypedProgram *program, size_t first,
                             size_t second, unsigned depth)
{
    const TypedType *left;
    const TypedType *right;
    size_t i;

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
    if (left->kind == NIPPY_TYPE_INT32 || left->kind == NIPPY_TYPE_BOOL ||
        left->kind == NIPPY_TYPE_NIL || left->kind == NIPPY_TYPE_BYTE ||
        left->kind == NIPPY_TYPE_STORE) {
        return true;
    }
    if (left->kind == NIPPY_TYPE_STRUCT) {
        return left->name_index == right->name_index;
    }
    if (left->kind == NIPPY_TYPE_HANDLE) {
        size_t left_element;
        size_t right_element;

        if (depth == 0 || left->child_count != 1 ||
            right->child_count != 1) {
            return false;
        }
        left_element = *(const size_t *)list_get(&program->type_children,
                                                 left->first_child);
        right_element = *(const size_t *)list_get(&program->type_children,
                                                  right->first_child);
        return types_equivalent(program, left_element, right_element,
                                depth - 1);
    }
    if (left->kind == NIPPY_TYPE_FUNCTION) {
        const TypedFunctionSignature *left_signature =
            typed_program_signature(program, left->signature_index);
        const TypedFunctionSignature *right_signature =
            typed_program_signature(program, right->signature_index);

        if (depth == 0 || left_signature == NULL ||
            right_signature == NULL ||
            left_signature->parameter_count !=
                right_signature->parameter_count ||
            !types_equivalent(program, left_signature->return_type,
                              right_signature->return_type, depth - 1)) {
            return false;
        }
        for (i = 0; i < left_signature->parameter_count; i++) {
            size_t left_parameter = *(const size_t *)list_get(
                &program->signature_parameters,
                left_signature->first_parameter + i);
            size_t right_parameter = *(const size_t *)list_get(
                &program->signature_parameters,
                right_signature->first_parameter + i);

            if (!types_equivalent(program, left_parameter, right_parameter,
                                  depth - 1)) {
                return false;
            }
        }
        return true;
    }
    if (left->kind == NIPPY_TYPE_UNION) {
        if (depth == 0 || left->child_count != right->child_count) {
            return false;
        }
        for (i = 0; i < left->child_count; i++) {
            size_t left_alternative = *(const size_t *)list_get(
                &program->type_children, left->first_child + i);
            size_t right_alternative = *(const size_t *)list_get(
                &program->type_children, right->first_child + i);

            if (!types_equivalent(program, left_alternative,
                                  right_alternative, depth - 1)) {
                return false;
            }
        }
        return true;
    }
    return false;
}

/* Locate the canonical tag of `wanted` inside a union's alternative run. */
static bool find_union_alternative(const TypedProgram *program,
                                   const TypedType *union_type, size_t wanted,
                                   size_t *out_tag)
{
    size_t i;

    for (i = 0; i < union_type->child_count; i++) {
        size_t alternative = *(const size_t *)list_get(
            &program->type_children, union_type->first_child + i);

        if (types_equivalent(program, alternative, wanted, 16)) {
            *out_tag = i;
            return true;
        }
    }
    return false;
}

static const TypedType *union_alternative_type(const TypedProgram *program,
                                               const TypedType *union_type,
                                               size_t position)
{
    size_t alternative;

    if (position >= union_type->child_count) {
        return NULL;
    }
    alternative = *(const size_t *)list_get(&program->type_children,
                                            union_type->first_child + position);
    return typed_program_type(program, alternative);
}

/* Store ids are assigned densely by the checker in declaration order. Locating
   a declaration by id lets the table be emitted without a scratch array. */
static size_t find_store_declaration(const TypedProgram *program,
                                     size_t store_id)
{
    size_t i;

    for (i = 0; i < program->roots.length; i++) {
        size_t root = *(const size_t *)list_get(&program->roots, i);
        const TypedNode *node = typed_program_node(program, root);

        if (node != NULL && node->kind == TYPED_STORE_DECLARATION &&
            node->store_id == store_id) {
            return root;
        }
    }
    return TYPED_NO_NODE;
}

static bool store_id_is_valid(const TypedProgram *program, size_t store_id)
{
    return find_store_declaration(program, store_id) != TYPED_NO_NODE;
}

/* C cannot initialise a struct member from another struct expression inside a
   compound literal: `(T){ other_T }` initialises T's first field, not T. Both
   structs and handles are C aggregates, so their value is carried through a
   one-member union instead. Scalars keep the plain `(T){ value }` shape. */
static bool type_is_c_aggregate(const TypedProgram *program, size_t type_index)
{
    const TypedType *type = typed_program_type(program, type_index);

    return type != NULL && (type->kind == NIPPY_TYPE_STRUCT ||
                            type->kind == NIPPY_TYPE_HANDLE);
}

static bool emit_expression(const TypedProgram *program, size_t node_index,
                            TextBuffer *buffer, ErrorState *errors);

static bool emit_children_expressions(const TypedProgram *program,
                                      const TypedNode *node,
                                      TextBuffer *buffer, ErrorState *errors)
{
    size_t i;

    for (i = 0; i < node->children_count; i++) {
        size_t child = *(const size_t *)list_get(&program->children,
                                                 node->children_start + i);

        if (i > 0 && !buffer_append(buffer, ", ")) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            return false;
        }
        if (!emit_expression(program, child, buffer, errors)) {
            return false;
        }
    }
    return true;
}

static const char *binary_operator_text(NippyOperator op)
{
    switch (op) {
    case NIPPY_OPERATOR_EQ:
        return "==";
    case NIPPY_OPERATOR_NE:
        return "!=";
    case NIPPY_OPERATOR_LT:
        return "<";
    case NIPPY_OPERATOR_LE:
        return "<=";
    case NIPPY_OPERATOR_GT:
        return ">";
    case NIPPY_OPERATOR_GE:
        return ">=";
    case NIPPY_OPERATOR_AND:
        return "&&";
    case NIPPY_OPERATOR_OR:
        return "||";
    default:
        return NULL;
    }
}

static bool emit_expression(const TypedProgram *program, size_t node_index,
                            TextBuffer *buffer, ErrorState *errors)
{
    const TypedNode *node = typed_program_node(program, node_index);
    char type_name[NIPPY_IDENTIFIER_MAX + 32];

    if (node == NULL) {
        error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_EXPRESSION);
        return false;
    }
    if (node->kind == TYPED_INTEGER_LITERAL) {
        if (!buffer_appendf(buffer, "%d", node->integer_value)) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            return false;
        }
        return true;
    }
    if (node->kind == TYPED_BOOL_LITERAL) {
        if (!buffer_append(buffer, node->integer_value ? "true" : "false")) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            return false;
        }
        return true;
    }
    if (node->kind == TYPED_IDENTIFIER) {
        const char *name = typed_program_symbol_text(program, node->name_index);

        if (!valid_identifier(name)) {
            error_report(errors, MESSAGE_GENERATOR_INVALID_IDENTIFIER);
            return false;
        }
        if (!buffer_append(buffer, name)) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            return false;
        }
        return true;
    }
    if (node->kind == TYPED_NIL_LITERAL) {
        if (!buffer_append(buffer, "((nippy_nil)0)")) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            return false;
        }
        return true;
    }
    if (node->kind == TYPED_SUM_INJECT) {
        const TypedType *union_type = typed_program_type(program, node->type_index);
        const TypedType *alternative;
        char union_name[NIPPY_IDENTIFIER_MAX + 32];

        if (union_type == NULL || union_type->kind != NIPPY_TYPE_UNION ||
            node->integer_value < 0) {
            error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_EXPRESSION);
            return false;
        }
        alternative = union_alternative_type(program, union_type,
                                             (size_t)node->integer_value);
        if (alternative == NULL) {
            error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_EXPRESSION);
            return false;
        }
        if (!format_c_type(program, node->type_index, union_name,
                           sizeof(union_name), errors)) {
            return false;
        }
        if (alternative->kind == NIPPY_TYPE_NIL) {
            if (!buffer_appendf(buffer, "((%s){ .tag = %d })", union_name,
                                node->integer_value)) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
                return false;
            }
            return true;
        }
        if (node->left_child == TYPED_NO_NODE) {
            error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_EXPRESSION);
            return false;
        }
        if (!buffer_appendf(buffer, "((%s){ .tag = %d, .value.v%d = ",
                            union_name, node->integer_value,
                            node->integer_value) ||
            !emit_expression(program, node->left_child, buffer, errors) ||
            !buffer_append(buffer, " })")) {
            if (!errors->reported) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            }
            return false;
        }
        return true;
    }
    if (node->kind == TYPED_SUM_PAYLOAD) {
        if (node->integer_value < 0) {
            error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_EXPRESSION);
            return false;
        }
        if (!buffer_append(buffer, "(") ||
            !emit_expression(program, node->left_child, buffer, errors) ||
            !buffer_appendf(buffer, ".value.v%d)", node->integer_value)) {
            if (!errors->reported) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            }
            return false;
        }
        return true;
    }
    if (node->kind == TYPED_IS) {
        const TypedNode *value = typed_program_node(program, node->left_child);
        const TypedType *value_type =
            value == NULL ? NULL : typed_program_type(program, value->type_index);

        if (value == NULL || value_type == NULL) {
            error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_EXPRESSION);
            return false;
        }
        if (value_type->kind == NIPPY_TYPE_UNION) {
            size_t tag = 0;
            bool runtime = true;

            if (node->auxiliary_type != TYPED_NO_NODE) {
                runtime = find_union_alternative(program, value_type,
                                                 node->auxiliary_type, &tag);
            } else if (node->integer_value >= 0 &&
                       (size_t)node->integer_value < value_type->child_count) {
                tag = (size_t)node->integer_value;
            } else {
                error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_EXPRESSION);
                return false;
            }
            if (!runtime) {
                /* A tested type absent from the union is a decided false. */
                if (!buffer_append(buffer, "false")) {
                    error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
                    return false;
                }
                return true;
            }
            if (!buffer_append(buffer, "(") ||
                !emit_expression(program, node->left_child, buffer, errors) ||
                !buffer_appendf(buffer, ".tag == %zu)", tag)) {
                if (!errors->reported) {
                    error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
                }
                return false;
            }
            return true;
        }
        {
            /* `.is<T>` on a non-union value is decided statically. */
            bool result;

            if (node->auxiliary_type != TYPED_NO_NODE) {
                result = types_equivalent(program, value->type_index,
                                          node->auxiliary_type, 16);
            } else if (node->integer_value >= 0) {
                result = node->integer_value != 0;
            } else {
                error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_EXPRESSION);
                return false;
            }
            if (!buffer_append(buffer, result ? "true" : "false")) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
                return false;
            }
            return true;
        }
    }
    if (node->kind == TYPED_BINARY) {
        if (node->op == NIPPY_OPERATOR_ADD) {
            if (!buffer_append(buffer, "(int32_t)((uint32_t)(") ||
                !emit_expression(program, node->left_child, buffer, errors) ||
                !buffer_append(buffer, ") + (uint32_t)(") ||
                !emit_expression(program, node->right_child, buffer, errors) ||
                !buffer_append(buffer, "))")) {
                if (!errors->reported) {
                    error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
                }
                return false;
            }
            return true;
        }
        {
            const char *op = binary_operator_text(node->op);

            if (op == NULL) {
                error_report(errors,
                             MESSAGE_GENERATOR_UNSUPPORTED_EXPRESSION);
                return false;
            }
            if (!buffer_append(buffer, "(") ||
                !emit_expression(program, node->left_child, buffer, errors) ||
                !buffer_appendf(buffer, " %s ", op) ||
                !emit_expression(program, node->right_child, buffer, errors) ||
                !buffer_append(buffer, ")")) {
                if (!errors->reported) {
                    error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
                }
                return false;
            }
            return true;
        }
    }
    if (node->kind == TYPED_UNARY) {
        if (node->op != NIPPY_OPERATOR_NOT) {
            error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_EXPRESSION);
            return false;
        }
        if (!buffer_append(buffer, "(!(") ||
            !emit_expression(program, node->left_child, buffer, errors) ||
            !buffer_append(buffer, "))")) {
            if (!errors->reported) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            }
            return false;
        }
        return true;
    }
    if (node->kind == TYPED_STRUCT_CONSTRUCTOR) {
        if (!format_c_type(program, node->type_index, type_name,
                           sizeof(type_name), errors) ||
            !buffer_appendf(buffer, "(%s){ ", type_name) ||
            !emit_children_expressions(program, node, buffer, errors) ||
            !buffer_append(buffer, " }")) {
            if (!errors->reported) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            }
            return false;
        }
        return true;
    }
    if (node->kind == TYPED_FUNCTION_CALL) {
        const char *name = typed_program_symbol_text(program, node->name_index);

        if (!valid_identifier(name)) {
            error_report(errors, MESSAGE_GENERATOR_INVALID_IDENTIFIER);
            return false;
        }
        if (!buffer_appendf(buffer, "%s(", name) ||
            !emit_children_expressions(program, node, buffer, errors) ||
            !buffer_append(buffer, ")")) {
            if (!errors->reported) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            }
            return false;
        }
        return true;
    }
    if (node->kind == TYPED_STORE_ALLOC) {
        size_t element;
        bool aggregate;
        char element_name[NIPPY_IDENTIFIER_MAX + 32];

        if (!store_id_is_valid(program, node->store_id) ||
            !handle_element_type(program, node->type_index, &element) ||
            !format_c_type(program, element, element_name,
                           sizeof(element_name), errors) ||
            node->left_child == TYPED_NO_NODE) {
            if (!errors->reported) {
                error_report(errors,
                             MESSAGE_GENERATOR_UNSUPPORTED_EXPRESSION);
            }
            return false;
        }
        aggregate = type_is_c_aggregate(program, element);
        if (!buffer_appendf(buffer,
                            "nippy_store_alloc(&nippy_stores[%zu], %zu, ",
                            node->store_id, node->store_id)) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            return false;
        }
        if (aggregate) {
            if (!buffer_appendf(
                    buffer, "&(union { %s nippy_value; }){ .nippy_value = ",
                    element_name)) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
                return false;
            }
        } else if (!buffer_appendf(buffer, "&(%s){ ", element_name)) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            return false;
        }
        if (!emit_expression(program, node->left_child, buffer, errors) ||
            !buffer_appendf(buffer, " }, sizeof(%s), _Alignof(%s))",
                            element_name, element_name)) {
            if (!errors->reported) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            }
            return false;
        }
        return true;
    }
    if (node->kind == TYPED_STORE_RESET) {
        const TypedType *type = typed_program_type(program, node->type_index);

        if (type == NULL || type->kind != NIPPY_TYPE_NIL ||
            !store_id_is_valid(program, node->store_id)) {
            error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_EXPRESSION);
            return false;
        }
        if (!buffer_appendf(buffer,
                            "((void)nippy_store_reset(&nippy_stores[%zu]), "
                            "(nippy_nil)0)",
                            node->store_id)) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            return false;
        }
        return true;
    }
    if (node->kind == TYPED_HANDLE_READ) {
        if (node->left_child == TYPED_NO_NODE ||
            !format_c_type(program, node->type_index, type_name,
                           sizeof(type_name), errors)) {
            if (!errors->reported) {
                error_report(errors,
                             MESSAGE_GENERATOR_UNSUPPORTED_EXPRESSION);
            }
            return false;
        }
        if (!buffer_appendf(buffer, "(*(%s *)nippy_handle_pointer(",
                            type_name) ||
            !emit_expression(program, node->left_child, buffer, errors) ||
            !buffer_appendf(buffer, ", sizeof(%s)))", type_name)) {
            if (!errors->reported) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            }
            return false;
        }
        return true;
    }
    if (node->kind == TYPED_HANDLE_WRITE) {
        const TypedNode *handle =
            typed_program_node(program, node->left_child);
        const TypedType *result = typed_program_type(program, node->type_index);
        size_t element;
        bool aggregate;
        char element_name[NIPPY_IDENTIFIER_MAX + 32];

        if (handle == NULL || result == NULL ||
            result->kind != NIPPY_TYPE_NIL ||
            node->right_child == TYPED_NO_NODE ||
            !handle_element_type(program, handle->type_index, &element) ||
            !format_c_type(program, element, element_name,
                           sizeof(element_name), errors)) {
            if (!errors->reported) {
                error_report(errors,
                             MESSAGE_GENERATOR_UNSUPPORTED_EXPRESSION);
            }
            return false;
        }
        aggregate = type_is_c_aggregate(program, element);
        if (!buffer_append(buffer, "((void)nippy_handle_write(") ||
            !emit_expression(program, node->left_child, buffer, errors) ||
            !buffer_append(buffer, ", ")) {
            if (!errors->reported) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            }
            return false;
        }
        if (aggregate) {
            if (!buffer_appendf(
                    buffer, "&(union { %s nippy_value; }){ .nippy_value = ",
                    element_name)) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
                return false;
            }
        } else if (!buffer_appendf(buffer, "&(%s){ ", element_name)) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            return false;
        }
        if (!emit_expression(program, node->right_child, buffer, errors) ||
            !buffer_appendf(buffer, " }, sizeof(%s)), (nippy_nil)0)",
                            element_name)) {
            if (!errors->reported) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            }
            return false;
        }
        return true;
    }
    error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_EXPRESSION);
    return false;
}

/* The checker records the concrete declaration node on a struct type. Older
   programs may only carry the declaration as a root, so fall back to matching
   its name there. */
static size_t find_struct_declaration(const TypedProgram *program,
                                      const TypedType *type)
{
    size_t i;

    if (type->declaration_node != TYPED_NO_NODE) {
        const TypedNode *node =
            typed_program_node(program, type->declaration_node);

        if (node != NULL && node->kind == TYPED_TYPE_DECLARATION) {
            return type->declaration_node;
        }
    }
    for (i = 0; i < program->roots.length; i++) {
        size_t root = *(const size_t *)list_get(&program->roots, i);
        const TypedNode *node = typed_program_node(program, root);

        if (node != NULL && node->kind == TYPED_TYPE_DECLARATION &&
            node->name_index == type->name_index) {
            return root;
        }
    }
    return TYPED_NO_NODE;
}

/* Several type records can describe one struct (for example a declaration and
   an instantiation that share a concrete node). A C typedef must be emitted
   once. */
static bool struct_emitted_before(const TypedProgram *program, size_t index,
                                  size_t declaration_node, size_t name_index)
{
    size_t i;

    for (i = 0; i < index; i++) {
        const TypedType *earlier = typed_program_type(program, i);

        if (earlier == NULL || earlier->kind != NIPPY_TYPE_STRUCT) {
            continue;
        }
        if (earlier->name_index == name_index) {
            return true;
        }
        if (declaration_node != TYPED_NO_NODE &&
            earlier->declaration_node == declaration_node) {
            return true;
        }
    }
    return false;
}

static bool emit_struct_type(const TypedProgram *program, const TypedType *type,
                             size_t declaration_node,
                             const char *effective_source, TextBuffer *header,
                             ErrorState *errors)
{
    const TypedNode *node = typed_program_node(program, declaration_node);
    const char *name = typed_program_symbol_text(program, type->name_index);
    size_t i;

    if (node == NULL || node->kind != TYPED_TYPE_DECLARATION ||
        node->children_count == 0) {
        error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_TYPE);
        return false;
    }
    if (!valid_identifier(name)) {
        error_report(errors, MESSAGE_GENERATOR_INVALID_IDENTIFIER);
        return false;
    }
    if (!buffer_appendf(header, "#line %zu \"%s\"\n", node->pos.line,
                        effective_source) ||
        !buffer_append(header, "typedef struct {\n")) {
        error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
        return false;
    }
    for (i = 0; i < node->children_count; i++) {
        size_t child = *(const size_t *)list_get(&program->children,
                                                 node->children_start + i);
        const TypedNode *field = typed_program_node(program, child);
        const char *field_name;
        char field_type_name[NIPPY_IDENTIFIER_MAX + 32];

        if (field == NULL || field->kind != TYPED_FIELD) {
            error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_FIELD);
            return false;
        }
        if (!format_c_type(program, field->type_index, field_type_name,
                           sizeof(field_type_name), errors)) {
            return false;
        }
        field_name = typed_program_symbol_text(program, field->name_index);
        if (!valid_identifier(field_name)) {
            error_report(errors, MESSAGE_GENERATOR_INVALID_IDENTIFIER);
            return false;
        }
        if (!buffer_appendf(header, "    %s %s;\n", field_type_name,
                            field_name)) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            return false;
        }
    }
    if (!buffer_appendf(header, "} %s;\n\n", name)) {
        error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
        return false;
    }
    return true;
}

/* The member name is the alternative's position, which is also the tag value,
   so a Nil alternative needs no member and no union at all. */
static bool emit_union_type(const TypedProgram *program, const TypedType *type,
                            size_t type_index, TextBuffer *header,
                            ErrorState *errors)
{
    bool has_payload = false;
    size_t i;

    for (i = 0; i < type->child_count; i++) {
        const TypedType *alternative =
            union_alternative_type(program, type, i);

        if (alternative == NULL) {
            error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_TYPE);
            return false;
        }
        if (alternative->kind != NIPPY_TYPE_NIL) {
            has_payload = true;
        }
    }
    if (!buffer_append(header, "typedef struct {\n    uint32_t tag;\n")) {
        error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
        return false;
    }
    if (has_payload) {
        if (!buffer_append(header, "    union {\n")) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            return false;
        }
        for (i = 0; i < type->child_count; i++) {
            const TypedType *alternative =
                union_alternative_type(program, type, i);
            char alternative_name[NIPPY_IDENTIFIER_MAX + 32];

            if (alternative == NULL) {
                error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_TYPE);
                return false;
            }
            if (alternative->kind == NIPPY_TYPE_NIL) {
                continue;
            }
            if (!format_c_type(program,
                               *(const size_t *)list_get(
                                   &program->type_children,
                                   type->first_child + i),
                               alternative_name, sizeof(alternative_name),
                               errors)) {
                return false;
            }
            if (!buffer_appendf(header, "        %s v%zu;\n",
                                alternative_name, i)) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
                return false;
            }
        }
        if (!buffer_append(header, "    } value;\n")) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            return false;
        }
    }
    if (!buffer_appendf(header, "} nippy_sum_%zu;\n\n", type_index)) {
        error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
        return false;
    }
    return true;
}

/* Types are created bottom-up by the checker, so index order places every type
   definition after the definitions it mentions. */
static bool emit_type_definitions(const TypedProgram *program,
                                  const char *effective_source,
                                  TextBuffer *header, ErrorState *errors)
{
    bool emitted_nil = false;
    size_t i;

    for (i = 0; i < program->types.length; i++) {
        const TypedType *type = typed_program_type(program, i);

        if (type == NULL || type->kind == NIPPY_TYPE_UNKNOWN) {
            error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_TYPE);
            return false;
        }
        if (type->kind == NIPPY_TYPE_NIL) {
            if (!emitted_nil) {
                if (!buffer_append(header,
                                   "typedef uint8_t nippy_nil;\n\n")) {
                    error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
                    return false;
                }
                emitted_nil = true;
            }
        } else if (type->kind == NIPPY_TYPE_STRUCT) {
            size_t declaration_node = find_struct_declaration(program, type);

            if (declaration_node == TYPED_NO_NODE) {
                error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_TYPE);
                return false;
            }
            if (struct_emitted_before(program, i, declaration_node,
                                      type->name_index)) {
                continue;
            }
            if (!emit_struct_type(program, type, declaration_node,
                                  effective_source, header, errors)) {
                return false;
            }
        } else if (type->kind == NIPPY_TYPE_UNION) {
            if (!emit_union_type(program, type, i, header, errors)) {
                return false;
            }
        }
    }
    return true;
}

static bool emit_function_typedefs(const TypedProgram *program,
                                   TextBuffer *header, ErrorState *errors)
{
    size_t i;

    if (program->signatures.length == 0) {
        return true;
    }
    for (i = 0; i < program->signatures.length; i++) {
        const TypedFunctionSignature *signature =
            typed_program_signature(program, i);
        char return_name[NIPPY_IDENTIFIER_MAX + 32];
        size_t j;

        if (signature == NULL ||
            !format_c_type(program, signature->return_type, return_name,
                           sizeof(return_name), errors)) {
            if (!errors->reported) {
                error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_TYPE);
            }
            return false;
        }
        if (!buffer_appendf(header, "typedef %s (*nippy_fun_%zu)(",
                            return_name, i)) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            return false;
        }
        if (signature->parameter_count == 0) {
            if (!buffer_append(header, "void")) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
                return false;
            }
        } else {
            for (j = 0; j < signature->parameter_count; j++) {
                size_t parameter_type = *(const size_t *)list_get(
                    &program->signature_parameters,
                    signature->first_parameter + j);
                char parameter_name[NIPPY_IDENTIFIER_MAX + 32];

                if (!format_c_type(program, parameter_type, parameter_name,
                                   sizeof(parameter_name), errors)) {
                    if (!errors->reported) {
                        error_report(errors,
                                     MESSAGE_GENERATOR_UNSUPPORTED_TYPE);
                    }
                    return false;
                }
                if (j > 0 && !buffer_append(header, ", ")) {
                    error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
                    return false;
                }
                if (!buffer_append(header, parameter_name)) {
                    error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
                    return false;
                }
            }
        }
        if (!buffer_append(header, ");\n")) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            return false;
        }
    }
    if (!buffer_append(header, "\n")) {
        error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
        return false;
    }
    return true;
}

static bool append_indent(TextBuffer *source, size_t depth)
{
    size_t i;

    for (i = 0; i < depth; i++) {
        if (!buffer_append(source, "    ")) {
            return false;
        }
    }
    return true;
}

static bool emit_statement(const TypedProgram *program, size_t node_index,
                           size_t depth, const char *effective_source,
                           TextBuffer *source, ErrorState *errors);

static bool emit_block(const TypedProgram *program, size_t block_index,
                       size_t depth, const char *effective_source,
                       TextBuffer *source, ErrorState *errors)
{
    const TypedNode *block = typed_program_node(program, block_index);
    size_t i;

    if (block == NULL || block->kind != TYPED_BLOCK) {
        error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_STATEMENT);
        return false;
    }
    for (i = 0; i < block->children_count; i++) {
        size_t child = *(const size_t *)list_get(&program->children,
                                                 block->children_start + i);

        if (!emit_statement(program, child, depth, effective_source, source,
                            errors)) {
            return false;
        }
    }
    return true;
}

static bool emit_statement(const TypedProgram *program, size_t node_index,
                           size_t depth, const char *effective_source,
                           TextBuffer *source, ErrorState *errors)
{
    const TypedNode *node = typed_program_node(program, node_index);

    if (node == NULL) {
        error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_STATEMENT);
        return false;
    }
    if (!buffer_appendf(source, "#line %zu \"%s\"\n", node->pos.line,
                        effective_source)) {
        error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
        return false;
    }

    if (node->kind == TYPED_RETURN) {
        if (!append_indent(source, depth) ||
            !buffer_append(source, "return ") ||
            !emit_expression(program, node->left_child, source, errors) ||
            !buffer_append(source, ";\n")) {
            if (!errors->reported) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            }
            return false;
        }
        return true;
    }

    if (node->kind == TYPED_BREAK || node->kind == TYPED_CONTINUE) {
        if (!append_indent(source, depth) ||
            !buffer_append(source,
                           node->kind == TYPED_BREAK ? "break;\n"
                                                     : "continue;\n")) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            return false;
        }
        return true;
    }

    if (node->kind == TYPED_IF) {
        size_t then_block;
        size_t else_block = TYPED_NO_NODE;

        if (node->children_count == 0) {
            error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_STATEMENT);
            return false;
        }
        then_block = *(const size_t *)list_get(&program->children,
                                               node->children_start);
        if (node->children_count > 1) {
            else_block = *(const size_t *)list_get(&program->children,
                                                   node->children_start + 1);
        }
        if (!append_indent(source, depth) ||
            !buffer_append(source, "if (") ||
            !emit_expression(program, node->left_child, source, errors) ||
            !buffer_append(source, ") {\n") ||
            !emit_block(program, then_block, depth + 1, effective_source,
                        source, errors)) {
            if (!errors->reported) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            }
            return false;
        }
        if (else_block != TYPED_NO_NODE) {
            if (!append_indent(source, depth) ||
                !buffer_append(source, "} else {\n") ||
                !emit_block(program, else_block, depth + 1, effective_source,
                            source, errors)) {
                if (!errors->reported) {
                    error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
                }
                return false;
            }
        }
        if (!append_indent(source, depth) || !buffer_append(source, "}\n")) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            return false;
        }
        return true;
    }

    if (node->kind == TYPED_WHILE) {
        if (!append_indent(source, depth) ||
            !buffer_append(source, "while (") ||
            !emit_expression(program, node->left_child, source, errors) ||
            !buffer_append(source, ") {\n") ||
            !emit_block(program, node->right_child, depth + 1,
                        effective_source, source, errors) ||
            !append_indent(source, depth) ||
            !buffer_append(source, "}\n")) {
            if (!errors->reported) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            }
            return false;
        }
        return true;
    }

    if (node->kind == TYPED_EXPRESSION_STATEMENT) {
        /* The outer cast discards the expression's value so a store reset or
           write statement is warning-free under -Wall. */
        if (!append_indent(source, depth) ||
            !buffer_append(source, "(void)(") ||
            !emit_expression(program, node->left_child, source, errors) ||
            !buffer_append(source, ");\n")) {
            if (!errors->reported) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            }
            return false;
        }
        return true;
    }

    if (node->kind == TYPED_BLOCK) {
        return emit_block(program, node_index, depth, effective_source,
                          source, errors);
    }

    error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_STATEMENT);
    return false;
}

static bool emit_function(const TypedProgram *program, const TypedNode *node,
                          const char *effective_source, TextBuffer *source,
                          ErrorState *errors)
{
    const char *name = typed_program_symbol_text(program, node->name_index);
    const TypedType *type = typed_program_type(program, node->type_index);
    const TypedFunctionSignature *signature;
    char return_name[NIPPY_IDENTIFIER_MAX + 32];
    size_t i;

    if (!valid_identifier(name) || type == NULL ||
        type->kind != NIPPY_TYPE_FUNCTION) {
        error_report(errors, valid_identifier(name)
                                 ? MESSAGE_GENERATOR_UNSUPPORTED_FUNCTION
                                 : MESSAGE_GENERATOR_INVALID_IDENTIFIER);
        return false;
    }
    signature = typed_program_signature(program, type->signature_index);
    if (signature == NULL ||
        !format_c_type(program, signature->return_type, return_name,
                       sizeof(return_name), errors)) {
        if (!errors->reported) {
            error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_FUNCTION);
        }
        return false;
    }

    if (!buffer_appendf(source, "#line %zu \"%s\"\n", node->pos.line,
                        effective_source) ||
        !buffer_appendf(source, "%s %s(", return_name, name)) {
        error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
        return false;
    }

    if (node->children_count == 0) {
        if (!buffer_append(source, "void")) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            return false;
        }
    } else {
        for (i = 0; i < node->children_count; i++) {
            size_t child = *(const size_t *)list_get(&program->children,
                                                     node->children_start + i);
            const TypedNode *parameter = typed_program_node(program, child);
            const char *parameter_name;
            char parameter_type[NIPPY_IDENTIFIER_MAX + 32];

            if (parameter == NULL || parameter->kind != TYPED_PARAMETER ||
                !format_c_type(program, parameter->type_index, parameter_type,
                               sizeof(parameter_type), errors)) {
                if (!errors->reported) {
                    error_report(errors,
                                 MESSAGE_GENERATOR_UNSUPPORTED_FUNCTION);
                }
                return false;
            }
            parameter_name = typed_program_symbol_text(program,
                                                       parameter->name_index);
            if (!valid_identifier(parameter_name)) {
                error_report(errors, MESSAGE_GENERATOR_INVALID_IDENTIFIER);
                return false;
            }
            if (i > 0 && !buffer_append(source, ", ")) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
                return false;
            }
            if (!buffer_appendf(source, "%s %s", parameter_type,
                                parameter_name)) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
                return false;
            }
        }
    }

    if (!buffer_append(source, ")\n{\n")) {
        error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
        return false;
    }

    for (i = 0; i < node->children_count; i++) {
        size_t child = *(const size_t *)list_get(&program->children,
                                                 node->children_start + i);
        const TypedNode *parameter = typed_program_node(program, child);
        const char *parameter_name = typed_program_symbol_text(
            program, parameter->name_index);

        if (!buffer_appendf(source, "    (void)%s;\n", parameter_name)) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            return false;
        }
    }

    if (!emit_block(program, node->left_child, 1, effective_source, source,
                    errors) ||
        !buffer_append(source, "}\n\n")) {
        if (!errors->reported) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
        }
        return false;
    }
    return true;
}

/* The store runtime is emitted once, ahead of every function, so helpers and
   the table are visible to all generated code. Only programs with a store
   declaration get it; a program that merely mentions Handle<T> needs only the
   header typedef. */
static bool emit_store_runtime(TextBuffer *source, size_t store_count,
                               ErrorState *errors)
{
    bool ok = true;

    ok = ok && buffer_appendf(
                   source,
                   "#define NIPPY_STORE_COUNT %zu\n\n"
                   "static nippy_store nippy_stores[NIPPY_STORE_COUNT];\n\n",
                   store_count);
    ok = ok && buffer_append(
                   source,
                   "static void nippy_panic(const char *message)\n"
                   "{\n"
                   "    fprintf(stderr, \"%s\\n\", message);\n"
                   "    exit(1);\n"
                   "}\n\n");
    ok = ok && buffer_append(
                   source,
                   "static size_t nippy_store_align(size_t offset, "
                   "size_t alignment)\n"
                   "{\n"
                   "    size_t remainder;\n\n"
                   "    if (alignment <= 1) {\n"
                   "        return offset;\n"
                   "    }\n"
                   "    remainder = offset % alignment;\n"
                   "    if (remainder == 0) {\n"
                   "        return offset;\n"
                   "    }\n"
                   "    if (offset > (size_t)-1 - (alignment - remainder)) {\n"
                   "        nippy_panic(\"" MESSAGE_RUNTIME_OUT_OF_MEMORY "\");\n"
                   "    }\n"
                   "    return offset + (alignment - remainder);\n"
                   "}\n\n");
    ok = ok && buffer_append(
                   source,
                   "static void nippy_store_init(nippy_store *store, "
                   "size_t initial_bytes)\n"
                   "{\n"
                   "    if (initial_bytes == 0) {\n"
                   "        initial_bytes = 1;\n"
                   "    }\n"
                   "    store->data = malloc(initial_bytes);\n"
                   "    if (store->data == NULL) {\n"
                   "        nippy_panic(\"" MESSAGE_RUNTIME_OUT_OF_MEMORY
                   "\");\n"
                   "    }\n"
                   "    store->used = 0;\n"
                   "    store->capacity = initial_bytes;\n"
                   "    store->generation = 1;\n"
                   "}\n\n");
    ok = ok && buffer_append(
                   source,
                   "static void nippy_store_reserve(nippy_store *store, "
                   "size_t required)\n"
                   "{\n"
                   "    size_t capacity;\n"
                   "    unsigned char *grown;\n\n"
                   "    if (required <= store->capacity) {\n"
                   "        return;\n"
                   "    }\n"
                   "    capacity = store->capacity == 0 ? 1 : "
                   "store->capacity;\n"
                   "    while (capacity < required) {\n"
                   "        if (capacity > (size_t)-1 / 2) {\n"
                   "            nippy_panic(\"" MESSAGE_RUNTIME_OUT_OF_MEMORY
                   "\");\n"
                   "        }\n"
                   "        capacity *= 2;\n"
                   "    }\n"
                   "    grown = realloc(store->data, capacity);\n"
                   "    if (grown == NULL) {\n"
                   "        nippy_panic(\"" MESSAGE_RUNTIME_OUT_OF_MEMORY
                   "\");\n"
                   "    }\n"
                   "    store->data = grown;\n"
                   "    store->capacity = capacity;\n"
                   "}\n\n");
    ok = ok && buffer_append(
                   source,
                   "static nippy_handle nippy_store_alloc(nippy_store *store, "
                   "size_t id,\n"
                   "                                      const void *value, "
                   "size_t size,\n"
                   "                                      size_t align)\n"
                   "{\n"
                   "    nippy_handle handle;\n"
                   "    size_t offset;\n"
                   "    size_t end;\n\n"
                   "    if (id >= (size_t)NIPPY_STORE_COUNT) {\n"
                   "        nippy_panic(\"" MESSAGE_RUNTIME_STORE_RANGE
                   "\");\n"
                   "    }\n"
                   "    if (size == 0) {\n"
                   "        nippy_panic(\"" MESSAGE_RUNTIME_HANDLE_BOUNDS
                   "\");\n"
                   "    }\n"
                   "    offset = nippy_store_align(store->used, align);\n"
                   "    if (offset > (size_t)-1 - size) {\n"
                   "        nippy_panic(\"" MESSAGE_RUNTIME_HANDLE_BOUNDS
                   "\");\n"
                   "    }\n"
                   "    end = offset + size;\n"
                   "    nippy_store_reserve(store, end);\n"
                   "    memcpy(store->data + offset, value, size);\n"
                   "    store->used = end;\n"
                   "    handle.store = (uint32_t)id;\n"
                   "    handle.offset = offset;\n"
                   "    handle.generation = store->generation;\n"
                   "    return handle;\n"
                   "}\n\n");
    ok = ok && buffer_append(
                   source,
                   "static unsigned char *nippy_handle_pointer("
                   "nippy_handle handle,\n"
                   "                                           size_t size)\n"
                   "{\n"
                   "    nippy_store *store;\n\n"
                   "    if ((size_t)handle.store >= "
                   "(size_t)NIPPY_STORE_COUNT) {\n"
                   "        nippy_panic(\"" MESSAGE_RUNTIME_STORE_RANGE
                   "\");\n"
                   "    }\n"
                   "    store = &nippy_stores[handle.store];\n"
                   "    if (handle.generation != store->generation) {\n"
                   "        nippy_panic(\"" MESSAGE_RUNTIME_STALE_HANDLE
                   "\");\n"
                   "    }\n"
                   "    if (handle.offset > store->used ||\n"
                   "        size > store->used - handle.offset) {\n"
                   "        nippy_panic(\"" MESSAGE_RUNTIME_HANDLE_BOUNDS
                   "\");\n"
                   "    }\n"
                   "    return store->data + handle.offset;\n"
                   "}\n\n");
    ok = ok && buffer_append(
                   source,
                   "static void nippy_handle_write(nippy_handle handle, "
                   "const void *value,\n"
                   "                               size_t size)\n"
                   "{\n"
                   "    memcpy(nippy_handle_pointer(handle, size), value, "
                   "size);\n"
                   "}\n\n");
    ok = ok && buffer_append(
                   source,
                   "static void nippy_store_reset(nippy_store *store)\n"
                   "{\n"
                   "    store->used = 0;\n"
                   "    if (store->generation == UINT64_MAX) {\n"
                   "        nippy_panic(\"" MESSAGE_RUNTIME_STALE_HANDLE
                   "\");\n"
                   "    }\n"
                   "    store->generation++;\n"
                   "}\n\n");
    /* Referencing the helpers here keeps the whole runtime "used" even when a
       particular program never allocates, writes, or resets. Generated C is
       compiled with -Wall -Wextra. */
    ok = ok && buffer_append(
                   source,
                   "static void nippy_store_free(nippy_store *store)\n"
                   "{\n"
                   "    free(store->data);\n"
                   "    store->data = NULL;\n"
                   "    store->used = 0;\n"
                   "    store->capacity = 0;\n"
                   "    store->generation = 0;\n"
                   "    (void)nippy_store_reserve;\n"
                   "    (void)nippy_store_alloc;\n"
                   "    (void)nippy_handle_pointer;\n"
                   "    (void)nippy_handle_write;\n"
                   "    (void)nippy_store_reset;\n"
                   "}\n\n");
    if (!ok) {
        error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
        return false;
    }
    return true;
}

static bool emit_store_inits(const TypedProgram *program, size_t store_count,
                             TextBuffer *source, ErrorState *errors)
{
    size_t id;

    for (id = 0; id < store_count; id++) {
        size_t declaration_index = find_store_declaration(program, id);
        const TypedNode *declaration =
            typed_program_node(program, declaration_index);

        if (declaration == NULL || declaration->kind != TYPED_STORE_DECLARATION ||
            declaration->integer_value < 0) {
            error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_ROOT);
            return false;
        }
        if (!buffer_appendf(source,
                            "    nippy_store_init(&nippy_stores[%zu], %d);\n",
                            id, declaration->integer_value)) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            return false;
        }
    }
    return true;
}

static bool emit_store_frees(size_t store_count, TextBuffer *source,
                             ErrorState *errors)
{
    size_t id;

    for (id = 0; id < store_count; id++) {
        if (!buffer_appendf(source, "    nippy_store_free(&nippy_stores[%zu]);\n",
                            id)) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            return false;
        }
    }
    return true;
}

static bool emit_var_to_main(const TypedProgram *program, const TypedNode *node,
                             const char *effective_source, TextBuffer *source,
                             ErrorState *errors)
{
    const char *name = typed_program_symbol_text(program, node->name_index);
    char type_name[NIPPY_IDENTIFIER_MAX + 32];

    if (!valid_identifier(name) ||
        !format_c_type(program, node->type_index, type_name,
                       sizeof(type_name), errors)) {
        if (!errors->reported) {
            error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_ROOT);
        }
        return false;
    }
    if (!buffer_appendf(source, "#line %zu \"%s\"\n", node->pos.line,
                        effective_source) ||
        !buffer_appendf(source, "    %s %s = ", type_name, name) ||
        !emit_expression(program, node->left_child, source, errors) ||
        !buffer_append(source, ";\n") ||
        !buffer_appendf(source, "    (void)%s;\n", name)) {
        if (!errors->reported) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
        }
        return false;
    }
    return true;
}

static bool append_print_program(const TypedProgram *program,
                                 const TypedNode *var, size_t store_count,
                                 const char *effective_source,
                                 TextBuffer *source, ErrorState *errors)
{
    const char *name = typed_program_symbol_text(program, var->name_index);

    if (!valid_identifier(name)) {
        error_report(errors, MESSAGE_GENERATOR_INVALID_IDENTIFIER);
        return false;
    }

    if (!buffer_append(source, "int32_t nippy_program_value(void)\n{\n") ||
        !buffer_appendf(source, "#line %zu \"%s\"\n", var->pos.line,
                        effective_source) ||
        !buffer_appendf(source, "    int32_t %s = ", name) ||
        !emit_expression(program, var->left_child, source, errors) ||
        !buffer_append(source, ";\n") ||
        !buffer_appendf(source, "    return %s;\n", name) ||
        !buffer_append(source, "}\n\nint main(void)\n{\n") ||
        !emit_store_inits(program, store_count, source, errors) ||
        !buffer_append(source,
                       "    printf(\"%d\\n\", (int)nippy_program_value());\n") ||
        !emit_store_frees(store_count, source, errors) ||
        !buffer_append(source, "    return 0;\n}\n")) {
        if (!errors->reported) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
        }
        return false;
    }
    return true;
}

static size_t count_store_declarations(const TypedProgram *program)
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

/* A store or handle anywhere in the resolved type table means the header must
   define the runtime record shapes, even if the program never declares a
   store. */
static bool program_uses_handle_or_store_type(const TypedProgram *program)
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

static bool validate_store_declarations(const TypedProgram *program,
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

bool generator_generate(const TypedProgram *program, const char *header_name,
                        const char *source_name, char **c_source,
                        char **h_source, ErrorState *errors)
{
    const char *effective_header = header_name == NULL ? "nippy.h" : header_name;
    const char *effective_source = source_name == NULL ? "nippy.nip" : source_name;
    TextBuffer header;
    TextBuffer source;
    size_t var_count = 0;
    const TypedNode *single_var = NULL;
    bool print_program = false;
    bool has_expression_statement = false;
    bool uses_store_or_handle;
    size_t store_count;
    size_t i;
    bool ok = false;

    *c_source = NULL;
    *h_source = NULL;

    if (!valid_header_name(effective_header)) {
        error_report(errors, MESSAGE_GENERATOR_INVALID_HEADER_NAME);
        return false;
    }
    if (!valid_line_name(effective_source)) {
        error_report(errors, MESSAGE_GENERATOR_INVALID_LINE_NAME);
        return false;
    }

    buffer_init(&header);
    buffer_init(&source);

    store_count = count_store_declarations(program);
    if (!validate_store_declarations(program, store_count, errors)) {
        goto done;
    }
    uses_store_or_handle =
        store_count > 0 || program_uses_handle_or_store_type(program);

    if (!buffer_append(&header,
                       "#ifndef NIPPY_GENERATED_H\n"
                       "#define NIPPY_GENERATED_H\n\n"
                       "#include <stdbool.h>\n"
                       "#include <stdint.h>\n")) {
        error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
        goto done;
    }
    if (uses_store_or_handle) {
        if (!buffer_append(&header, "#include <stddef.h>\n")) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            goto done;
        }
    }
    if (!buffer_append(&header, "\n")) {
        error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
        goto done;
    }

    if (uses_store_or_handle) {
        if (!buffer_append(
                &header,
                "typedef struct {\n"
                "    uint32_t store;\n"
                "    size_t offset;\n"
                "    uint64_t generation;\n"
                "} nippy_handle;\n\n"
                "typedef struct {\n"
                "    unsigned char *data;\n"
                "    size_t used;\n"
                "    size_t capacity;\n"
                "    uint64_t generation;\n"
                "} nippy_store;\n\n")) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            goto done;
        }
    }

    if (!emit_type_definitions(program, effective_source, &header, errors)) {
        goto done;
    }

    for (i = 0; i < program->roots.length; i++) {
        size_t root = *(const size_t *)list_get(&program->roots, i);
        const TypedNode *node = typed_program_node(program, root);

        if (node == NULL) {
            error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_ROOT);
            goto done;
        }
        if (node->kind == TYPED_TYPE_DECLARATION) {
            /* Struct and union definitions come from program->types so that
               instantiations that have no root are emitted too. */
            continue;
        } else if (node->kind == TYPED_STORE_DECLARATION) {
            /* Store roots become table entries and main() init calls, never
               C locals. */
            continue;
        } else if (node->kind == TYPED_DECLARATION) {
            var_count++;
            single_var = node;
        } else if (node->kind == TYPED_EXPRESSION_STATEMENT) {
            has_expression_statement = true;
        } else if (node->kind != TYPED_FUNCTION_DECLARATION) {
            error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_ROOT);
            goto done;
        }
    }

    if (!emit_function_typedefs(program, &header, errors)) {
        goto done;
    }

    if (!has_expression_statement && var_count == 1 &&
        typed_program_type(program, single_var->type_index) != NULL &&
        typed_program_type(program, single_var->type_index)->kind ==
            NIPPY_TYPE_INT32) {
        print_program = true;
    }

    if (print_program &&
        !buffer_append(&header, "int32_t nippy_program_value(void);\n\n")) {
        error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
        goto done;
    }
    if (!buffer_append(&header, "#endif\n")) {
        error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
        goto done;
    }

    if (!buffer_appendf(&source, "#include \"%s\"\n\n", effective_header)) {
        error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
        goto done;
    }
    if (store_count > 0) {
        if (!buffer_append(&source,
                           "#include <stdio.h>\n"
                           "#include <stdlib.h>\n"
                           "#include <string.h>\n\n")) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            goto done;
        }
        if (!emit_store_runtime(&source, store_count, errors)) {
            goto done;
        }
    } else if (print_program &&
               !buffer_append(&source, "#include <stdio.h>\n\n")) {
        error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
        goto done;
    }

    for (i = 0; i < program->roots.length; i++) {
        size_t root = *(const size_t *)list_get(&program->roots, i);
        const TypedNode *node = typed_program_node(program, root);

        if (node->kind == TYPED_FUNCTION_DECLARATION &&
            !emit_function(program, node, effective_source, &source, errors)) {
            goto done;
        }
    }

    if (print_program) {
        if (!append_print_program(program, single_var, store_count,
                                  effective_source, &source, errors)) {
            goto done;
        }
    } else {
        if (!buffer_append(&source, "int main(void)\n{\n") ||
            !emit_store_inits(program, store_count, &source, errors)) {
            if (!errors->reported) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            }
            goto done;
        }
        for (i = 0; i < program->roots.length; i++) {
            size_t root = *(const size_t *)list_get(&program->roots, i);
            const TypedNode *node = typed_program_node(program, root);

            if (node->kind == TYPED_DECLARATION &&
                !emit_var_to_main(program, node, effective_source, &source,
                                  errors)) {
                goto done;
            }
            if (node->kind == TYPED_EXPRESSION_STATEMENT &&
                !emit_statement(program, root, 1, effective_source, &source,
                                errors)) {
                goto done;
            }
        }
        if (!emit_store_frees(store_count, &source, errors) ||
            !buffer_append(&source, "    return 0;\n}\n")) {
            if (!errors->reported) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            }
            goto done;
        }
    }

    *h_source = header.text;
    header.text = NULL;
    *c_source = source.text;
    source.text = NULL;
    ok = true;

done:
    buffer_free(&header);
    buffer_free(&source);
    return ok;
}
