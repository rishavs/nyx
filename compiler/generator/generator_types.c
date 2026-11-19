#include "generator_internal.h"

bool struct_emitted_before(const TypedProgram *program, size_t index,
                                  size_t declaration_node, size_t name_index)
{
    size_t i;

    for (i = 0; i < index; i++) {
        const TypedType *earlier = typed_program_type(program, i);

        if (earlier->kind == NYX_TYPE_STRUCT &&
            (earlier->name_index == name_index ||
             earlier->declaration_node == declaration_node)) {
            return true;
        }
    }
    return false;
}


static bool emit_struct_type_buffer_appendf(TextBuffer *header, const TypedNode *node, const char *effective_source, ErrorState *errors, size_t *i, const TypedProgram *program)
{
    buffer_appendf(header, "#line %zu \"%s\"\n", node->pos.line,
                    effective_source);
    buffer_append(header, "typedef struct {\n");
    for ((*i) = 0; (*i) < node->children_count; (*i)++) {
        size_t child = *(const size_t *)list_get(&program->children,
                                                 node->children_start + (*i));
        const TypedNode *field = typed_program_node(program, child);
        char field_text[USER_NAME_CAPACITY];
        const char *field_name;
        char field_type_name[NYX_IDENTIFIER_MAX + 32];

        if (field == NULL || field->kind != TYPED_FIELD) {
            error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_FIELD);
            return false;
        }
        if (!format_c_type(program, field->type_index, field_type_name,
                           sizeof(field_type_name), errors)) {
            return false;
        }
        field_name = user_c_name(program, field->name_index, field_text,
                                 sizeof(field_text));
        if (!valid_identifier(field_name)) {
            error_report(errors, MESSAGE_GENERATOR_INVALID_IDENTIFIER);
            return false;
        }
        buffer_appendf(header, "    %s %s;\n", field_type_name,
                        field_name);
    }
    return true;
}

bool emit_struct_type(const TypedProgram *program, const TypedType *type,
                             size_t declaration_node,
                             const char *effective_source, TextBuffer *header,
                             ErrorState *errors)
{
    const TypedNode *node = typed_program_node(program, declaration_node);
    char name_text[USER_NAME_CAPACITY];
    const char *name = user_c_name(program, type->name_index,
                                  name_text, sizeof(name_text));
    size_t i;

    if (node == NULL || node->kind != TYPED_TYPE_DECLARATION) {
        error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_TYPE);
        return false;
    }
    if (!valid_identifier(name)) {
        error_report(errors, MESSAGE_GENERATOR_INVALID_IDENTIFIER);
        return false;
    }
    if (!emit_struct_type_buffer_appendf(header, node, effective_source, errors, &i, program)) {
        return false;
    }
    if (node->children_count == 0) {
        /* A case that carries nothing still needs a member: C has no empty
           struct. */
        buffer_append(header, "    char nyx_unit;\n");
    }
    buffer_appendf(header, "} %s;\n\n", name);
    return true;
}

static bool emit_union_type_alternative(size_t *i, const TypedType *type, const TypedProgram *program, ErrorState *errors, TextBuffer *header)
{
    for ((*i) = 0; (*i) < type->child_count; (*i)++) {
        const TypedType *alternative =
            union_alternative_type(program, type, (*i));
        char alternative_name[NYX_IDENTIFIER_MAX + 32];

        if (alternative == NULL) {
            error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_TYPE);
            return false;
        }
        if (alternative->kind == NYX_TYPE_NIL) {
            continue;
        }
        {
            size_t alternative_index = *(const size_t *)list_get(
                &program->type_children, type->first_child + (*i));

            if (alternative->kind == NYX_TYPE_FUNCTION) {
                char pointer_name[256];
                char field_name[32];

                snprintf(field_name, sizeof(field_name), "v%zu", (*i));
                if (!format_function_pointer(
                    program, alternative_index, field_name,
                    pointer_name, sizeof(pointer_name), errors)) {
                    return false;
                }
                buffer_appendf(header, "        %s;\n",
                                pointer_name);
            } else if (!format_c_type(program, alternative_index,
                                      alternative_name,
                                      sizeof(alternative_name), errors) ||
                       !buffer_appendf(header, "        %s v%zu;\n",
                                       alternative_name, (*i))) {
                return false;
            }
        }
    }
    return true;
}

static bool emit_union_type_payload(const TypedType *type, const TypedProgram *program, ErrorState *errors, TextBuffer *header)
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
        if (alternative->kind != NYX_TYPE_NIL) {
            has_payload = true;
        }
    }
    buffer_append(header, "typedef struct {\n    uint32_t tag;\n");
    if (has_payload) {
        buffer_append(header, "    union {\n");
        if (!emit_union_type_alternative(&i, type, program, errors, header)) {
        return false;
    }
        buffer_append(header, "    } value;\n");
    }
    return true;
}

bool emit_union_type(const TypedProgram *program, const TypedType *type,
                            size_t type_index, TextBuffer *header,
                            ErrorState *errors)
{
    if (!emit_union_type_payload(type, program, errors, header)) {
        return false;
    }
    buffer_appendf(header, "} nyx_sum_%zu;\n\n", type_index);
    return true;
}

/* The member of a case struct that holds its text: a case with a field is the
   one that carries a header of its own (`Other`), and the field is it. */
static const TypedNode *case_header_field(const TypedProgram *program,
                                          const TypedType *alternative)
{
    const TypedNode *declaration =
        typed_program_node(program, alternative->declaration_node);

    return declaration->children_count == 0
               ? NULL
               : typed_program_node(
                     program, *(const size_t *)list_get(
                                  &program->children,
                                  declaration->children_start));
}

/* One case of the header switch: a kind with no header of its own reads as its
   name; one with a header (`Other`) reads as that. */
static void emit_error_header_case(const TypedProgram *program,
                                   const TypedType *kind, size_t i,
                                   TextBuffer *header)
{
    const TypedType *alternative = union_alternative_type(program, kind, i);
    const TypedNode *field = case_header_field(program, alternative);
    char field_text[USER_NAME_CAPACITY];

    if (field != NULL) {
        buffer_appendf(header, "    case %zu: return e.kind.value.v%zu.%s;\n", i, i,
                       user_c_name(program, field->name_index, field_text,
                                   sizeof(field_text)));
        return;
    }
    buffer_appendf(header, "    case %zu: text = \"%s\"; break;\n", i,
                   typed_program_symbol_text(program,
                                             alternative->source_name_index));
}

/* The text of an Error's kind, as the `header()` method reads it. */
static void emit_error_header_helper(const TypedProgram *program,
                                     const TypedType *kind, const char *string_name,
                                     TextBuffer *header)
{
    size_t i;

    buffer_appendf(header,
                   "static inline %s nyx_error_header(nyx_error e)\n{\n"
                   "    %s r = { {0}, 0 };\n"
                   "    const char *text = \"\";\n"
                   "    switch (e.kind.tag) {\n",
                   string_name, string_name);
    for (i = 0; i < kind->child_count; i++) {
        emit_error_header_case(program, kind, i, header);
    }
    buffer_appendf(header,
                   "    default: break;\n    }\n"
                   "    while (text[r.length] != '\\0' && r.length < %d) {\n"
                   "        r.data[r.length] = (uint8_t)text[r.length];\n"
                   "        r.length++;\n    }\n"
                   "    return r;\n}\n\n",
                   program->error_header_capacity);
}

static bool emit_types_in_order_header(const TypedType *type, ErrorState *errors, const TypedProgram *program, TextBuffer *header)
{
    size_t parts[3];
    char kind_name[NYX_IDENTIFIER_MAX + 32];
    char description_name[NYX_IDENTIFIER_MAX + 32];
    char filename_name[NYX_IDENTIFIER_MAX + 32];
    char header_name[NYX_IDENTIFIER_MAX + 32];
    size_t i;

    if (type->child_count != 3) {
        error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_TYPE);
        return false;
    }
    for (i = 0; i < 3; i++) {
        parts[i] = *(const size_t *)list_get(&program->type_children,
                                             type->first_child + i);
    }
    if (!format_c_type(program, parts[0], kind_name, sizeof(kind_name), errors) ||
        !format_c_type(program, parts[1], description_name,
                       sizeof(description_name), errors) ||
        !format_c_type(program, parts[2], filename_name, sizeof(filename_name),
                       errors)) {
        return false;
    }
    buffer_appendf(
        header,
        "typedef struct {\n"
        "    %s kind;\n"
        "    %s description;\n"
        "    %s filename;\n"
        "    int32_t line;\n"
        "    int32_t col;\n"
        "    int32_t pos;\n"
        "} nyx_error;\n\n",
        kind_name, description_name, filename_name);
    /* The header text is a String of the configured header capacity, which is
       also the type of an `Other` kind's header. */
    snprintf(header_name, sizeof(header_name), "nyx_string_%d",
             program->error_header_capacity);
    emit_error_header_helper(program, typed_program_type(program, parts[0]),
                             header_name, header);
    return true;
}

static bool emit_types_in_order_element_name(const TypedType *type, ErrorState *errors, const TypedProgram *program, size_t i, TextBuffer *header)
{
    char element_name[NYX_IDENTIFIER_MAX + 32];
    char list_name[NYX_IDENTIFIER_MAX + 32];
    size_t element;
    size_t capacity = type->fixed_length;

    if (type->child_count != 1) {
        error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_TYPE);
        return false;
    }
    element = *(const size_t *)list_get(&program->type_children,
                                        type->first_child);
    snprintf(list_name, sizeof(list_name), "nyx_list_%zu", i);
    if (!fixed_list_element_name(program, i, element, element_name,
                                 sizeof(element_name), errors)) {
        return false;
    }
    {
        const TypedType *element_type = typed_program_type(program,
                                                           element);

        if (element_type != NULL &&
            element_type->kind == NYX_TYPE_FUNCTION) {
            char pointer_name[256];

            if (!format_function_pointer(program, element,
                                     element_name, pointer_name,
                                     sizeof(pointer_name),
                                     errors)) {
                return false;
            }
            buffer_appendf(header, "typedef %s;\n\n",
                            pointer_name);
        }
    }
    buffer_appendf(
        header,
        "typedef struct {\n"
        "    %s data[%zu];\n"
        "    uint32_t length;\n"
        "} %s;\n\n",
        element_name, capacity == 0 ? (size_t)1 : capacity,
        list_name);
    return true;
}

/* A String of this capacity is defined once, however many types name it. */
static bool emit_types_in_order_already(size_t i, const TypedProgram *program, const TypedType *type, TextBuffer *header, ErrorState *errors)
{
    size_t j;

    (void)errors;
    for (j = 0; j < i; j++) {
        const TypedType *earlier = typed_program_type(program, j);

        if (earlier->kind == NYX_TYPE_STRING &&
            earlier->fixed_length == type->fixed_length) {
            return true;
        }
    }
    buffer_appendf(header,
                   "typedef struct {\n"
                   "    uint8_t data[%zu];\n"
                   "    uint32_t length;\n"
                   "} nyx_string_%zu;\n\n",
                   type->fixed_length + 1, type->fixed_length);
    buffer_appendf(header,
                   "static inline uint8_t nyx_string_%zu_at(nyx_string_%zu s, "
                   "int32_t i, const char *file, size_t line, size_t column, "
                   "size_t offset);\n\n",
                   type->fixed_length, type->fixed_length);
    return true;
}

static bool emit_types_in_order_key_name(const TypedProgram *program, const TypedType *type, ErrorState *errors, TextBuffer *header, size_t i)
{
    char key_name[NYX_IDENTIFIER_MAX + 32];
    char value_name[NYX_IDENTIFIER_MAX + 32];
    size_t key_type = *(const size_t *)list_get(
        &program->type_children, type->first_child);
    size_t value_type = *(const size_t *)list_get(
        &program->type_children, type->first_child + 1);
    size_t buckets = type->fixed_length == 0
                         ? 1
                         : type->fixed_length + 1;

    if (!format_c_type(program, key_type, key_name,
                   sizeof(key_name), errors)) {
        return false;
    }
    if (!format_c_type(program, value_type, value_name,
                   sizeof(value_name), errors)) {
        return false;
    }
    buffer_appendf(
        header,
        "typedef struct {\n"
        "    %s keys[%zu];\n"
        "    %s values[%zu];\n"
        "    uint8_t states[%zu];\n"
        "    size_t length;\n"
        "} nyx_record_%zu;\n\n",
        key_name, buckets, value_name, buckets, buckets, i);
    return true;
}

static bool emit_types_in_order_element(const TypedProgram *program, const TypedType *type, ErrorState *errors, TextBuffer *header, size_t i)
{
    size_t element = *(const size_t *)list_get(&program->type_children,
                                                type->first_child);
    char element_name[NYX_IDENTIFIER_MAX + 32];
    if (!format_c_type(program, element, element_name, sizeof(element_name),
                   errors)) {
        return false;
    }
    buffer_appendf(header,
        "typedef struct { %s start, end; int64_t step; } nyx_range_%zu;\n\n",
        element_name, i);
    return true;
}

/* How the C for one kind of type is written: fixed text written once however
   many types of the kind there are, or a function that writes it for each. */
typedef bool (*TypeWriter)(const TypedProgram *program, const TypedType *type,
                           size_t index, const char *effective_source,
                           TextBuffer *header, ErrorState *errors);

typedef struct {
    const char *once;
    TypeWriter write;
} TypeRule;

static bool write_range(const TypedProgram *program, const TypedType *type,
                        size_t index, const char *effective_source,
                        TextBuffer *header, ErrorState *errors)
{
    (void)effective_source;
    return emit_types_in_order_element(program, type, errors, header, index);
}

static bool write_struct(const TypedProgram *program, const TypedType *type,
                         size_t index, const char *effective_source,
                         TextBuffer *header, ErrorState *errors)
{
    size_t declaration = type->declaration_node;

    /* Instantiations of one generic struct share a declaration. */
    if (struct_emitted_before(program, index, declaration, type->name_index)) {
        return true;
    }
    return emit_struct_type(program, type, declaration, effective_source,
                            header, errors);
}

static bool write_union(const TypedProgram *program, const TypedType *type,
                        size_t index, const char *effective_source,
                        TextBuffer *header, ErrorState *errors)
{
    (void)effective_source;
    return emit_union_type(program, type, index, header, errors);
}

static bool write_record(const TypedProgram *program, const TypedType *type,
                         size_t index, const char *effective_source,
                         TextBuffer *header, ErrorState *errors)
{
    (void)effective_source;
    return emit_types_in_order_key_name(program, type, errors, header, index);
}

static bool write_fixed_list(const TypedProgram *program, const TypedType *type,
                             size_t index, const char *effective_source,
                             TextBuffer *header, ErrorState *errors)
{
    (void)effective_source;
    return emit_types_in_order_element_name(type, errors, program, index,
                                            header);
}

static bool write_string(const TypedProgram *program, const TypedType *type,
                         size_t index, const char *effective_source,
                         TextBuffer *header, ErrorState *errors)
{
    (void)effective_source;
    return emit_types_in_order_already(index, program, type, header, errors);
}

static bool write_error(const TypedProgram *program, const TypedType *type,
                        size_t index, const char *effective_source,
                        TextBuffer *header, ErrorState *errors)
{
    (void)index;
    (void)effective_source;
    return emit_types_in_order_header(type, errors, program, header);
}

/* A handle to a collection or channel is the same record for every element
   type, so it is written once. */
#define STORE_HANDLE_STRUCT(name)                                             \
    "typedef struct {\n"                                                      \
    "    nyx_store *store;\n"                                                 \
    "    size_t offset;\n"                                                    \
    "    uint64_t generation;\n"                                              \
    "} " name ";\n\n"

static const TypeRule type_rules[NYX_TYPE_KIND_COUNT] = {
    [NYX_TYPE_NIL] = {.once = "typedef uint8_t nyx_nil;\n\n"},
    [NYX_TYPE_DYNAMIC_STRING] = {.once =
        "typedef struct {\n"
        "    const uint8_t *static_data;\n"
        "    nyx_store *store;\n"
        "    size_t offset;\n"
        "    size_t length;\n"
        "    uint64_t generation;\n"
        "} nyx_string;\n\n"},
    [NYX_TYPE_TEXT_BUILDER] = {.once = STORE_HANDLE_STRUCT("nyx_builder")},
    [NYX_TYPE_LIST] = {.once = STORE_HANDLE_STRUCT("nyx_list")},
    [NYX_TYPE_CHANNEL] = {.once = STORE_HANDLE_STRUCT("nyx_chanref")},
    [NYX_TYPE_TABLE] = {.once = STORE_HANDLE_STRUCT("nyx_table")},
    [NYX_TYPE_SLICE] = {.once =
        "typedef struct {\n"
        "    nyx_store *store;\n"
        "    size_t offset; /* the parent list's state */\n"
        "    size_t start;  /* index of the first element */\n"
        "    size_t length;\n"
        "    uint64_t generation;\n"
        "} nyx_slice;\n\n"},
    [NYX_TYPE_RANGE] = {.write = write_range},
    [NYX_TYPE_STRUCT] = {.write = write_struct},
    [NYX_TYPE_UNION] = {.write = write_union},
    [NYX_TYPE_RECORD] = {.write = write_record},
    [NYX_TYPE_FIXED_LIST] = {.write = write_fixed_list},
    [NYX_TYPE_STRING] = {.write = write_string},
    [NYX_TYPE_ERROR] = {.write = write_error}
};

static bool emit_types_in_order(const TypedProgram *program,
                                const char *effective_source,
                                TextBuffer *header, ErrorState *errors,
                                const List *order)
{
    bool written[NYX_TYPE_KIND_COUNT] = {false};
    size_t position;

    for (position = 0; position < order->length; position++) {
        size_t index = *(const size_t *)list_get(order, position);
        const TypedType *type = typed_program_type(program, index);
        const TypeRule *rule = &type_rules[type->kind];

        if (type->kind == NYX_TYPE_UNKNOWN) {
            error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_TYPE);
            return false;
        }
        if (rule->once != NULL) {
            if (!written[type->kind]) {
                buffer_append(header, rule->once);
            }
            written[type->kind] = true;
        } else if (rule->write != NULL &&
                   !rule->write(program, type, index, effective_source, header,
                                errors)) {
            return false;
        }
    }
    return true;
}


/* The definitions of every type, in the order the analyser worked out (each
   after the types it holds by value). */
bool emit_type_definitions(const TypedProgram *program,
                           const Analysis *analysis,
                           const char *effective_source,
                           TextBuffer *header, ErrorState *errors)
{
    return emit_types_in_order(program, effective_source, header, errors,
                               &analysis->type_emit_order);
}

/* "void", or the parameter types of a signature, comma separated. */
static bool write_signature_parameters(const TypedProgram *program,
                                       const TypedFunctionSignature *signature,
                                       TextBuffer *header, ErrorState *errors)
{
    size_t i;

    if (signature->parameter_count == 0) {
        buffer_append(header, "void");
    }
    for (i = 0; i < signature->parameter_count; i++) {
        char parameter_name[NYX_IDENTIFIER_MAX + 32];

        if (!format_c_type(program,
                           *(const size_t *)list_get(
                               &program->signature_parameters,
                               signature->first_parameter + i),
                           parameter_name, sizeof(parameter_name), errors)) {
            return false;
        }
        buffer_appendf(header, i > 0 ? ", %s" : "%s", parameter_name);
    }
    return true;
}

/* One `typedef R (*nyx_fun_N)(P, ...);` for the signature at `index`. */
static bool write_signature_typedef(const TypedProgram *program, size_t index,
                                    TextBuffer *header, ErrorState *errors)
{
    const TypedFunctionSignature *signature =
        typed_program_signature(program, index);
    char return_name[NYX_IDENTIFIER_MAX + 32];
    bool ok = format_c_type(program, signature->return_type, return_name,
                            sizeof(return_name), errors);

    if (!ok) {
        return false;
    }
    buffer_appendf(header, "typedef %s (*nyx_fun_%zu)(", return_name, index);
    ok = write_signature_parameters(program, signature, header, errors);
    buffer_append(header, ");\n");
    return ok;
}

/* A function type is one signature, so each signature in the program's
   signature table is one typedef, in table order, followed by a blank line if
   there were any. */
bool emit_function_typedefs(const TypedProgram *program,
                                   TextBuffer *header, ErrorState *errors)
{
    size_t i;

    if (program->signatures.length == 0) {
        return true;
    }
    for (i = 0; i < program->signatures.length; i++) {
        if (!write_signature_typedef(program, i, header, errors)) {
            return false;
        }
    }
    buffer_append(header, "\n");
    return true;
}
