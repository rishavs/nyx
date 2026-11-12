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

#if defined(__GNUC__) || defined(__clang__)
static bool buffer_appendf(TextBuffer *buffer, const char *format, ...)
    __attribute__((format(printf, 2, 3)));
#endif
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
    if (type->kind == NIPPY_TYPE_STRING) {
        snprintf(out, out_size, "nippy_string_%zu", type->fixed_length);
        return true;
    }
    if (type->kind == NIPPY_TYPE_DYNAMIC_STRING) {
        snprintf(out, out_size, "nippy_string");
        return true;
    }
    if (type->kind == NIPPY_TYPE_TEXT_BUILDER) {
        snprintf(out, out_size, "nippy_builder");
        return true;
    }
    if (type->kind == NIPPY_TYPE_LIST) {
        snprintf(out, out_size, "nippy_list");
        return true;
    }
    if (type->kind == NIPPY_TYPE_FIXED_LIST) {
        snprintf(out, out_size, "nippy_list_%zu", type_index);
        return true;
    }
    if (type->kind == NIPPY_TYPE_SLICE) {
        snprintf(out, out_size, "nippy_slice");
        return true;
    }
    if (type->kind == NIPPY_TYPE_RECORD) {
        snprintf(out, out_size, "nippy_record_%zu", type_index);
        return true;
    }
    if (type->kind == NIPPY_TYPE_TABLE) {
        snprintf(out, out_size, "nippy_table");
        return true;
    }
    if (type->kind == NIPPY_TYPE_ERROR) {
        snprintf(out, out_size, "nippy_error");
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

/* Inline function-pointer spelling for contexts that cannot wait for the
   nippy_fun_N typedefs (fixed-list elements and union payloads). Nested function
   parameter/return types are rejected in this context. */
static bool format_function_pointer(const TypedProgram *program,
                                    size_t type_index, const char *name,
                                    char *out, size_t out_size,
                                    ErrorState *errors)
{
    const TypedType *type = typed_program_type(program, type_index);
    const TypedFunctionSignature *signature;
    char return_name[NIPPY_IDENTIFIER_MAX + 32];
    char params[512];
    size_t used = 0;
    size_t i;

    if (type == NULL || type->kind != NIPPY_TYPE_FUNCTION ||
        type->signature_index == TYPED_NO_NODE) {
        error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_TYPE);
        return false;
    }
    signature = typed_program_signature(program, type->signature_index);
    if (signature == NULL ||
        !format_c_type(program, signature->return_type, return_name,
                       sizeof(return_name), errors)) {
        return false;
    }
    {
        const TypedType *return_type = typed_program_type(
            program, signature->return_type);

        if (return_type != NULL && return_type->kind == NIPPY_TYPE_FUNCTION) {
            error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_TYPE);
            return false;
        }
    }
    params[0] = '\0';
    if (signature->parameter_count == 0) {
        snprintf(params, sizeof(params), "void");
    } else {
        for (i = 0; i < signature->parameter_count; i++) {
            size_t parameter = *(const size_t *)list_get(
                &program->signature_parameters,
                signature->first_parameter + i);
            const TypedType *parameter_type = typed_program_type(program,
                                                                 parameter);
            char parameter_name[NIPPY_IDENTIFIER_MAX + 32];
            int written;

            if (parameter_type == NULL ||
                parameter_type->kind == NIPPY_TYPE_FUNCTION ||
                !format_c_type(program, parameter, parameter_name,
                               sizeof(parameter_name), errors)) {
                return false;
            }
            written = snprintf(params + used, sizeof(params) - used, "%s%s",
                               i == 0 ? "" : ", ", parameter_name);
            if (written < 0 ||
                (size_t)written >= sizeof(params) - used) {
                error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_TYPE);
                return false;
            }
            used += (size_t)written;
        }
    }
    if (snprintf(out, out_size, "%s (*%s)(%s)", return_name, name, params) <
            0 ||
        strlen(out) >= out_size - 1) {
        error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_TYPE);
        return false;
    }
    return true;
}

static bool fixed_list_element_name(const TypedProgram *program,
                                    size_t list_type_index, size_t element,
                                    char *out, size_t out_size,
                                    ErrorState *errors)
{
    const TypedType *element_type = typed_program_type(program, element);

    if (element_type != NULL && element_type->kind == NIPPY_TYPE_FUNCTION) {
        if (snprintf(out, out_size, "nippy_list_%zu_elem",
                     list_type_index) < 0 ||
            strlen(out) >= out_size - 1) {
            error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_TYPE);
            return false;
        }
        return true;
    }
    return format_c_type(program, element, out, out_size, errors);
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
   `typeof x is T` matches alternatives structurally. The depth bound keeps a function
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
                            type->kind == NIPPY_TYPE_HANDLE ||
                            type->kind == NIPPY_TYPE_STRING ||
                            type->kind == NIPPY_TYPE_DYNAMIC_STRING ||
                            type->kind == NIPPY_TYPE_TEXT_BUILDER ||
                            type->kind == NIPPY_TYPE_LIST ||
                            type->kind == NIPPY_TYPE_FIXED_LIST ||
                            type->kind == NIPPY_TYPE_SLICE ||
                            type->kind == NIPPY_TYPE_RECORD ||
                            type->kind == NIPPY_TYPE_TABLE ||
                            type->kind == NIPPY_TYPE_UNION ||
                            type->kind == NIPPY_TYPE_ERROR);
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
    case NIPPY_OPERATOR_SUB:
        return "-";
    case NIPPY_OPERATOR_MUL:
        return "*";
    case NIPPY_OPERATOR_DIV:
        return "/";
    case NIPPY_OPERATOR_MOD:
        return "%";
    case NIPPY_OPERATOR_BIT_AND:
        return "&";
    case NIPPY_OPERATOR_BIT_OR:
        return "|";
    case NIPPY_OPERATOR_BIT_XOR:
        return "^";
    case NIPPY_OPERATOR_SHL:
        return "<<";
    case NIPPY_OPERATOR_SHR:
        return ">>";
    case NIPPY_OPERATOR_NEG:
        return "-";
    case NIPPY_OPERATOR_BIT_NOT:
        return "~";
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
    if (node->kind == TYPED_IF_EXPRESSION) {
        size_t else_expression = *(const size_t *)list_get(
            &program->children, node->children_start);

        if (!buffer_append(buffer, "(") ||
            !emit_expression(program, node->left_child, buffer, errors) ||
            !buffer_append(buffer, " ? ") ||
            !emit_expression(program, node->right_child, buffer, errors) ||
            !buffer_append(buffer, " : ") ||
            !emit_expression(program, else_expression, buffer, errors) ||
            !buffer_append(buffer, ")")) {
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
            /* A type test on a non-union value is decided statically. */
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
    if (node->kind == TYPED_STRING_LITERAL) {
        const TypedType *type = typed_program_type(program, node->type_index);
        char type_name[NIPPY_IDENTIFIER_MAX + 32];
        size_t i;

        if (type == NULL) {
            error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_EXPRESSION);
            return false;
        }
        if (type->kind == NIPPY_TYPE_DYNAMIC_STRING) {
            if (!buffer_appendf(buffer,
                                "(nippy_string){ nippy_dyn_str_%zu, 0, 0, "
                                "%zu, 0 }",
                                node_index, node->string_length)) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
                return false;
            }
            return true;
        }
        if (type->kind != NIPPY_TYPE_STRING ||
            !format_c_type(program, node->type_index, type_name,
                           sizeof(type_name), errors)) {
            if (!errors->reported) {
                error_report(errors,
                             MESSAGE_GENERATOR_UNSUPPORTED_EXPRESSION);
            }
            return false;
        }
        if (!buffer_appendf(buffer, "(%s){ { ", type_name)) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            return false;
        }
        for (i = 0; i < node->string_length; i++) {
            const unsigned char *byte = list_get(
                &program->bytes, node->string_offset + i);

            if (byte == NULL ||
                !buffer_appendf(buffer, "%u, ", (unsigned)*byte)) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
                return false;
            }
        }
        if (!buffer_appendf(buffer, "0 }, %zu }", node->string_length)) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            return false;
        }
        return true;
    }
    if (node->kind == TYPED_STRING_LEN) {
        const TypedNode *receiver =
            typed_program_node(program, node->left_child);
        const TypedType *receiver_type =
            receiver == NULL
                ? NULL
                : typed_program_type(program, receiver->type_index);

        if (receiver_type != NULL &&
            receiver_type->kind == NIPPY_TYPE_DYNAMIC_STRING) {
            if (!buffer_append(buffer, "nippy_string_len(") ||
                !emit_expression(program, node->left_child, buffer, errors) ||
                !buffer_append(buffer, ")")) {
                if (!errors->reported) {
                    error_report(errors,
                                 MESSAGE_GENERATOR_OUT_OF_MEMORY);
                }
                return false;
            }
        } else if (!buffer_append(buffer, "((int32_t)(") ||
                   !emit_expression(program, node->left_child, buffer,
                                    errors) ||
                   !buffer_append(buffer, ").length)")) {
            if (!errors->reported) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            }
            return false;
        }
        return true;
    }
    if (node->kind == TYPED_STRING_AT) {
        const TypedNode *receiver =
            typed_program_node(program, node->left_child);
        const TypedType *receiver_type = receiver == NULL
                                             ? NULL
                                             : typed_program_type(
                                                   program,
                                                   receiver->type_index);

        if (receiver_type == NULL ||
            (receiver_type->kind != NIPPY_TYPE_STRING &&
             receiver_type->kind != NIPPY_TYPE_DYNAMIC_STRING)) {
            error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_EXPRESSION);
            return false;
        }
        if (receiver_type->kind == NIPPY_TYPE_DYNAMIC_STRING) {
            if (!buffer_append(buffer, "nippy_string_byte_at(") ||
                !emit_expression(program, node->left_child, buffer, errors) ||
                !buffer_append(buffer, ", ") ||
                !emit_expression(program, node->right_child, buffer,
                                 errors) ||
                !buffer_append(buffer, ")")) {
                if (!errors->reported) {
                    error_report(errors,
                                 MESSAGE_GENERATOR_OUT_OF_MEMORY);
                }
                return false;
            }
            return true;
        }
        if (!buffer_appendf(buffer, "nippy_string_%zu_at(",
                            receiver_type->fixed_length) ||
            !emit_expression(program, node->left_child, buffer, errors) ||
            !buffer_append(buffer, ", ") ||
            !emit_expression(program, node->right_child, buffer, errors) ||
            !buffer_append(buffer, ")")) {
            if (!errors->reported) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            }
            return false;
        }
        return true;
    }
    if (node->kind == TYPED_STRING_SLICE) {
        if (node->left_child == TYPED_NO_NODE ||
            node->children_count != 2) {
            error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_EXPRESSION);
            return false;
        }
        {
            size_t start = *(const size_t *)list_get(
                &program->children, node->children_start);
            size_t length = *(const size_t *)list_get(
                &program->children, node->children_start + 1);

            if (!buffer_append(buffer, "nippy_string_slice(") ||
                !emit_expression(program, node->left_child, buffer,
                                 errors) ||
                !buffer_append(buffer, ", ") ||
                !emit_expression(program, start, buffer, errors) ||
                !buffer_append(buffer, ", ") ||
                !emit_expression(program, length, buffer, errors) ||
                !buffer_append(buffer, ")")) {
                if (!errors->reported) {
                    error_report(errors,
                                 MESSAGE_GENERATOR_OUT_OF_MEMORY);
                }
                return false;
            }
        }
        return true;
    }
    if (node->kind == TYPED_STRING_CONCAT ||
        node->kind == TYPED_STRING_COPY) {
        if (node->left_child == TYPED_NO_NODE ||
            !store_id_is_valid(program, node->store_id)) {
            error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_EXPRESSION);
            return false;
        }
        if (!buffer_appendf(buffer, "nippy_string_%s(&nippy_stores[%zu], "
                                    "%zu, ",
                            node->kind == TYPED_STRING_CONCAT ? "concat"
                                                              : "copy",
                            node->store_id, node->store_id) ||
            !emit_expression(program, node->left_child, buffer, errors)) {
            if (!errors->reported) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            }
            return false;
        }
        if (node->kind == TYPED_STRING_CONCAT) {
            if (node->right_child == TYPED_NO_NODE ||
                !buffer_append(buffer, ", ") ||
                !emit_expression(program, node->right_child, buffer,
                                 errors)) {
                if (!errors->reported) {
                    error_report(errors,
                                 MESSAGE_GENERATOR_OUT_OF_MEMORY);
                }
                return false;
            }
        }
        if (!buffer_append(buffer, ")")) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            return false;
        }
        return true;
    }
    if (node->kind == TYPED_STRING_FROM_FIXED) {
        const TypedNode *source = typed_program_node(program,
                                                     node->left_child);
        const TypedType *source_type =
            source == NULL
                ? NULL
                : typed_program_type(program, source->type_index);

        if (source_type == NULL || source_type->kind != NIPPY_TYPE_STRING ||
            !store_id_is_valid(program, node->store_id)) {
            error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_EXPRESSION);
            return false;
        }
        if (!buffer_appendf(buffer,
                            "nippy_string_from_fixed_%zu(&nippy_stores[%zu], "
                            "%zu, ",
                            source_type->fixed_length, node->store_id,
                            node->store_id) ||
            !emit_expression(program, node->left_child, buffer, errors) ||
            !buffer_append(buffer, ")")) {
            if (!errors->reported) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            }
            return false;
        }
        return true;
    }
    if (node->kind == TYPED_BUILDER_CREATE) {
        if (node->left_child == TYPED_NO_NODE ||
            !store_id_is_valid(program, node->store_id)) {
            error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_EXPRESSION);
            return false;
        }
        if (!buffer_appendf(buffer,
                            "nippy_builder_create(&nippy_stores[%zu], %zu, ",
                            node->store_id, node->store_id) ||
            !emit_expression(program, node->left_child, buffer, errors) ||
            !buffer_append(buffer, ")")) {
            if (!errors->reported) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            }
            return false;
        }
        return true;
    }
    if (node->kind == TYPED_BUILDER_APPEND ||
        node->kind == TYPED_BUILDER_APPEND_BYTE) {
        if (node->left_child == TYPED_NO_NODE ||
            node->right_child == TYPED_NO_NODE) {
            error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_EXPRESSION);
            return false;
        }
        if (!buffer_append(buffer,
                           node->kind == TYPED_BUILDER_APPEND
                               ? "nippy_builder_append("
                               : "nippy_builder_append_byte(") ||
            !emit_expression(program, node->left_child, buffer, errors) ||
            !buffer_append(buffer, ", ") ||
            !emit_expression(program, node->right_child, buffer, errors) ||
            !buffer_append(buffer, ")")) {
            if (!errors->reported) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            }
            return false;
        }
        return true;
    }
    if (node->kind == TYPED_BUILDER_LEN) {
        if (node->left_child == TYPED_NO_NODE ||
            !buffer_append(buffer, "nippy_builder_len(") ||
            !emit_expression(program, node->left_child, buffer, errors) ||
            !buffer_append(buffer, ")")) {
            if (!errors->reported) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            }
            return false;
        }
        return true;
    }
    if (node->kind == TYPED_BUILDER_FINISH) {
        if (node->left_child == TYPED_NO_NODE ||
            !buffer_append(buffer, "nippy_builder_finish(") ||
            !emit_expression(program, node->left_child, buffer, errors) ||
            !buffer_append(buffer, ")")) {
            if (!errors->reported) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            }
            return false;
        }
        return true;
    }
    if (node->kind == TYPED_LIST_LITERAL) {
        char type_name[NIPPY_IDENTIFIER_MAX + 32];
        const TypedType *type = typed_program_type(program, node->type_index);

        if (type == NULL || type->kind != NIPPY_TYPE_FIXED_LIST ||
            !format_c_type(program, node->type_index, type_name,
                           sizeof(type_name), errors)) {
            error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_EXPRESSION);
            return false;
        }
        if (node->children_count == 0) {
            if (!buffer_appendf(buffer, "(%s){ { 0 }, 0 }", type_name)) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
                return false;
            }
            return true;
        }
        if (!buffer_appendf(buffer, "(%s){ { ", type_name) ||
            !emit_children_expressions(program, node, buffer, errors) ||
            !buffer_appendf(buffer, " }, %zu }", node->children_count)) {
            if (!errors->reported) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            }
            return false;
        }
        return true;
    }
    if (node->kind == TYPED_LIST_LEN) {
        const TypedNode *receiver =
            typed_program_node(program, node->left_child);
        const TypedType *receiver_type =
            receiver == NULL
                ? NULL
                : typed_program_type(program, receiver->type_index);

        if (receiver_type == NULL) {
            error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_EXPRESSION);
            return false;
        }
        if (receiver_type->kind == NIPPY_TYPE_FIXED_LIST) {
            if (!buffer_append(buffer, "((int32_t)(") ||
                !emit_expression(program, node->left_child, buffer, errors) ||
                !buffer_append(buffer, ").length)")) {
                if (!errors->reported) {
                    error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
                }
                return false;
            }
            return true;
        }
        if (!buffer_append(buffer, "nippy_list_len(") ||
            !emit_expression(program, node->left_child, buffer, errors) ||
            !buffer_append(buffer, ")")) {
            if (!errors->reported) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            }
            return false;
        }
        return true;
    }
    if (node->kind == TYPED_LIST_GET) {
        const TypedNode *receiver =
            typed_program_node(program, node->left_child);
        const TypedType *receiver_type =
            receiver == NULL
                ? NULL
                : typed_program_type(program, receiver->type_index);
        const TypedType *element_type =
            typed_program_type(program, node->type_index);
        char element_name[NIPPY_IDENTIFIER_MAX + 32];

        if (receiver_type == NULL || element_type == NULL ||
            !format_c_type(program, node->type_index, element_name,
                           sizeof(element_name), errors)) {
            error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_EXPRESSION);
            return false;
        }
        if (receiver_type->kind == NIPPY_TYPE_FIXED_LIST) {
            if (!buffer_appendf(buffer, "nippy_list_%zu_get(",
                                receiver->type_index) ||
                !emit_expression(program, node->left_child, buffer, errors) ||
                !buffer_append(buffer, ", ") ||
                !emit_expression(program, node->right_child, buffer, errors) ||
                !buffer_append(buffer, ")")) {
                if (!errors->reported) {
                    error_report(errors,
                                 MESSAGE_GENERATOR_OUT_OF_MEMORY);
                }
                return false;
            }
            return true;
        }
        if (!buffer_appendf(buffer, "(*(%s *)nippy_list_data(",
                            element_name) ||
            !emit_expression(program, node->left_child, buffer, errors) ||
            !buffer_append(buffer, ", ") ||
            !emit_expression(program, node->right_child, buffer, errors) ||
            !buffer_appendf(buffer, ", sizeof(%s)))", element_name)) {
            if (!errors->reported) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            }
            return false;
        }
        return true;
    }
    if (node->kind == TYPED_LIST_SET) {
        const TypedNode *receiver =
            typed_program_node(program, node->left_child);
        const TypedType *receiver_type =
            receiver == NULL
                ? NULL
                : typed_program_type(program, receiver->type_index);
        const TypedType *element_type =
            typed_program_type(program, node->auxiliary_type);
        char element_name[NIPPY_IDENTIFIER_MAX + 32];
        size_t index;
        size_t value;

        if (receiver_type == NULL || element_type == NULL ||
            node->children_count != 2 ||
            !format_c_type(program, node->auxiliary_type, element_name,
                           sizeof(element_name), errors)) {
            error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_EXPRESSION);
            return false;
        }
        index = *(const size_t *)list_get(&program->children,
                                          node->children_start);
        value = *(const size_t *)list_get(&program->children,
                                          node->children_start + 1);
        if (receiver_type->kind == NIPPY_TYPE_FIXED_LIST) {
            const char *name = typed_program_symbol_text(
                program, node->name_index);

            if (!valid_identifier(name) ||
                !buffer_appendf(buffer, "(*nippy_list_%zu_at(&%s, ",
                                receiver->type_index, name) ||
                !emit_expression(program, index, buffer, errors) ||
                !buffer_append(buffer, ") = ") ||
                !emit_expression(program, value, buffer, errors) ||
                !buffer_append(buffer, ")")) {
                if (!errors->reported) {
                    error_report(errors,
                                 MESSAGE_GENERATOR_OUT_OF_MEMORY);
                }
                return false;
            }
            return true;
        }
        if (!buffer_appendf(buffer, "(*(%s *)nippy_list_data(",
                            element_name) ||
            !emit_expression(program, node->left_child, buffer, errors) ||
            !buffer_append(buffer, ", ") ||
            !emit_expression(program, index, buffer, errors) ||
            !buffer_appendf(buffer, ", sizeof(%s)) = ", element_name) ||
            !emit_expression(program, value, buffer, errors) ||
            !buffer_append(buffer, ")")) {
            if (!errors->reported) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            }
            return false;
        }
        return true;
    }
    if (node->kind == TYPED_LIST_PUSH) {
        const TypedNode *receiver =
            typed_program_node(program, node->left_child);
        const TypedType *receiver_type =
            receiver == NULL
                ? NULL
                : typed_program_type(program, receiver->type_index);
        const TypedType *element_type =
            typed_program_type(program, node->auxiliary_type);
        const char *name = typed_program_symbol_text(program,
                                                      node->name_index);
        char element_name[NIPPY_IDENTIFIER_MAX + 32];

        if (receiver_type == NULL || element_type == NULL ||
            !valid_identifier(name) ||
            !format_c_type(program, node->auxiliary_type, element_name,
                           sizeof(element_name), errors)) {
            error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_EXPRESSION);
            return false;
        }
        if (receiver_type->kind == NIPPY_TYPE_FIXED_LIST) {
            if (!buffer_appendf(buffer, "nippy_list_%zu_push(&%s, ",
                                receiver->type_index, name) ||
                !emit_expression(program, node->right_child, buffer, errors) ||
                !buffer_append(buffer, ")")) {
                if (!errors->reported) {
                    error_report(errors,
                                 MESSAGE_GENERATOR_OUT_OF_MEMORY);
                }
                return false;
            }
            return true;
        }
        if (!buffer_appendf(buffer, "nippy_list_push(%s, (%s[]){ ", name,
                            element_name) ||
            !emit_expression(program, node->right_child, buffer, errors) ||
            !buffer_appendf(buffer, " }, sizeof(%s), _Alignof(%s))",
                            element_name, element_name)) {
            if (!errors->reported) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            }
            return false;
        }
        return true;
    }
    if (node->kind == TYPED_LIST_POP) {
        const TypedNode *receiver =
            typed_program_node(program, node->left_child);
        const TypedType *receiver_type =
            receiver == NULL
                ? NULL
                : typed_program_type(program, receiver->type_index);
        const char *name = typed_program_symbol_text(program,
                                                      node->name_index);

        if (receiver_type == NULL || !valid_identifier(name)) {
            error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_EXPRESSION);
            return false;
        }
        if (!buffer_appendf(buffer, "nippy_list_pop_%zu(", node_index)) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            return false;
        }
        if (receiver_type->kind == NIPPY_TYPE_FIXED_LIST) {
            if (!buffer_appendf(buffer, "&%s", name)) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
                return false;
            }
        } else if (!buffer_append(buffer, name)) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            return false;
        }
        if (!buffer_append(buffer, ")")) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            return false;
        }
        return true;
    }
    if (node->kind == TYPED_LIST_CREATE) {
        const TypedType *element_type =
            typed_program_type(program, node->auxiliary_type);
        char element_name[NIPPY_IDENTIFIER_MAX + 32];

        if (element_type == NULL ||
            !store_id_is_valid(program, node->store_id) ||
            !format_c_type(program, node->auxiliary_type, element_name,
                           sizeof(element_name), errors)) {
            error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_EXPRESSION);
            return false;
        }
        if (!buffer_appendf(buffer,
                            "nippy_list_create(&nippy_stores[%zu], %zu, ",
                            node->store_id, node->store_id) ||
            !emit_expression(program, node->left_child, buffer, errors) ||
            !buffer_appendf(buffer, ", sizeof(%s), _Alignof(%s))",
                            element_name, element_name)) {
            if (!errors->reported) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            }
            return false;
        }
        return true;
    }
    if (node->kind == TYPED_SLICE_CREATE) {
        const TypedNode *receiver =
            typed_program_node(program, node->left_child);
        const TypedType *receiver_type =
            receiver == NULL
                ? NULL
                : typed_program_type(program, receiver->type_index);
        char element_name[NIPPY_IDENTIFIER_MAX + 32];
        size_t start;
        size_t length;

        if (receiver_type == NULL || node->children_count != 2 ||
            !format_c_type(program, node->auxiliary_type, element_name,
                           sizeof(element_name), errors)) {
            error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_EXPRESSION);
            return false;
        }
        start = *(const size_t *)list_get(&program->children,
                                          node->children_start);
        length = *(const size_t *)list_get(&program->children,
                                           node->children_start + 1);
        if (!buffer_appendf(buffer, "nippy_slice_%s(",
                            receiver_type->kind == NIPPY_TYPE_SLICE
                                ? "sub"
                                : "from_list") ||
            !emit_expression(program, node->left_child, buffer, errors) ||
            !buffer_append(buffer, ", ") ||
            !emit_expression(program, start, buffer, errors) ||
            !buffer_append(buffer, ", ") ||
            !emit_expression(program, length, buffer, errors) ||
            !buffer_appendf(buffer, ", sizeof(%s))", element_name)) {
            if (!errors->reported) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            }
            return false;
        }
        return true;
    }
    if (node->kind == TYPED_SLICE_LEN) {
        if (!buffer_append(buffer, "nippy_slice_len(") ||
            !emit_expression(program, node->left_child, buffer, errors) ||
            !buffer_append(buffer, ")")) {
            if (!errors->reported) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            }
            return false;
        }
        return true;
    }
    if (node->kind == TYPED_SLICE_GET) {
        char element_name[NIPPY_IDENTIFIER_MAX + 32];

        if (!format_c_type(program, node->type_index, element_name,
                           sizeof(element_name), errors)) {
            error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_EXPRESSION);
            return false;
        }
        if (!buffer_appendf(buffer, "(*(%s *)nippy_slice_data(",
                            element_name) ||
            !emit_expression(program, node->left_child, buffer, errors) ||
            !buffer_append(buffer, ", ") ||
            !emit_expression(program, node->right_child, buffer, errors) ||
            !buffer_appendf(buffer, ", sizeof(%s)))", element_name)) {
            if (!errors->reported) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            }
            return false;
        }
        return true;
    }
    if (node->kind == TYPED_RECORD_LITERAL) {
        const TypedType *type = typed_program_type(program, node->type_index);
        char record_name[64];
        char key_name[64];
        char value_name[64];
        size_t key_type;
        size_t value_type;
        size_t i;

        if (type == NULL || type->child_count != 2 ||
            !format_c_type(program, node->type_index, record_name,
                           sizeof(record_name), errors)) {
            error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_EXPRESSION);
            return false;
        }
        key_type = *(const size_t *)list_get(&program->type_children,
                                             type->first_child);
        value_type = *(const size_t *)list_get(&program->type_children,
                                               type->first_child + 1);
        if (!format_c_type(program, key_type, key_name, sizeof(key_name),
                           errors) ||
            !format_c_type(program, value_type, value_name,
                           sizeof(value_name), errors)) {
            return false;
        }
        if (node->children_count == 0) {
            if (!buffer_appendf(buffer, "(%s){0}", record_name)) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
                return false;
            }
            return true;
        }
        if (!buffer_appendf(buffer,
                            "nippy_record_%zu_literal((%s[]){ ",
                            node->type_index, key_name)) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            return false;
        }
        for (i = 0; i + 1 < node->children_count; i += 2) {
            size_t key = *(const size_t *)list_get(&program->children,
                                                   node->children_start + i);

            if (i != 0 && !buffer_append(buffer, ", ")) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
                return false;
            }
            if (!emit_expression(program, key, buffer, errors)) {
                return false;
            }
        }
        if (!buffer_appendf(buffer, " }, (%s[]){ ", value_name)) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            return false;
        }
        for (i = 1; i < node->children_count; i += 2) {
            size_t value = *(const size_t *)list_get(&program->children,
                                                     node->children_start + i);

            if (i != 1 && !buffer_append(buffer, ", ")) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
                return false;
            }
            if (!emit_expression(program, value, buffer, errors)) {
                return false;
            }
        }
        if (!buffer_appendf(buffer, " }, %zu)", node->children_count / 2)) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            return false;
        }
        return true;
    }
    if (node->kind == TYPED_TABLE_CREATE) {
        if (!buffer_appendf(buffer,
                            "nippy_table_%zu_create(&nippy_stores[%zu], %zu, ",
                            node->type_index, node->store_id, node->store_id) ||
            !emit_expression(program, node->left_child, buffer, errors) ||
            !buffer_append(buffer, ")")) {
            if (!errors->reported) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            }
            return false;
        }
        return true;
    }
    if (node->kind == TYPED_TABLE_GET) {
        const TypedNode *receiver = typed_program_node(program,
                                                       node->left_child);

        if (receiver == NULL ||
            !buffer_appendf(buffer, "nippy_table_%zu_get_required(",
                            receiver->type_index) ||
            !emit_expression(program, node->left_child, buffer, errors) ||
            !buffer_append(buffer, ", ") ||
            !emit_expression(program, node->right_child, buffer, errors) ||
            !buffer_append(buffer, ")")) {
            if (!errors->reported) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            }
            return false;
        }
        return true;
    }
    if (node->kind == TYPED_TABLE_HAS ||
        node->kind == TYPED_TABLE_REMOVE ||
        node->kind == TYPED_TABLE_LEN) {
        const TypedNode *receiver = typed_program_node(program,
                                                       node->left_child);
        const char *suffix = node->kind == TYPED_TABLE_HAS
                                 ? "has"
                                 : (node->kind == TYPED_TABLE_REMOVE
                                        ? "remove"
                                        : "len");

        if (receiver == NULL) {
            error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_EXPRESSION);
            return false;
        }
        if (node->kind == TYPED_TABLE_LEN) {
            if (!buffer_appendf(buffer, "nippy_table_%zu_len(",
                                receiver->type_index) ||
                !emit_expression(program, node->left_child, buffer, errors) ||
                !buffer_append(buffer, ")")) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
                return false;
            }
        } else {
            if (!buffer_appendf(buffer, "nippy_table_%zu_%s(",
                                receiver->type_index, suffix) ||
                (node->kind == TYPED_TABLE_REMOVE &&
                 !buffer_append(buffer, "&(")) ||
                !emit_expression(program, node->left_child, buffer, errors) ||
                (node->kind == TYPED_TABLE_REMOVE &&
                 !buffer_append(buffer, ")")) ||
                !buffer_append(buffer, ", ") ||
                !emit_expression(program, node->right_child, buffer, errors) ||
                !buffer_append(buffer, ")")) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
                return false;
            }
        }
        return true;
    }
    if (node->kind == TYPED_RECORD_GET) {
        const TypedType *receiver_type = NULL;
        const TypedNode *receiver = typed_program_node(program,
                                                       node->left_child);

        receiver_type = receiver == NULL
                            ? NULL
                            : typed_program_type(program,
                                                 receiver->type_index);
        if (receiver_type == NULL ||
            receiver_type->kind != NIPPY_TYPE_RECORD) {
            error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_EXPRESSION);
            return false;
        }
        if (!buffer_appendf(buffer, "nippy_record_%zu_get_required(&(",
                            receiver->type_index) ||
            !emit_expression(program, node->left_child, buffer, errors) ||
            !buffer_append(buffer, "), ") ||
            !emit_expression(program, node->right_child, buffer, errors) ||
            !buffer_append(buffer, ")")) {
            if (!errors->reported) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            }
            return false;
        }
        return true;
    }
    if (node->kind == TYPED_RECORD_HAS ||
        node->kind == TYPED_RECORD_REMOVE ||
        node->kind == TYPED_RECORD_LEN) {
        const TypedNode *receiver = typed_program_node(program,
                                                       node->left_child);
        const char *suffix = node->kind == TYPED_RECORD_HAS
                                 ? "has"
                                 : (node->kind == TYPED_RECORD_REMOVE
                                        ? "remove"
                                        : "len");
        const char *prefix = node->kind == TYPED_RECORD_LEN
                                 ? "nippy_record_%zu_len(&("
                                 : "nippy_record_%zu_%s(&(";

        if (receiver == NULL) {
            error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_EXPRESSION);
            return false;
        }
        if (node->kind == TYPED_RECORD_LEN) {
            if (!buffer_appendf(buffer, prefix, receiver->type_index) ||
                !emit_expression(program, node->left_child, buffer, errors) ||
                !buffer_append(buffer, "))")) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
                return false;
            }
        } else {
            if (!buffer_appendf(buffer, prefix, receiver->type_index,
                                suffix) ||
                !emit_expression(program, node->left_child, buffer, errors) ||
                !buffer_append(buffer, "), ") ||
                !emit_expression(program, node->right_child, buffer, errors) ||
                !buffer_append(buffer, ")")) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
                return false;
            }
        }
        return true;
    }
    if (node->kind == TYPED_MEMBER_ACCESS) {
        const char *field = typed_program_symbol_text(program,
                                                      node->name_index);

        if (!valid_identifier(field) ||
            !buffer_append(buffer, "(") ||
            !emit_expression(program, node->left_child, buffer, errors) ||
            !buffer_appendf(buffer, ").%s", field)) {
            if (!errors->reported) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            }
            return false;
        }
        return true;
    }
    if (node->kind == TYPED_ERROR_THROW) {
        char filename_type[NIPPY_IDENTIFIER_MAX + 32];
        size_t i;

        if (!format_c_type(program, node->auxiliary_type, filename_type,
                           sizeof(filename_type), errors) ||
            node->children_count != 2) {
            error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_EXPRESSION);
            return false;
        }
        if (!buffer_append(buffer, "(nippy_error){ ") ||
            !emit_expression(program, *(const size_t *)list_get(
                                         &program->children,
                                         node->children_start),
                            buffer, errors)) {
            if (!errors->reported) {
                error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_EXPRESSION);
            }
            return false;
        }
        if (!buffer_append(buffer, ", ") ||
            !emit_expression(program, *(const size_t *)list_get(
                                         &program->children,
                                         node->children_start + 1),
                            buffer, errors) ||
            !buffer_appendf(buffer, ", (%s){ { ", filename_type)) {
            if (!errors->reported) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            }
            return false;
        }
        for (i = 0; i < node->string_length; i++) {
            unsigned char byte = *(const unsigned char *)list_get(
                &program->bytes, node->string_offset + i);

            if (!buffer_appendf(buffer, "%u%s", (unsigned)byte,
                                i + 1 == node->string_length ? "" : ", ")) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
                return false;
            }
        }
        if (!buffer_appendf(buffer,
                            "%s0 }, %zu }, %d, %d, %d }",
                            node->string_length == 0 ? "" : ", ",
                            node->string_length, (int)node->pos.line,
                            (int)node->pos.column, (int)node->pos.offset)) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            return false;
        }
        return true;
    }
    if (node->kind == TYPED_ERROR_CONSTRUCT) {
        if (!buffer_append(buffer, "(nippy_error){ ") ||
            !emit_children_expressions(program, node, buffer, errors) ||
            !buffer_append(buffer, " }")) {
            if (!errors->reported) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            }
            return false;
        }
        return true;
    }
    if (node->kind == TYPED_ERROR_FIELD) {
        static const char *const fields[] = {
            "header", "description", "filename", "line", "col", "pos"
        };

        if (node->integer_value < 0 || node->integer_value > 5) {
            error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_EXPRESSION);
            return false;
        }
        if (!buffer_append(buffer, "(") ||
            !emit_expression(program, node->left_child, buffer, errors) ||
            !buffer_appendf(buffer, ").%s",
                            fields[node->integer_value])) {
            if (!errors->reported) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            }
            return false;
        }
        return true;
    }
    if (node->kind == TYPED_BINARY) {
        if (node->op == NIPPY_OPERATOR_EQ || node->op == NIPPY_OPERATOR_NE) {
            const TypedNode *left = typed_program_node(program,
                                                       node->left_child);
            const TypedType *left_type =
                left == NULL
                    ? NULL
                    : typed_program_type(program, left->type_index);

            if (left_type != NULL &&
                left_type->kind == NIPPY_TYPE_DYNAMIC_STRING) {
                if (!buffer_append(buffer,
                                   node->op == NIPPY_OPERATOR_NE ? "(!"
                                                                 : "(") ||
                    !buffer_append(buffer, "nippy_string_equal(") ||
                    !emit_expression(program, node->left_child, buffer,
                                     errors) ||
                    !buffer_append(buffer, ", ") ||
                    !emit_expression(program, node->right_child, buffer,
                                     errors) ||
                    !buffer_append(buffer, "))")) {
                    if (!errors->reported) {
                        error_report(errors,
                                     MESSAGE_GENERATOR_OUT_OF_MEMORY);
                    }
                    return false;
                }
                return true;
            }
            if (left_type != NULL &&
                left_type->kind == NIPPY_TYPE_STRING) {
                if (!buffer_append(buffer,
                                   node->op == NIPPY_OPERATOR_NE ? "(!"
                                                                 : "(") ||
                    !buffer_appendf(buffer, "nippy_string_%zu_equal(",
                                    left_type->fixed_length) ||
                    !emit_expression(program, node->left_child, buffer,
                                     errors) ||
                    !buffer_append(buffer, ", ") ||
                    !emit_expression(program, node->right_child, buffer,
                                     errors) ||
                    !buffer_append(buffer, "))")) {
                    if (!errors->reported) {
                        error_report(errors,
                                     MESSAGE_GENERATOR_OUT_OF_MEMORY);
                    }
                    return false;
                }
                return true;
            }
        }
        if (node->op == NIPPY_OPERATOR_SUB ||
            node->op == NIPPY_OPERATOR_MUL) {
            const char *op = node->op == NIPPY_OPERATOR_SUB ? "-" : "*";

            if (!buffer_append(buffer, "(int32_t)((uint32_t)(") ||
                !emit_expression(program, node->left_child, buffer, errors) ||
                !buffer_appendf(buffer, ") %s (uint32_t)(", op) ||
                !emit_expression(program, node->right_child, buffer, errors) ||
                !buffer_append(buffer, "))")) {
                if (!errors->reported) {
                    error_report(errors,
                                 MESSAGE_GENERATOR_OUT_OF_MEMORY);
                }
                return false;
            }
            return true;
        }
        if (node->op == NIPPY_OPERATOR_DIV ||
            node->op == NIPPY_OPERATOR_MOD ||
            node->op == NIPPY_OPERATOR_SHL ||
            node->op == NIPPY_OPERATOR_SHR) {
            const char *helper = node->op == NIPPY_OPERATOR_DIV
                                     ? "nippy_div_i32"
                                     : node->op == NIPPY_OPERATOR_MOD
                                           ? "nippy_mod_i32"
                                           : node->op == NIPPY_OPERATOR_SHL
                                                 ? "nippy_shl_i32"
                                                 : "nippy_shr_i32";

            if (!buffer_appendf(buffer, "%s(", helper) ||
                !emit_expression(program, node->left_child, buffer, errors) ||
                !buffer_append(buffer, ", ") ||
                !emit_expression(program, node->right_child, buffer, errors) ||
                !buffer_append(buffer, ")")) {
                if (!errors->reported) {
                    error_report(errors,
                                 MESSAGE_GENERATOR_OUT_OF_MEMORY);
                }
                return false;
            }
            return true;
        }
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
        if (node->op == NIPPY_OPERATOR_NOT) {
            if (!buffer_append(buffer, "(!(") ||
                !emit_expression(program, node->left_child, buffer, errors) ||
                !buffer_append(buffer, "))")) {
                if (!errors->reported) {
                    error_report(errors,
                                 MESSAGE_GENERATOR_OUT_OF_MEMORY);
                }
                return false;
            }
            return true;
        }
        if (node->op == NIPPY_OPERATOR_NEG) {
            if (!buffer_append(buffer, "(int32_t)(0u - (uint32_t)(") ||
                !emit_expression(program, node->left_child, buffer, errors) ||
                !buffer_append(buffer, "))")) {
                if (!errors->reported) {
                    error_report(errors,
                                 MESSAGE_GENERATOR_OUT_OF_MEMORY);
                }
                return false;
            }
            return true;
        }
        if (node->op == NIPPY_OPERATOR_BIT_NOT) {
            if (!buffer_append(buffer, "(~(") ||
                !emit_expression(program, node->left_child, buffer, errors) ||
                !buffer_append(buffer, "))")) {
                if (!errors->reported) {
                    error_report(errors,
                                 MESSAGE_GENERATOR_OUT_OF_MEMORY);
                }
                return false;
            }
            return true;
        }
        error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_EXPRESSION);
        return false;
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
            {
                size_t alternative_index = *(const size_t *)list_get(
                    &program->type_children, type->first_child + i);

                if (alternative->kind == NIPPY_TYPE_FUNCTION) {
                    char pointer_name[256];
                    char field_name[32];

                    snprintf(field_name, sizeof(field_name), "v%zu", i);
                    if (!format_function_pointer(
                            program, alternative_index, field_name,
                            pointer_name, sizeof(pointer_name), errors) ||
                        !buffer_appendf(header, "        %s;\n",
                                        pointer_name)) {
                        return false;
                    }
                } else if (!format_c_type(program, alternative_index,
                                          alternative_name,
                                          sizeof(alternative_name), errors) ||
                           !buffer_appendf(header, "        %s v%zu;\n",
                                           alternative_name, i)) {
                    return false;
                }
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
    bool table_emitted = false;
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
        } else if (type->kind == NIPPY_TYPE_DYNAMIC_STRING) {
            if (!buffer_append(
                    header,
                    "typedef struct {\n"
                    "    const uint8_t *static_data;\n"
                    "    uint32_t store;\n"
                    "    size_t offset;\n"
                    "    size_t length;\n"
                    "    uint64_t generation;\n"
                    "} nippy_string;\n\n")) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
                return false;
            }
        } else if (type->kind == NIPPY_TYPE_TEXT_BUILDER) {
            if (!buffer_append(
                    header,
                    "typedef struct {\n"
                    "    uint32_t store;\n"
                    "    size_t offset;\n"
                    "    uint64_t generation;\n"
                    "} nippy_builder;\n\n")) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
                return false;
            }
        } else if (type->kind == NIPPY_TYPE_LIST) {
            bool emitted = false;
            size_t j;

            for (j = 0; j < i; j++) {
                const TypedType *earlier = typed_program_type(program, j);

                if (earlier != NULL && earlier->kind == NIPPY_TYPE_LIST) {
                    emitted = true;
                    break;
                }
            }
            if (!emitted &&
                !buffer_append(
                    header,
                    "typedef struct {\n"
                    "    uint32_t store;\n"
                    "    size_t offset;\n"
                    "    uint64_t generation;\n"
                    "} nippy_list;\n\n")) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
                return false;
            }
        } else if (type->kind == NIPPY_TYPE_RECORD) {
            char key_name[NIPPY_IDENTIFIER_MAX + 32];
            char value_name[NIPPY_IDENTIFIER_MAX + 32];
            size_t key_type = *(const size_t *)list_get(
                &program->type_children, type->first_child);
            size_t value_type = *(const size_t *)list_get(
                &program->type_children, type->first_child + 1);
            size_t buckets = type->fixed_length == 0
                                 ? 1
                                 : type->fixed_length + 1;

            if (!format_c_type(program, key_type, key_name,
                               sizeof(key_name), errors) ||
                !format_c_type(program, value_type, value_name,
                               sizeof(value_name), errors) ||
                !buffer_appendf(
                    header,
                    "typedef struct {\n"
                    "    %s keys[%zu];\n"
                    "    %s values[%zu];\n"
                    "    uint8_t states[%zu];\n"
                    "    size_t length;\n"
                    "} nippy_record_%zu;\n\n",
                    key_name, buckets, value_name, buckets, buckets, i)) {
                if (!errors->reported) {
                    error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
                }
                return false;
            }
        } else if (type->kind == NIPPY_TYPE_TABLE) {
            if (!table_emitted &&
                !buffer_append(
                    header,
                    "typedef struct {\n"
                    "    uint32_t store;\n"
                    "    size_t offset;\n"
                    "    uint64_t generation;\n"
                    "} nippy_table;\n\n")) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
                return false;
            }
            table_emitted = true;
        } else if (type->kind == NIPPY_TYPE_SLICE) {
            bool emitted = false;
            size_t j;

            for (j = 0; j < i; j++) {
                const TypedType *earlier = typed_program_type(program, j);

                if (earlier != NULL && earlier->kind == NIPPY_TYPE_SLICE) {
                    emitted = true;
                    break;
                }
            }
            if (!emitted &&
                !buffer_append(
                    header,
                    "typedef struct {\n"
                    "    uint32_t store;\n"
                    "    size_t offset;\n"
                    "    size_t length;\n"
                    "    uint64_t generation;\n"
                    "} nippy_slice;\n\n")) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
                return false;
            }
        } else if (type->kind == NIPPY_TYPE_FIXED_LIST) {
            char element_name[NIPPY_IDENTIFIER_MAX + 32];
            char list_name[NIPPY_IDENTIFIER_MAX + 32];
            size_t element;
            size_t capacity = type->fixed_length;

            if (type->child_count != 1) {
                error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_TYPE);
                return false;
            }
            element = *(const size_t *)list_get(&program->type_children,
                                                type->first_child);
            snprintf(list_name, sizeof(list_name), "nippy_list_%zu", i);
            if (!fixed_list_element_name(program, i, element, element_name,
                                         sizeof(element_name), errors)) {
                return false;
            }
            {
                const TypedType *element_type = typed_program_type(program,
                                                                   element);

                if (element_type != NULL &&
                    element_type->kind == NIPPY_TYPE_FUNCTION) {
                    char pointer_name[256];

                    if (!format_function_pointer(program, element,
                                                 element_name, pointer_name,
                                                 sizeof(pointer_name),
                                                 errors) ||
                        !buffer_appendf(header, "typedef %s;\n\n",
                                        pointer_name)) {
                        return false;
                    }
                }
            }
            if (!buffer_appendf(
                    header,
                    "typedef struct {\n"
                    "    %s data[%zu];\n"
                    "    uint32_t length;\n"
                    "} %s;\n\n",
                    element_name, capacity == 0 ? (size_t)1 : capacity,
                    list_name)) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
                return false;
            }
        } else if (type->kind == NIPPY_TYPE_STRING) {
            bool already = false;
            size_t j;

            for (j = 0; j < i; j++) {
                const TypedType *earlier = typed_program_type(program, j);

                if (earlier != NULL && earlier->kind == NIPPY_TYPE_STRING &&
                    earlier->fixed_length == type->fixed_length) {
                    already = true;
                    break;
                }
            }
            if (!already &&
                (!buffer_appendf(
                     header,
                     "typedef struct {\n"
                     "    uint8_t data[%zu];\n"
                     "    uint32_t length;\n"
                     "} nippy_string_%zu;\n\n",
                     type->fixed_length + 1, type->fixed_length) ||
                 !buffer_appendf(
                     header,
                     "static uint8_t nippy_string_%zu_at(nippy_string_%zu s, "
                     "int32_t i);\n\n",
                     type->fixed_length, type->fixed_length))) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
                return false;
            }
        } else if (type->kind == NIPPY_TYPE_ERROR) {
            const size_t *header_type;
            const size_t *description_type;
            const size_t *filename_type;
            char header_name[NIPPY_IDENTIFIER_MAX + 32];
            char description_name[NIPPY_IDENTIFIER_MAX + 32];
            char filename_name[NIPPY_IDENTIFIER_MAX + 32];

            if (type->child_count != 3) {
                error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_TYPE);
                return false;
            }
            header_type = list_get(&program->type_children,
                                   type->first_child);
            description_type = list_get(&program->type_children,
                                        type->first_child + 1);
            filename_type = list_get(&program->type_children,
                                     type->first_child + 2);
            if (header_type == NULL || description_type == NULL ||
                filename_type == NULL ||
                !format_c_type(program, *header_type, header_name,
                               sizeof(header_name), errors) ||
                !format_c_type(program, *description_type, description_name,
                               sizeof(description_name), errors) ||
                !format_c_type(program, *filename_type, filename_name,
                               sizeof(filename_name), errors)) {
                if (!errors->reported) {
                    error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_TYPE);
                }
                return false;
            }
            if (!buffer_appendf(
                    header,
                    "typedef struct {\n"
                    "    %s header;\n"
                    "    %s description;\n"
                    "    %s filename;\n"
                    "    int32_t line;\n"
                    "    int32_t col;\n"
                    "    int32_t pos;\n"
                    "} nippy_error;\n\n",
                    header_name, description_name, filename_name)) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
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
static bool emit_print_statement(const TypedProgram *program,
                                 const TypedNode *node, TextBuffer *source,
                                 ErrorState *errors, size_t depth);

static bool emit_block(const TypedProgram *program, size_t block_index,
                       size_t depth, const char *effective_source,
                       TextBuffer *source, ErrorState *errors);

static bool emit_map_for(const TypedProgram *program, const TypedNode *node,
                          const TypedType *iterable_type, size_t depth,
                          const char *effective_source, TextBuffer *source,
                          ErrorState *errors)
{
    const char *key_name = typed_program_symbol_text(program,
                                                     node->name_index);
    const char *value_name =
        node->second_name_index == TYPED_NO_NODE
            ? NULL
            : typed_program_symbol_text(program, node->second_name_index);
    size_t key_type = *(const size_t *)list_get(&program->type_children,
                                                iterable_type->first_child);
    size_t value_type = *(const size_t *)list_get(
        &program->type_children, iterable_type->first_child + 1);
    char key_c[NIPPY_IDENTIFIER_MAX + 32];
    char value_c[NIPPY_IDENTIFIER_MAX + 32];
    size_t offset = node->pos.offset;

    if (key_name == NULL || !valid_identifier(key_name) ||
        (value_name != NULL && !valid_identifier(value_name)) ||
        !format_c_type(program, key_type, key_c, sizeof(key_c), errors) ||
        !format_c_type(program, value_type, value_c, sizeof(value_c), errors)) {
        error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_STATEMENT);
        return false;
    }
    if (iterable_type->kind == NIPPY_TYPE_RECORD) {
        size_t buckets = iterable_type->fixed_length == 0
                             ? 1
                             : iterable_type->fixed_length + 1;

        if (!append_indent(source, depth) ||
            !buffer_append(source, "{\n") ||
            !append_indent(source, depth + 1)) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            return false;
        }
        {
            size_t child = node->left_child;
            const TypedNode *iter = typed_program_node(program, child);
            char record_name[64];

            if (iter == NULL ||
                !format_c_type(program, iter->type_index, record_name,
                               sizeof(record_name), errors) ||
                !buffer_appendf(source, "%s nippy_for_src_%zu = ",
                                record_name, offset) ||
                !emit_expression(program, child, source, errors) ||
                !buffer_append(source, ";\n")) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
                return false;
            }
        }
        if (!append_indent(source, depth + 1) ||
            !buffer_appendf(source,
                            "for (size_t nippy_for_i_%zu = 0; "
                            "nippy_for_i_%zu < %zu; nippy_for_i_%zu++) {\n",
                            offset, offset, buckets, offset) ||
            !append_indent(source, depth + 2) ||
            !buffer_appendf(source,
                            "if (nippy_for_src_%zu.states[nippy_for_i_%zu] "
                            "!= 1) continue;\n",
                            offset, offset) ||
            !append_indent(source, depth + 2) ||
            !buffer_appendf(source, "%s %s = nippy_for_src_%zu.keys["
                                    "nippy_for_i_%zu];\n",
                            key_c, key_name, offset, offset) ||
            !append_indent(source, depth + 2) ||
            !buffer_appendf(source, "(void)%s;\n", key_name)) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            return false;
        }
        if (value_name != NULL) {
            if (!append_indent(source, depth + 2) ||
                !buffer_appendf(source, "%s %s = nippy_for_src_%zu.values["
                                        "nippy_for_i_%zu];\n",
                                value_c, value_name, offset, offset) ||
                !append_indent(source, depth + 2) ||
                !buffer_appendf(source, "(void)%s;\n", value_name)) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
                return false;
            }
        }
    } else {
        const TypedNode *iterable = typed_program_node(program,
                                                       node->left_child);
        size_t table_type = iterable == NULL ? TYPED_NO_NODE
                                             : iterable->type_index;

        if (table_type == TYPED_NO_NODE ||
            !append_indent(source, depth) ||
            !buffer_append(source, "{\n") ||
            !append_indent(source, depth + 1) ||
            !buffer_appendf(source, "nippy_table nippy_for_src_%zu = ",
                            offset) ||
            !emit_expression(program, node->left_child, source, errors) ||
            !buffer_append(source, ";\n") ||
            !append_indent(source, depth + 1) ||
            !buffer_appendf(source,
                            "for (size_t nippy_for_i_%zu = 0;; "
                            "nippy_for_i_%zu++) {\n",
                            offset, offset) ||
            !append_indent(source, depth + 2) ||
            !buffer_appendf(source,
                            "nippy_table_state_%zu *nippy_for_state_%zu = "
                            "nippy_table_%zu_state(nippy_for_src_%zu);\n",
                            table_type, offset, table_type, offset) ||
            !append_indent(source, depth + 2) ||
            !buffer_appendf(source,
                            "if (nippy_for_i_%zu >= "
                            "nippy_for_state_%zu->buckets) break;\n",
                            offset, offset)) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            return false;
        }
        if (!append_indent(source, depth + 2) ||
            !buffer_appendf(source,
                            "nippy_store *nippy_for_store_%zu = "
                            "&nippy_stores[nippy_for_src_%zu.store];\n",
                            offset, offset) ||
            !append_indent(source, depth + 2) ||
            !buffer_appendf(source,
                            "uint8_t *nippy_for_states_%zu = (uint8_t *)"
                            "(void *)(nippy_for_store_%zu->data + "
                            "nippy_for_state_%zu->states_offset);\n"
                            "        if (nippy_for_states_%zu[nippy_for_i_%zu]"
                            " != 1) continue;\n",
                            offset, offset, offset, offset, offset)) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            return false;
        }
        if (!append_indent(source, depth + 2) ||
            !buffer_appendf(source,
                            "%s *nippy_for_keys_%zu = (%s *)(void *)"
                            "(nippy_for_store_%zu->data + "
                            "nippy_for_state_%zu->keys_offset);\n",
                            key_c, offset, key_c, offset, offset) ||
            !append_indent(source, depth + 2) ||
            !buffer_appendf(source, "%s %s = nippy_for_keys_%zu["
                                    "nippy_for_i_%zu];\n",
                            key_c, key_name, offset, offset) ||
            !append_indent(source, depth + 2) ||
            !buffer_appendf(source, "(void)%s;\n", key_name)) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            return false;
        }
        if (value_name != NULL) {
            if (!append_indent(source, depth + 2) ||
                !buffer_appendf(source,
                                "%s *nippy_for_values_%zu = (%s *)(void *)"
                                "(nippy_for_store_%zu->data + "
                                "nippy_for_state_%zu->values_offset);\n",
                                value_c, offset, value_c, offset, offset) ||
                !append_indent(source, depth + 2) ||
                !buffer_appendf(source, "%s %s = nippy_for_values_%zu["
                                        "nippy_for_i_%zu];\n",
                                value_c, value_name, offset, offset) ||
                !append_indent(source, depth + 2) ||
                !buffer_appendf(source, "(void)%s;\n", value_name)) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
                return false;
            }
        }
    }
    if (!emit_block(program, node->right_child, depth + 2, effective_source,
                    source, errors) ||
        !append_indent(source, depth + 1) ||
        !buffer_append(source, "}\n") ||
        !append_indent(source, depth) ||
        !buffer_append(source, "}\n")) {
        error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
        return false;
    }
    return true;
}

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

    if (node->kind == TYPED_FOR) {
        const TypedNode *iterable_node = typed_program_node(
            program, node->left_child);
        const TypedType *iterable_type =
            iterable_node == NULL
                ? NULL
                : typed_program_type(program, iterable_node->type_index);
        const char *value_name = typed_program_symbol_text(
            program, node->name_index);
        const char *index_name =
            node->second_name_index == TYPED_NO_NODE
                ? NULL
                : typed_program_symbol_text(program, node->second_name_index);
        char iterable_name[NIPPY_IDENTIFIER_MAX + 32];
        char element_name[NIPPY_IDENTIFIER_MAX + 32];
        size_t offset = node->pos.offset;
        const char *value_c;
        const char *index_c;
        bool check_length;

        if (iterable_type != NULL &&
            (iterable_type->kind == NIPPY_TYPE_RECORD ||
             iterable_type->kind == NIPPY_TYPE_TABLE)) {
            return emit_map_for(program, node, iterable_type, depth,
                                effective_source, source, errors);
        }
        if (iterable_type == NULL || value_name == NULL ||
            !valid_identifier(value_name) ||
            (index_name != NULL && !valid_identifier(index_name)) ||
            !format_c_type(program, iterable_node->type_index, iterable_name,
                           sizeof(iterable_name), errors) ||
            !format_c_type(program, node->auxiliary_type, element_name,
                           sizeof(element_name), errors)) {
            error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_STATEMENT);
            return false;
        }
        value_c = value_name;
        index_c = index_name;
        check_length = iterable_type->kind == NIPPY_TYPE_LIST ||
                       iterable_type->kind == NIPPY_TYPE_FIXED_LIST;

        if (!append_indent(source, depth) ||
            !buffer_append(source, "{\n") ||
            !append_indent(source, depth + 1) ||
            !buffer_appendf(source, "%s nippy_for_src_%zu = ",
                            iterable_name, offset) ||
            !emit_expression(program, node->left_child, source, errors) ||
            !buffer_append(source, ";\n") ||
            !append_indent(source, depth + 1) ||
            !buffer_appendf(source, "size_t nippy_for_len_%zu = (size_t)(",
                            offset)) {
            if (!errors->reported) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            }
            return false;
        }
        if (iterable_type->kind == NIPPY_TYPE_LIST) {
            if (!buffer_appendf(source, "nippy_list_len(nippy_for_src_%zu)",
                                offset)) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
                return false;
            }
        } else if (iterable_type->kind == NIPPY_TYPE_FIXED_LIST ||
                   iterable_type->kind == NIPPY_TYPE_STRING) {
            if (!buffer_appendf(source,
                                "((int32_t)(nippy_for_src_%zu).length)",
                                offset)) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
                return false;
            }
        } else if (iterable_type->kind == NIPPY_TYPE_SLICE) {
            if (!buffer_appendf(source, "nippy_slice_len(nippy_for_src_%zu)",
                                offset)) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
                return false;
            }
        } else if (iterable_type->kind == NIPPY_TYPE_DYNAMIC_STRING) {
            if (!buffer_appendf(source, "nippy_string_len(nippy_for_src_%zu)",
                                offset)) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
                return false;
            }
        } else {
            error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_STATEMENT);
            return false;
        }
        if (!buffer_append(source, ");\n") ||
            !append_indent(source, depth + 1) ||
            !buffer_appendf(source,
                            "for (size_t nippy_for_i_%zu = 0; "
                            "nippy_for_i_%zu < nippy_for_len_%zu; "
                            "nippy_for_i_%zu++) {\n",
                            offset, offset, offset, offset) ||
            !append_indent(source, depth + 2)) {
            if (!errors->reported) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            }
            return false;
        }
        if (check_length) {
            if (iterable_type->kind == NIPPY_TYPE_LIST) {
                if (!buffer_appendf(source,
                                    "if ((size_t)nippy_list_len("
                                    "nippy_for_src_%zu) != "
                                    "nippy_for_len_%zu) { fprintf(stderr, \"%s\\n\"); "
                                    "exit(1); }\n",
                                    offset, offset,
                                    MESSAGE_RUNTIME_FOR_MUTATED)) {
                    error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
                    return false;
                }
            } else if (!buffer_appendf(source,
                                       "if ((size_t)(nippy_for_src_%zu).length "
                                       "!= nippy_for_len_%zu) { "
                                       "fprintf(stderr, \"%s\\n\"); "
                                       "exit(1); }\n",
                                       offset, offset,
                                       MESSAGE_RUNTIME_FOR_MUTATED)) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
                return false;
            }
            if (!append_indent(source, depth + 2)) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
                return false;
            }
        }
        if (!buffer_appendf(source, "%s %s = ", element_name, value_c)) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            return false;
        }
        if (iterable_type->kind == NIPPY_TYPE_FIXED_LIST) {
            if (!buffer_appendf(source, "nippy_list_%zu_get(nippy_for_src_%zu, "
                                        "nippy_for_i_%zu)",
                                iterable_node->type_index, offset, offset)) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
                return false;
            }
        } else if (iterable_type->kind == NIPPY_TYPE_LIST) {
            if (!buffer_appendf(source,
                                "(*(%s *)nippy_list_data(nippy_for_src_%zu, "
                                "nippy_for_i_%zu, sizeof(%s)))",
                                element_name, offset, offset, element_name)) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
                return false;
            }
        } else if (iterable_type->kind == NIPPY_TYPE_SLICE) {
            if (!buffer_appendf(source,
                                "(*(%s *)nippy_slice_data(nippy_for_src_%zu, "
                                "nippy_for_i_%zu, sizeof(%s)))",
                                element_name, offset, offset, element_name)) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
                return false;
            }
        } else if (iterable_type->kind == NIPPY_TYPE_STRING) {
            if (!buffer_appendf(source, "nippy_string_%zu_at(nippy_for_src_%zu, "
                                        "nippy_for_i_%zu)",
                                iterable_type->fixed_length, offset, offset)) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
                return false;
            }
        } else if (iterable_type->kind == NIPPY_TYPE_DYNAMIC_STRING) {
            if (!buffer_appendf(source, "nippy_string_byte_at(nippy_for_src_%zu, "
                                        "nippy_for_i_%zu)",
                                offset, offset)) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
                return false;
            }
        } else {
            error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_STATEMENT);
            return false;
        }
        if (!buffer_append(source, ";\n")) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            return false;
        }
        if (index_c != NULL) {
            if (!append_indent(source, depth + 2) ||
                !buffer_appendf(source, "int32_t %s = (int32_t)nippy_for_i_%zu;\n",
                                index_c, offset)) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
                return false;
            }
        }
        if (!emit_block(program, node->right_child, depth + 2,
                        effective_source, source, errors)) {
            return false;
        }
        if (check_length) {
            if (!append_indent(source, depth + 2)) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
                return false;
            }
            if (iterable_type->kind == NIPPY_TYPE_LIST) {
                if (!buffer_appendf(source,
                                    "if ((size_t)nippy_list_len("
                                    "nippy_for_src_%zu) != "
                                    "nippy_for_len_%zu) { fprintf(stderr, \"%s\\n\"); "
                                    "exit(1); }\n",
                                    offset, offset,
                                    MESSAGE_RUNTIME_FOR_MUTATED)) {
                    error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
                    return false;
                }
            } else if (!buffer_appendf(source,
                                       "if ((size_t)(nippy_for_src_%zu).length "
                                       "!= nippy_for_len_%zu) { "
                                       "fprintf(stderr, \"%s\\n\"); "
                                       "exit(1); }\n",
                                       offset, offset,
                                       MESSAGE_RUNTIME_FOR_MUTATED)) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
                return false;
            }
        }
        if (!append_indent(source, depth + 1) ||
            !buffer_append(source, "}\n") ||
            !append_indent(source, depth) ||
            !buffer_append(source, "}\n")) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            return false;
        }
        return true;
    }

    if (node->kind == TYPED_TABLE_SET) {
        size_t key = *(const size_t *)list_get(&program->children,
                                               node->children_start);
        size_t value = *(const size_t *)list_get(&program->children,
                                                 node->children_start + 1);
        const TypedNode *receiver = typed_program_node(
            program, node->left_child);

        if (receiver == NULL || !append_indent(source, depth) ||
            !buffer_appendf(source, "nippy_table_%zu_set(&(",
                            receiver->type_index) ||
            !emit_expression(program, node->left_child, source, errors) ||
            !buffer_append(source, "), ") ||
            !emit_expression(program, key, source, errors) ||
            !buffer_append(source, ", ") ||
            !emit_expression(program, value, source, errors) ||
            !buffer_append(source, ");\n")) {
            if (!errors->reported) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            }
            return false;
        }
        return true;
    }

    if (node->kind == TYPED_RECORD_SET) {
        size_t key = *(const size_t *)list_get(&program->children,
                                               node->children_start);
        size_t value = *(const size_t *)list_get(&program->children,
                                                 node->children_start + 1);
        const TypedNode *receiver = typed_program_node(
            program, node->left_child);

        if (receiver == NULL || !append_indent(source, depth) ||
            !buffer_append(source, "nippy_record_") ||
            !buffer_appendf(source, "%zu", receiver->type_index) ||
            !buffer_append(source, "_set(&(") ||
            !emit_expression(program, node->left_child, source, errors) ||
            !buffer_append(source, "), ") ||
            !emit_expression(program, key, source, errors) ||
            !buffer_append(source, ", ") ||
            !emit_expression(program, value, source, errors) ||
            !buffer_append(source, ");\n")) {
            if (!errors->reported) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            }
            return false;
        }
        return true;
    }

    if (node->kind == TYPED_LOCAL_DECLARATION) {
        const char *name = typed_program_symbol_text(program,
                                                     node->name_index);
        char type_name[NIPPY_IDENTIFIER_MAX + 32];

        if (!valid_identifier(name) ||
            !format_c_type(program, node->type_index, type_name,
                           sizeof(type_name), errors) ||
            node->left_child == TYPED_NO_NODE) {
            error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_STATEMENT);
            return false;
        }
        if (!append_indent(source, depth) ||
            !buffer_appendf(source, "%s %s = ", type_name, name) ||
            !emit_expression(program, node->left_child, source, errors) ||
            !buffer_append(source, ";\n")) {
            if (!errors->reported) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            }
            return false;
        }
        return true;
    }

    if (node->kind == TYPED_MEMBER_WRITE) {
        if (!append_indent(source, depth) ||
            !buffer_append(source, "(void)(") ||
            !emit_expression(program, node->left_child, source, errors) ||
            !buffer_append(source, " = ") ||
            !emit_expression(program, node->right_child, source, errors) ||
            !buffer_append(source, ");\n")) {
            if (!errors->reported) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            }
            return false;
        }
        return true;
    }

    if (node->kind == TYPED_ASSIGNMENT) {
        const char *name = typed_program_symbol_text(program,
                                                     node->name_index);

        if (!valid_identifier(name)) {
            error_report(errors, MESSAGE_GENERATOR_INVALID_IDENTIFIER);
            return false;
        }
        if (!append_indent(source, depth) ||
            !buffer_appendf(source, "%s = ", name) ||
            !emit_expression(program, node->left_child, source, errors) ||
            !buffer_append(source, ";\n")) {
            if (!errors->reported) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            }
            return false;
        }
        return true;
    }

    if (node->kind == TYPED_LIST_SET) {
        if (!append_indent(source, depth) ||
            !buffer_append(source, "(void)(") ||
            !emit_expression(program, node_index, source, errors) ||
            !buffer_append(source, ");\n")) {
            if (!errors->reported) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            }
            return false;
        }
        return true;
    }

    if (node->kind == TYPED_PRINT) {
        return emit_print_statement(program, node, source, errors, depth);
    }

    if (node->kind == TYPED_EXPRESSION_STATEMENT) {
        const TypedNode *expression = typed_program_node(
            program, node->left_child);

        if (expression != NULL && expression->kind == TYPED_PRINT) {
            return emit_print_statement(program, expression, source, errors,
                                        depth);
        }
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


static bool program_uses_string_builder(const TypedProgram *program)
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

static bool program_uses_list(const TypedProgram *program)
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

static bool program_uses_dynamic_list(const TypedProgram *program)
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

static bool program_uses_slice(const TypedProgram *program)
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

static bool program_uses_dynamic_string(const TypedProgram *program)
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

static bool program_uses_string(const TypedProgram *program)
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

static bool emit_string_runtime(const TypedProgram *program, TextBuffer *source,
                                ErrorState *errors)
{
    bool emitted_bounds = false;
    size_t i;

    for (i = 0; i < program->types.length; i++) {
        const TypedType *type = typed_program_type(program, i);
        bool already = false;
        size_t j;

        if (type == NULL || type->kind != NIPPY_TYPE_STRING) {
            continue;
        }
        for (j = 0; j < i; j++) {
            const TypedType *earlier = typed_program_type(program, j);

            if (earlier != NULL && earlier->kind == NIPPY_TYPE_STRING &&
                earlier->fixed_length == type->fixed_length) {
                already = true;
                break;
            }
        }
        if (already) {
            continue;
        }
        if (!emitted_bounds) {
            if (!buffer_append(
                    source,
                    "static void nippy_string_bounds(void)\n"
                    "{\n"
                    "    fprintf(stderr, \"" MESSAGE_RUNTIME_STRING_BOUNDS
                    "\\n\");\n"
                    "    exit(1);\n"
                    "}\n\n")) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
                return false;
            }
            emitted_bounds = true;
        }
        if (!buffer_appendf(
                source,
                "static uint8_t nippy_string_%zu_at(nippy_string_%zu s, "
                "int32_t i)\n"
                "{\n"
                "    if (i < 0 || (uint32_t)i >= s.length) {\n"
                "        nippy_string_bounds();\n"
                "    }\n"
                "    return s.data[i];\n"
                "}\n\n"
                "static bool nippy_string_%zu_equal(nippy_string_%zu a, "
                "nippy_string_%zu b)\n"
                "{\n"
                "    return a.length == b.length &&\n"
                "           memcmp(a.data, b.data, a.length) == 0;\n"
                "}\n\n",
                type->fixed_length, type->fixed_length, type->fixed_length,
                type->fixed_length, type->fixed_length)) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            return false;
        }
    }
    return true;
}

static bool emit_dynamic_string_runtime(const TypedProgram *program,
                                         size_t store_count,
                                         TextBuffer *source,
                                         ErrorState *errors)
{
    bool ok = true;

    ok = ok && buffer_append(
                   source,
                   "static NIPPY_UNUSED void nippy_string_fail(const char *message)\n"
                   "{\n"
                   "    fprintf(stderr, \"%s\\n\", message);\n"
                   "    exit(1);\n"
                   "}\n\n");
    if (store_count > 0) {
        ok = ok && buffer_append(
                       source,
                       "static NIPPY_UNUSED const uint8_t *"
                       "nippy_string_data(nippy_string s)\n"
                       "{\n"
                       "    nippy_store *store;\n\n"
                       "    if (s.static_data != NULL) {\n"
                       "        return s.static_data;\n"
                       "    }\n"
                       "    if (s.store >= (uint32_t)NIPPY_STORE_COUNT) {\n"
                       "        nippy_string_fail(\"" MESSAGE_RUNTIME_STORE_RANGE
                       "\");\n"
                       "    }\n"
                       "    store = &nippy_stores[s.store];\n"
                       "    if (store->generation != s.generation) {\n"
                       "        nippy_string_fail(\"" MESSAGE_RUNTIME_STALE_HANDLE
                       "\");\n"
                       "    }\n"
                       "    if (s.offset > store->used ||\n"
                       "        s.length > store->used - s.offset ||\n"
                       "        s.length > (size_t)-1 - s.offset - 1 ||\n"
                       "        s.offset + s.length + 1 > store->used) {\n"
                       "        nippy_string_fail(\"" MESSAGE_RUNTIME_STRING_BOUNDS
                       "\");\n"
                       "    }\n"
                       "    return store->data + s.offset;\n"
                       "}\n\n");
    } else {
        ok = ok && buffer_append(
                       source,
                       "static NIPPY_UNUSED const uint8_t *"
                       "nippy_string_data(nippy_string s)\n"
                       "{\n"
                       "    if (s.static_data != NULL) {\n"
                       "        return s.static_data;\n"
                       "    }\n"
                       "    nippy_string_fail(\"" MESSAGE_RUNTIME_STORE_RANGE
                       "\");\n"
                       "    return NULL;\n"
                       "}\n\n");
    }
    ok = ok && buffer_append(
                   source,
                   "static NIPPY_UNUSED int32_t nippy_string_len(nippy_string s)\n"
                   "{\n"
                   "    (void)nippy_string_data(s);\n"
                   "    return (int32_t)s.length;\n"
                   "}\n\n"
                   "static NIPPY_UNUSED uint8_t nippy_string_byte_at(nippy_string s, "
                   "int32_t i)\n"
                   "{\n"
                   "    if (i < 0 || (size_t)i >= s.length) {\n"
                   "        nippy_string_fail(\"" MESSAGE_RUNTIME_STRING_BOUNDS
                   "\");\n"
                   "    }\n"
                   "    return nippy_string_data(s)[i];\n"
                   "}\n\n"
                   "static NIPPY_UNUSED nippy_string "
                   "nippy_string_slice(nippy_string s, "
                   "int32_t start, int32_t length)\n"
                   "{\n"
                   "    nippy_string result;\n\n"
                   "    if (start < 0 || length < 0 ||\n"
                   "        (size_t)start > s.length ||\n"
                   "        (size_t)length > s.length - (size_t)start) {\n"
                   "        nippy_string_fail(\"" MESSAGE_RUNTIME_STRING_BOUNDS
                   "\");\n"
                   "    }\n"
                   "    (void)nippy_string_data(s);\n"
                   "    result = s;\n"
                   "    if (result.static_data != NULL) {\n"
                   "        result.static_data += start;\n"
                   "    } else {\n"
                   "        result.offset += (size_t)start;\n"
                   "    }\n"
                   "    result.length = (size_t)length;\n"
                   "    return result;\n"
                   "}\n\n"
                   "static NIPPY_UNUSED bool nippy_string_equal(nippy_string a, "
                   "nippy_string b)\n"
                   "{\n"
                   "    return a.length == b.length &&\n"
                   "           memcmp(nippy_string_data(a), nippy_string_data(b), "
                   "a.length) == 0;\n"
                   "}\n\n");
    if (store_count > 0) {
        ok = ok && buffer_append(
                       source,
                       "static NIPPY_UNUSED nippy_string nippy_string_from_bytes("
                       "nippy_store *store, uint32_t id,\n"
                       "                                             "
                       "const uint8_t *bytes, size_t length)\n"
                       "{\n"
                       "    size_t offset;\n"
                       "    nippy_string result;\n\n"
                       "    if (length > (size_t)-1 - 1) {\n"
                       "        nippy_string_fail(\"" MESSAGE_RUNTIME_OUT_OF_MEMORY
                       "\");\n"
                       "    }\n"
                       "    offset = nippy_store_alloc_bytes(store, id, "
                       "length + 1);\n"
                       "    memcpy(store->data + offset, bytes, length);\n"
                       "    store->data[offset + length] = 0;\n"
                       "    result.static_data = NULL;\n"
                       "    result.store = id;\n"
                       "    result.offset = offset;\n"
                       "    result.length = length;\n"
                       "    result.generation = store->generation;\n"
                       "    return result;\n"
                       "}\n\n"
                       "static NIPPY_UNUSED nippy_string nippy_string_concat("
                       "nippy_store *store, uint32_t id,\n"
                       "                                        "
                       "nippy_string a, nippy_string b)\n"
                       "{\n"
                       "    size_t total;\n"
                       "    size_t offset;\n"
                       "    nippy_string result;\n"
                       "    const uint8_t *a_data;\n"
                       "    const uint8_t *b_data;\n\n"
                       "    if (a.length > (size_t)-1 - 1 ||\n"
                       "        b.length > (size_t)-1 - 1 - a.length) {\n"
                       "        nippy_string_fail(\"" MESSAGE_RUNTIME_OUT_OF_MEMORY
                       "\");\n"
                       "    }\n"
                       "    total = a.length + b.length;\n"
                       "    offset = nippy_store_alloc_bytes(store, id, "
                       "total + 1);\n"
                       "    a_data = nippy_string_data(a);\n"
                       "    b_data = nippy_string_data(b);\n"
                       "    memcpy(store->data + offset, a_data, a.length);\n"
                       "    memcpy(store->data + offset + a.length, b_data, "
                       "b.length);\n"
                       "    store->data[offset + total] = 0;\n"
                       "    result.static_data = NULL;\n"
                       "    result.store = id;\n"
                       "    result.offset = offset;\n"
                       "    result.length = total;\n"
                       "    result.generation = store->generation;\n"
                       "    return result;\n"
                       "}\n\n"
                       "static NIPPY_UNUSED nippy_string nippy_string_copy("
                       "nippy_store *store, uint32_t id,\n"
                       "                                      nippy_string s)\n"
                       "{\n"
                       "    size_t offset;\n"
                       "    nippy_string result;\n"
                       "    const uint8_t *data;\n\n"
                       "    if (s.length > (size_t)-1 - 1) {\n"
                       "        nippy_string_fail(\"" MESSAGE_RUNTIME_OUT_OF_MEMORY
                       "\");\n"
                       "    }\n"
                       "    offset = nippy_store_alloc_bytes(store, id, "
                       "s.length + 1);\n"
                       "    data = nippy_string_data(s);\n"
                       "    memcpy(store->data + offset, data, s.length);\n"
                       "    store->data[offset + s.length] = 0;\n"
                       "    result.static_data = NULL;\n"
                       "    result.store = id;\n"
                       "    result.offset = offset;\n"
                       "    result.length = s.length;\n"
                       "    result.generation = store->generation;\n"
                       "    return result;\n"
                       "}\n\n");
        {
            size_t i;

            for (i = 0; i < program->types.length; i++) {
                const TypedType *type = typed_program_type(program, i);
                bool already = false;
                size_t j;

                if (type == NULL || type->kind != NIPPY_TYPE_STRING) {
                    continue;
                }
                for (j = 0; j < i; j++) {
                    const TypedType *earlier = typed_program_type(program, j);

                    if (earlier != NULL &&
                        earlier->kind == NIPPY_TYPE_STRING &&
                        earlier->fixed_length == type->fixed_length) {
                        already = true;
                        break;
                    }
                }
                if (already) {
                    continue;
                }
                ok = ok && buffer_appendf(
                               source,
                               "static NIPPY_UNUSED nippy_string "
                               "nippy_string_from_fixed_%zu("
                               "nippy_store *store, uint32_t id,\n"
                               "                                             "
                               "nippy_string_%zu s)\n"
                               "{\n"
                               "    return nippy_string_from_bytes(store, id, "
                               "s.data, s.length);\n"
                               "}\n\n",
                               type->fixed_length, type->fixed_length);
            }
        }
    }
    if (!ok) {
        error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
        return false;
    }
    return true;
}

static bool emit_dynamic_string_literals(const TypedProgram *program,
                                         TextBuffer *source,
                                         ErrorState *errors)
{
    size_t i;

    for (i = 0; i < program->nodes.length; i++) {
        const TypedNode *node = typed_program_node(program, i);
        const TypedType *type;
        size_t j;

        if (node == NULL || node->kind != TYPED_STRING_LITERAL) {
            continue;
        }
        type = typed_program_type(program, node->type_index);
        if (type == NULL || type->kind != NIPPY_TYPE_DYNAMIC_STRING) {
            continue;
        }
        if (!buffer_appendf(source,
                            "static const uint8_t nippy_dyn_str_%zu[] = { ",
                            i)) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            return false;
        }
        for (j = 0; j < node->string_length; j++) {
            const unsigned char *byte = list_get(&program->bytes,
                                                 node->string_offset + j);

            if (byte == NULL ||
                !buffer_appendf(source, "%u, ", (unsigned)*byte)) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
                return false;
            }
        }
        if (!buffer_append(source, "0 };\n")) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            return false;
        }
    }
    return true;
}

/* The store runtime is emitted once, ahead of every function, so helpers and
   the table are visible to all generated code. Only programs with a store
   declaration get it; a program that merely mentions Handle<T> needs only the
   header typedef. */
static bool emit_store_runtime(TextBuffer *source, size_t store_count,
                               bool needs_bytes_alloc,
                               bool needs_aligned_alloc,
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
    if (needs_bytes_alloc) {
        ok = ok && buffer_append(
                       source,
                       "static size_t nippy_store_alloc_bytes("
                       "nippy_store *store, uint32_t id, size_t size)\n"
                       "{\n"
                       "    size_t offset;\n\n"
                       "    if (id >= (uint32_t)NIPPY_STORE_COUNT) {\n"
                       "        nippy_panic(\"" MESSAGE_RUNTIME_STORE_RANGE
                       "\");\n"
                       "    }\n"
                       "    if (size == 0) {\n"
                       "        nippy_panic(\"" MESSAGE_RUNTIME_HANDLE_BOUNDS
                       "\");\n"
                       "    }\n"
                       "    offset = store->used;\n"
                       "    if (offset > (size_t)-1 - size) {\n"
                       "        nippy_panic(\"" MESSAGE_RUNTIME_OUT_OF_MEMORY
                       "\");\n"
                       "    }\n"
                       "    nippy_store_reserve(store, offset + size);\n"
                       "    store->used = offset + size;\n"
                       "    return offset;\n"
                       "}\n\n");
    }
    if (needs_aligned_alloc) {
        ok = ok && buffer_append(
                       source,
                       "static size_t nippy_store_alloc_bytes_aligned("
                       "nippy_store *store, uint32_t id, size_t size,\n"
                       "                                              "
                       "size_t alignment)\n"
                       "{\n"
                       "    size_t offset;\n\n"
                       "    if (id >= (uint32_t)NIPPY_STORE_COUNT) {\n"
                       "        nippy_panic(\"" MESSAGE_RUNTIME_STORE_RANGE
                       "\");\n"
                       "    }\n"
                       "    if (size == 0) {\n"
                       "        nippy_panic(\"" MESSAGE_RUNTIME_HANDLE_BOUNDS
                       "\");\n"
                       "    }\n"
                       "    offset = nippy_store_align(store->used, alignment);\n"
                       "    if (offset > (size_t)-1 - size) {\n"
                       "        nippy_panic(\"" MESSAGE_RUNTIME_OUT_OF_MEMORY
                       "\");\n"
                       "    }\n"
                       "    nippy_store_reserve(store, offset + size);\n"
                       "    store->used = offset + size;\n"
                       "    return offset;\n"
                       "}\n\n");
    }
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
        !buffer_append(source, ";\n")) {
        if (!errors->reported) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
        }
        return false;
    }
    {
        size_t i;

        for (i = 0; i < program->roots.length; i++) {
            size_t root = *(const size_t *)list_get(&program->roots, i);
            const TypedNode *node = typed_program_node(program, root);

            if (node != NULL && node->kind == TYPED_ASSIGNMENT &&
                !emit_statement(program, root, 1, effective_source, source,
                                errors)) {
                return false;
            }
        }
    }
    if (!buffer_appendf(source, "    return %s;\n", name) ||
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

static bool emit_builder_runtime(TextBuffer *source, ErrorState *errors)
{
    bool ok = true;

    ok = ok && buffer_append(
                   source,
                   "typedef struct {\n"
                   "    size_t offset;\n"
                   "    size_t length;\n"
                   "    size_t capacity;\n"
                   "} nippy_builder_state;\n\n"
                   "static NIPPY_UNUSED nippy_store *nippy_builder_store("
                   "nippy_builder b)\n"
                   "{\n"
                   "    if (b.store >= (uint32_t)NIPPY_STORE_COUNT) {\n"
                   "        nippy_string_fail(\"" MESSAGE_RUNTIME_STORE_RANGE
                   "\");\n"
                   "    }\n"
                   "    if (nippy_stores[b.store].generation != b.generation) {\n"
                   "        nippy_string_fail(\"" MESSAGE_RUNTIME_STALE_HANDLE
                   "\");\n"
                   "    }\n"
                   "    return &nippy_stores[b.store];\n"
                   "}\n\n"
                   "static NIPPY_UNUSED nippy_builder_state *nippy_builder_get_state("
                   "nippy_builder b)\n"
                   "{\n"
                   "    nippy_store *store = nippy_builder_store(b);\n\n"
                   "    if (b.offset > store->used ||\n"
                   "        sizeof(nippy_builder_state) > store->used - b.offset) {\n"
                   "        nippy_string_fail(\"" MESSAGE_RUNTIME_HANDLE_BOUNDS
                   "\");\n"
                   "    }\n"
                   "    return (nippy_builder_state *)(void *)(store->data + "
                   "b.offset);\n"
                   "}\n\n");
    ok = ok && buffer_append(
                   source,
                   "static NIPPY_UNUSED nippy_builder nippy_builder_create("
                   "nippy_store *store, uint32_t id, int32_t capacity)\n"
                   "{\n"
                   "    nippy_builder_state state;\n"
                   "    nippy_handle handle;\n"
                   "    nippy_builder builder;\n"
                   "    size_t cap;\n"
                   "    size_t buffer;\n\n"
                   "    if (capacity < 0) {\n"
                   "        nippy_string_fail(\"" MESSAGE_RUNTIME_STRING_BOUNDS
                   "\");\n"
                   "    }\n"
                   "    cap = (size_t)capacity;\n"
                   "    if (cap > (size_t)-1 - 1) {\n"
                   "        nippy_string_fail(\"" MESSAGE_RUNTIME_OUT_OF_MEMORY
                   "\");\n"
                   "    }\n"
                   "    buffer = nippy_store_alloc_bytes(store, id, cap + 1);\n"
                   "    store->data[buffer] = 0;\n"
                   "    state.offset = buffer;\n"
                   "    state.length = 0;\n"
                   "    state.capacity = cap;\n"
                   "    handle = nippy_store_alloc(store, id, &state, sizeof(state),\n"
                   "                               _Alignof(nippy_builder_state));\n"
                   "    builder.store = id;\n"
                   "    builder.offset = handle.offset;\n"
                   "    builder.generation = handle.generation;\n"
                   "    return builder;\n"
                   "}\n\n"
                   "static NIPPY_UNUSED void nippy_builder_reserve("
                   "nippy_builder b, size_t needed)\n"
                   "{\n"
                   "    nippy_store *store = nippy_builder_store(b);\n"
                   "    nippy_builder_state *state = nippy_builder_get_state(b);\n"
                   "    size_t capacity;\n"
                   "    size_t old_offset;\n"
                   "    size_t old_length;\n"
                   "    size_t offset;\n\n"
                   "    if (needed <= state->capacity) {\n"
                   "        return;\n"
                   "    }\n"
                   "    capacity = state->capacity == 0 ? 1 : state->capacity;\n"
                   "    while (capacity < needed) {\n"
                   "        if (capacity > (size_t)-1 / 2) {\n"
                   "            nippy_string_fail(\""
                   MESSAGE_RUNTIME_OUT_OF_MEMORY "\");\n"
                   "        }\n"
                   "        capacity *= 2;\n"
                   "    }\n"
                   "    old_offset = state->offset;\n"
                   "    old_length = state->length;\n"
                   "    offset = nippy_store_alloc_bytes(store, b.store, "
                   "capacity + 1);\n"
                   "    state = nippy_builder_get_state(b);\n"
                   "    memcpy(store->data + offset, store->data + old_offset,\n"
                   "           old_length);\n"
                   "    state->offset = offset;\n"
                   "    state->capacity = capacity;\n"
                   "    state->length = old_length;\n"
                   "    store->data[offset + old_length] = 0;\n"
                   "}\n\n");
    ok = ok && buffer_append(
                   source,
                   "static NIPPY_UNUSED void nippy_builder_append("
                   "nippy_builder b, nippy_string s)\n"
                   "{\n"
                   "    nippy_builder_state *state = nippy_builder_get_state(b);\n"
                   "    size_t needed;\n"
                   "    const uint8_t *data;\n"
                   "    nippy_store *store;\n\n"
                   "    if (s.length > (size_t)-1 - state->length) {\n"
                   "        nippy_string_fail(\"" MESSAGE_RUNTIME_OUT_OF_MEMORY
                   "\");\n"
                   "    }\n"
                   "    needed = state->length + s.length;\n"
                   "    nippy_builder_reserve(b, needed);\n"
                   "    state = nippy_builder_get_state(b);\n"
                   "    store = nippy_builder_store(b);\n"
                   "    data = nippy_string_data(s);\n"
                   "    memcpy(store->data + state->offset + state->length, data,\n"
                   "           s.length);\n"
                   "    state->length = needed;\n"
                   "    store->data[state->offset + needed] = 0;\n"
                   "}\n\n"
                   "static NIPPY_UNUSED void nippy_builder_append_byte("
                   "nippy_builder b, uint8_t byte)\n"
                   "{\n"
                   "    nippy_builder_state *state = nippy_builder_get_state(b);\n"
                   "    nippy_store *store;\n"
                   "    size_t needed;\n\n"
                   "    if (state->length == (size_t)-1) {\n"
                   "        nippy_string_fail(\"" MESSAGE_RUNTIME_OUT_OF_MEMORY
                   "\");\n"
                   "    }\n"
                   "    needed = state->length + 1;\n"
                   "    nippy_builder_reserve(b, needed);\n"
                   "    state = nippy_builder_get_state(b);\n"
                   "    store = nippy_builder_store(b);\n"
                   "    store->data[state->offset + state->length] = byte;\n"
                   "    state->length = needed;\n"
                   "    store->data[state->offset + needed] = 0;\n"
                   "}\n\n");
    ok = ok && buffer_append(
                   source,
                   "static NIPPY_UNUSED int32_t nippy_builder_len("
                   "nippy_builder b)\n"
                   "{\n"
                   "    return (int32_t)nippy_builder_get_state(b)->length;\n"
                   "}\n\n"
                   "static NIPPY_UNUSED nippy_string nippy_builder_finish("
                   "nippy_builder b)\n"
                   "{\n"
                   "    nippy_builder_state *state = nippy_builder_get_state(b);\n"
                   "    nippy_store *store = nippy_builder_store(b);\n"
                   "    nippy_string result;\n"
                   "    size_t length = state->length;\n"
                   "    size_t source = state->offset;\n"
                   "    size_t offset;\n\n"
                   "    if (length > (size_t)-1 - 1) {\n"
                   "        nippy_string_fail(\"" MESSAGE_RUNTIME_OUT_OF_MEMORY
                   "\");\n"
                   "    }\n"
                   "    offset = nippy_store_alloc_bytes(store, b.store, "
                   "length + 1);\n"
                   "    memcpy(store->data + offset, store->data + source, "
                   "length);\n"
                   "    store->data[offset + length] = 0;\n"
                   "    result.static_data = NULL;\n"
                   "    result.store = b.store;\n"
                   "    result.offset = offset;\n"
                   "    result.length = length;\n"
                   "    result.generation = store->generation;\n"
                   "    return result;\n"
                   "}\n\n");
    if (!ok) {
        error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
        return false;
    }
    return true;
}

static bool emit_list_runtime(const TypedProgram *program, size_t store_count,
                              TextBuffer *source, ErrorState *errors)
{
    bool ok = true;
    size_t i;

    ok = ok && buffer_append(
                   source,
                   "static NIPPY_UNUSED void nippy_list_fail("
                   "const char *message)\n"
                   "{\n"
                   "    fprintf(stderr, \"%s\\n\", message);\n"
                   "    exit(1);\n"
                   "}\n\n");
    for (i = 0; i < program->types.length; i++) {
        const TypedType *type = typed_program_type(program, i);
        char element_name[NIPPY_IDENTIFIER_MAX + 32];
        char list_name[NIPPY_IDENTIFIER_MAX + 32];
        size_t element;

        if (type == NULL || type->kind != NIPPY_TYPE_FIXED_LIST ||
            type->child_count != 1) {
            continue;
        }
        element = *(const size_t *)list_get(&program->type_children,
                                            type->first_child);
        if (!fixed_list_element_name(program, i, element, element_name,
                                     sizeof(element_name), errors)) {
            return false;
        }
        snprintf(list_name, sizeof(list_name), "nippy_list_%zu", i);
        ok = ok && buffer_appendf(
                       source,
                       "static NIPPY_UNUSED %s nippy_list_%zu_get("
                       "%s list, int32_t i)\n"
                       "{\n"
                       "    if (i < 0 || (size_t)i >= list.length) {\n"
                       "        nippy_list_fail(\"" MESSAGE_RUNTIME_LIST_BOUNDS
                       "\");\n"
                       "    }\n"
                       "    return list.data[i];\n"
                       "}\n\n"
                       "static NIPPY_UNUSED void nippy_list_%zu_push("
                       "%s *list, %s value)\n"
                       "{\n"
                       "    if (list->length >= %zu) {\n"
                       "        nippy_list_fail(\"" MESSAGE_RUNTIME_LIST_BOUNDS
                       "\");\n"
                       "    }\n"
                       "    list->data[list->length] = value;\n"
                       "    list->length++;\n"
                       "}\n\n"
                       "static NIPPY_UNUSED %s *nippy_list_%zu_at("
                       "%s *list, int32_t i)\n"
                       "{\n"
                       "    if (i < 0 || (size_t)i >= list->length) {\n"
                       "        nippy_list_fail(\"" MESSAGE_RUNTIME_LIST_BOUNDS
                       "\");\n"
                       "    }\n"
                       "    return &list->data[i];\n"
                       "}\n\n",
                       element_name, i, list_name,
                       i, list_name, element_name, type->fixed_length,
                       element_name, i, list_name);
    }
    if (store_count > 0) {
        bool dynamic_list = false;

        for (i = 0; i < program->types.length; i++) {
            const TypedType *type = typed_program_type(program, i);

            if (type != NULL && type->kind == NIPPY_TYPE_LIST) {
                dynamic_list = true;
                break;
            }
        }
        if (dynamic_list) {
            ok = ok && buffer_append(
                           source,
                           "typedef struct {\n"
                           "    size_t offset;\n"
                           "    size_t length;\n"
                           "    size_t capacity;\n"
                           "} nippy_list_state;\n\n"
                           "static NIPPY_UNUSED nippy_store *nippy_list_store("
                           "nippy_list list)\n"
                           "{\n"
                           "    if (list.store >= (uint32_t)NIPPY_STORE_COUNT) {\n"
                           "        nippy_list_fail(\""
                           MESSAGE_RUNTIME_STORE_RANGE "\");\n"
                           "    }\n"
                           "    if (nippy_stores[list.store].generation != "
                           "list.generation) {\n"
                           "        nippy_list_fail(\""
                           MESSAGE_RUNTIME_STALE_HANDLE "\");\n"
                           "    }\n"
                           "    return &nippy_stores[list.store];\n"
                           "}\n\n"
                           "static NIPPY_UNUSED nippy_list_state *"
                           "nippy_list_get_state(nippy_list list)\n"
                           "{\n"
                           "    nippy_store *store = nippy_list_store(list);\n\n"
                           "    if (list.offset > store->used ||\n"
                           "        sizeof(nippy_list_state) > "
                           "store->used - list.offset) {\n"
                           "        nippy_list_fail(\""
                           MESSAGE_RUNTIME_HANDLE_BOUNDS "\");\n"
                           "    }\n"
                           "    return (nippy_list_state *)(void *)"
                           "(store->data + list.offset);\n"
                           "}\n\n"
                           "static NIPPY_UNUSED void *nippy_list_data("
                           "nippy_list list, int32_t index,\n"
                           "                                      "
                           "size_t element_size)\n"
                           "{\n"
                           "    nippy_list_state *state = nippy_list_get_state(list);\n"
                           "    nippy_store *store = nippy_list_store(list);\n"
                           "    size_t offset;\n\n"
                           "    if (index < 0 || (size_t)index >= state->length ||\n"
                           "        element_size == 0) {\n"
                           "        nippy_list_fail(\"");
            ok = ok && buffer_append(
                           source,
                           MESSAGE_RUNTIME_LIST_BOUNDS "\");\n"
                           "    }\n"
                           "    if ((size_t)index > (size_t)-1 / "
                           "element_size) {\n"
                           "        nippy_list_fail(\""
                           MESSAGE_RUNTIME_OUT_OF_MEMORY "\");\n"
                           "    }\n"
                           "    offset = state->offset + (size_t)index * "
                           "element_size;\n"
                           "    if (offset > store->used ||\n"
                           "        element_size > store->used - offset) {\n"
                           "        nippy_list_fail(\"");
            ok = ok && buffer_append(
                           source,
                           MESSAGE_RUNTIME_HANDLE_BOUNDS "\");\n"
                           "    }\n"
                           "    return (void *)(store->data + offset);\n"
                           "}\n\n");
            ok = ok && buffer_append(
                           source,
                           "static NIPPY_UNUSED nippy_list nippy_list_create("
                           "nippy_store *store, uint32_t id, int32_t capacity,\n"
                           "                                              "
                           "size_t element_size, size_t alignment)\n"
                           "{\n"
                           "    nippy_list_state state;\n"
                           "    nippy_handle handle;\n"
                           "    nippy_list list;\n"
                           "    size_t cap;\n"
                           "    size_t bytes;\n"
                           "    size_t buffer;\n\n"
                           "    if (capacity < 0 || element_size == 0) {\n"
                           "        nippy_list_fail(\""
                           MESSAGE_RUNTIME_LIST_BOUNDS "\");\n"
                           "    }\n"
                           "    cap = (size_t)capacity;\n"
                           "    if (cap > (size_t)-1 / element_size) {\n"
                           "        nippy_list_fail(\""
                           MESSAGE_RUNTIME_OUT_OF_MEMORY "\");\n"
                           "    }\n"
                           "    bytes = cap * element_size;\n"
                           "    buffer = nippy_store_alloc_bytes_aligned(store, id,\n"
                           "        bytes == 0 ? 1 : bytes, alignment);\n"
                           "    state.offset = buffer;\n"
                           "    state.length = 0;\n"
                           "    state.capacity = cap;\n"
                           "    handle = nippy_store_alloc(store, id, &state, "
                           "sizeof(state),\n"
                           "                               "
                           "_Alignof(nippy_list_state));\n"
                           "    list.store = id;\n"
                           "    list.offset = handle.offset;\n"
                           "    list.generation = handle.generation;\n"
                           "    return list;\n"
                           "}\n\n");
            ok = ok && buffer_append(
                           source,
                           "static NIPPY_UNUSED void nippy_list_push("
                           "nippy_list list, const void *value,\n"
                           "                                      "
                           "size_t element_size, size_t alignment)\n"
                           "{\n"
                           "    nippy_list_state *state = nippy_list_get_state(list);\n"
                           "    nippy_store *store;\n"
                           "    size_t needed;\n"
                           "    size_t capacity;\n\n"
                           "    if (state->length == (size_t)-1) {\n"
                           "        nippy_list_fail(\"");
            ok = ok && buffer_append(
                           source,
                           MESSAGE_RUNTIME_OUT_OF_MEMORY "\");\n"
                           "    }\n"
                           "    needed = state->length + 1;\n"
                           "    if (needed > state->capacity) {\n"
                           "        size_t old_offset = state->offset;\n"
                           "        size_t old_length = state->length;\n"
                           "        size_t old_bytes = old_length * element_size;\n"
                           "        size_t new_bytes;\n"
                           "        size_t new_offset;\n\n"
                           "        capacity = state->capacity == 0 ? 1 : "
                           "state->capacity;\n"
                           "        while (capacity < needed) {\n"
                           "            if (capacity > (size_t)-1 / 2) {\n"
                           "                nippy_list_fail(\"");
            ok = ok && buffer_append(
                           source,
                           MESSAGE_RUNTIME_OUT_OF_MEMORY "\");\n"
                           "            }\n"
                           "            capacity *= 2;\n"
                           "        }\n"
                           "        if (capacity > (size_t)-1 / element_size) {\n"
                           "            nippy_list_fail(\"");
            ok = ok && buffer_append(
                           source,
                           MESSAGE_RUNTIME_OUT_OF_MEMORY "\");\n"
                           "        }\n"
                           "        new_bytes = capacity * element_size;\n"
                           "        new_offset = nippy_store_alloc_bytes_aligned(\n"
                           "            nippy_list_store(list), list.store, "
                           "new_bytes,\n"
                           "            alignment);\n"
                           "        store = nippy_list_store(list);\n"
                           "        memcpy(store->data + new_offset, "
                           "store->data + old_offset,\n"
                           "               old_bytes);\n"
                           "        state = nippy_list_get_state(list);\n"
                           "        state->offset = new_offset;\n"
                           "        state->capacity = capacity;\n"
                           "    }\n"
                           "    state = nippy_list_get_state(list);\n"
                           "    store = nippy_list_store(list);\n"
                           "    memcpy(store->data + state->offset + "
                           "state->length * element_size,\n"
                           "           value, element_size);\n"
                           "    state->length = needed;\n"
                           "}\n\n"
                           "static NIPPY_UNUSED int32_t nippy_list_len("
                           "nippy_list list)\n"
                           "{\n"
                           "    return (int32_t)nippy_list_get_state(list)->length;\n"
                           "}\n\n");
        }
    }
    for (i = 0; i < program->nodes.length; i++) {
        const TypedNode *node = typed_program_node(program, i);
        const TypedNode *receiver;
        const TypedType *receiver_type;
        const TypedType *union_type;
        const TypedType *element_type;
        size_t nil_type = TYPED_NO_NODE;
        size_t element_tag = TYPED_NO_NODE;
        size_t nil_tag;
        bool element_is_union;
        bool same_result;
        char union_name[NIPPY_IDENTIFIER_MAX + 32];
        char element_name[NIPPY_IDENTIFIER_MAX + 32];
        char list_name[NIPPY_IDENTIFIER_MAX + 32];
        TextBuffer body;
        size_t j;

        if (node == NULL || node->kind != TYPED_LIST_POP) {
            continue;
        }
        receiver = typed_program_node(program, node->left_child);
        receiver_type = receiver == NULL
                            ? NULL
                            : typed_program_type(program, receiver->type_index);
        union_type = typed_program_type(program, node->type_index);
        element_type = typed_program_type(program, node->auxiliary_type);
        if (receiver_type != NULL && union_type != NULL &&
            union_type->kind == NIPPY_TYPE_NIL) {
            if (!format_c_type(program, receiver->type_index, list_name,
                               sizeof(list_name), errors)) {
                return false;
            }
            if (receiver_type->kind == NIPPY_TYPE_FIXED_LIST) {
                ok = ok && buffer_appendf(
                               source,
                               "static NIPPY_UNUSED nippy_nil "
                               "nippy_list_pop_%zu(%s *list)\n"
                               "{\n"
                               "    if (list->length != 0) {\n"
                               "        list->length--;\n"
                               "    }\n"
                               "    return (nippy_nil)0;\n"
                               "}\n\n",
                               i, list_name);
            } else {
                ok = ok && buffer_appendf(
                               source,
                               "static NIPPY_UNUSED nippy_nil "
                               "nippy_list_pop_%zu(nippy_list list)\n"
                               "{\n"
                               "    nippy_list_state *state = "
                               "nippy_list_get_state(list);\n"
                               "    if (state->length != 0) {\n"
                               "        state->length--;\n"
                               "    }\n"
                               "    return (nippy_nil)0;\n"
                               "}\n\n",
                               i);
            }
            continue;
        }
        if (receiver_type == NULL || union_type == NULL ||
            element_type == NULL || union_type->kind != NIPPY_TYPE_UNION ||
            !format_c_type(program, node->type_index, union_name,
                           sizeof(union_name), errors) ||
            !format_c_type(program, node->auxiliary_type, element_name,
                           sizeof(element_name), errors) ||
            !format_c_type(program, receiver->type_index, list_name,
                           sizeof(list_name), errors)) {
            return false;
        }
        element_is_union = element_type->kind == NIPPY_TYPE_UNION;
        same_result = types_equivalent(program, node->auxiliary_type,
                                       node->type_index, 16);
        for (j = 0; j < program->types.length; j++) {
            const TypedType *candidate = typed_program_type(program, j);

            if (candidate != NULL && candidate->kind == NIPPY_TYPE_NIL) {
                nil_type = j;
                break;
            }
        }
        if (nil_type == TYPED_NO_NODE ||
            !find_union_alternative(program, union_type, nil_type, &nil_tag)) {
            return false;
        }
        if (!element_is_union &&
            !find_union_alternative(program, union_type,
                                    node->auxiliary_type, &element_tag)) {
            return false;
        }
        buffer_init(&body);
        if (!buffer_appendf(
                &body,
                "static NIPPY_UNUSED %s nippy_list_pop_%zu(",
                union_name, i) ||
            !buffer_append(&body,
                           receiver_type->kind == NIPPY_TYPE_FIXED_LIST
                               ? ""
                               : "nippy_list list)\n{\n")) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            buffer_free(&body);
            return false;
        }
        if (receiver_type->kind == NIPPY_TYPE_FIXED_LIST) {
            if (!buffer_appendf(&body, "%s *list)\n{\n", list_name)) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
                buffer_free(&body);
                return false;
            }
        }
        if (!buffer_appendf(&body, "    %s result;\n    %s value;\n\n",
                            union_name, element_name) ||
            !buffer_appendf(&body,
                            "    if (%s == 0) {\n"
                            "        result = (%s){ .tag = %zu };\n"
                            "        return result;\n"
                            "    }\n",
                            receiver_type->kind == NIPPY_TYPE_FIXED_LIST
                                ? "list->length"
                                : "nippy_list_get_state(list)->length",
                            union_name, nil_tag)) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            buffer_free(&body);
            return false;
        }
        if (receiver_type->kind == NIPPY_TYPE_FIXED_LIST) {
            if (!buffer_append(
                    &body,
                    "    value = list->data[list->length - 1];\n"
                    "    list->length--;\n")) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
                buffer_free(&body);
                return false;
            }
        } else {
            if (!buffer_appendf(
                    &body,
                    "    {\n"
                    "        nippy_list_state *state = "
                    "nippy_list_get_state(list);\n"
                    "        nippy_store *store = nippy_list_store(list);\n"
                    "        size_t offset = state->offset + "
                    "(state->length - 1) * sizeof(%s);\n\n"
                    "        value = *(%s *)(void *)(store->data + offset);\n"
                    "        state->length--;\n"
                    "    }\n",
                    element_name, element_name)) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
                buffer_free(&body);
                return false;
            }
        }
        if (element_is_union && !same_result) {
            if (!buffer_append(&body, "    switch (value.tag) {\n")) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
                buffer_free(&body);
                return false;
            }
            for (j = 0; j < element_type->child_count; j++) {
                const TypedType *alternative =
                    union_alternative_type(program, element_type, j);
                size_t result_tag;

                if (alternative == NULL ||
                    alternative->kind == NIPPY_TYPE_NIL ||
                    !find_union_alternative(
                        program, union_type,
                        *(const size_t *)list_get(
                            &program->type_children,
                            element_type->first_child + j),
                        &result_tag)) {
                    error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_TYPE);
                    buffer_free(&body);
                    return false;
                }
                if (!buffer_appendf(
                        &body,
                        "    case %zu:\n"
                        "        result = (%s){ .tag = %zu, "
                        ".value.v%zu = value.value.v%zu };\n"
                        "        break;\n",
                        j, union_name, result_tag, result_tag, j)) {
                    error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
                    buffer_free(&body);
                    return false;
                }
            }
            if (!buffer_append(&body, "    }\n")) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
                buffer_free(&body);
                return false;
            }
        } else if (element_is_union) {
            if (!buffer_append(&body, "    result = value;\n")) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
                buffer_free(&body);
                return false;
            }
        } else if (!buffer_appendf(
                       &body,
                       "    result = (%s){ .tag = %zu, "
                       ".value.v%zu = value };\n",
                       union_name, element_tag, element_tag)) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            buffer_free(&body);
            return false;
        }
        if (!buffer_append(&body, "    return result;\n}\n\n") ||
            !buffer_append(source, body.text)) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            buffer_free(&body);
            return false;
        }
        buffer_free(&body);
    }
    if (!ok) {
        error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
        return false;
    }
    return true;
}

static bool emit_slice_runtime(TextBuffer *source, ErrorState *errors)
{
    bool ok = true;

    ok = ok && buffer_append(
                   source,
                   "static NIPPY_UNUSED nippy_store *nippy_slice_store("
                   "nippy_slice slice)\n"
                   "{\n"
                   "    if (slice.store >= (uint32_t)NIPPY_STORE_COUNT) {\n"
                   "        nippy_list_fail(\""
                   MESSAGE_RUNTIME_STORE_RANGE "\");\n"
                   "    }\n"
                   "    if (nippy_stores[slice.store].generation != "
                   "slice.generation) {\n"
                   "        nippy_list_fail(\""
                   MESSAGE_RUNTIME_STALE_HANDLE "\");\n"
                   "    }\n"
                   "    return &nippy_stores[slice.store];\n"
                   "}\n\n"
                   "static NIPPY_UNUSED nippy_slice nippy_slice_from_list("
                   "nippy_list list, int32_t start, int32_t length,\n"
                   "                                              "
                   "size_t element_size)\n"
                   "{\n"
                   "    nippy_list_state *state = nippy_list_get_state(list);\n"
                   "    nippy_store *store = nippy_list_store(list);\n"
                   "    nippy_slice result;\n"
                   "    size_t offset;\n\n"
                   "    if (start < 0 || length < 0 ||\n"
                   "        (size_t)start > state->length ||\n"
                   "        (size_t)length > state->length - (size_t)start ||\n"
                   "        element_size == 0 ||\n"
                   "        (size_t)start > (size_t)-1 / element_size) {\n"
                   "        nippy_list_fail(\"" MESSAGE_RUNTIME_LIST_BOUNDS
                   "\");\n"
                   "    }\n"
                   "    offset = state->offset + (size_t)start * element_size;\n"
                   "    result.store = list.store;\n"
                   "    result.offset = offset;\n"
                   "    result.length = (size_t)length;\n"
                   "    result.generation = store->generation;\n"
                   "    return result;\n"
                   "}\n\n"
                   "static NIPPY_UNUSED nippy_slice nippy_slice_sub("
                   "nippy_slice slice, int32_t start, int32_t length,\n"
                   "                                        size_t element_size)\n"
                   "{\n"
                   "    nippy_slice result;\n\n"
                   "    if (start < 0 || length < 0 ||\n"
                   "        (size_t)start > slice.length ||\n"
                   "        (size_t)length > slice.length - (size_t)start ||\n"
                   "        element_size == 0 ||\n"
                   "        (size_t)start > (size_t)-1 / element_size) {\n"
                   "        nippy_list_fail(\"" MESSAGE_RUNTIME_LIST_BOUNDS
                   "\");\n"
                   "    }\n"
                   "    (void)nippy_slice_store(slice);\n"
                   "    result = slice;\n"
                   "    result.offset += (size_t)start * element_size;\n"
                   "    result.length = (size_t)length;\n"
                   "    return result;\n"
                   "}\n\n"
                   "static NIPPY_UNUSED const void *nippy_slice_data("
                   "nippy_slice slice, int32_t index, size_t element_size)\n"
                   "{\n"
                   "    nippy_store *store = nippy_slice_store(slice);\n"
                   "    size_t offset;\n\n"
                   "    if (index < 0 || (size_t)index >= slice.length ||\n"
                   "        element_size == 0 ||\n"
                   "        (size_t)index > (size_t)-1 / element_size) {\n"
                   "        nippy_list_fail(\"" MESSAGE_RUNTIME_LIST_BOUNDS
                   "\");\n"
                   "    }\n"
                   "    offset = slice.offset + (size_t)index * element_size;\n"
                   "    if (offset > store->used ||\n"
                   "        element_size > store->used - offset) {\n"
                   "        nippy_list_fail(\""
                   MESSAGE_RUNTIME_HANDLE_BOUNDS "\");\n"
                   "    }\n"
                   "    return (const void *)(store->data + offset);\n"
                   "}\n\n"
                   "static NIPPY_UNUSED int32_t nippy_slice_len("
                   "nippy_slice slice)\n"
                   "{\n"
                   "    (void)nippy_slice_store(slice);\n"
                   "    return (int32_t)slice.length;\n"
                   "}\n\n");
    if (!ok) {
        error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
        return false;
    }
    return true;
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


static bool program_uses_binary_operator(const TypedProgram *program,
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

static bool program_uses_print(const TypedProgram *program)
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

static bool print_type_in_list(const List *types, size_t type_index)
{
    size_t i;

    for (i = 0; i < types->length; i++) {
        if (*(const size_t *)list_get(types, i) == type_index) {
            return true;
        }
    }
    return false;
}

static bool collect_print_type(const TypedProgram *program, size_t type_index,
                               List *types, unsigned depth, ErrorState *errors)
{
    const TypedType *type = typed_program_type(program, type_index);
    size_t i;

    if (type == NULL || depth == 0) {
        error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_TYPE);
        return false;
    }
    if (print_type_in_list(types, type_index)) {
        return true;
    }
    if (!list_push(types, &type_index)) {
        error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
        return false;
    }
    switch (type->kind) {
    case NIPPY_TYPE_UNION:
        for (i = 0; i < type->child_count; i++) {
            size_t alternative = *(const size_t *)list_get(
                &program->type_children, type->first_child + i);

            if (!collect_print_type(program, alternative, types,
                                    depth - 1, errors)) {
                return false;
            }
        }
        return true;
    case NIPPY_TYPE_STRUCT: {
        const TypedNode *declaration = typed_program_node(
            program, type->declaration_node);

        if (declaration == NULL) {
            error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_TYPE);
            return false;
        }
        for (i = 0; i < declaration->children_count; i++) {
            size_t child = *(const size_t *)list_get(
                &program->children, declaration->children_start + i);
            const TypedNode *field = typed_program_node(program, child);

            if (field == NULL ||
                !collect_print_type(program, field->type_index, types,
                                    depth - 1, errors)) {
                return false;
            }
        }
        return true;
    }
    case NIPPY_TYPE_ERROR:
        for (i = 0; i < type->child_count; i++) {
            size_t field = *(const size_t *)list_get(
                &program->type_children, type->first_child + i);

            if (!collect_print_type(program, field, types, depth - 1,
                                    errors)) {
                return false;
            }
        }
        return true;
    case NIPPY_TYPE_RECORD:
    case NIPPY_TYPE_TABLE:
        if (type->child_count != 2) {
            error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_TYPE);
            return false;
        }
        for (i = 0; i < 2; i++) {
            size_t part = *(const size_t *)list_get(
                &program->type_children, type->first_child + i);

            if (!collect_print_type(program, part, types, depth - 1,
                                    errors)) {
                return false;
            }
        }
        return true;
    case NIPPY_TYPE_LIST:
    case NIPPY_TYPE_FIXED_LIST:
    case NIPPY_TYPE_SLICE:
        if (type->child_count != 1) {
            error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_TYPE);
            return false;
        }
        return collect_print_type(
            program, *(const size_t *)list_get(&program->type_children,
                                               type->first_child),
            types, depth - 1, errors);
    case NIPPY_TYPE_STORE:
    case NIPPY_TYPE_UNKNOWN:
    case NIPPY_TYPE_STRING_LITERAL:
        error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_TYPE);
        return false;
    default:
        return true;
    }
}

static bool emit_print_helper(TextBuffer *source, const TypedProgram *program,
                              size_t type_index, ErrorState *errors)
{
    const TypedType *type = typed_program_type(program, type_index);
    char c_type[NIPPY_IDENTIFIER_MAX + 32];
    size_t i;

    if (type == NULL ||
        !format_c_type(program, type_index, c_type, sizeof(c_type), errors)) {
        return false;
    }
    if (!buffer_appendf(source,
                        "static void nippy_print_%zu(%s value, int depth)\n"
                        "{\n"
                        "    (void)value;\n"
                        "    if (depth > %d) {\n"
                        "        fputs(\"<...>\", stdout);\n"
                        "        return;\n"
                        "    }\n",
                        type_index, c_type, program->max_print_depth)) {
        error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
        return false;
    }
    switch (type->kind) {
    case NIPPY_TYPE_INT32:
        if (!buffer_append(source,
                           "    fprintf(stdout, \"%d\", (int)value);\n")) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            return false;
        }
        break;
    case NIPPY_TYPE_BYTE:
        if (!buffer_append(source,
                           "    fprintf(stdout, \"%u\", (unsigned)value);\n")) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            return false;
        }
        break;
    case NIPPY_TYPE_BOOL:
        if (!buffer_append(source,
                           "    fputs(value ? \"true\" : \"false\", stdout);\n")) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            return false;
        }
        break;
    case NIPPY_TYPE_NIL:
        if (!buffer_append(source, "    fputs(\"nil\", stdout);\n")) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            return false;
        }
        break;
    case NIPPY_TYPE_FUNCTION:
        if (!buffer_append(source, "    fputs(\"<function>\", stdout);\n")) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            return false;
        }
        break;
    case NIPPY_TYPE_DYNAMIC_STRING:
        if (!buffer_append(source,
                           "    if (value.length != 0) {\n"
                           "        fwrite(nippy_string_data(value), 1, "
                           "value.length, stdout);\n"
                           "    }\n")) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            return false;
        }
        break;
    case NIPPY_TYPE_STRING:
        if (!buffer_append(source,
                           "    if (value.length != 0) {\n"
                           "        fwrite(value.data, 1, value.length, "
                           "stdout);\n"
                           "    }\n")) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            return false;
        }
        break;
    case NIPPY_TYPE_TEXT_BUILDER:
        if (!buffer_append(source,
                           "    nippy_builder_state *state = "
                           "nippy_builder_get_state(value);\n"
                           "    nippy_store *store = nippy_builder_store(value);\n"
                           "    if (state->length != 0) {\n"
                           "        fwrite(store->data + state->offset, 1, "
                           "state->length, stdout);\n"
                           "    }\n")) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            return false;
        }
        break;
    case NIPPY_TYPE_HANDLE:
        if (!buffer_append(source,
                           "    fprintf(stdout, \"<handle store=%u offset=%zu "
                           "generation=%llu>\", value.store, value.offset, "
                           "(unsigned long long)value.generation);\n")) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            return false;
        }
        break;
    case NIPPY_TYPE_STRUCT: {
        const TypedNode *declaration = typed_program_node(
            program, type->declaration_node);
        const char *name = type->source_name_index == TYPED_NO_NODE
                               ? "struct"
                               : typed_program_symbol_text(
                                     program, type->source_name_index);

        if (declaration == NULL || name == NULL) {
            error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_TYPE);
            return false;
        }
        if (!buffer_appendf(source, "    fputs(\"%s { \", stdout);\n", name)) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            return false;
        }
        for (i = 0; i < declaration->children_count; i++) {
            size_t child = *(const size_t *)list_get(
                &program->children, declaration->children_start + i);
            const TypedNode *field = typed_program_node(program, child);
            const char *field_name =
                field == NULL
                    ? NULL
                    : typed_program_symbol_text(program, field->name_index);

            if (field == NULL || field_name == NULL) {
                error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_TYPE);
                return false;
            }
            if (!buffer_appendf(source, "    fputs(\"%s%s: \", stdout);\n",
                                i == 0 ? "" : ", ", field_name) ||
                !buffer_appendf(source,
                                "    nippy_print_%zu(value.%s, depth + 1);\n",
                                field->type_index, field_name)) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
                return false;
            }
        }
        if (!buffer_append(source, "    fputs(\" }\", stdout);\n")) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            return false;
        }
        break;
    }
    case NIPPY_TYPE_UNION:
        if (!buffer_append(source, "    switch (value.tag) {\n")) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            return false;
        }
        for (i = 0; i < type->child_count; i++) {
            size_t alternative = *(const size_t *)list_get(
                &program->type_children, type->first_child + i);
            const TypedType *alternative_type = typed_program_type(
                program, alternative);

            if (alternative_type == NULL) {
                error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_TYPE);
                return false;
            }
            if (!buffer_appendf(source, "    case %zu:\n", i)) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
                return false;
            }
            if (alternative_type->kind == NIPPY_TYPE_NIL) {
                if (!buffer_append(source,
                                   "        fputs(\"nil\", stdout);\n")) {
                    error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
                    return false;
                }
            } else if (!buffer_appendf(
                           source,
                           "        nippy_print_%zu(value.value.v%zu, "
                           "depth + 1);\n",
                           alternative, i)) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
                return false;
            }
            if (!buffer_append(source, "        break;\n")) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
                return false;
            }
        }
        if (!buffer_append(source,
                           "    default:\n"
                           "        fputs(\"<invalid union>\", stdout);\n"
                           "        break;\n"
                           "    }\n")) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            return false;
        }
        break;
    case NIPPY_TYPE_ERROR: {
        static const char *const names[] = {
            "header", "description", "filename", "line", "col", "pos"
        };

        if (!buffer_append(source, "    fputs(\"Error(\", stdout);\n")) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            return false;
        }
        for (i = 0; i < type->child_count && i < 6; i++) {
            size_t field = *(const size_t *)list_get(
                &program->type_children, type->first_child + i);

            if (!buffer_appendf(source, "    fputs(\"%s\", stdout);\n",
                                i == 0 ? "" : ", ") ||
                !buffer_appendf(source,
                                "    nippy_print_%zu(value.%s, depth + 1);\n",
                                field, names[i])) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
                return false;
            }
        }
        if (!buffer_appendf(source,
                            "    fprintf(stdout, \", %%d, %%d, %%d\", "
                            "(int)value.line, (int)value.col, (int)value.pos);\n"
                            "    fputs(\")\", stdout);\n")) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            return false;
        }
        break;
    }
    case NIPPY_TYPE_FIXED_LIST: {
        size_t element = *(const size_t *)list_get(&program->type_children,
                                                   type->first_child);

        if (!buffer_append(source,
                           "    fputs(\"[\", stdout);\n"
                           "    for (size_t i = 0; i < value.length; i++) {\n"
                           "        if (i != 0) fputs(\", \", stdout);\n") ||
            !buffer_appendf(source,
                            "        nippy_print_%zu(value.data[i], "
                            "depth + 1);\n",
                            element) ||
            !buffer_append(source,
                           "    }\n"
                           "    fputs(\"]\", stdout);\n")) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            return false;
        }
        break;
    }
    case NIPPY_TYPE_LIST:
    case NIPPY_TYPE_SLICE: {
        size_t element = *(const size_t *)list_get(&program->type_children,
                                                   type->first_child);
        char element_name[NIPPY_IDENTIFIER_MAX + 32];
        const char *get = type->kind == NIPPY_TYPE_LIST
                              ? "nippy_list_len(value)"
                              : "nippy_slice_len(value)";
        const char *data = type->kind == NIPPY_TYPE_LIST
                               ? "nippy_list_data"
                               : "nippy_slice_data";

        if (!format_c_type(program, element, element_name,
                           sizeof(element_name), errors)) {
            return false;
        }
        if (!buffer_appendf(source,
                            "    fputs(\"[\", stdout);\n"
                            "    for (size_t i = 0; i < (size_t)%s; i++) {\n"
                            "        if (i != 0) fputs(\", \", stdout);\n"
                            "        nippy_print_%zu(*(%s *)%s(value, i, "
                            "sizeof(%s)), depth + 1);\n"
                            "    }\n"
                            "    fputs(\"]\", stdout);\n",
                            get, element, element_name, data, element_name)) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            return false;
        }
        break;
    }
    case NIPPY_TYPE_RECORD: {
        size_t key_type = *(const size_t *)list_get(&program->type_children,
                                                    type->first_child);
        size_t value_type = *(const size_t *)list_get(
            &program->type_children, type->first_child + 1);
        size_t buckets = type->fixed_length == 0 ? 1 : type->fixed_length + 1;

        if (!buffer_append(source,
                           "    fputs(\"{\", stdout);\n"
                           "    size_t count = 0;\n"
                           "    for (size_t i = 0; i < ") ||
            !buffer_appendf(source, "%zu", buckets) ||
            !buffer_append(source,
                           "; i++) {\n"
                           "        if (value.states[i] != 1) continue;\n"
                           "        if (count++ != 0) fputs(\", \", stdout);\n") ||
            !buffer_appendf(source, "        nippy_print_%zu(value.keys[i], "
                                    "depth + 1);\n"
                                    "        fputs(\": \", stdout);\n"
                                    "        nippy_print_%zu(value.values[i], "
                                    "depth + 1);\n"
                                    "    }\n"
                                    "    fputs(\"}\", stdout);\n",
                            key_type, value_type)) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            return false;
        }
        break;
    }
    case NIPPY_TYPE_TABLE: {
        size_t key_type = *(const size_t *)list_get(&program->type_children,
                                                    type->first_child);
        size_t value_type = *(const size_t *)list_get(
            &program->type_children, type->first_child + 1);
        char key_name[64];
        char value_name[64];

        if (!format_c_type(program, key_type, key_name, sizeof(key_name),
                           errors) ||
            !format_c_type(program, value_type, value_name,
                           sizeof(value_name), errors)) {
            return false;
        }
        if (!buffer_appendf(source,
            "    nippy_table_state_%zu *state = nippy_table_%zu_state(value);\n"
            "    nippy_store *store = &nippy_stores[value.store];\n"
            "    %s *keys = (%s *)(void *)(store->data + state->keys_offset);\n"
            "    %s *values = (%s *)(void *)(store->data + state->values_offset);\n"
            "    uint8_t *states = (uint8_t *)(void *)(store->data + "
            "state->states_offset);\n"
            "    fputs(\"{\", stdout);\n"
            "    size_t count = 0;\n"
            "    for (size_t i = 0; i < state->buckets; i++) {\n"
            "        if (states[i] != 1) continue;\n"
            "        if (count++ != 0) fputs(\", \", stdout);\n"
            "        nippy_print_%zu(keys[i], depth + 1);\n"
            "        fputs(\": \", stdout);\n"
            "        nippy_print_%zu(values[i], depth + 1);\n"
            "    }\n"
            "    fputs(\"}\", stdout);\n",
            type_index, type_index, key_name, key_name, value_name,
            value_name, key_type, value_type)) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            return false;
        }
        break;
    }
    default:
        error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_TYPE);
        return false;
    }
    if (!buffer_append(source, "}\n\n")) {
        error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
        return false;
    }
    return true;
}

static bool emit_print_helpers(const TypedProgram *program, TextBuffer *source,
                               ErrorState *errors)
{
    List types;
    size_t i;
    bool ok = true;

    list_init(&types, sizeof(size_t));
    for (i = 0; i < program->nodes.length && ok; i++) {
        const TypedNode *node = typed_program_node(program, i);

        if (node == NULL || node->kind != TYPED_PRINT) {
            continue;
        }
        for (size_t j = 0; j < node->children_count; j++) {
            size_t child = *(const size_t *)list_get(
                &program->children, node->children_start + j);
            const TypedNode *argument = typed_program_node(program, child);

            if (argument == NULL ||
                !collect_print_type(program, argument->type_index, &types,
                                    32, errors)) {
                ok = false;
                break;
            }
        }
    }
    if (ok) {
        for (i = 0; i < types.length; i++) {
            size_t type_index = *(const size_t *)list_get(&types, i);
            char c_type[NIPPY_IDENTIFIER_MAX + 32];

            if (!format_c_type(program, type_index, c_type,
                               sizeof(c_type), errors) ||
                !buffer_appendf(source,
                                "static void nippy_print_%zu(%s value, int "
                                "depth);\n",
                                type_index, c_type)) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
                ok = false;
                break;
            }
        }
    }
    for (i = 0; ok && i < types.length; i++) {
        size_t type_index = *(const size_t *)list_get(&types, i);

        if (!emit_print_helper(source, program, type_index, errors)) {
            ok = false;
        }
    }
    if (ok && types.length > 0 &&
        !buffer_append(source, "\n")) {
        ok = false;
    }
    list_free(&types);
    return ok;
}

static bool emit_print_statement(const TypedProgram *program,
                                 const TypedNode *node, TextBuffer *source,
                                 ErrorState *errors, size_t depth)
{
    size_t i;

    if (!append_indent(source, depth) ||
        !buffer_append(source, "{\n")) {
        error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
        return false;
    }
    for (i = 0; i < node->children_count; i++) {
        size_t child = *(const size_t *)list_get(
            &program->children, node->children_start + i);
        const TypedNode *argument = typed_program_node(program, child);
        char c_type[NIPPY_IDENTIFIER_MAX + 32];

        if (argument == NULL ||
            !format_c_type(program, argument->type_index, c_type,
                           sizeof(c_type), errors)) {
            error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_EXPRESSION);
            return false;
        }
        if (!append_indent(source, depth + 1) ||
            !buffer_appendf(source, "%s nippy_print_arg_%zu_%zu = ",
                            c_type, node->pos.offset, i) ||
            !emit_expression(program, child, source, errors) ||
            !buffer_append(source, ";\n")) {
            if (!errors->reported) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            }
            return false;
        }
        if (i != 0) {
            if (!append_indent(source, depth + 1) ||
                !buffer_append(source, "fputc(' ', stdout);\n")) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
                return false;
            }
        }
        if (!append_indent(source, depth + 1) ||
            !buffer_appendf(source,
                            "nippy_print_%zu(nippy_print_arg_%zu_%zu, 0);\n",
                            argument->type_index, node->pos.offset, i)) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            return false;
        }
    }
    if (!append_indent(source, depth + 1) ||
        !buffer_append(source, "fputc('\\n', stdout);\n") ||
        !append_indent(source, depth) ||
        !buffer_append(source, "}\n")) {
        error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
        return false;
    }
    return true;
}


static bool map_key_names(const TypedProgram *program, size_t key_type,
                          size_t value_type, char *key_name, size_t key_size,
                          char *value_name, size_t value_size,
                          ErrorState *errors)
{
    return format_c_type(program, key_type, key_name, key_size, errors) &&
           format_c_type(program, value_type, value_name, value_size, errors);
}

static bool emit_record_runtime(const TypedProgram *program, size_t type_index,
                                const TypedType *type, TextBuffer *source,
                                ErrorState *errors)
{
    size_t key_type = *(const size_t *)list_get(&program->type_children,
                                                type->first_child);
    size_t value_type = *(const size_t *)list_get(
        &program->type_children, type->first_child + 1);
    char key_name[64];
    char value_name[64];
    char record_name[64];
    size_t capacity = type->fixed_length;
    size_t buckets = capacity == 0 ? 1 : capacity + 1;

    if (!map_key_names(program, key_type, value_type, key_name,
                       sizeof(key_name), value_name, sizeof(value_name),
                       errors) ||
        !format_c_type(program, type_index, record_name, sizeof(record_name),
                       errors)) {
        return false;
    }
    if (!buffer_appendf(source,
        "static uint64_t nippy_record_%zu_hash(%s key)\n"
        "{\n"
        "    uint64_t h = 14695981039346656037ULL;\n"
        "    for (size_t i = 0; i < key.length; i++) {\n"
        "        h ^= key.data[i];\n"
        "        h *= 1099511628211ULL;\n"
        "    }\n"
        "    return h;\n"
        "}\n\n"
        "static bool nippy_record_%zu_eq(%s a, %s b)\n"
        "{\n"
        "    return a.length == b.length &&\n"
        "           memcmp(a.data, b.data, a.length) == 0;\n"
        "}\n\n"
        "static size_t nippy_record_%zu_find(const %s *r, %s key)\n"
        "{\n"
        "    size_t start = (size_t)(nippy_record_%zu_hash(key) %% %zu);\n"
        "    for (size_t p = 0; p < %zu; p++) {\n"
        "        size_t i = (start + p) %% %zu;\n"
        "        if (r->states[i] == 0) return SIZE_MAX;\n"
        "        if (r->states[i] == 1 && nippy_record_%zu_eq(r->keys[i], key)) "
        "return i;\n"
        "    }\n"
        "    return SIZE_MAX;\n"
        "}\n\n"
        "static bool nippy_record_%zu_get(const %s *r, %s key, %s *out)\n"
        "{\n"
        "    size_t i = nippy_record_%zu_find(r, key);\n"
        "    if (i == SIZE_MAX) return false;\n"
        "    *out = r->values[i];\n"
        "    return true;\n"
        "}\n\n"
        "static bool nippy_record_%zu_has(const %s *r, %s key)\n"
        "{\n"
        "    return nippy_record_%zu_find(r, key) != SIZE_MAX;\n"
        "}\n\n"
        "static int32_t nippy_record_%zu_len(const %s *r)\n"
        "{\n"
        "    return (int32_t)r->length;\n"
        "}\n\n"
        "static void nippy_record_%zu_set(%s *r, %s key, %s value)\n"
        "{\n"
        "    size_t i = nippy_record_%zu_find(r, key);\n"
        "    if (i != SIZE_MAX) { r->values[i] = value; return; }\n"
        "    if (r->length >= %zu) nippy_map_fail(\"%s\");\n"
        "    size_t start = (size_t)(nippy_record_%zu_hash(key) %% %zu);\n"
        "    for (size_t p = 0; p < %zu; p++) {\n"
        "        size_t j = (start + p) %% %zu;\n"
        "        if (r->states[j] != 1) {\n"
        "            r->keys[j] = key;\n"
        "            r->values[j] = value;\n"
        "            r->states[j] = 1;\n"
        "            r->length++;\n"
        "            return;\n"
        "        }\n"
        "    }\n"
        "    nippy_map_fail(\"%s\");\n"
        "}\n\n"
        "static bool nippy_record_%zu_remove(%s *r, %s key)\n"
        "{\n"
        "    size_t i = nippy_record_%zu_find(r, key);\n"
        "    if (i == SIZE_MAX) return false;\n"
        "    r->states[i] = 2;\n"
        "    r->length--;\n"
        "    return true;\n"
        "}\n\n"
        "static %s nippy_record_%zu_get_required(const %s *r, %s key)\n"
        "{\n"
        "    %s out;\n"
        "    if (!nippy_record_%zu_get(r, key, &out)) "
        "nippy_map_fail(\"%s\");\n"
        "    return out;\n"
        "}\n\n"
        "static %s nippy_record_%zu_literal(%s *keys, %s *values, size_t n)\n"
        "{\n"
        "    %s r = {0};\n"
        "    for (size_t i = 0; i < n; i++) "
        "nippy_record_%zu_set(&r, keys[i], values[i]);\n"
        "    return r;\n"
        "}\n\n",
        type_index, key_name,
        type_index, key_name, key_name,
        type_index, record_name, key_name,
        type_index, buckets, buckets, buckets, type_index,
        type_index, record_name, key_name, value_name, type_index,
        type_index, record_name, key_name, type_index,
        type_index, record_name,
        type_index, record_name, key_name, value_name, type_index,
        capacity, MESSAGE_RUNTIME_RECORD_FULL,
        type_index, buckets, buckets, buckets,
        MESSAGE_RUNTIME_RECORD_FULL,
        type_index, record_name, key_name, type_index,
        value_name, type_index, record_name, key_name,
        value_name, type_index, MESSAGE_RUNTIME_MAP_MISSING,
        record_name, type_index, key_name, value_name,
        record_name, type_index)) {
        error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
        return false;
    }
    return true;
}

static bool emit_arithmetic_runtime(TextBuffer *source, ErrorState *errors)
{
    if (!buffer_append(
            source,
            "static void nippy_arith_fail(const char *message)\n"
            "{\n"
            "    fprintf(stderr, \"%s\\n\", message);\n"
            "    exit(1);\n"
            "}\n\n"
            "static NIPPY_UNUSED int32_t nippy_div_i32(int32_t a, int32_t b)\n"
            "{\n"
            "    if (b == 0) nippy_arith_fail(\"division by zero\");\n"
            "    if (a == INT32_MIN && b == -1) "
            "nippy_arith_fail(\"integer overflow\");\n"
            "    return a / b;\n"
            "}\n\n"
            "static NIPPY_UNUSED int32_t nippy_mod_i32(int32_t a, int32_t b)\n"
            "{\n"
            "    if (b == 0) nippy_arith_fail(\"division by zero\");\n"
            "    if (a == INT32_MIN && b == -1) return 0;\n"
            "    return a % b;\n"
            "}\n\n"
            "static NIPPY_UNUSED int32_t nippy_shl_i32(int32_t a, int32_t b)\n"
            "{\n"
            "    return (int32_t)((uint32_t)a << ((uint32_t)b & 31u));\n"
            "}\n\n"
            "static NIPPY_UNUSED int32_t nippy_shr_i32(int32_t a, int32_t b)\n"
            "{\n"
            "    return (int32_t)((uint32_t)a >> ((uint32_t)b & 31u));\n"
            "}\n\n")) {
        error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
        return false;
    }
    return true;
}

static bool emit_map_runtime(const TypedProgram *program, size_t store_count,
                             TextBuffer *source, ErrorState *errors)
{
    size_t i;
    bool map_helpers = false;

    for (i = 0; i < program->types.length; i++) {
        const TypedType *type = typed_program_type(program, i);

        if (type != NULL && (type->kind == NIPPY_TYPE_RECORD ||
                             type->kind == NIPPY_TYPE_TABLE)) {
            map_helpers = true;
        }
    }
    if (!map_helpers) {
        return true;
    }
    if (!buffer_append(source,
        "static void nippy_map_fail(const char *message)\n"
        "{\n"
        "    fprintf(stderr, \"%s\\n\", message);\n"
        "    exit(1);\n"
        "}\n\n")) {
        error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
        return false;
    }
    (void)store_count;
    for (i = 0; i < program->types.length; i++) {
        const TypedType *type = typed_program_type(program, i);

        if (type == NULL || type->kind != NIPPY_TYPE_RECORD) {
            continue;
        }
        if (!emit_record_runtime(program, i, type, source, errors)) {
            return false;
        }
    }
    for (i = 0; i < program->types.length; i++) {
        const TypedType *type = typed_program_type(program, i);
        size_t key_type;
        size_t value_type;
        char key_name[64];
        char value_name[64];

        if (type == NULL || type->kind != NIPPY_TYPE_TABLE) {
            continue;
        }
        key_type = *(const size_t *)list_get(&program->type_children,
                                             type->first_child);
        value_type = *(const size_t *)list_get(&program->type_children,
                                               type->first_child + 1);
        if (!map_key_names(program, key_type, value_type, key_name,
                           sizeof(key_name), value_name, sizeof(value_name),
                           errors)) {
            return false;
        }
        if (!buffer_appendf(source,
            "typedef struct {\n"
            "    size_t buckets;\n"
            "    size_t length;\n"
            "    size_t capacity;\n"
            "    size_t keys_offset;\n"
            "    size_t values_offset;\n"
            "    size_t states_offset;\n"
            "} nippy_table_state_%zu;\n\n"
            "static uint64_t nippy_table_%zu_hash(%s key)\n"
            "{\n"
            "    uint64_t h = 14695981039346656037ULL;\n"
            "    for (size_t i = 0; i < key.length; i++) {\n"
            "        h ^= key.data[i];\n"
            "        h *= 1099511628211ULL;\n"
            "    }\n"
            "    return h;\n"
            "}\n\n"
            "static bool nippy_table_%zu_eq(%s a, %s b)\n"
            "{\n"
            "    return a.length == b.length &&\n"
            "           memcmp(a.data, b.data, a.length) == 0;\n"
            "}\n\n"
            "static nippy_table_state_%zu *nippy_table_%zu_state(nippy_table t)\n"
            "{\n"
            "    nippy_store *store;\n"
            "    if (t.store >= (uint32_t)NIPPY_STORE_COUNT) "
            "nippy_map_fail(\"%s\");\n"
            "    store = &nippy_stores[t.store];\n"
            "    if (store->generation != t.generation) "
            "nippy_map_fail(\"%s\");\n"
            "    if (t.offset > store->used ||\n"
            "        sizeof(nippy_table_state_%zu) > store->used - t.offset) "
            "nippy_map_fail(\"%s\");\n"
            "    return (nippy_table_state_%zu *)(void *)(store->data + "
            "t.offset);\n"
            "}\n\n",
            i, i, key_name, i, key_name, key_name,
            i, i, MESSAGE_RUNTIME_STORE_RANGE, MESSAGE_RUNTIME_STALE_HANDLE,
            i, MESSAGE_RUNTIME_HANDLE_BOUNDS, i)) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            return false;
        }
        /* The real finder needs the descriptor's store; use a helper with the
           descriptor so it can locate both the state and the arrays. */
        if (!buffer_appendf(source,
            "static size_t nippy_table_%zu_find_in(nippy_table t, "
            "nippy_table_state_%zu *s, %s key)\n"
            "{\n"
            "    nippy_store *store = &nippy_stores[t.store];\n"
            "    %s *keys = (%s *)(void *)(store->data + s->keys_offset);\n"
            "    uint8_t *states = (uint8_t *)(void *)(store->data + "
            "s->states_offset);\n"
            "    size_t start = (size_t)(nippy_table_%zu_hash(key) %% "
            "s->buckets);\n"
            "    for (size_t p = 0; p < s->buckets; p++) {\n"
            "        size_t j = (start + p) %% s->buckets;\n"
            "        if (states[j] == 0) return SIZE_MAX;\n"
            "        if (states[j] == 1 && nippy_table_%zu_eq(keys[j], key)) "
            "return j;\n"
            "    }\n"
            "    return SIZE_MAX;\n"
            "}\n\n",
            i, i, key_name, key_name, key_name, i, i)) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            return false;
        }
        if (!buffer_appendf(source,
            "static bool nippy_table_%zu_get(nippy_table t, %s key, %s *out)\n"
            "{\n"
            "    nippy_table_state_%zu *s = nippy_table_%zu_state(t);\n"
            "    nippy_store *store = &nippy_stores[t.store];\n"
            "    %s *values = (%s *)(void *)(store->data + s->values_offset);\n"
            "    size_t p = nippy_table_%zu_find_in(t, s, key);\n"
            "    if (p == SIZE_MAX) return false;\n"
            "    *out = values[p];\n"
            "    return true;\n"
            "}\n\n"
            "static bool nippy_table_%zu_has(nippy_table t, %s key)\n"
            "{\n"
            "    nippy_table_state_%zu *s = nippy_table_%zu_state(t);\n"
            "    return nippy_table_%zu_find_in(t, s, key) != SIZE_MAX;\n"
            "}\n\n"
            "static int32_t nippy_table_%zu_len(nippy_table t)\n"
            "{\n"
            "    return (int32_t)nippy_table_%zu_state(t)->length;\n"
            "}\n\n",
            i, key_name, value_name, i, i, value_name, value_name, i,
            i, key_name, i, i, i, i, i)) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            return false;
        }
        if (!buffer_appendf(source,
            "static void nippy_table_%zu_grow(nippy_table *t)\n"
            "{\n"
            "    nippy_table_state_%zu *s = nippy_table_%zu_state(*t);\n"
            "    nippy_store *store = &nippy_stores[t->store];\n"
            "    size_t old_buckets = s->buckets;\n"
            "    size_t old_length = s->length;\n"
            "    size_t old_keys = s->keys_offset;\n"
            "    size_t old_values = s->values_offset;\n"
            "    size_t old_states = s->states_offset;\n"
            "    size_t new_capacity = s->capacity == 0 ? 1 : s->capacity * 2;\n"
            "    size_t new_buckets = new_capacity * 2 + 1;\n"
            "    size_t new_keys = nippy_store_alloc_bytes_aligned(store, "
            "t->store, sizeof(%s) * new_buckets, _Alignof(%s));\n"
            "    size_t new_values = nippy_store_alloc_bytes_aligned(store, "
            "t->store, sizeof(%s) * new_buckets, _Alignof(%s));\n"
            "    size_t new_states = nippy_store_alloc_bytes_aligned(store, "
            "t->store, new_buckets, 1);\n"
            "    %s *keys;\n"
            "    %s *values;\n"
            "    uint8_t *states;\n"
            "    memset(store->data + new_states, 0, new_buckets);\n"
            "    s = nippy_table_%zu_state(*t);\n"
            "    keys = (%s *)(void *)(store->data + new_keys);\n"
            "    values = (%s *)(void *)(store->data + new_values);\n"
            "    states = (uint8_t *)(void *)(store->data + new_states);\n"
            "    for (size_t i = 0; i < old_buckets; i++) {\n"
            "        uint8_t *old_state = (uint8_t *)(void *)(store->data + "
            "old_states);\n"
            "        if (old_state[i] != 1) continue;\n"
            "        %s key = *(%s *)(void *)(store->data + old_keys + "
            "i * sizeof(%s));\n"
            "        %s value = *(%s *)(void *)(store->data + old_values + "
            "i * sizeof(%s));\n"
            "        size_t start = (size_t)(nippy_table_%zu_hash(key) %% "
            "new_buckets);\n"
            "        for (size_t p = 0; p < new_buckets; p++) {\n"
            "            size_t j = (start + p) %% new_buckets;\n"
            "            if (states[j] == 0) {\n"
            "                keys[j] = key; values[j] = value; states[j] = 1; "
            "break;\n"
            "            }\n"
            "        }\n"
            "    }\n"
            "    s->buckets = new_buckets;\n"
            "    s->capacity = new_capacity;\n"
            "    s->length = old_length;\n"
            "    s->keys_offset = new_keys;\n"
            "    s->values_offset = new_values;\n"
            "    s->states_offset = new_states;\n"
            "}\n\n"
            "static void nippy_table_%zu_set(nippy_table *t, %s key, %s value)\n"
            "{\n"
            "    nippy_table_state_%zu *s = nippy_table_%zu_state(*t);\n"
            "    size_t p = nippy_table_%zu_find_in(*t, s, key);\n"
            "    if (p != SIZE_MAX) {\n"
            "        nippy_store *store = &nippy_stores[t->store];\n"
            "        %s *values = (%s *)(void *)(store->data + "
            "s->values_offset);\n"
            "        values[p] = value;\n"
            "        return;\n"
            "    }\n"
            "    if (s->length >= s->capacity) { nippy_table_%zu_grow(t); "
            "s = nippy_table_%zu_state(*t); }\n"
            "    nippy_store *store = &nippy_stores[t->store];\n"
            "    %s *keys = (%s *)(void *)(store->data + s->keys_offset);\n"
            "    %s *values = (%s *)(void *)(store->data + s->values_offset);\n"
            "    uint8_t *states = (uint8_t *)(void *)(store->data + "
            "s->states_offset);\n"
            "    size_t start = (size_t)(nippy_table_%zu_hash(key) %% "
            "s->buckets);\n"
            "    for (size_t q = 0; q < s->buckets; q++) {\n"
            "        size_t j = (start + q) %% s->buckets;\n"
            "        if (states[j] != 1) {\n"
            "            keys[j] = key; values[j] = value; states[j] = 1;\n"
            "            s->length++;\n"
            "            return;\n"
            "        }\n"
            "    }\n"
            "    nippy_map_fail(\"%s\");\n"
            "}\n\n",
            i, i, i, key_name, key_name, value_name, value_name, key_name,
            value_name, i, key_name, value_name, key_name, key_name,
            key_name, value_name, value_name, value_name, i,
            i, key_name, value_name, i, i, i, value_name, value_name,
            i, i, key_name, key_name, value_name, value_name, i,
            MESSAGE_RUNTIME_RECORD_FULL)) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            return false;
        }
        if (!buffer_appendf(source,
            "static bool nippy_table_%zu_remove(nippy_table *t, %s key)\n"
            "{\n"
            "    nippy_table_state_%zu *s = nippy_table_%zu_state(*t);\n"
            "    size_t p = nippy_table_%zu_find_in(*t, s, key);\n"
            "    nippy_store *store;\n"
            "    uint8_t *states;\n"
            "    if (p == SIZE_MAX) return false;\n"
            "    store = &nippy_stores[t->store];\n"
            "    states = (uint8_t *)(void *)(store->data + s->states_offset);\n"
            "    states[p] = 2;\n"
            "    s->length--;\n"
            "    return true;\n"
            "}\n\n"
            "static %s nippy_table_%zu_get_required(nippy_table t, %s key)\n"
            "{\n"
            "    %s out;\n"
            "    if (!nippy_table_%zu_get(t, key, &out)) "
            "nippy_map_fail(\"%s\");\n"
            "    return out;\n"
            "}\n\n"
            "static nippy_table nippy_table_%zu_create(nippy_store *store, "
            "uint32_t id, int32_t cap)\n"
            "{\n"
            "    nippy_table_state_%zu *s;\n"
            "    nippy_table result;\n"
            "    size_t capacity = cap < 0 ? 0 : (size_t)cap;\n"
            "    size_t buckets = capacity == 0 ? 1 : capacity * 2 + 1;\n"
            "    size_t state_offset = nippy_store_alloc_bytes_aligned(store, "
            "id, sizeof(nippy_table_state_%zu), _Alignof(nippy_table_state_%zu));\n"
            "    size_t keys_offset = nippy_store_alloc_bytes_aligned(store, "
            "id, sizeof(%s) * buckets, _Alignof(%s));\n"
            "    size_t values_offset = nippy_store_alloc_bytes_aligned(store, "
            "id, sizeof(%s) * buckets, _Alignof(%s));\n"
            "    size_t states_offset = nippy_store_alloc_bytes_aligned(store, "
            "id, buckets, 1);\n"
            "    memset(store->data + states_offset, 0, buckets);\n"
            "    s = (nippy_table_state_%zu *)(void *)(store->data + "
            "state_offset);\n"
            "    s->buckets = buckets;\n"
            "    s->length = 0;\n"
            "    s->capacity = capacity;\n"
            "    s->keys_offset = keys_offset;\n"
            "    s->values_offset = values_offset;\n"
            "    s->states_offset = states_offset;\n"
            "    result.store = id;\n"
            "    result.offset = state_offset;\n"
            "    result.generation = store->generation;\n"
            "    return result;\n"
            "}\n\n",
            i, key_name, i, i, i, value_name, i, key_name, value_name, i,
            MESSAGE_RUNTIME_MAP_MISSING, i, i, i, i, key_name, key_name,
            value_name, value_name, i)) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
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
    bool uses_dynamic_string;
    bool uses_string_builder;
    bool uses_list;
    bool uses_dynamic_list;
    bool uses_slice;
    bool uses_print;
    bool uses_map;
    bool uses_table;
    bool uses_arith_helpers;
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
    uses_string_builder = program_uses_string_builder(program);
    uses_dynamic_string =
        program_uses_dynamic_string(program) || uses_string_builder;
    uses_list = program_uses_list(program);
    uses_dynamic_list = program_uses_dynamic_list(program);
    uses_slice = program_uses_slice(program);
    uses_print = program_uses_print(program);
    uses_arith_helpers =
        program_uses_binary_operator(program, NIPPY_OPERATOR_DIV) ||
        program_uses_binary_operator(program, NIPPY_OPERATOR_MOD) ||
        program_uses_binary_operator(program, NIPPY_OPERATOR_SHL) ||
        program_uses_binary_operator(program, NIPPY_OPERATOR_SHR);
    uses_map = false;
    uses_table = false;
    {
        size_t type_i;

        for (type_i = 0; type_i < program->types.length; type_i++) {
            const TypedType *type = typed_program_type(program, type_i);

            if (type != NULL && type->kind == NIPPY_TYPE_RECORD) {
                uses_map = true;
            }
            if (type != NULL && type->kind == NIPPY_TYPE_TABLE) {
                uses_map = true;
                uses_table = true;
            }
        }
    }

    if (!buffer_append(&header,
                       "#ifndef NIPPY_GENERATED_H\n"
                       "#define NIPPY_GENERATED_H\n\n"
                       "#include <stdbool.h>\n"
                       "#include <stdint.h>\n")) {
        error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
        goto done;
    }
    if (uses_store_or_handle || uses_dynamic_string || uses_list ||
        uses_slice) {
        if (!buffer_append(&header, "#include <stddef.h>\n")) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            goto done;
        }
    }
    if (!buffer_append(&header, "\n")) {
        error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
        goto done;
    }
    if (uses_map && !buffer_append(&header, "#include <stddef.h>\n")) {
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
        } else if (node->kind == TYPED_ASSIGNMENT ||
                   node->kind == TYPED_RECORD_SET ||
                   node->kind == TYPED_TABLE_SET ||
                   node->kind == TYPED_FOR ||
                   node->kind == TYPED_IF) {
            /* An assignment changes the printed value instead of replacing the
               program-value entry point. */
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
    if (store_count > 0 || program_uses_string(program) ||
        uses_dynamic_string || uses_list || uses_slice || uses_print ||
        uses_map || uses_arith_helpers) {
        if (!buffer_append(&source,
                           "#include <stdio.h>\n"
                           "#include <stdlib.h>\n"
                           "#include <string.h>\n\n")) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            goto done;
        }
    } else if (print_program &&
               !buffer_append(&source, "#include <stdio.h>\n\n")) {
        error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
        goto done;
    }
    if (uses_dynamic_string || uses_list || uses_slice || uses_arith_helpers) {
        if (!buffer_append(&source,
                           "#if defined(__GNUC__) || defined(__clang__)\n"
                           "#define NIPPY_UNUSED __attribute__((unused))\n"
                           "#else\n"
                           "#define NIPPY_UNUSED\n"
                           "#endif\n\n")) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            goto done;
        }
    }
    if (uses_arith_helpers &&
        !emit_arithmetic_runtime(&source, errors)) {
        goto done;
    }
    if (store_count > 0 &&
        !emit_store_runtime(&source, store_count, uses_dynamic_string,
                            uses_dynamic_list || uses_table, errors)) {
        goto done;
    }
    if (program_uses_string(program) &&
        !emit_string_runtime(program, &source, errors)) {
        goto done;
    }
    if (uses_dynamic_string &&
        !emit_dynamic_string_runtime(program, store_count, &source, errors)) {
        goto done;
    }
    if (uses_dynamic_string &&
        !emit_dynamic_string_literals(program, &source, errors)) {
        goto done;
    }
    if (uses_string_builder && store_count > 0 &&
        !emit_builder_runtime(&source, errors)) {
        goto done;
    }
    if (uses_list && !emit_list_runtime(program, store_count, &source,
                                        errors)) {
        goto done;
    }
    if (uses_slice && uses_dynamic_list && store_count > 0 &&
        !emit_slice_runtime(&source, errors)) {
        goto done;
    }
    if (uses_map && !emit_map_runtime(program, store_count, &source, errors)) {
        goto done;
    }
    if (uses_print && !emit_print_helpers(program, &source, errors)) {
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
            if ((node->kind == TYPED_EXPRESSION_STATEMENT ||
                 node->kind == TYPED_ASSIGNMENT ||
                 node->kind == TYPED_RECORD_SET ||
                 node->kind == TYPED_TABLE_SET ||
                 node->kind == TYPED_FOR ||
                 node->kind == TYPED_IF) &&
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
