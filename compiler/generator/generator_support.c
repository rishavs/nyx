#include "generator_internal.h"

bool emit_match_temporaries(const TypedProgram *program, size_t scope,
                            TextBuffer *source, ErrorState *errors)
{
    const size_t *head = list_get(&program->match_scopes, scope);
    size_t index = head == NULL ? TYPED_NO_NODE : *head;

    while (index != TYPED_NO_NODE) {
        const TypedMatchTemporary *temporary =
            list_get(&program->match_temporaries, index);
        char type_name[NYX_IDENTIFIER_MAX + 32];
        if (temporary == NULL) {
            return false;
        }
        if (!format_c_type(program, temporary->type_index, type_name,
                       sizeof(type_name), errors)) {
            return false;
        }
        buffer_appendf(source, "    %s nyx_match_%zu;\n", type_name, index);
        index = temporary->next;
    }
    return true;
}


bool valid_header_name(const char *name)
{
    const unsigned char *cursor = (const unsigned char *)name;

    if (name == NULL || *name == '\0') {
        return false;
    }
    while (*cursor != '\0') {
        if (!isalnum(*cursor) && *cursor != '_' && *cursor != '-' &&
            *cursor != '.' && *cursor != ' ') {
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

const char *user_c_name(const TypedProgram *program, size_t symbol_index,
                        char *out, size_t size)
{
    const char *name = typed_program_symbol_text(program, symbol_index);

    /* The prefix would make almost any text a valid identifier, so the source
       name is checked first. */
    if (!valid_identifier(name) ||
        strlen(name) + sizeof(NYX_USER_PREFIX) > size) {
        return NULL;
    }
    snprintf(out, size, "%s%s", NYX_USER_PREFIX, name);
    return out;
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
    buffer->failed = false;
}


void buffer_free(TextBuffer *buffer)
{
    free(buffer->text);
    buffer->text = NULL;
    buffer->length = 0;
    buffer->capacity = 0;
    buffer->failed = false;
}


bool buffer_reserve(TextBuffer *buffer, size_t required)
{
    size_t capacity;
    char *grown;

    if (buffer->failed) {
        return false;
    }
    if (required <= buffer->capacity) {
        return true;
    }
    capacity = buffer->capacity == 0 ? 64 : buffer->capacity;
    while (capacity < required) {
        if (capacity > (size_t)-1 / 2) {
            buffer->failed = true;
            return false;
        }
        capacity *= 2;
    }
    grown = nyx_realloc(buffer->text, capacity);
    if (grown == NULL) {
        buffer->failed = true;
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

/* Appends everything `part` holds. A part that ran out of memory while it was
   written is incomplete, so it fails `buffer` too instead of being appended. */
bool buffer_append_buffer(TextBuffer *buffer, const TextBuffer *part)
{
    if (part->failed) {
        buffer->failed = true;
        return false;
    }
    return part->text == NULL || buffer_append(buffer, part->text);
}

bool buffer_vappendf(TextBuffer *buffer, const char *format, va_list args)
{
    va_list measure;
    int needed;

    va_copy(measure, args);
    needed = vsnprintf(NULL, 0, format, measure);
    va_end(measure);
    if (needed < 0) {
        buffer->failed = true;
        return false;
    }
    if (!buffer_reserve(buffer, buffer->length + (size_t)needed + 1)) {
        return false;
    }
    vsnprintf(buffer->text + buffer->length, buffer->capacity - buffer->length,
              format, args);
    buffer->length += (size_t)needed;
    return true;
}

bool buffer_appendf(TextBuffer *buffer, const char *format, ...)
{
    va_list args;
    bool ok;

    va_start(args, format);
    ok = buffer_vappendf(buffer, format, args);
    va_end(args);
    return ok;
}

bool buffer_line(TextBuffer *buffer, size_t depth, const char *format, ...)
{
    va_list args;
    bool ok;

    append_indent(buffer, depth);
    va_start(args, format);
    ok = buffer_vappendf(buffer, format, args);
    va_end(args);
    return ok;
}

const char *checked_user_name(const TypedProgram *program, size_t symbol_index,
                              char *out, size_t size, ErrorState *errors)
{
    const char *name = user_c_name(program, symbol_index, out, size);

    if (name == NULL) {
        error_report(errors, MESSAGE_GENERATOR_INVALID_IDENTIFIER);
    }
    return name;
}

/* The C type of every kind that does not depend on the particular type. */
static const char *const plain_c_names[NYX_TYPE_KIND_COUNT] = {
    [NYX_TYPE_INT8] = "int8_t",
    [NYX_TYPE_INT16] = "int16_t",
    [NYX_TYPE_INT32] = "int32_t",
    [NYX_TYPE_INT64] = "int64_t",
    [NYX_TYPE_UINT16] = "uint16_t",
    [NYX_TYPE_UINT32] = "uint32_t",
    [NYX_TYPE_UINT64] = "uint64_t",
    [NYX_TYPE_FLOAT32] = "float",
    [NYX_TYPE_FLOAT64] = "double",
    [NYX_TYPE_BOOL] = "bool",
    [NYX_TYPE_NIL] = "nyx_nil",
    [NYX_TYPE_BYTE] = "uint8_t",
    [NYX_TYPE_STORE] = "nyx_store *",
    [NYX_TYPE_HANDLE] = "nyx_handle",
    [NYX_TYPE_DYNAMIC_STRING] = "nyx_string",
    [NYX_TYPE_TEXT_BUILDER] = "nyx_builder",
    [NYX_TYPE_LIST] = "nyx_list",
    [NYX_TYPE_CHANNEL] = "nyx_chanref",
    [NYX_TYPE_SLICE] = "nyx_slice",
    [NYX_TYPE_TABLE] = "nyx_table",
    [NYX_TYPE_ERROR] = "nyx_error",
};

/* The kinds whose C type is numbered: by the type's own index, or by a number
   it holds. */
static size_t type_number(const TypedType *type, size_t type_index)
{
    (void)type;
    return type_index;
}

static size_t string_capacity(const TypedType *type, size_t type_index)
{
    (void)type_index;
    return type->fixed_length;
}

static size_t signature_number(const TypedType *type, size_t type_index)
{
    (void)type_index;
    return type->signature_index;
}

static const struct {
    NyxType kind;
    const char *format;
    size_t (*number)(const TypedType *, size_t);
} numbered_c_names[] = {
    {NYX_TYPE_STRING, "nyx_string_%zu", string_capacity},
    {NYX_TYPE_FIXED_LIST, "nyx_list_%zu", type_number},
    {NYX_TYPE_RANGE, "nyx_range_%zu", type_number},
    {NYX_TYPE_RECORD, "nyx_record_%zu", type_number},
    {NYX_TYPE_FUNCTION, "nyx_fun_%zu", signature_number},
    {NYX_TYPE_UNION, "nyx_sum_%zu", type_number},
};

bool format_c_type(const TypedProgram *program, size_t type_index,
                          char *out, size_t out_size, ErrorState *errors)
{
    const TypedType *type = typed_program_type(program, type_index);
    char name_text[USER_NAME_CAPACITY];
    const char *name;
    size_t i;

    if (type == NULL) {
        error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_TYPE);
        return false;
    }
    if (plain_c_names[type->kind] != NULL) {
        snprintf(out, out_size, "%s", plain_c_names[type->kind]);
        return true;
    }
    for (i = 0; i < NYX_COUNT_OF(numbered_c_names); i++) {
        if (numbered_c_names[i].kind == type->kind) {
            snprintf(out, out_size, numbered_c_names[i].format,
                     numbered_c_names[i].number(type, type_index));
            return true;
        }
    }
    if (type->kind != NYX_TYPE_STRUCT) {
        error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_TYPE);
        return false;
    }
    name = checked_user_name(program, type->name_index, name_text,
                             sizeof(name_text), errors);
    if (name != NULL) {
        snprintf(out, out_size, "%s", name);
    }
    return name != NULL;
}

/* A function pointer's return and parameter types may not themselves be
   function types. */
static bool format_pointer_part(const TypedProgram *program, size_t type_index,
                                char *out, size_t size, ErrorState *errors)
{
    const TypedType *type = typed_program_type(program, type_index);

    if (type != NULL && type->kind == NYX_TYPE_FUNCTION) {
        error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_TYPE);
        return false;
    }
    return format_c_type(program, type_index, out, size, errors);
}

/* The parameter types of a function pointer, comma separated. */
static bool format_pointer_parameters(const TypedProgram *program,
                                      const TypedFunctionSignature *signature,
                                      char *params, size_t size,
                                      ErrorState *errors)
{
    size_t used = 0;
    size_t i;

    snprintf(params, size, "%s", signature->parameter_count == 0 ? "void" : "");
    for (i = 0; i < signature->parameter_count; i++) {
        char parameter_name[NYX_IDENTIFIER_MAX + 32];
        int written;

        if (!format_pointer_part(
                program,
                *(const size_t *)list_get(&program->signature_parameters,
                                          signature->first_parameter + i),
                parameter_name, sizeof(parameter_name), errors)) {
            return false;
        }
        written = snprintf(params + used, size - used, "%s%s",
                           i == 0 ? "" : ", ", parameter_name);
        if ((size_t)written >= size - used) {
            error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_TYPE);
            return false;
        }
        used += (size_t)written;
    }
    return true;
}

bool format_function_pointer(const TypedProgram *program,
                                    size_t type_index, const char *name,
                                    char *out, size_t out_size,
                                    ErrorState *errors)
{
    const TypedType *type = typed_program_type(program, type_index);
    const TypedFunctionSignature *signature;
    char return_name[NYX_IDENTIFIER_MAX + 32];
    char params[512];

    if (type == NULL || type->kind != NYX_TYPE_FUNCTION) {
        error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_TYPE);
        return false;
    }
    signature = typed_program_signature(program, type->signature_index);
    if (!format_pointer_part(program, signature->return_type, return_name,
                             sizeof(return_name), errors) ||
        !format_pointer_parameters(program, signature, params, sizeof(params),
                                   errors)) {
        return false;
    }
    if ((size_t)snprintf(out, out_size, "%s (*%s)(%s)", return_name, name,
                         params) >= out_size - 1) {
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

    if (element_type != NULL && element_type->kind == NYX_TYPE_FUNCTION) {
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

    if (handle == NULL || handle->kind != NYX_TYPE_HANDLE ||
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

/* Locate the canonical tag of `wanted` inside a union's alternative run. */
bool find_union_alternative(const TypedProgram *program,
                                   const TypedType *union_type, size_t wanted,
                                   size_t *out_tag)
{
    size_t i;

    for (i = 0; i < union_type->child_count; i++) {
        size_t alternative = *(const size_t *)list_get(
            &program->type_children, union_type->first_child + i);

        if (typed_types_equivalent(program, alternative, wanted, 16)) {
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

bool store_reference_is_valid(const TypedProgram *program, const TypedNode *node)
{
    if (node->store_id != TYPED_NO_NODE)
        return store_id_is_valid(program, node->store_id);
    return valid_identifier(typed_program_symbol_text(program, node->name_index));
}

bool emit_store_reference(const TypedProgram *program, const TypedNode *node,
                          TextBuffer *buffer, ErrorState *errors)
{
    if (!store_reference_is_valid(program, node)) {
        error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_EXPRESSION);
        return false;
    }
    if (node->store_id == TYPED_NO_NODE) {
        char name[USER_NAME_CAPACITY];
        return buffer_append(buffer, user_c_name(program, node->name_index,
                                                  name, sizeof(name)));
    }
    return buffer_appendf(buffer, "&nyx_stores[%zu]", node->store_id);
}

/* Kinds a braced initialiser cannot stand for directly: they are C structs. */
bool type_is_c_aggregate(const TypedProgram *program, size_t type_index)
{
    static const NyxType aggregates[] = {
        NYX_TYPE_STRUCT,      NYX_TYPE_HANDLE,     NYX_TYPE_STRING,
        NYX_TYPE_DYNAMIC_STRING, NYX_TYPE_TEXT_BUILDER, NYX_TYPE_LIST,
        NYX_TYPE_FIXED_LIST,  NYX_TYPE_SLICE,      NYX_TYPE_RECORD,
        NYX_TYPE_TABLE,       NYX_TYPE_UNION,      NYX_TYPE_ERROR,
    };
    NyxType kind = typed_program_type(program, type_index) == NULL
                       ? NYX_TYPE_KIND_COUNT
                       : typed_program_type(program, type_index)->kind;
    size_t i;

    for (i = 0; i < NYX_COUNT_OF(aggregates); i++) {
        if (aggregates[i] == kind) {
            return true;
        }
    }
    return false;
}


bool append_indent(TextBuffer *source, size_t depth)
{
    size_t i;

    for (i = 0; i < depth; i++) {
        buffer_append(source, "    ");
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

/* A union element widened into a union that also holds Nil: each alternative
   keeps its value and takes its tag in the wider union. */
static bool append_widened_union(TextBuffer *body, const TypedProgram *program,
                                 const TypedType *union_type,
                                 const TypedType *element_type,
                                 const char *union_name,
                                 const char *value_name,
                                 const char *result_name)
{
    size_t i;

    buffer_appendf(body, "    switch (%s.tag) {\n", value_name);
    for (i = 0; i < element_type->child_count; i++) {
        const TypedType *alternative =
            union_alternative_type(program, element_type, i);
        size_t result_tag;

        if (alternative == NULL) {
            return false;
        }
        if (alternative->kind == NYX_TYPE_NIL) {
            return false;
        }
        if (!find_union_alternative(
            program, union_type,
            *(const size_t *)list_get(&program->type_children,
                                      element_type->first_child + i),
            &result_tag)) {
            return false;
        }
        buffer_appendf(
            body,
            "    case %zu:\n"
            "        %s = (%s){ .tag = %zu, .value.v%zu = "
            "%s.value.v%zu };\n"
            "        break;\n",
            i, result_name, union_name, result_tag, result_tag,
            value_name, i);
    }
    return buffer_append(body, "    }\n");
}

/* The tag of Nil in `union_type`; an optional result must have one. */
bool find_nil_tag(const TypedProgram *program, GeneratorContext *gen, const TypedType *union_type,
                  size_t *out_tag)
{
    size_t nil = analysis_nil_type(gen->analysis);

    return nil != TYPED_NO_NODE &&
           find_union_alternative(program, union_type, nil, out_tag);
}

/* Wraps `value_name`, a value of the element type, into the optional union
   `union_name` and assigns it to `result_name`. */
bool append_optional_union(GeneratorContext *gen, TextBuffer *body, const TypedProgram *program,
                           size_t union_index, size_t element_index,
                           const char *union_name, const char *value_name,
                           const char *result_name)
{
    const TypedType *union_type = typed_program_type(program, union_index);
    const TypedType *element_type = typed_program_type(program, element_index);
    size_t element_tag;
    size_t nil_tag;

    if (union_type == NULL || element_type == NULL ||
        union_type->kind != NYX_TYPE_UNION ||
        !find_nil_tag(program, gen, union_type, &nil_tag)) {
        return false;
    }
    if (element_type->kind == NYX_TYPE_UNION) {
        if (typed_types_equivalent(program, element_index, union_index, 16)) {
            return buffer_appendf(body, "    %s = %s;\n", result_name,
                                  value_name);
        }
        return append_widened_union(body, program, union_type, element_type,
                                    union_name, value_name, result_name);
    }
    if (!find_union_alternative(program, union_type, element_index,
                                &element_tag)) {
        return false;
    }
    return buffer_appendf(body,
                          "    %s = (%s){ .tag = %zu, .value.v%zu = %s };\n",
                          result_name, union_name, element_tag, element_tag,
                          value_name);
}


/* The C that traps a handle used after its Store's scope ended. A Store's
   generation changes only when a scope or a parallel pool is released, so in a
   program with neither the comparison can never fire and is not emitted. */
bool append_generation_check(TextBuffer *source, GeneratorContext *gen,
                             const char *store_generation,
                             const char *handle_generation, const char *fail)
{
    if (!analysis_generations_vary(gen->analysis)) {
        return true;
    }
    return buffer_appendf(source,
                          "    if (%s != %s) {\n"
                          "        %s(\"" MESSAGE_RUNTIME_STALE_HANDLE
                          "\", file, line, column, pos);\n"
                          "    }\n",
                          store_generation, handle_generation, fail);
}

void node_merge_init(NodeMerge *merge, const size_t *a, size_t a_count,
                     const size_t *b, size_t b_count)
{
    merge->a = a;
    merge->b = b;
    merge->a_count = a_count;
    merge->b_count = b_count;
    merge->a_next = 0;
    merge->b_next = 0;
}

bool node_merge_next(NodeMerge *merge, size_t *index)
{
    bool take_a;

    if (merge->a_next >= merge->a_count && merge->b_next >= merge->b_count) {
        return false;
    }
    take_a = merge->b_next >= merge->b_count ||
             (merge->a_next < merge->a_count &&
              merge->a[merge->a_next] < merge->b[merge->b_next]);
    *index = take_a ? merge->a[merge->a_next++] : merge->b[merge->b_next++];
    return true;
}
