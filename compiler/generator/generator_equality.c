#include "generator_internal.h"

static bool equality_operator(NippyOperator op)
{
    return op == NIPPY_OPERATOR_EQ || op == NIPPY_OPERATOR_NE ||
           op == NIPPY_OPERATOR_STRICT_EQ;
}

static bool string_kind(NippyType kind)
{
    return kind == NIPPY_TYPE_STRING || kind == NIPPY_TYPE_DYNAMIC_STRING;
}

static bool composite_kind(NippyType kind)
{
    return kind == NIPPY_TYPE_STRUCT || kind == NIPPY_TYPE_FIXED_LIST ||
           kind == NIPPY_TYPE_LIST || kind == NIPPY_TYPE_RECORD ||
           kind == NIPPY_TYPE_TABLE || kind == NIPPY_TYPE_HANDLE;
}

static bool emit_one_equality(const TypedProgram *p, size_t index, TextBuffer *out,
                              ErrorState *errors)
{
    const TypedType *type = typed_program_type(p, index);
    char name[NIPPY_IDENTIFIER_MAX + 32];
    size_t i;
    if (type == NULL || !format_c_type(p, index, name, sizeof(name), errors) ||
        !buffer_appendf(out, "static inline bool nyx_equal_%zu(%s a, %s b, const char *file, size_t line, size_t column, size_t pos)\n{\n    (void)file; (void)line; (void)column; (void)pos;\n", index,
                        name, name)) {
        return false;
    }
    if (nippy_type_is_numeric(type->kind) || type->kind == NIPPY_TYPE_BOOL) {
        return buffer_append(out, "    return a == b;\n}\n\n");
    }
    if (type->kind == NIPPY_TYPE_STRING) {
        return buffer_appendf(out, "    return nyx_string_%zu_equal(a, b);\n}\n\n",
                              type->fixed_length);
    }
    if (type->kind == NIPPY_TYPE_DYNAMIC_STRING) {
        return buffer_append(out, "    return nyx_string_equal_at(a, b, file, line, column, pos);\n}\n\n");
    }
    if (type->kind == NIPPY_TYPE_HANDLE) {
        return buffer_append(out,
                             "    nyx_equal_validate_handle(a, file, line, column, pos);\n"
                             "    nyx_equal_validate_handle(b, file, line, column, pos);\n"
                             "    return a.store == b.store && a.offset == b.offset &&\n"
                             "           a.generation == b.generation;\n}\n\n");
    }
    if (type->kind == NIPPY_TYPE_STRUCT) {
        const TypedNode *decl = typed_program_node(p, type->declaration_node);
        if (decl == NULL) {
            return false;
        }
        for (i = 0; i < decl->children_count; i++) {
            size_t child =
                *(const size_t *)list_get(&p->children, decl->children_start + i);
            const TypedNode *field = typed_program_node(p, child);
            char storage[USER_NAME_CAPACITY];
            const char *member = field == NULL ? NULL
                                               : user_c_name(p, field->name_index,
                                                             storage, sizeof(storage));
            if (member == NULL ||
                !buffer_appendf(out,
                                "    if (!nyx_equal_%zu(a.%s, b.%s, file, line, column, pos)) return false;\n",
                                field->type_index, member, member)) {
                return false;
            }
        }
        return buffer_append(out, "    return true;\n}\n\n");
    }
    if (type->kind == NIPPY_TYPE_FIXED_LIST) {
        size_t element =
            *(const size_t *)list_get(&p->type_children, type->first_child);
        return buffer_appendf(
            out,
            "    if (a.length != b.length) return false;\n"
            "    for (size_t i = 0; i < a.length; i++)\n"
            "        if (!nyx_equal_%zu(a.data[i], b.data[i], file, line, column, pos)) return false;\n"
            "    return true;\n}\n\n",
            element);
    }
    if (type->kind == NIPPY_TYPE_LIST) {
        size_t element =
            *(const size_t *)list_get(&p->type_children, type->first_child);
        char element_name[NIPPY_IDENTIFIER_MAX + 32];
        if (!format_c_type(p, element, element_name, sizeof(element_name), errors)) {
            return false;
        }
        return buffer_appendf(
            out,
            "    int32_t na = nyx_list_len_at(a, file, line, column, pos), nb = nyx_list_len_at(b, file, line, column, pos);\n"
            "    if (na != nb) return false;\n"
            "    for (int32_t i = 0; i < na; i++)\n"
            "        if (!nyx_equal_%zu(*(%s *)nyx_list_data_at(a, i, sizeof(%s), file, line, column, pos),\n"
            "                           *(%s *)nyx_list_data_at(b, i, sizeof(%s), file, line, column, pos), file, line, column, pos)) "
            "return false;\n"
            "    return true;\n}\n\n",
            element, element_name, element_name, element_name, element_name);
    }
    if (type->kind == NIPPY_TYPE_RECORD) {
        size_t value =
            *(const size_t *)list_get(&p->type_children, type->first_child + 1);
        size_t buckets = type->fixed_length == 0 ? 1 : type->fixed_length + 1;
        return buffer_appendf(
            out,
            "    if (a.length != b.length) return false;\n"
            "    for (size_t i = 0; i < %zu; i++) {\n"
            "        if (a.states[i] != 1) continue;\n"
            "        size_t j = nyx_record_%zu_find(&b, a.keys[i]);\n"
            "        if (j == SIZE_MAX || !nyx_equal_%zu(a.values[i], b.values[j], file, line, column, pos)) "
            "return false;\n"
            "    }\n    return true;\n}\n\n",
            buckets, index, value);
    }
    if (type->kind == NIPPY_TYPE_TABLE) {
        size_t key = *(const size_t *)list_get(&p->type_children, type->first_child);
        size_t value =
            *(const size_t *)list_get(&p->type_children, type->first_child + 1);
        char kn[NIPPY_IDENTIFIER_MAX + 32], vn[NIPPY_IDENTIFIER_MAX + 32];
        if (!format_c_type(p, key, kn, sizeof(kn), errors) ||
            !format_c_type(p, value, vn, sizeof(vn), errors)) {
            return false;
        }
        return buffer_appendf(
            out,
            "    nyx_table_state_%zu *sa = nyx_table_%zu_state_at(a, file, line, column, pos);\n"
            "    nyx_table_state_%zu *sb = nyx_table_%zu_state_at(b, file, line, column, pos);\n"
            "    if (sa->length != sb->length) return false;\n"
            "    nyx_store *as = &nyx_stores[a.store], *bs = &nyx_stores[b.store];\n"
            "    %s *keys = (%s *)(void *)(as->data + sa->keys_offset);\n"
            "    %s *av = (%s *)(void *)(as->data + sa->values_offset);\n"
            "    %s *bv = (%s *)(void *)(bs->data + sb->values_offset);\n"
            "    uint8_t *states = as->data + sa->states_offset;\n"
            "    for (size_t i = 0; i < sa->buckets; i++) {\n"
            "        if (states[i] != 1) continue;\n"
            "        size_t j = nyx_table_%zu_find_in(b, sb, keys[i]);\n"
            "        if (j == SIZE_MAX || !nyx_equal_%zu(av[i], bv[j], file, line, column, pos)) return false;\n"
            "    }\n    return true;\n}\n\n",
            index, index, index, index, kn, kn, vn, vn, vn, vn, index, value);
    }
    return false;
}

bool emit_equality_helpers(const TypedProgram *p, TextBuffer *out, ErrorState *errors)
{
    typedef struct {
        size_t left;
        size_t right;
    } EqualityPair;
    List types;
    List pairs;
    size_t i, j;
    bool ok = true;
    bool validate_handle = false;
    list_init(&types, sizeof(size_t));
    list_init(&pairs, sizeof(EqualityPair));
    for (i = 0; i < p->nodes.length && ok; i++) {
        const TypedNode *n = typed_program_node(p, i), *a, *b;
        const TypedType *at, *bt;
        char an[NIPPY_IDENTIFIER_MAX + 32], bn[NIPPY_IDENTIFIER_MAX + 32];
        bool duplicate = false;
        if (n == NULL || n->kind != TYPED_BINARY || !equality_operator(n->op)) {
            continue;
        }
        a = typed_program_node(p, n->left_child);
        b = typed_program_node(p, n->right_child);
        if (a == NULL || b == NULL) {
            continue;
        }
        at = typed_program_type(p, a->type_index);
        bt = typed_program_type(p, b->type_index);
        if (at == NULL || bt == NULL) {
            continue;
        }
        if (at->kind == NIPPY_TYPE_HANDLE || bt->kind == NIPPY_TYPE_HANDLE) {
            validate_handle = true;
        }
        if (composite_kind(at->kind) &&
            types_equivalent(p, a->type_index, b->type_index, 64)) {
            ok = collect_print_type(p, a->type_index, &types, 64, errors);
        }
        if (!string_kind(at->kind) || !string_kind(bt->kind) ||
            types_equivalent(p, a->type_index, b->type_index, 64) ||
            n->op == NIPPY_OPERATOR_STRICT_EQ) {
            continue;
        }
        for (j = 0; j < pairs.length; j++) {
            const EqualityPair *pair = list_get(&pairs, j);
            if (pair->left == a->type_index && pair->right == b->type_index) {
                duplicate = true;
                break;
            }
        }
        if (duplicate) {
            continue;
        }
        {
            EqualityPair pair = {a->type_index, b->type_index};
            if (!list_push(&pairs, &pair)) {
                ok = false;
                break;
            }
        }
        if (!format_c_type(p, a->type_index, an, sizeof(an), errors) ||
            !format_c_type(p, b->type_index, bn, sizeof(bn), errors) ||
            !buffer_appendf(
                out,
                "static inline bool nyx_equal_pair_%zu_%zu(%s a, %s b, const char *file, size_t line, size_t column, size_t pos)\n"
                "{ (void)file; (void)line; (void)column; (void)pos; const uint8_t *ap = %s, *bp = %s; return a.length == b.length && memcmp(ap, bp, a.length) == 0; }\n\n",
                a->type_index, b->type_index, an, bn,
                at->kind == NIPPY_TYPE_STRING ? "a.data" : "nyx_string_data_at(a, file, line, column, pos)",
                bt->kind == NIPPY_TYPE_STRING ? "b.data" : "nyx_string_data_at(b, file, line, column, pos)")) {
            ok = false;
        }
    }
    for (i = 0; i < types.length; i++) {
        size_t index = *(const size_t *)list_get(&types, i);
        const TypedType *type = typed_program_type(p, index);
        if (type != NULL && type->kind == NIPPY_TYPE_HANDLE) {
            validate_handle = true;
        }
    }
    if (ok && validate_handle) {
        if (count_store_declarations(p) != 0) {
            ok = buffer_append(out,
                "static inline void nyx_equal_validate_handle(nyx_handle h, const char *file, size_t line, size_t column, size_t pos)\n"
                "{ (void)nyx_handle_pointer_at(h, 0, file, line, column, pos); }\n\n");
        } else {
            ok = buffer_append(out,
                "#include <stdio.h>\n#include <stdlib.h>\n"
                "static inline void nyx_equal_validate_handle(nyx_handle h, const char *file, size_t line, size_t column, size_t pos)\n"
                "{ (void)h; fflush(stdout); fprintf(stderr, \"" MESSAGE_RUNTIME_POSITION "\\n\", file, line, column, pos, \"" MESSAGE_RUNTIME_STORE_RANGE "\" + sizeof(\"" MESSAGE_RUNTIME_PREFIX "\") - 1); exit(1); }\n\n");
        }
    }
    for (i = 0; ok && i < types.length; i++) {
        size_t type = *(const size_t *)list_get(&types, i);
        char name[NIPPY_IDENTIFIER_MAX + 32];
        if (!format_c_type(p, type, name, sizeof(name), errors) ||
            !buffer_appendf(out, "static inline bool nyx_equal_%zu(%s a, %s b, const char *file, size_t line, size_t column, size_t pos);\n",
                            type, name, name)) {
            ok = false;
        }
    }
    for (i = 0; ok && i < types.length; i++) {
        ok = emit_one_equality(p, *(const size_t *)list_get(&types, i), out, errors);
    }
    list_free(&types);
    list_free(&pairs);
    if (!ok && !errors->reported) {
        error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
    }
    return ok;
}
