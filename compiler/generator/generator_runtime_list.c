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
                   "    fflush(stdout);\n"
                   "    fprintf(stderr, \"%s\\n\", message);\n"
                   "    exit(1);\n"
                   "}\n\n"
                   "static NIPPY_UNUSED void nyx_list_fail_at(const char *message, const char *file, size_t line, size_t column, size_t offset)\n"
                   "{\n"
                   "    if (file == NULL) nyx_list_fail(message);\n"
                   "    fflush(stdout);\n"
                   "    fprintf(stderr, \"" MESSAGE_RUNTIME_POSITION "\\n\", file, line, column, offset, message + sizeof(\"" MESSAGE_RUNTIME_PREFIX "\") - 1);\n"
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
                       "%s list, int32_t i, const char *file, size_t line, size_t column, size_t offset)\n"
                       "{\n"
                       "    if (i < 0 || (size_t)i >= list.length) {\n"
                       "        nyx_list_fail_at(\"" MESSAGE_RUNTIME_LIST_BOUNDS
                       "\", file, line, column, offset);\n"
                       "    }\n"
                       "    return list.data[i];\n"
                       "}\n\n"
                       "static NIPPY_UNUSED void nyx_list_%zu_push("
                       "%s *list, %s value, const char *file, size_t line, size_t column, size_t offset)\n"
                       "{\n"
                       "    if (list->length %s %zu) {\n"
                       "        nyx_list_fail_at(\"" MESSAGE_RUNTIME_ARRAY_FULL
                       "\", file, line, column, offset);\n"
                       "    }\n"
                       "    list->data[list->length] = value;\n"
                       "    list->length++;\n"
                       "}\n\n"
                       "static NIPPY_UNUSED %s *nyx_list_%zu_at("
                       "%s *list, int32_t i, const char *file, size_t line, size_t column, size_t offset)\n"
                       "{\n"
                       "    if (i < 0 || (size_t)i >= list->length) {\n"
                       "        nyx_list_fail_at(\"" MESSAGE_RUNTIME_LIST_BOUNDS
                       "\", file, line, column, offset);\n"
                       "    }\n"
                       "    return &list->data[i];\n"
                       "}\n\n",
                       element_name, i, list_name,
                       i, list_name, element_name,
                       type->fixed_length == 0 ? "==" : ">=",
                       type->fixed_length,
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
                           "static NIPPY_UNUSED nyx_store *nyx_list_store_at("
                           "nyx_list list, const char *file, size_t line, size_t column, size_t pos)\n"
                           "{\n"
                           "    if (list.store >= (uint32_t)NIPPY_STORE_COUNT) {\n"
                           "        nyx_list_fail_at(\""
                           MESSAGE_RUNTIME_STORE_RANGE "\", file, line, column, pos);\n"
                           "    }\n"
                           "    if (nyx_stores[list.store].generation != "
                           "list.generation) {\n"
                           "        nyx_list_fail_at(\""
                           MESSAGE_RUNTIME_STALE_HANDLE "\", file, line, column, pos);\n"
                           "    }\n"
                           "    return &nyx_stores[list.store];\n"
                           "}\n\n"
                           "static NIPPY_UNUSED nyx_store *nyx_list_store(nyx_list list)\n"
                           "{ return nyx_list_store_at(list, NULL, 0, 0, 0); }\n\n"
                           "static NIPPY_UNUSED nyx_list_state *"
                           "nyx_list_get_state_at(nyx_list list, const char *file, size_t line, size_t column, size_t pos)\n"
                           "{\n"
                           "    nyx_store *store = nyx_list_store_at(list, file, line, column, pos);\n\n"
                           "    if (list.offset > store->used ||\n"
                           "        sizeof(nyx_list_state) > "
                           "store->used - list.offset) {\n"
                           "        nyx_list_fail_at(\""
                           MESSAGE_RUNTIME_HANDLE_BOUNDS "\", file, line, column, pos);\n"
                           "    }\n"
                           "    return (nyx_list_state *)(void *)"
                           "(store->data + list.offset);\n"
                           "}\n\n"
                           "static NIPPY_UNUSED nyx_list_state *nyx_list_get_state(nyx_list list)\n"
                           "{ return nyx_list_get_state_at(list, NULL, 0, 0, 0); }\n\n"
                           "static NIPPY_UNUSED void *nyx_list_data_at("
                           "nyx_list list, int32_t index,\n"
                           "                                      "
                           "size_t element_size, const char *file, size_t line, size_t column, size_t pos)\n"
                           "{\n"
                           "    nyx_list_state *state = nyx_list_get_state_at(list, file, line, column, pos);\n"
                           "    nyx_store *store = nyx_list_store_at(list, file, line, column, pos);\n"
                           "    size_t offset;\n\n"
                           "    if (index < 0 || (size_t)index >= state->length ||\n"
                           "        element_size == 0) {\n"
                           "        nyx_list_fail_at(\"");
            ok = ok && buffer_append(
                           source,
                           MESSAGE_RUNTIME_LIST_BOUNDS "\", file, line, column, pos);\n"
                           "    }\n"
                           "    if ((size_t)index > (size_t)-1 / "
                           "element_size) {\n"
                           "        nyx_list_fail_at(\""
                           MESSAGE_RUNTIME_OUT_OF_MEMORY "\", file, line, column, pos);\n"
                           "    }\n"
                           "    offset = state->offset + (size_t)index * "
                           "element_size;\n"
                           "    if (offset > store->used ||\n"
                           "        element_size > store->used - offset) {\n"
                           "        nyx_list_fail_at(\"");
            ok = ok && buffer_append(
                           source,
                           MESSAGE_RUNTIME_HANDLE_BOUNDS "\", file, line, column, pos);\n"
                           "    }\n"
                           "    return (void *)(store->data + offset);\n"
                           "}\n\n"
                           "static NIPPY_UNUSED void *nyx_list_data(nyx_list list, int32_t index, size_t element_size)\n"
                           "{ return nyx_list_data_at(list, index, element_size, NULL, 0, 0, 0); }\n\n");
            ok = ok && buffer_append(
                           source,
                           "static NIPPY_UNUSED nyx_list nyx_list_create("
                           "nyx_store *store, uint32_t id, int32_t capacity,\n"
                           "                                              "
                           "size_t element_size, size_t alignment, const char *file, size_t line, size_t column, size_t pos)\n"
                           "{\n"
                           "    nyx_list_state state;\n"
                           "    nyx_handle handle;\n"
                           "    nyx_list list;\n"
                           "    size_t cap;\n"
                           "    size_t bytes;\n"
                           "    size_t buffer;\n\n"
                           "    if (capacity < 0) {\n"
                           "        nyx_list_fail_at(\""
                           MESSAGE_RUNTIME_NEGATIVE_CAPACITY "\", file, line, column, pos);\n"
                           "    }\n"
                           "    if (element_size == 0) {\n"
                           "        nyx_list_fail_at(\""
                           MESSAGE_RUNTIME_LIST_BOUNDS "\", file, line, column, pos);\n"
                           "    }\n"
                           "    cap = (size_t)capacity;\n"
                           "    if (cap > (size_t)-1 / element_size) {\n"
                           "        nyx_list_fail_at(\""
                           MESSAGE_RUNTIME_OUT_OF_MEMORY "\", file, line, column, pos);\n"
                           "    }\n"
                           "    bytes = cap * element_size;\n"
                           "    buffer = nyx_store_alloc_bytes_aligned_at(store, id,\n"
                           "        bytes == 0 ? 1 : bytes, alignment, file, line, column, pos);\n"
                           "    state.offset = buffer;\n"
                           "    state.length = 0;\n"
                           "    state.capacity = cap;\n"
                           "    handle = nyx_store_alloc_at(store, id, &state, "
                           "sizeof(state),\n"
                           "                               "
                           "_Alignof(nyx_list_state), file, line, column, pos);\n"
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
                           "size_t element_size, size_t alignment, const char *file, size_t line, size_t column, size_t pos)\n"
                           "{\n"
                           "    nyx_list_state *state = nyx_list_get_state_at(list, file, line, column, pos);\n"
                           "    nyx_store *store;\n"
                           "    size_t needed;\n"
                           "    size_t capacity;\n\n"
                           "    if (state->length == (size_t)-1) {\n"
                           "        nyx_list_fail_at(\"");
            ok = ok && buffer_append(
                           source,
                           MESSAGE_RUNTIME_OUT_OF_MEMORY "\", file, line, column, pos);\n"
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
                           "                nyx_list_fail_at(\"");
            ok = ok && buffer_append(
                           source,
                           MESSAGE_RUNTIME_OUT_OF_MEMORY "\", file, line, column, pos);\n"
                           "            }\n"
                           "            capacity *= 2;\n"
                           "        }\n"
                           "        if (capacity > (size_t)-1 / element_size) {\n"
                           "            nyx_list_fail_at(\"");
            ok = ok && buffer_append(
                           source,
                           MESSAGE_RUNTIME_OUT_OF_MEMORY "\", file, line, column, pos);\n"
                           "        }\n"
                           "        new_bytes = capacity * element_size;\n"
                           "        new_offset = nyx_store_alloc_bytes_aligned_at(\n"
                           "            nyx_list_store_at(list, file, line, column, pos), list.store, "
                           "new_bytes,\n"
                           "            alignment, file, line, column, pos);\n"
                           "        store = nyx_list_store_at(list, file, line, column, pos);\n"
                           "        memcpy(store->data + new_offset, "
                           "store->data + old_offset,\n"
                           "               old_bytes);\n"
                           "        state = nyx_list_get_state_at(list, file, line, column, pos);\n"
                           "        state->offset = new_offset;\n"
                           "        state->capacity = capacity;\n"
                           "    }\n"
                           "    state = nyx_list_get_state_at(list, file, line, column, pos);\n"
                           "    store = nyx_list_store_at(list, file, line, column, pos);\n"
                           "    memcpy(store->data + state->offset + "
                           "state->length * element_size,\n"
                           "           value, element_size);\n"
                           "    state->length = needed;\n"
                           "}\n\n"
                           "static NIPPY_UNUSED int32_t nyx_list_len_at("
                           "nyx_list list, const char *file, size_t line, size_t column, size_t pos)\n"
                           "{\n"
                           "    return (int32_t)nyx_list_get_state_at(list, file, line, column, pos)->length;\n"
                           "}\n\n"
                           "static NIPPY_UNUSED int32_t nyx_list_len(nyx_list list)\n"
                           "{ return nyx_list_len_at(list, NULL, 0, 0, 0); }\n\n");
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
                               "nyx_list_pop_%zu(nyx_list list, const char *file, size_t line, size_t column, size_t pos)\n"
                               "{\n"
                               "    nyx_list_state *state = "
                               "nyx_list_get_state_at(list, file, line, column, pos);\n"
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
                               : "nyx_list list, const char *file, size_t line, size_t column, size_t pos)\n{\n")) {
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
                                : "nyx_list_get_state_at(list, file, line, column, pos)->length",
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
                    "nyx_list_get_state_at(list, file, line, column, pos);\n"
                    "        nyx_store *store = nyx_list_store_at(list, file, line, column, pos);\n"
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
    for (i = 0; i < program->nodes.length; i++) {
        const TypedNode *node = typed_program_node(program, i);
        const TypedNode *receiver;
        const TypedType *receiver_type;
        const TypedType *union_type;
        const TypedType *element_type;
        size_t nil_type = TYPED_NO_NODE;
        size_t nil_tag;
        char union_name[NIPPY_IDENTIFIER_MAX + 32];
        char element_name[NIPPY_IDENTIFIER_MAX + 32];
        char list_name[NIPPY_IDENTIFIER_MAX + 32];
        TextBuffer body;
        size_t j;

        if (node == NULL || node->kind != TYPED_LIST_GET_OPTIONAL) {
            continue;
        }
        receiver = typed_program_node(program, node->left_child);
        receiver_type = receiver == NULL
                            ? NULL
                            : typed_program_type(program, receiver->type_index);
        if (receiver_type == NULL || receiver_type->kind == NIPPY_TYPE_SLICE) {
            continue;
        }
        union_type = typed_program_type(program, node->type_index);
        element_type = typed_program_type(program, node->auxiliary_type);
        if (union_type == NULL || element_type == NULL) {
            return false;
        }
        if (union_type->kind == NIPPY_TYPE_NIL) {
            ok = ok && buffer_appendf(
                           source,
                           "static NIPPY_UNUSED nyx_nil "
                           "nyx_list_get_optional_%zu(nyx_list list, "
                           "int32_t index)\n"
                           "{\n"
                           "    (void)list;\n"
                           "    (void)index;\n"
                           "    return (nyx_nil)0;\n"
                           "}\n\n",
                           i);
            continue;
        }
        if (!format_c_type(program, node->type_index, union_name,
                           sizeof(union_name), errors) ||
            !format_c_type(program, node->auxiliary_type, element_name,
                           sizeof(element_name), errors)) {
            return false;
        }
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
        buffer_init(&body);
        if (receiver_type->kind == NIPPY_TYPE_FIXED_LIST) {
            if (!format_c_type(program, receiver->type_index, list_name,
                               sizeof(list_name), errors) ||
                !buffer_appendf(
                    &body,
                    "static NIPPY_UNUSED %s nyx_list_get_optional_%zu("
                    "%s list, int32_t index)\n"
                    "{\n"
                    "    %s result;\n"
                    "    %s value;\n\n"
                    "    if (index < 0 || (size_t)index >= list.length) {\n"
                    "        result = (%s){ .tag = %zu };\n"
                    "        return result;\n"
                    "    }\n"
                    "    value = list.data[index];\n",
                    union_name, i, list_name, union_name, element_name,
                    union_name, nil_tag)) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
                buffer_free(&body);
                return false;
            }
        } else {
            if (!buffer_appendf(
                    &body,
                    "static NIPPY_UNUSED %s nyx_list_get_optional_%zu("
                    "nyx_list list, int32_t index, const char *file, size_t line, size_t column, size_t pos)\n"
                    "{\n"
                    "    %s result;\n"
                    "    %s value;\n\n"
                    "    {\n"
                    "        nyx_list_state *state = "
                    "nyx_list_get_state_at(list, file, line, column, pos);\n"
                    "        nyx_store *store;\n"
                    "        size_t offset;\n\n"
                    "        if (index < 0 || "
                    "(size_t)index >= state->length) {\n"
                    "            result = (%s){ .tag = %zu };\n"
                    "            return result;\n"
                    "        }\n"
                    "        store = nyx_list_store_at(list, file, line, column, pos);\n"
                    "        offset = state->offset + "
                    "(size_t)index * sizeof(%s);\n"
                    "        if (offset > store->used ||\n"
                    "            sizeof(%s) > store->used - offset) {\n"
                    "            nyx_list_fail_at(\""
                    MESSAGE_RUNTIME_HANDLE_BOUNDS "\", file, line, column, pos);\n"
                    "        }\n"
                    "        value = *(%s *)(void *)"
                    "(store->data + offset);\n"
                    "    }\n",
                    union_name, i, union_name, element_name, union_name,
                    nil_tag, element_name, element_name, element_name)) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
                buffer_free(&body);
                return false;
            }
        }
        if (!append_optional_union(&body, program, node->type_index,
                                   node->auxiliary_type, union_name, "value",
                                   "result") ||
            !buffer_append(&body, "    return result;\n}\n\n") ||
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


bool emit_slice_runtime(const TypedProgram *program, TextBuffer *source,
                        ErrorState *errors)
{
    bool ok = true;
    size_t i;

    /* A slice is a window onto its list: it holds the list's state offset, the
       index of its first element, and its length, and finds the buffer through
       the list's current state on every access. Growing the list moves the
       buffer and the window follows; if the list has shrunk below the window,
       any use of the slice traps. It never reads a stale copy. */
    ok = ok && buffer_append(
                   source,
                   "static NIPPY_UNUSED nyx_store *nyx_slice_store_at(nyx_slice slice, const char *file, size_t line, size_t column, size_t pos)\n"
                   "{\n"
                   "    if (slice.store >= (uint32_t)NIPPY_STORE_COUNT) {\n"
                   "        nyx_list_fail_at(\"" MESSAGE_RUNTIME_STORE_RANGE "\", file, line, column, pos);\n"
                   "    }\n"
                   "    if (nyx_stores[slice.store].generation != slice.generation) {\n"
                   "        nyx_list_fail_at(\"" MESSAGE_RUNTIME_STALE_HANDLE "\", file, line, column, pos);\n"
                   "    }\n"
                   "    return &nyx_stores[slice.store];\n"
                   "}\n"
                   "\n"
                   "static NIPPY_UNUSED nyx_list_state *nyx_slice_window_at(nyx_slice slice, const char *file, size_t line, size_t column, size_t pos)\n"
                   "{\n"
                   "    nyx_store *store = nyx_slice_store_at(slice, file, line, column, pos);\n"
                   "    nyx_list_state *state;\n"
                   "\n"
                   "    if (slice.offset > store->used ||\n"
                   "        sizeof(nyx_list_state) > store->used - slice.offset) {\n"
                   "        nyx_list_fail_at(\"" MESSAGE_RUNTIME_HANDLE_BOUNDS "\", file, line, column, pos);\n"
                   "    }\n"
                   "    state = (nyx_list_state *)(void *)(store->data + slice.offset);\n"
                   "    if (slice.start > state->length ||\n"
                   "        slice.length > state->length - slice.start) {\n"
                   "        nyx_list_fail_at(\"" MESSAGE_RUNTIME_LIST_BOUNDS "\", file, line, column, pos);\n"
                   "    }\n"
                   "    return state;\n"
                   "}\n"
                   "\n");
    ok = ok && buffer_append(
                   source,
                   "static NIPPY_UNUSED nyx_slice nyx_slice_from_list(nyx_list list, int32_t start, int32_t length,\n"
                   "                                                  size_t element_size, const char *file, size_t line, size_t column, size_t pos)\n"
                   "{\n"
                   "    nyx_list_state *state = nyx_list_get_state_at(list, file, line, column, pos);\n"
                   "    nyx_store *store = nyx_list_store_at(list, file, line, column, pos);\n"
                   "    nyx_slice result;\n"
                   "\n"
                   "    (void)element_size;\n"
                   "    if (start < 0 || length < 0 ||\n"
                   "        (size_t)start > state->length ||\n"
                   "        (size_t)length > state->length - (size_t)start) {\n"
                   "        nyx_list_fail_at(\"" MESSAGE_RUNTIME_LIST_BOUNDS "\", file, line, column, pos);\n"
                   "    }\n"
                   "    result.store = list.store;\n"
                   "    result.offset = list.offset;\n"
                   "    result.start = (size_t)start;\n"
                   "    result.length = (size_t)length;\n"
                   "    result.generation = store->generation;\n"
                   "    return result;\n"
                   "}\n"
                   "\n"
                   "static NIPPY_UNUSED nyx_slice nyx_slice_sub(nyx_slice slice, int32_t start, int32_t length,\n"
                   "                                            size_t element_size, const char *file, size_t line, size_t column, size_t pos)\n"
                   "{\n"
                   "    nyx_slice result;\n"
                   "\n"
                   "    (void)element_size;\n"
                   "    if (start < 0 || length < 0 ||\n"
                   "        (size_t)start > slice.length ||\n"
                   "        (size_t)length > slice.length - (size_t)start) {\n"
                   "        nyx_list_fail_at(\"" MESSAGE_RUNTIME_LIST_BOUNDS "\", file, line, column, pos);\n"
                   "    }\n"
                   "    (void)nyx_slice_window_at(slice, file, line, column, pos);\n"
                   "    result = slice;\n"
                   "    result.start += (size_t)start;\n"
                   "    result.length = (size_t)length;\n"
                   "    return result;\n"
                   "}\n"
                   "\n");
    ok = ok && buffer_append(
                   source,
                   "static NIPPY_UNUSED const void *nyx_slice_data_at(nyx_slice slice, int32_t index, size_t element_size, const char *file, size_t line, size_t column, size_t pos)\n"
                   "{\n"
                   "    nyx_store *store = nyx_slice_store_at(slice, file, line, column, pos);\n"
                   "    nyx_list_state *state = nyx_slice_window_at(slice, file, line, column, pos);\n"
                   "    size_t position;\n"
                   "    size_t offset;\n"
                   "\n"
                   "    if (index < 0 || (size_t)index >= slice.length || element_size == 0) {\n"
                   "        nyx_list_fail_at(\"" MESSAGE_RUNTIME_LIST_BOUNDS "\", file, line, column, pos);\n"
                   "    }\n"
                   "    position = slice.start + (size_t)index;\n"
                   "    if (position > ((size_t)-1 - state->offset) / element_size) {\n"
                   "        nyx_list_fail_at(\"" MESSAGE_RUNTIME_LIST_BOUNDS "\", file, line, column, pos);\n"
                   "    }\n"
                   "    offset = state->offset + position * element_size;\n"
                   "    if (offset > store->used ||\n"
                   "        element_size > store->used - offset) {\n"
                   "        nyx_list_fail_at(\"" MESSAGE_RUNTIME_HANDLE_BOUNDS "\", file, line, column, pos);\n"
                   "    }\n"
                   "    return (const void *)(store->data + offset);\n"
                   "}\n"
                   "\n"
                   "static NIPPY_UNUSED int32_t nyx_slice_len_at(nyx_slice slice, const char *file, size_t line, size_t column, size_t pos)\n"
                   "{\n"
                   "    (void)nyx_slice_window_at(slice, file, line, column, pos);\n"
                   "    return (int32_t)slice.length;\n"
                   "}\n\n"
                   "static NIPPY_UNUSED const void *nyx_slice_data(nyx_slice slice, int32_t index, size_t size)\n"
                   "{ return nyx_slice_data_at(slice, index, size, NULL, 0, 0, 0); }\n\n"
                   "static NIPPY_UNUSED int32_t nyx_slice_len(nyx_slice slice)\n"
                   "{ return nyx_slice_len_at(slice, NULL, 0, 0, 0); }\n");
    for (i = 0; i < program->nodes.length; i++) {
        const TypedNode *node = typed_program_node(program, i);
        const TypedNode *receiver;
        const TypedType *receiver_type;
        const TypedType *union_type;
        const TypedType *element_type;
        size_t nil_type = TYPED_NO_NODE;
        size_t nil_tag;
        char union_name[NIPPY_IDENTIFIER_MAX + 32];
        char element_name[NIPPY_IDENTIFIER_MAX + 32];
        TextBuffer body;
        size_t j;

        if (node == NULL || node->kind != TYPED_LIST_GET_OPTIONAL) {
            continue;
        }
        receiver = typed_program_node(program, node->left_child);
        receiver_type = receiver == NULL
                            ? NULL
                            : typed_program_type(program, receiver->type_index);
        if (receiver_type == NULL || receiver_type->kind != NIPPY_TYPE_SLICE) {
            continue;
        }
        union_type = typed_program_type(program, node->type_index);
        element_type = typed_program_type(program, node->auxiliary_type);
        if (union_type == NULL || element_type == NULL) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            return false;
        }
        if (union_type->kind == NIPPY_TYPE_NIL) {
            ok = ok && buffer_appendf(
                           source,
                           "static NIPPY_UNUSED nyx_nil "
                           "nyx_slice_get_optional_%zu(nyx_slice slice, "
                           "int32_t index, const char *file, size_t line, size_t column, size_t pos)\n"
                           "{\n"
                           "    (void)nyx_slice_store_at(slice, file, line, column, pos);\n"
                           "    (void)index;\n"
                           "    return (nyx_nil)0;\n"
                           "}\n\n",
                           i);
            continue;
        }
        if (!format_c_type(program, node->type_index, union_name,
                           sizeof(union_name), errors) ||
            !format_c_type(program, node->auxiliary_type, element_name,
                           sizeof(element_name), errors)) {
            return false;
        }
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
        buffer_init(&body);
        if (!buffer_appendf(
                &body,
                "static NIPPY_UNUSED %s nyx_slice_get_optional_%zu("
                "nyx_slice slice, int32_t index, const char *file, size_t line, size_t column, size_t pos)\n"
                "{\n"
                "    %s result;\n"
                "    %s value;\n"
                "    nyx_store *store = nyx_slice_store_at(slice, file, line, column, pos);\n"
                "    nyx_list_state *state = nyx_slice_window_at(slice, file, line, column, pos);\n"
                "    size_t offset;\n\n"
                "    if (index < 0 || (size_t)index >= slice.length) {\n"
                "        result = (%s){ .tag = %zu };\n"
                "        return result;\n"
                "    }\n"
                "    offset = state->offset + (slice.start + (size_t)index) * "
                "sizeof(%s);\n"
                "    if (offset > store->used ||\n"
                "        sizeof(%s) > store->used - offset) {\n"
                "        nyx_list_fail_at(\""
                MESSAGE_RUNTIME_HANDLE_BOUNDS "\", file, line, column, pos);\n"
                "    }\n"
                "    value = *(%s *)(const void *)(store->data + offset);\n",
                union_name, i, union_name, element_name, union_name,
                nil_tag, element_name, element_name, element_name)) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            buffer_free(&body);
            return false;
        }
        if (!append_optional_union(&body, program, node->type_index,
                                   node->auxiliary_type, union_name, "value",
                                   "result") ||
            !buffer_append(&body, "    return result;\n}\n\n") ||
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
