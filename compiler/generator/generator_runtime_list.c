#include "generator_internal.h"


bool emit_list_runtime(const TypedProgram *program, size_t store_count,
                              TextBuffer *source, ErrorState *errors)
{
    bool ok = true;
    size_t i;

    ok = ok && buffer_append(
                   source,
                   "static NIPPY_UNUSED void nyx_list_fail("
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
        snprintf(list_name, sizeof(list_name), "nyx_list_%zu", i);
        ok = ok && buffer_appendf(
                       source,
                       "static NIPPY_UNUSED %s nyx_list_%zu_get("
                       "%s list, int32_t i)\n"
                       "{\n"
                       "    if (i < 0 || (size_t)i >= list.length) {\n"
                       "        nyx_list_fail(\"" MESSAGE_RUNTIME_LIST_BOUNDS
                       "\");\n"
                       "    }\n"
                       "    return list.data[i];\n"
                       "}\n\n"
                       "static NIPPY_UNUSED void nyx_list_%zu_push("
                       "%s *list, %s value)\n"
                       "{\n"
                       "    if (list->length >= %zu) {\n"
                       "        nyx_list_fail(\"" MESSAGE_RUNTIME_LIST_BOUNDS
                       "\");\n"
                       "    }\n"
                       "    list->data[list->length] = value;\n"
                       "    list->length++;\n"
                       "}\n\n"
                       "static NIPPY_UNUSED %s *nyx_list_%zu_at("
                       "%s *list, int32_t i)\n"
                       "{\n"
                       "    if (i < 0 || (size_t)i >= list->length) {\n"
                       "        nyx_list_fail(\"" MESSAGE_RUNTIME_LIST_BOUNDS
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
                           "} nyx_list_state;\n\n"
                           "static NIPPY_UNUSED nyx_store *nyx_list_store("
                           "nyx_list list)\n"
                           "{\n"
                           "    if (list.store >= (uint32_t)NIPPY_STORE_COUNT) {\n"
                           "        nyx_list_fail(\""
                           MESSAGE_RUNTIME_STORE_RANGE "\");\n"
                           "    }\n"
                           "    if (nyx_stores[list.store].generation != "
                           "list.generation) {\n"
                           "        nyx_list_fail(\""
                           MESSAGE_RUNTIME_STALE_HANDLE "\");\n"
                           "    }\n"
                           "    return &nyx_stores[list.store];\n"
                           "}\n\n"
                           "static NIPPY_UNUSED nyx_list_state *"
                           "nyx_list_get_state(nyx_list list)\n"
                           "{\n"
                           "    nyx_store *store = nyx_list_store(list);\n\n"
                           "    if (list.offset > store->used ||\n"
                           "        sizeof(nyx_list_state) > "
                           "store->used - list.offset) {\n"
                           "        nyx_list_fail(\""
                           MESSAGE_RUNTIME_HANDLE_BOUNDS "\");\n"
                           "    }\n"
                           "    return (nyx_list_state *)(void *)"
                           "(store->data + list.offset);\n"
                           "}\n\n"
                           "static NIPPY_UNUSED void *nyx_list_data("
                           "nyx_list list, int32_t index,\n"
                           "                                      "
                           "size_t element_size)\n"
                           "{\n"
                           "    nyx_list_state *state = nyx_list_get_state(list);\n"
                           "    nyx_store *store = nyx_list_store(list);\n"
                           "    size_t offset;\n\n"
                           "    if (index < 0 || (size_t)index >= state->length ||\n"
                           "        element_size == 0) {\n"
                           "        nyx_list_fail(\"");
            ok = ok && buffer_append(
                           source,
                           MESSAGE_RUNTIME_LIST_BOUNDS "\");\n"
                           "    }\n"
                           "    if ((size_t)index > (size_t)-1 / "
                           "element_size) {\n"
                           "        nyx_list_fail(\""
                           MESSAGE_RUNTIME_OUT_OF_MEMORY "\");\n"
                           "    }\n"
                           "    offset = state->offset + (size_t)index * "
                           "element_size;\n"
                           "    if (offset > store->used ||\n"
                           "        element_size > store->used - offset) {\n"
                           "        nyx_list_fail(\"");
            ok = ok && buffer_append(
                           source,
                           MESSAGE_RUNTIME_HANDLE_BOUNDS "\");\n"
                           "    }\n"
                           "    return (void *)(store->data + offset);\n"
                           "}\n\n");
            ok = ok && buffer_append(
                           source,
                           "static NIPPY_UNUSED nyx_list nyx_list_create("
                           "nyx_store *store, uint32_t id, int32_t capacity,\n"
                           "                                              "
                           "size_t element_size, size_t alignment)\n"
                           "{\n"
                           "    nyx_list_state state;\n"
                           "    nyx_handle handle;\n"
                           "    nyx_list list;\n"
                           "    size_t cap;\n"
                           "    size_t bytes;\n"
                           "    size_t buffer;\n\n"
                           "    if (capacity < 0 || element_size == 0) {\n"
                           "        nyx_list_fail(\""
                           MESSAGE_RUNTIME_LIST_BOUNDS "\");\n"
                           "    }\n"
                           "    cap = (size_t)capacity;\n"
                           "    if (cap > (size_t)-1 / element_size) {\n"
                           "        nyx_list_fail(\""
                           MESSAGE_RUNTIME_OUT_OF_MEMORY "\");\n"
                           "    }\n"
                           "    bytes = cap * element_size;\n"
                           "    buffer = nyx_store_alloc_bytes_aligned(store, id,\n"
                           "        bytes == 0 ? 1 : bytes, alignment);\n"
                           "    state.offset = buffer;\n"
                           "    state.length = 0;\n"
                           "    state.capacity = cap;\n"
                           "    handle = nyx_store_alloc(store, id, &state, "
                           "sizeof(state),\n"
                           "                               "
                           "_Alignof(nyx_list_state));\n"
                           "    list.store = id;\n"
                           "    list.offset = handle.offset;\n"
                           "    list.generation = handle.generation;\n"
                           "    return list;\n"
                           "}\n\n");
            ok = ok && buffer_append(
                           source,
                           "static NIPPY_UNUSED void nyx_list_push("
                           "nyx_list list, const void *value,\n"
                           "                                      "
                           "size_t element_size, size_t alignment)\n"
                           "{\n"
                           "    nyx_list_state *state = nyx_list_get_state(list);\n"
                           "    nyx_store *store;\n"
                           "    size_t needed;\n"
                           "    size_t capacity;\n\n"
                           "    if (state->length == (size_t)-1) {\n"
                           "        nyx_list_fail(\"");
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
                           "                nyx_list_fail(\"");
            ok = ok && buffer_append(
                           source,
                           MESSAGE_RUNTIME_OUT_OF_MEMORY "\");\n"
                           "            }\n"
                           "            capacity *= 2;\n"
                           "        }\n"
                           "        if (capacity > (size_t)-1 / element_size) {\n"
                           "            nyx_list_fail(\"");
            ok = ok && buffer_append(
                           source,
                           MESSAGE_RUNTIME_OUT_OF_MEMORY "\");\n"
                           "        }\n"
                           "        new_bytes = capacity * element_size;\n"
                           "        new_offset = nyx_store_alloc_bytes_aligned(\n"
                           "            nyx_list_store(list), list.store, "
                           "new_bytes,\n"
                           "            alignment);\n"
                           "        store = nyx_list_store(list);\n"
                           "        memcpy(store->data + new_offset, "
                           "store->data + old_offset,\n"
                           "               old_bytes);\n"
                           "        state = nyx_list_get_state(list);\n"
                           "        state->offset = new_offset;\n"
                           "        state->capacity = capacity;\n"
                           "    }\n"
                           "    state = nyx_list_get_state(list);\n"
                           "    store = nyx_list_store(list);\n"
                           "    memcpy(store->data + state->offset + "
                           "state->length * element_size,\n"
                           "           value, element_size);\n"
                           "    state->length = needed;\n"
                           "}\n\n"
                           "static NIPPY_UNUSED int32_t nyx_list_len("
                           "nyx_list list)\n"
                           "{\n"
                           "    return (int32_t)nyx_list_get_state(list)->length;\n"
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
                               "static NIPPY_UNUSED nyx_nil "
                               "nyx_list_pop_%zu(%s *list)\n"
                               "{\n"
                               "    if (list->length != 0) {\n"
                               "        list->length--;\n"
                               "    }\n"
                               "    return (nyx_nil)0;\n"
                               "}\n\n",
                               i, list_name);
            } else {
                ok = ok && buffer_appendf(
                               source,
                               "static NIPPY_UNUSED nyx_nil "
                               "nyx_list_pop_%zu(nyx_list list)\n"
                               "{\n"
                               "    nyx_list_state *state = "
                               "nyx_list_get_state(list);\n"
                               "    if (state->length != 0) {\n"
                               "        state->length--;\n"
                               "    }\n"
                               "    return (nyx_nil)0;\n"
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
                "static NIPPY_UNUSED %s nyx_list_pop_%zu(",
                union_name, i) ||
            !buffer_append(&body,
                           receiver_type->kind == NIPPY_TYPE_FIXED_LIST
                               ? ""
                               : "nyx_list list)\n{\n")) {
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
                                : "nyx_list_get_state(list)->length",
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
                    "        nyx_list_state *state = "
                    "nyx_list_get_state(list);\n"
                    "        nyx_store *store = nyx_list_store(list);\n"
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


bool emit_slice_runtime(TextBuffer *source, ErrorState *errors)
{
    bool ok = true;

    ok = ok && buffer_append(
                   source,
                   "static NIPPY_UNUSED nyx_store *nyx_slice_store("
                   "nyx_slice slice)\n"
                   "{\n"
                   "    if (slice.store >= (uint32_t)NIPPY_STORE_COUNT) {\n"
                   "        nyx_list_fail(\""
                   MESSAGE_RUNTIME_STORE_RANGE "\");\n"
                   "    }\n"
                   "    if (nyx_stores[slice.store].generation != "
                   "slice.generation) {\n"
                   "        nyx_list_fail(\""
                   MESSAGE_RUNTIME_STALE_HANDLE "\");\n"
                   "    }\n"
                   "    return &nyx_stores[slice.store];\n"
                   "}\n\n"
                   "static NIPPY_UNUSED nyx_slice nyx_slice_from_list("
                   "nyx_list list, int32_t start, int32_t length,\n"
                   "                                              "
                   "size_t element_size)\n"
                   "{\n"
                   "    nyx_list_state *state = nyx_list_get_state(list);\n"
                   "    nyx_store *store = nyx_list_store(list);\n"
                   "    nyx_slice result;\n"
                   "    size_t offset;\n\n"
                   "    if (start < 0 || length < 0 ||\n"
                   "        (size_t)start > state->length ||\n"
                   "        (size_t)length > state->length - (size_t)start ||\n"
                   "        element_size == 0 ||\n"
                   "        (size_t)start > (size_t)-1 / element_size) {\n"
                   "        nyx_list_fail(\"" MESSAGE_RUNTIME_LIST_BOUNDS
                   "\");\n"
                   "    }\n"
                   "    offset = state->offset + (size_t)start * element_size;\n"
                   "    result.store = list.store;\n"
                   "    result.offset = offset;\n"
                   "    result.length = (size_t)length;\n"
                   "    result.generation = store->generation;\n"
                   "    return result;\n"
                   "}\n\n"
                   "static NIPPY_UNUSED nyx_slice nyx_slice_sub("
                   "nyx_slice slice, int32_t start, int32_t length,\n"
                   "                                        size_t element_size)\n"
                   "{\n"
                   "    nyx_slice result;\n\n"
                   "    if (start < 0 || length < 0 ||\n"
                   "        (size_t)start > slice.length ||\n"
                   "        (size_t)length > slice.length - (size_t)start ||\n"
                   "        element_size == 0 ||\n"
                   "        (size_t)start > (size_t)-1 / element_size) {\n"
                   "        nyx_list_fail(\"" MESSAGE_RUNTIME_LIST_BOUNDS
                   "\");\n"
                   "    }\n"
                   "    (void)nyx_slice_store(slice);\n"
                   "    result = slice;\n"
                   "    result.offset += (size_t)start * element_size;\n"
                   "    result.length = (size_t)length;\n"
                   "    return result;\n"
                   "}\n\n"
                   "static NIPPY_UNUSED const void *nyx_slice_data("
                   "nyx_slice slice, int32_t index, size_t element_size)\n"
                   "{\n"
                   "    nyx_store *store = nyx_slice_store(slice);\n"
                   "    size_t offset;\n\n"
                   "    if (index < 0 || (size_t)index >= slice.length ||\n"
                   "        element_size == 0 ||\n"
                   "        (size_t)index > (size_t)-1 / element_size) {\n"
                   "        nyx_list_fail(\"" MESSAGE_RUNTIME_LIST_BOUNDS
                   "\");\n"
                   "    }\n"
                   "    offset = slice.offset + (size_t)index * element_size;\n"
                   "    if (offset > store->used ||\n"
                   "        element_size > store->used - offset) {\n"
                   "        nyx_list_fail(\""
                   MESSAGE_RUNTIME_HANDLE_BOUNDS "\");\n"
                   "    }\n"
                   "    return (const void *)(store->data + offset);\n"
                   "}\n\n"
                   "static NIPPY_UNUSED int32_t nyx_slice_len("
                   "nyx_slice slice)\n"
                   "{\n"
                   "    (void)nyx_slice_store(slice);\n"
                   "    return (int32_t)slice.length;\n"
                   "}\n\n");
    if (!ok) {
        error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
        return false;
    }
    return true;
}
