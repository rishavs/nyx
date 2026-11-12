#include "generator_internal.h"


bool valid_header_name(const char *name)
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

bool valid_line_name(const char *name)
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

bool valid_identifier(const char *name)
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


void buffer_init(TextBuffer *buffer)
{
    buffer->text = NULL;
    buffer->length = 0;
    buffer->capacity = 0;
}


void buffer_free(TextBuffer *buffer)
{
    free(buffer->text);
    buffer->text = NULL;
    buffer->length = 0;
    buffer->capacity = 0;
}


bool buffer_reserve(TextBuffer *buffer, size_t required)
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


bool buffer_append(TextBuffer *buffer, const char *text)
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

bool buffer_appendf(TextBuffer *buffer, const char *format, ...)
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

bool format_c_type(const TypedProgram *program, size_t type_index,
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
        snprintf(out, out_size, "nyx_nil");
        return true;
    }
    if (type->kind == NIPPY_TYPE_BYTE) {
        snprintf(out, out_size, "uint8_t");
        return true;
    }
    if (type->kind == NIPPY_TYPE_HANDLE) {
        snprintf(out, out_size, "nyx_handle");
        return true;
    }
    if (type->kind == NIPPY_TYPE_STRING) {
        snprintf(out, out_size, "nyx_string_%zu", type->fixed_length);
        return true;
    }
    if (type->kind == NIPPY_TYPE_DYNAMIC_STRING) {
        snprintf(out, out_size, "nyx_string");
        return true;
    }
    if (type->kind == NIPPY_TYPE_TEXT_BUILDER) {
        snprintf(out, out_size, "nyx_builder");
        return true;
    }
    if (type->kind == NIPPY_TYPE_LIST) {
        snprintf(out, out_size, "nyx_list");
        return true;
    }
    if (type->kind == NIPPY_TYPE_FIXED_LIST) {
        snprintf(out, out_size, "nyx_list_%zu", type_index);
        return true;
    }
    if (type->kind == NIPPY_TYPE_SLICE) {
        snprintf(out, out_size, "nyx_slice");
        return true;
    }
    if (type->kind == NIPPY_TYPE_RECORD) {
        snprintf(out, out_size, "nyx_record_%zu", type_index);
        return true;
    }
    if (type->kind == NIPPY_TYPE_TABLE) {
        snprintf(out, out_size, "nyx_table");
        return true;
    }
    if (type->kind == NIPPY_TYPE_ERROR) {
        snprintf(out, out_size, "nyx_error");
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
        snprintf(out, out_size, "nyx_fun_%zu", type->signature_index);
        return true;
    }
    if (type->kind == NIPPY_TYPE_UNION) {
        snprintf(out, out_size, "nyx_sum_%zu", type_index);
        return true;
    }
    error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_TYPE);
    return false;
}

bool format_function_pointer(const TypedProgram *program,
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


bool fixed_list_element_name(const TypedProgram *program,
                                    size_t list_type_index, size_t element,
                                    char *out, size_t out_size,
                                    ErrorState *errors)
{
    const TypedType *element_type = typed_program_type(program, element);

    if (element_type != NULL && element_type->kind == NIPPY_TYPE_FUNCTION) {
        if (snprintf(out, out_size, "nyx_list_%zu_elem",
                     list_type_index) < 0 ||
            strlen(out) >= out_size - 1) {
            error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_TYPE);
            return false;
        }
        return true;
    }
    return format_c_type(program, element, out, out_size, errors);
}

bool handle_element_type(const TypedProgram *program,
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

bool types_equivalent(const TypedProgram *program, size_t first,
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
bool find_union_alternative(const TypedProgram *program,
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


const TypedType *union_alternative_type(const TypedProgram *program,
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

size_t find_store_declaration(const TypedProgram *program,
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


bool store_id_is_valid(const TypedProgram *program, size_t store_id)
{
    return find_store_declaration(program, store_id) != TYPED_NO_NODE;
}

bool type_is_c_aggregate(const TypedProgram *program, size_t type_index)
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


bool append_indent(TextBuffer *source, size_t depth)
{
    size_t i;

    for (i = 0; i < depth; i++) {
        if (!buffer_append(source, "    ")) {
            return false;
        }
    }
    return true;
}



bool map_key_names(const TypedProgram *program, size_t key_type,
                          size_t value_type, char *key_name, size_t key_size,
                          char *value_name, size_t value_size,
                          ErrorState *errors)
{
    return format_c_type(program, key_type, key_name, key_size, errors) &&
           format_c_type(program, value_type, value_name, value_size, errors);
}
