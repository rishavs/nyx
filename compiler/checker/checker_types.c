#include "checker_internal.h"

bool builtin_type(CheckContext *ctx, NippyType kind, size_t *out_index)
{
    size_t i;

    for (i = 0; i < ctx->program->types.length; i++) {
        const TypedType *type = typed_program_type(ctx->program, i);

        if (type != NULL && type->kind == kind) {
            *out_index = i;
            return true;
        }
    }
    return add_type_record(ctx, kind, TYPED_NO_NODE, TYPED_NO_NODE, out_index);
}

bool string_type(CheckContext *ctx, int capacity, size_t *out_index)
{
    size_t i;

    if (capacity < 0 || capacity > ctx->program->max_string_capacity) {
        error_report(ctx->errors, MESSAGE_CHECKER_STRING_CAPACITY,
                     ctx->program->max_string_capacity);
        return false;
    }
    for (i = 0; i < ctx->program->types.length; i++) {
        const TypedType *type = typed_program_type(ctx->program, i);

        if (type != NULL && type->kind == NIPPY_TYPE_STRING &&
            type->fixed_length == (size_t)capacity) {
            *out_index = i;
            return true;
        }
    }
    {
        TypedType type = {0};

        type.kind = NIPPY_TYPE_STRING;
        type.name_index = TYPED_NO_NODE;
        type.signature_index = TYPED_NO_NODE;
        type.fixed_length = (size_t)capacity;
        type.declaration_node = TYPED_NO_NODE;
        type.source_name_index = TYPED_NO_NODE;
        if (!typed_program_add_type(ctx->program, &type, out_index)) {
            error_report(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
            return false;
        }
        return true;
    }
}

bool string_literal_type(CheckContext *ctx, size_t *out_index)
{
    size_t i;

    for (i = 0; i < ctx->program->types.length; i++) {
        const TypedType *type = typed_program_type(ctx->program, i);

        if (type != NULL && type->kind == NIPPY_TYPE_STRING_LITERAL) {
            *out_index = i;
            return true;
        }
    }
    {
        TypedType type = {0};

        type.kind = NIPPY_TYPE_STRING_LITERAL;
        type.name_index = TYPED_NO_NODE;
        type.signature_index = TYPED_NO_NODE;
        type.declaration_node = TYPED_NO_NODE;
        type.source_name_index = TYPED_NO_NODE;
        if (!typed_program_add_type(ctx->program, &type, out_index)) {
            error_report(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
            return false;
        }
        return true;
    }
}

bool error_type(CheckContext *ctx, size_t *out_index)
{
    size_t header;
    size_t description;
    size_t filename;
    size_t children_start;
    size_t i;

    for (i = 0; i < ctx->program->types.length; i++) {
        const TypedType *type = typed_program_type(ctx->program, i);

        if (type != NULL && type->kind == NIPPY_TYPE_ERROR) {
            *out_index = i;
            return true;
        }
    }
    if (!string_type(ctx, ctx->program->error_header_capacity, &header) ||
        !string_type(ctx, ctx->program->error_description_capacity,
                     &description) ||
        !string_type(ctx, ctx->program->error_filename_capacity, &filename)) {
        return false;
    }
    children_start = ctx->program->type_children.length;
    if (!typed_program_add_type_child(ctx->program, header) ||
        !typed_program_add_type_child(ctx->program, description) ||
        !typed_program_add_type_child(ctx->program, filename)) {
        error_report(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
        return false;
    }
    {
        TypedType type = {0};

        type.kind = NIPPY_TYPE_ERROR;
        type.name_index = TYPED_NO_NODE;
        type.signature_index = TYPED_NO_NODE;
        type.first_child = children_start;
        type.child_count = 3;
        type.declaration_node = TYPED_NO_NODE;
        type.source_name_index = TYPED_NO_NODE;
        if (!typed_program_add_type(ctx->program, &type, out_index)) {
            error_report(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
            return false;
        }
        return true;
    }
}

bool error_field_type(CheckContext *ctx, size_t error_index,
                             size_t field, size_t *out_type)
{
    const TypedType *error = typed_program_type(ctx->program, error_index);

    if (error == NULL || error->kind != NIPPY_TYPE_ERROR ||
        error->child_count != 3) {
        return false;
    }
    if (field < 3) {
        const size_t *type = list_get(&ctx->program->type_children,
                                      error->first_child + field);

        if (type == NULL) {
            return false;
        }
        *out_type = *type;
        return true;
    }
    return builtin_type(ctx, NIPPY_TYPE_INT32, out_type);
}

bool is_sized_type(const TypedProgram *program, size_t type_index)
{
    const TypedType *type = typed_program_type(program, type_index);

    if (type == NULL) {
        return false;
    }
    return nippy_type_is_numeric(type->kind) ||
           type->kind == NIPPY_TYPE_BOOL || type->kind == NIPPY_TYPE_NIL ||
           type->kind == NIPPY_TYPE_STRUCT ||
           type->kind == NIPPY_TYPE_HANDLE ||
           type->kind == NIPPY_TYPE_STRING ||
           type->kind == NIPPY_TYPE_DYNAMIC_STRING ||
           type->kind == NIPPY_TYPE_TEXT_BUILDER ||
           type->kind == NIPPY_TYPE_LIST ||
           type->kind == NIPPY_TYPE_FIXED_LIST ||
           type->kind == NIPPY_TYPE_SLICE ||
           type->kind == NIPPY_TYPE_RANGE ||
           type->kind == NIPPY_TYPE_RECORD ||
           type->kind == NIPPY_TYPE_TABLE ||
           type->kind == NIPPY_TYPE_UNION ||
           type->kind == NIPPY_TYPE_FUNCTION ||
           type->kind == NIPPY_TYPE_ERROR;
}

bool is_fixed_type_depth(const TypedProgram *program, size_t type_index,
                                unsigned depth)
{
    const TypedType *type = typed_program_type(program, type_index);
    size_t i;

    if (type == NULL || depth == 0) {
        return false;
    }
    if (nippy_type_is_numeric(type->kind)) {
        return true;
    }
    switch (type->kind) {
    case NIPPY_TYPE_BOOL:
    case NIPPY_TYPE_NIL:
    case NIPPY_TYPE_STRING:
    case NIPPY_TYPE_FUNCTION:
    case NIPPY_TYPE_ERROR:
    case NIPPY_TYPE_RANGE:
        return true;
    case NIPPY_TYPE_FIXED_LIST:
        if (type->child_count != 1) {
            return false;
        }
        return is_fixed_type_depth(
            program, *(const size_t *)list_get(&program->type_children,
                                               type->first_child),
            depth - 1);
    case NIPPY_TYPE_UNION:
        for (i = 0; i < type->child_count; i++) {
            size_t alternative = *(const size_t *)list_get(
                &program->type_children, type->first_child + i);

            if (!is_fixed_type_depth(program, alternative, depth - 1)) {
                return false;
            }
        }
        return true;
    case NIPPY_TYPE_STRUCT: {
        const TypedNode *declaration = typed_program_node(
            program, type->declaration_node);

        if (declaration == NULL) {
            return false;
        }
        for (i = 0; i < declaration->children_count; i++) {
            size_t child = *(const size_t *)list_get(
                &program->children, declaration->children_start + i);
            const TypedNode *field = typed_program_node(program, child);

            if (field == NULL ||
                !is_fixed_type_depth(program, field->type_index,
                                     depth - 1)) {
                return false;
            }
        }
        return true;
    }
    default:
        return false;
    }
}

bool is_fixed_type(const TypedProgram *program, size_t type_index)
{
    return is_fixed_type_depth(program, type_index, NIPPY_TYPE_DEPTH);
}

bool handle_type(CheckContext *ctx, size_t element, size_t *out_index)
{
    size_t i;

    if (!is_fixed_type(ctx->program, element)) {
        error_report(ctx->errors, MESSAGE_CHECKER_HANDLE_FIXED);
        return false;
    }
    for (i = 0; i < ctx->program->types.length; i++) {
        const TypedType *type = typed_program_type(ctx->program, i);

        if (type == NULL || type->kind != NIPPY_TYPE_HANDLE ||
            type->child_count != 1) {
            continue;
        }
        {
            size_t existing = *(const size_t *)list_get(
                &ctx->program->type_children, type->first_child);

            if (same_type(ctx->program, existing, element)) {
                *out_index = i;
                return true;
            }
        }
    }
    {
        TypedType type = {0};
        size_t first = ctx->program->type_children.length;

        if (!typed_program_add_type_child(ctx->program, element)) {
            error_report(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
            return false;
        }
        type.kind = NIPPY_TYPE_HANDLE;
        type.name_index = TYPED_NO_NODE;
        type.signature_index = TYPED_NO_NODE;
        type.first_child = first;
        type.child_count = 1;
        type.declaration_node = TYPED_NO_NODE;
        type.source_name_index = TYPED_NO_NODE;
        if (!typed_program_add_type(ctx->program, &type, out_index)) {
            error_report(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
            return false;
        }
        return true;
    }
}

bool add_type_record(CheckContext *ctx, NippyType kind,
                            size_t name_index, size_t signature_index,
                            size_t *out_index)
{
    TypedType type = {0};

    type.kind = kind;
    type.name_index = name_index;
    type.signature_index = signature_index;
    type.first_child = 0;
    type.child_count = 0;
    type.declaration_node = TYPED_NO_NODE;
    type.source_name_index = TYPED_NO_NODE;
    if (!typed_program_add_type(ctx->program, &type, out_index)) {
        error_report(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
        return false;
    }
    return true;
}

bool text_builder_type(CheckContext *ctx, size_t *out_index)
{
    size_t dynamic_string;

    if (!builtin_type(ctx, NIPPY_TYPE_DYNAMIC_STRING, &dynamic_string)) {
        return false;
    }
    return builtin_type(ctx, NIPPY_TYPE_TEXT_BUILDER, out_index);
}

bool is_list_element_type(CheckContext *ctx, size_t element_type,
                                 bool fixed)
{
    const TypedType *type = typed_program_type(ctx->program, element_type);

    (void)fixed;
    if (type == NULL || type->kind == NIPPY_TYPE_STORE ||
        type->kind == NIPPY_TYPE_LIST ||
        type->kind == NIPPY_TYPE_FIXED_LIST ||
        type->kind == NIPPY_TYPE_SLICE ||
        type->kind == NIPPY_TYPE_RECORD ||
        type->kind == NIPPY_TYPE_TABLE ||
        type->kind == NIPPY_TYPE_TEXT_BUILDER) {
        return false;
    }
    if (type->kind == NIPPY_TYPE_FUNCTION) {
        return true;
    }
    if (type->kind == NIPPY_TYPE_UNION) {
        return true;
    }
    return is_sized_type(ctx->program, element_type);
}

bool map_type(CheckContext *ctx, size_t key_type, size_t value_type,
                     bool fixed, size_t capacity, size_t *out_index)
{
    const TypedType *key = typed_program_type(ctx->program, key_type);
    size_t i;

    if (key == NULL || key->kind != NIPPY_TYPE_STRING) {
        error_report(ctx->errors, MESSAGE_CHECKER_MAP_KEY);
        return false;
    }
    if (!is_list_element_type(ctx, value_type, fixed)) {
        error_report(ctx->errors, MESSAGE_CHECKER_MAP_VALUE);
        return false;
    }
    if (fixed && capacity > (size_t)ctx->program->max_list_capacity) {
        error_report(ctx->errors, MESSAGE_CHECKER_RECORD_CAPACITY);
        return false;
    }
    for (i = 0; i < ctx->program->types.length; i++) {
        const TypedType *type = typed_program_type(ctx->program, i);

        if (type == NULL ||
            type->kind != (fixed ? NIPPY_TYPE_RECORD : NIPPY_TYPE_TABLE) ||
            type->child_count != 2) {
            continue;
        }
        {
            size_t existing_key = *(const size_t *)list_get(
                &ctx->program->type_children, type->first_child);
            size_t existing_value = *(const size_t *)list_get(
                &ctx->program->type_children, type->first_child + 1);

            if (same_type(ctx->program, existing_key, key_type) &&
                same_type(ctx->program, existing_value, value_type) &&
                (!fixed || type->fixed_length == capacity)) {
                *out_index = i;
                return true;
            }
        }
    }
    {
        TypedType type = {0};

        type.kind = fixed ? NIPPY_TYPE_RECORD : NIPPY_TYPE_TABLE;
        type.name_index = TYPED_NO_NODE;
        type.signature_index = TYPED_NO_NODE;
        type.first_child = ctx->program->type_children.length;
        type.child_count = 2;
        type.fixed_length = fixed ? capacity : 0;
        type.declaration_node = TYPED_NO_NODE;
        type.source_name_index = TYPED_NO_NODE;
        if (!typed_program_add_type_child(ctx->program, key_type) ||
            !typed_program_add_type_child(ctx->program, value_type) ||
            !typed_program_add_type(ctx->program, &type, out_index)) {
            error_report(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
            return false;
        }
        return true;
    }
}

bool list_type(CheckContext *ctx, size_t element_type, bool fixed,
                      size_t capacity, size_t *out_index)
{
    size_t i;

    if (!is_list_element_type(ctx, element_type, fixed)) {
        error_report(ctx->errors,
                     fixed ? MESSAGE_CHECKER_ARRAY_ELEMENT
                           : MESSAGE_CHECKER_COLLECTION_ELEMENT);
        return false;
    }
    for (i = 0; i < ctx->program->types.length; i++) {
        const TypedType *type = typed_program_type(ctx->program, i);

        if (type == NULL) {
            continue;
        }
        if (type->kind != (fixed ? NIPPY_TYPE_FIXED_LIST
                                 : NIPPY_TYPE_LIST)) {
            continue;
        }
        if (type->child_count == 1) {
            size_t existing = *(const size_t *)list_get(
                &ctx->program->type_children, type->first_child);

            if (same_type(ctx->program, existing, element_type) &&
                (!fixed || type->fixed_length == capacity)) {
                *out_index = i;
                return true;
            }
        }
    }
    {
        TypedType type = {0};

        type.kind = fixed ? NIPPY_TYPE_FIXED_LIST : NIPPY_TYPE_LIST;
        type.name_index = TYPED_NO_NODE;
        type.signature_index = TYPED_NO_NODE;
        type.first_child = ctx->program->type_children.length;
        type.child_count = 1;
        type.fixed_length = fixed ? capacity : 0;
        type.declaration_node = TYPED_NO_NODE;
        type.source_name_index = TYPED_NO_NODE;
        if (!typed_program_add_type(ctx->program, &type, out_index)) {
            error_report(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
            return false;
        }
        if (!typed_program_add_type_child(ctx->program, element_type)) {
            error_report(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_PROGRAM);
            return false;
        }
        return true;
    }
}

bool slice_type(CheckContext *ctx, size_t element_type,
                       size_t *out_index)
{
    size_t i;

    if (!is_list_element_type(ctx, element_type, false)) {
        error_report(ctx->errors, MESSAGE_CHECKER_SLICE_ELEMENT);
        return false;
    }
    for (i = 0; i < ctx->program->types.length; i++) {
        const TypedType *type = typed_program_type(ctx->program, i);

        if (type == NULL || type->kind != NIPPY_TYPE_SLICE ||
            type->child_count != 1) {
            continue;
        }
        {
            size_t existing = *(const size_t *)list_get(
                &ctx->program->type_children, type->first_child);

            if (same_type(ctx->program, existing, element_type)) {
                *out_index = i;
                return true;
            }
        }
    }
    {
        TypedType type = {0};

        type.kind = NIPPY_TYPE_SLICE;
        type.name_index = TYPED_NO_NODE;
        type.signature_index = TYPED_NO_NODE;
        type.first_child = ctx->program->type_children.length;
        type.child_count = 1;
        type.declaration_node = TYPED_NO_NODE;
        type.source_name_index = TYPED_NO_NODE;
        if (!typed_program_add_type(ctx->program, &type, out_index)) {
            error_report(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
            return false;
        }
        if (!typed_program_add_type_child(ctx->program, element_type)) {
            error_report(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_PROGRAM);
            return false;
        }
        return true;
    }
}

bool range_type(CheckContext *ctx, size_t element_type, size_t *out_index)
{
    const TypedType *element = typed_program_type(ctx->program, element_type);
    TypedType type = {0};
    size_t i;
    if (element == NULL || !nippy_type_is_integer(element->kind)) return false;
    for (i = 0; i < ctx->program->types.length; i++) {
        const TypedType *existing = typed_program_type(ctx->program, i);
        if (existing != NULL && existing->kind == NIPPY_TYPE_RANGE &&
            existing->child_count == 1 && same_type(ctx->program, element_type,
                *(const size_t *)list_get(&ctx->program->type_children,
                                           existing->first_child))) {
            *out_index = i;
            return true;
        }
    }
    type.kind = NIPPY_TYPE_RANGE;
    type.name_index = TYPED_NO_NODE;
    type.signature_index = TYPED_NO_NODE;
    type.first_child = ctx->program->type_children.length;
    type.child_count = 1;
    type.declaration_node = TYPED_NO_NODE;
    type.source_name_index = TYPED_NO_NODE;
    if (!typed_program_add_type(ctx->program, &type, out_index) ||
        !typed_program_add_type_child(ctx->program, element_type)) {
        error_report(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
        return false;
    }
    return true;
}

bool add_function_type(CheckContext *ctx, const List *parameter_types,
                              size_t return_type, size_t *out_index)
{
    TypedFunctionSignature signature = {0};
    size_t signature_index;
    size_t i;

    signature.first_parameter = ctx->program->signature_parameters.length;
    signature.parameter_count = parameter_types->length;
    signature.return_type = return_type;

    for (i = 0; i < parameter_types->length; i++) {
        size_t parameter_type = *(const size_t *)list_get(parameter_types, i);

        if (!typed_program_add_signature_parameter(ctx->program,
                                                   parameter_type)) {
            error_report(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
            return false;
        }
    }
    if (!typed_program_add_signature(ctx->program, &signature,
                                     &signature_index)) {
        error_report(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
        return false;
    }
    return add_type_record(ctx, NIPPY_TYPE_FUNCTION, TYPED_NO_NODE,
                           signature_index, out_index);
}

size_t type_parameter_index(
    char parameters[][NIPPY_IDENTIFIER_MAX], size_t count, const char *name)
{
    size_t i;

    for (i = 0; i < count; i++) {
        if (strcmp(parameters[i], name) == 0) {
            return i;
        }
    }
    return TYPED_NO_NODE;
}

bool env_lookup(const TypeEnv *env, const char *name, size_t *out)
{
    size_t i;

    for (i = 0; i < env->count; i++) {
        if (env->names[i] != NULL && strcmp(env->names[i], name) == 0) {
            *out = env->types[i];
            return true;
        }
    }
    return false;
}

bool add_builtin_type(CheckContext *ctx, NippyType kind,
                             size_t *out_index)
{
    return add_type_record(ctx, kind, TYPED_NO_NODE, TYPED_NO_NODE, out_index);
}


static bool type_contains_error_depth(const TypedProgram *program,
                                      size_t type_index, unsigned depth)
{
    const TypedType *type = typed_program_type(program, type_index);
    size_t i;

    if (type == NULL || depth == 0) {
        return false;
    }
    if (type->kind == NIPPY_TYPE_ERROR) {
        return true;
    }
    if (type->kind == NIPPY_TYPE_STRUCT) {
        const TypedNode *declaration = typed_program_node(
            program, type->declaration_node);

        if (declaration != NULL) {
            for (i = 0; i < declaration->children_count; i++) {
                size_t child = *(const size_t *)list_get(
                    &program->children, declaration->children_start + i);
                const TypedNode *field = typed_program_node(program, child);

                if (field != NULL && field->kind == TYPED_FIELD &&
                    type_contains_error_depth(program, field->type_index,
                                              depth - 1)) {
                    return true;
                }
            }
        }
        return false;
    }
    if (type->kind == NIPPY_TYPE_UNION ||
        type->kind == NIPPY_TYPE_HANDLE ||
        type->kind == NIPPY_TYPE_LIST ||
        type->kind == NIPPY_TYPE_FIXED_LIST ||
        type->kind == NIPPY_TYPE_SLICE ||
        type->kind == NIPPY_TYPE_RECORD ||
        type->kind == NIPPY_TYPE_TABLE) {
        for (i = 0; i < type->child_count; i++) {
            size_t child = *(const size_t *)list_get(
                &program->type_children, type->first_child + i);

            if (type_contains_error_depth(program, child, depth - 1)) {
                return true;
            }
        }
    }
    return false;
}


bool type_contains_error(const TypedProgram *program, size_t type_index)
{
    return type_contains_error_depth(program, type_index, NIPPY_TYPE_DEPTH);
}

static bool add_value_size(size_t *total, size_t amount)
{
    if (*total > (size_t)-1 - amount) {
        *total = NIPPY_MAX_VALUE_BYTES + 1;
        return false;
    }
    *total += amount;
    return true;
}


static bool type_value_size_depth(const TypedProgram *program,
                                  size_t type_index, unsigned depth,
                                  size_t *out)
{
    const TypedType *type = typed_program_type(program, type_index);
    size_t i;

    if (type == NULL || depth == 0) {
        return false;
    }
    switch (type->kind) {
    case NIPPY_TYPE_INT8:
    case NIPPY_TYPE_BYTE:
        *out = 1;
        return true;
    case NIPPY_TYPE_INT16:
    case NIPPY_TYPE_UINT16:
        *out = 2;
        return true;
    case NIPPY_TYPE_INT32:
    case NIPPY_TYPE_UINT32:
    case NIPPY_TYPE_FLOAT32:
        *out = 4;
        return true;
    case NIPPY_TYPE_INT64:
    case NIPPY_TYPE_UINT64:
    case NIPPY_TYPE_FLOAT64:
        *out = 8;
        return true;
    case NIPPY_TYPE_BOOL:
    case NIPPY_TYPE_NIL:
        *out = 1;
        return true;
    case NIPPY_TYPE_STRING:
        *out = type->fixed_length + 5;
        return true;
    case NIPPY_TYPE_ERROR:
        *out = (size_t)program->error_header_capacity +
               (size_t)program->error_description_capacity +
               (size_t)program->error_filename_capacity + 3 + 12;
        return true;
    case NIPPY_TYPE_FIXED_LIST: {
        size_t element;

        if (type->child_count != 1 ||
            !type_value_size_depth(program,
                                   *(const size_t *)list_get(
                                       &program->type_children,
                                       type->first_child),
                                   depth - 1, &element)) {
            return false;
        }
        if (type->fixed_length != 0 &&
            element > ((size_t)-1) / type->fixed_length) {
            *out = NIPPY_MAX_VALUE_BYTES + 1;
            return true;
        }
        *out = (type->fixed_length == 0 ? 1 : type->fixed_length) * element;
        return true;
    }
    case NIPPY_TYPE_RECORD: {
        size_t key;
        size_t value;
        size_t capacity = type->fixed_length + 1;

        if (type->child_count != 2 ||
            !type_value_size_depth(program,
                                   *(const size_t *)list_get(
                                       &program->type_children,
                                       type->first_child),
                                   depth - 1, &key) ||
            !type_value_size_depth(program,
                                   *(const size_t *)list_get(
                                       &program->type_children,
                                       type->first_child + 1),
                                   depth - 1, &value)) {
            return false;
        }
        *out = 0;
        if (!add_value_size(out, key + value + 1) ||
            (capacity != 0 && *out > ((size_t)-1) / capacity)) {
            *out = NIPPY_MAX_VALUE_BYTES + 1;
            return true;
        }
        *out *= capacity;
        return true;
    }
    case NIPPY_TYPE_UNION: {
        size_t largest = 0;

        for (i = 0; i < type->child_count; i++) {
            size_t alternative;

            if (!type_value_size_depth(
                    program,
                    *(const size_t *)list_get(
                        &program->type_children, type->first_child + i),
                    depth - 1, &alternative)) {
                return false;
            }
            if (alternative > largest) {
                largest = alternative;
            }
        }
        *out = largest;
        add_value_size(out, 8);
        return true;
    }
    case NIPPY_TYPE_STRUCT: {
        const TypedNode *declaration = typed_program_node(
            program, type->declaration_node);
        size_t total = 0;

        if (declaration != NULL) {
            for (i = 0; i < declaration->children_count; i++) {
                size_t child = *(const size_t *)list_get(
                    &program->children, declaration->children_start + i);
                const TypedNode *field = typed_program_node(program, child);
                size_t field_size;

                if (field != NULL && field->kind == TYPED_FIELD) {
                    if (!type_value_size_depth(program, field->type_index,
                                               depth - 1, &field_size)) {
                        return false;
                    }
                    add_value_size(&total, field_size);
                }
            }
        }
        *out = total;
        return true;
    }
    case NIPPY_TYPE_HANDLE:
    case NIPPY_TYPE_DYNAMIC_STRING:
    case NIPPY_TYPE_TEXT_BUILDER:
    case NIPPY_TYPE_LIST:
    case NIPPY_TYPE_SLICE:
    case NIPPY_TYPE_RANGE:
    case NIPPY_TYPE_TABLE:
        *out = 32;
        return true;
    case NIPPY_TYPE_FUNCTION:
        *out = 16;
        return true;
    default:
        return false;
    }
}


bool check_type_value_size(CheckContext *ctx, size_t type_index,
                           SourcePos pos)
{
    size_t size;

    if (!type_value_size_depth(ctx->program, type_index, 32, &size)) {
        return true;
    }
    if (size <= NIPPY_MAX_VALUE_BYTES) {
        return true;
    }
    error_report_at(ctx->errors, pos, MESSAGE_CHECKER_VALUE_TOO_LARGE, size,
                    (size_t)NIPPY_MAX_VALUE_BYTES);
    return false;
}
