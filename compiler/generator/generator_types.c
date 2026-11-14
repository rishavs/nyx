#include "generator_internal.h"

size_t find_struct_declaration(const TypedProgram *program,
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

bool struct_emitted_before(const TypedProgram *program, size_t index,
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
        char field_text[USER_NAME_CAPACITY];
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
        field_name = user_c_name(program, field->name_index, field_text,
                                 sizeof(field_text));
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

bool emit_union_type(const TypedProgram *program, const TypedType *type,
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
    if (!buffer_appendf(header, "} nyx_sum_%zu;\n\n", type_index)) {
        error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
        return false;
    }
    return true;
}

static bool emit_types_in_order(const TypedProgram *program,
                                const char *effective_source,
                                TextBuffer *header, ErrorState *errors,
                                const List *order)
{
    bool emitted_nil = false;
    bool table_emitted = false;
    bool list_emitted = false, slice_emitted = false;
    size_t i, order_index;

    for (order_index = 0; order_index < order->length; order_index++) {
        i = *(const size_t *)list_get(order, order_index);
        const TypedType *type = typed_program_type(program, i);

        if (type == NULL || type->kind == NIPPY_TYPE_UNKNOWN) {
            error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_TYPE);
            return false;
        }
        if (type->kind == NIPPY_TYPE_NIL) {
            if (!emitted_nil) {
                if (!buffer_append(header,
                                   "typedef uint8_t nyx_nil;\n\n")) {
                    error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
                    return false;
                }
                emitted_nil = true;
            }
        } else if (type->kind == NIPPY_TYPE_RANGE) {
            size_t element = *(const size_t *)list_get(&program->type_children,
                                                        type->first_child);
            char element_name[NIPPY_IDENTIFIER_MAX + 32];
            if (!format_c_type(program, element, element_name, sizeof(element_name),
                               errors) ||
                !buffer_appendf(header,
                    "typedef struct { %s start, end; int64_t step; } nyx_range_%zu;\n\n",
                    element_name, i)) {
                if (!errors->reported) error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
                return false;
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
                    "} nyx_string;\n\n")) {
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
                    "} nyx_builder;\n\n")) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
                return false;
            }
        } else if (type->kind == NIPPY_TYPE_LIST) {
            if (!list_emitted &&
                !buffer_append(
                    header,
                    "typedef struct {\n"
                    "    uint32_t store;\n"
                    "    size_t offset;\n"
                    "    uint64_t generation;\n"
                    "} nyx_list;\n\n")) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
                return false;
            }
            list_emitted = true;
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
                    "} nyx_record_%zu;\n\n",
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
                    "} nyx_table;\n\n")) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
                return false;
            }
            table_emitted = true;
        } else if (type->kind == NIPPY_TYPE_SLICE) {
            if (!slice_emitted &&
                !buffer_append(
                    header,
                    "typedef struct {\n"
                    "    uint32_t store;\n"
                    "    size_t offset; /* the parent list's state */\n"
                    "    size_t start;  /* index of the first element */\n"
                    "    size_t length;\n"
                    "    uint64_t generation;\n"
                    "} nyx_slice;\n\n")) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
                return false;
            }
            slice_emitted = true;
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
            snprintf(list_name, sizeof(list_name), "nyx_list_%zu", i);
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
                     "} nyx_string_%zu;\n\n",
                     type->fixed_length + 1, type->fixed_length) ||
                 !buffer_appendf(
                     header,
                     "static inline uint8_t nyx_string_%zu_at(nyx_string_%zu s, "
                     "int32_t i, const char *file, size_t line, size_t column, size_t offset);\n\n",
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
                    "} nyx_error;\n\n",
                    header_name, description_name, filename_name)) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
                return false;
            }
        }
    }
    return true;
}


typedef struct {
    size_t index;
    size_t next;
} TypeOrderFrame;

static bool type_order_dependency(const TypedProgram *program, size_t index,
                                  size_t at, size_t *dependency, bool *found)
{
    const TypedType *type = typed_program_type(program, index);
    *found = false;
    if (type == NULL) return false;
    if (type->kind == NIPPY_TYPE_STRUCT) {
        const TypedNode *decl = typed_program_node(program, type->declaration_node);
        const TypedNode *field;
        size_t field_index;
        if (decl == NULL) return false;
        if (at >= decl->children_count) return true;
        field_index = *(const size_t *)list_get(&program->children,
                                                decl->children_start + at);
        field = typed_program_node(program, field_index);
        if (field == NULL) return false;
        *dependency = field->type_index;
    } else if (type->kind == NIPPY_TYPE_UNION ||
               type->kind == NIPPY_TYPE_FIXED_LIST ||
               type->kind == NIPPY_TYPE_RECORD || type->kind == NIPPY_TYPE_ERROR) {
        if (at >= type->child_count) return true;
        *dependency = *(const size_t *)list_get(&program->type_children,
                                                type->first_child + at);
    } else if (type->kind == NIPPY_TYPE_FUNCTION) {
        const TypedFunctionSignature *signature = typed_program_signature(
            program, type->signature_index);
        if (signature == NULL) return false;
        if (at > signature->parameter_count) return true;
        *dependency = at == 0 ? signature->return_type :
            *(const size_t *)list_get(&program->signature_parameters,
                                      signature->first_parameter + at - 1);
    } else {
        return true;
    }
    *found = true;
    return *dependency < program->types.length;
}

bool emit_type_definitions(const TypedProgram *program,
                           const char *effective_source,
                           TextBuffer *header, ErrorState *errors)
{
    unsigned char *state = NULL;
    TypeOrderFrame *stack = NULL;
    List order;
    size_t i, depth = 0, count = program->types.length;
    bool ok = false;
    list_init(&order, sizeof(size_t));
    if (count > SIZE_MAX / sizeof(*stack)) goto oom;
    state = nippy_malloc(count == 0 ? 1 : count);
    stack = nippy_malloc(count == 0 ? sizeof(*stack) : count * sizeof(*stack));
    if (state == NULL || stack == NULL) goto oom;
    memset(state, 0, count);
    for (i = 0; i < count; i++) {
        if (state[i] != 0) continue;
        stack[depth++] = (TypeOrderFrame){i, 0};
        state[i] = 1;
        while (depth > 0) {
            TypeOrderFrame *frame = &stack[depth - 1];
            size_t dependency;
            bool found;
            if (!type_order_dependency(program, frame->index, frame->next++,
                                       &dependency, &found)) goto invalid;
            if (!found) {
                size_t completed = frame->index;
                state[completed] = 2;
                depth--;
                if (!list_push(&order, &completed)) goto oom;
            } else if (state[dependency] == 1) {
                goto invalid;
            } else if (state[dependency] == 0) {
                state[dependency] = 1;
                stack[depth++] = (TypeOrderFrame){dependency, 0};
            }
        }
    }
    ok = emit_types_in_order(program, effective_source, header, errors, &order);
    goto done;
oom:
    error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
    goto done;
invalid:
    error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_TYPE);
done:
    list_free(&order);
    free(stack);
    free(state);
    return ok;
}

bool emit_function_typedefs(const TypedProgram *program,
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
        if (!buffer_appendf(header, "typedef %s (*nyx_fun_%zu)(",
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
