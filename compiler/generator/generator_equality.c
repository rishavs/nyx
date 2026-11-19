#include "generator_internal.h"

typedef bool (*EqualityBody)(const TypedProgram *p, const TypedType *type,
                            size_t index, TextBuffer *out, ErrorState *errors);

static size_t type_part(const TypedProgram *p, const TypedType *type, size_t at)
{
    return *(const size_t *)list_get(&p->type_children, type->first_child + at);
}

static bool equality_scalar(const TypedProgram *p, const TypedType *type,
                            size_t index, TextBuffer *out, ErrorState *errors)
{
    (void)p;
    (void)type;
    (void)index;
    (void)errors;
    return buffer_append(out, "    return a == b;\n}\n\n");
}

static bool equality_string(const TypedProgram *p, const TypedType *type,
                            size_t index, TextBuffer *out, ErrorState *errors)
{
    (void)p;
    (void)index;
    (void)errors;
    return buffer_appendf(out, "    return nyx_string_%zu_equal(a, b);\n}\n\n",
                          type->fixed_length);
}

static bool equality_dynamic_string(const TypedProgram *p,
                                    const TypedType *type, size_t index,
                                    TextBuffer *out, ErrorState *errors)
{
    (void)p;
    (void)type;
    (void)index;
    (void)errors;
    return buffer_append(
        out,
        "    return nyx_string_equal_at(a, b, file, line, column, pos);\n}\n\n");
}

static bool equality_handle(const TypedProgram *p, const TypedType *type,
                            size_t index, TextBuffer *out, ErrorState *errors)
{
    (void)p;
    (void)type;
    (void)index;
    (void)errors;
    return buffer_append(
        out,
        "    nyx_equal_validate_handle(a, file, line, column, pos);\n"
        "    nyx_equal_validate_handle(b, file, line, column, pos);\n"
        "    return a.store == b.store && a.offset == b.offset &&\n"
        "           a.generation == b.generation;\n}\n\n");
}

/* Every field, in order. */
static bool equality_struct(const TypedProgram *p, const TypedType *type,
                            size_t index, TextBuffer *out, ErrorState *errors)
{
    const TypedNode *declaration =
        typed_program_node(p, type->declaration_node);
    size_t i;

    (void)index;
    (void)errors;
    if (declaration->children_count == 0) {
        buffer_append(out, "    (void)a; (void)b;\n");
    }
    for (i = 0; i < declaration->children_count; i++) {
        const TypedNode *field = typed_program_node(
            p, *(const size_t *)list_get(&p->children,
                                         declaration->children_start + i));
        char storage[USER_NAME_CAPACITY];
        const char *member =
            user_c_name(p, field->name_index, storage, sizeof(storage));

        if (member == NULL) {
            return false;
        }
        buffer_appendf(out,
                       "    if (!nyx_equal_%zu(a.%s, b.%s, file, line, column, "
                       "pos)) return false;\n",
                       field->type_index, member, member);
    }
    return buffer_append(out, "    return true;\n}\n\n");
}

/* The same alternative, then the values of that alternative. An alternative
   that carries nothing (Nil) is equal to itself. */
static bool equality_union(const TypedProgram *p, const TypedType *type,
                           size_t index, TextBuffer *out, ErrorState *errors)
{
    size_t i;

    (void)index;
    (void)errors;
    buffer_append(out, "    if (a.tag != b.tag) return false;\n    switch (a.tag) {\n");
    for (i = 0; i < type->child_count; i++) {
        if (typed_program_type(p, type_part(p, type, i))->kind == NYX_TYPE_NIL) {
            continue;
        }
        buffer_appendf(out,
                       "    case %zu: return nyx_equal_%zu(a.value.v%zu, b.value.v%zu, file, line, column, pos);\n",
                       i, type_part(p, type, i), i, i);
    }
    return buffer_append(out, "    default: return true;\n    }\n}\n\n");
}

static bool equality_fixed_list(const TypedProgram *p, const TypedType *type,
                                size_t index, TextBuffer *out,
                                ErrorState *errors)
{
    (void)index;
    (void)errors;
    return buffer_appendf(
        out,
        "    if (a.length != b.length) return false;\n"
        "    for (size_t i = 0; i < a.length; i++)\n"
        "        if (!nyx_equal_%zu(a.data[i], b.data[i], file, line, column, pos)) return false;\n"
        "    return true;\n}\n\n",
        type_part(p, type, 0));
}

static bool equality_list(const TypedProgram *p, const TypedType *type,
                          size_t index, TextBuffer *out, ErrorState *errors)
{
    size_t element = type_part(p, type, 0);
    char element_name[NYX_IDENTIFIER_MAX + 32];

    (void)index;
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

static bool equality_record(const TypedProgram *p, const TypedType *type,
                            size_t index, TextBuffer *out, ErrorState *errors)
{
    size_t buckets = type->fixed_length == 0 ? 1 : type->fixed_length + 1;

    (void)errors;
    return buffer_appendf(
        out,
        "    if (a.length != b.length) return false;\n"
        "    for (size_t i = 0; i < %zu; i++) {\n"
        "        if (a.states[i] != 1) continue;\n"
        "        size_t j = nyx_record_%zu_find(&b, a.keys[i]);\n"
        "        if (j == SIZE_MAX || !nyx_equal_%zu(a.values[i], b.values[j], file, line, column, pos)) "
        "return false;\n"
        "    }\n    return true;\n}\n\n",
        buckets, index, type_part(p, type, 1));
}

static bool equality_table(const TypedProgram *p, const TypedType *type,
                           size_t index, TextBuffer *out, ErrorState *errors)
{
    size_t value = type_part(p, type, 1);
    char kn[NYX_IDENTIFIER_MAX + 32], vn[NYX_IDENTIFIER_MAX + 32];

    if (!format_c_type(p, type_part(p, type, 0), kn, sizeof(kn), errors) ||
        !format_c_type(p, value, vn, sizeof(vn), errors)) {
        return false;
    }
    return buffer_appendf(
        out,
        "    nyx_table_state_%zu *sa = nyx_table_%zu_state_at(a, file, line, column, pos);\n"
        "    nyx_table_state_%zu *sb = nyx_table_%zu_state_at(b, file, line, column, pos);\n"
        "    if (sa->length != sb->length) return false;\n"
        "    nyx_store *as = a.store, *bs = b.store;\n"
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

static const struct {
    NyxType kind;
    EqualityBody body;
} equality_bodies[] = {
    {NYX_TYPE_STRING, equality_string},
    {NYX_TYPE_DYNAMIC_STRING, equality_dynamic_string},
    {NYX_TYPE_HANDLE, equality_handle},
    {NYX_TYPE_STRUCT, equality_struct},
    {NYX_TYPE_UNION, equality_union},
    {NYX_TYPE_FIXED_LIST, equality_fixed_list},
    {NYX_TYPE_LIST, equality_list},
    {NYX_TYPE_RECORD, equality_record},
    {NYX_TYPE_TABLE, equality_table},
};

/* The body for a type: a number or a Bool compares with ==, the rest by
   kind. A kind with no body cannot be compared. */
static EqualityBody equality_body(NyxType kind)
{
    size_t i;

    if (nyx_type_is_numeric(kind) || kind == NYX_TYPE_BOOL) {
        return equality_scalar;
    }
    for (i = 0; i < NYX_COUNT_OF(equality_bodies); i++) {
        if (equality_bodies[i].kind == kind) {
            return equality_bodies[i].body;
        }
    }
    return NULL;
}

static bool emit_one_equality(const TypedProgram *p, size_t index, TextBuffer *out,
                              ErrorState *errors)
{
    const TypedType *type = typed_program_type(p, index);
    EqualityBody body = equality_body(type->kind);
    char name[NYX_IDENTIFIER_MAX + 32];

    if (!format_c_type(p, index, name, sizeof(name), errors)) {
        return false;
    }
    buffer_appendf(out,
                   "static inline bool nyx_equal_%zu(%s a, %s b, const char "
                   "*file, size_t line, size_t column, size_t pos)\n{\n    "
                   "(void)file; (void)line; (void)column; (void)pos;\n",
                   index, name, name);
    return body != NULL && body(p, type, index, out, errors);
}

static bool emit_equality_pair_helper(const TypedProgram *p, size_t left,
                                      size_t right, TextBuffer *out,
                                      ErrorState *errors)
{
    const TypedType *at = typed_program_type(p, left);
    const TypedType *bt = typed_program_type(p, right);
    char an[NYX_IDENTIFIER_MAX + 32], bn[NYX_IDENTIFIER_MAX + 32];

    return at != NULL && bt != NULL &&
           format_c_type(p, left, an, sizeof(an), errors) &&
           format_c_type(p, right, bn, sizeof(bn), errors) &&
           buffer_appendf(
               out,
               "static inline bool nyx_equal_pair_%zu_%zu(%s a, %s b, const char *file, size_t line, size_t column, size_t pos)\n"
               "{ (void)file; (void)line; (void)column; (void)pos; const uint8_t *ap = %s, *bp = %s; return a.length == b.length && memcmp(ap, bp, a.length) == 0; }\n\n",
               left, right, an, bn,
               at->kind == NYX_TYPE_STRING ? "a.data" : "nyx_string_data_at(a, file, line, column, pos)",
               bt->kind == NYX_TYPE_STRING ? "b.data" : "nyx_string_data_at(b, file, line, column, pos)");
}

static bool emit_equality_pairs(const TypedProgram *p, const List *pairs,
                                TextBuffer *out, ErrorState *errors)
{
    size_t i;

    for (i = 0; i + 1 < pairs->length; i += 2) {
        if (!emit_equality_pair_helper(p, *(const size_t *)list_get(pairs, i),
                                       *(const size_t *)list_get(pairs, i + 1),
                                       out, errors)) {
            return false;
        }
    }
    return true;
}

/* The helpers call one another, so each is declared before any is defined. */
static bool emit_equality_declarations(const TypedProgram *p, const List *types,
                                       TextBuffer *out, ErrorState *errors)
{
    size_t i;

    for (i = 0; i < types->length; i++) {
        size_t type = *(const size_t *)list_get(types, i);
        char name[NYX_IDENTIFIER_MAX + 32];

        if (!format_c_type(p, type, name, sizeof(name), errors)) {
            return false;
        }
        buffer_appendf(out,
                       "static inline bool nyx_equal_%zu(%s a, %s b, const char "
                       "*file, size_t line, size_t column, size_t pos);\n",
                       type, name, name);
    }
    return true;
}

/* The helpers for comparing with == and !=: one per pair of different string
   types, then one per composite type, with the types declared first because
   they call one another. The analyser found which. */
bool emit_equality_helpers(const TypedProgram *p, GeneratorContext *gen, TextBuffer *out, ErrorState *errors)
{
    const List *types = &gen->analysis->equal_types;
    size_t i;
    bool ok = emit_equality_pairs(p, &gen->analysis->equal_pairs, out, errors);

    if (ok && analysis_has(gen->analysis, ANALYSIS_EQUALITY_HANDLE)) {
        buffer_append(out,
            "static inline void nyx_equal_validate_handle(nyx_handle h, const char *file, size_t line, size_t column, size_t pos)\n"
            "{ (void)nyx_handle_pointer_at(h, 0, file, line, column, pos); }\n\n");
    }
    ok = ok && emit_equality_declarations(p, types, out, errors);
    for (i = 0; ok && i < types->length; i++) {
        ok = emit_one_equality(p, *(const size_t *)list_get(types, i), out,
                               errors);
    }
    return ok;
}
