#include "checker_internal.h"

bool builtin_type(CheckContext *ctx, NyxType kind, size_t *out_index)
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

        if (type != NULL && type->kind == NYX_TYPE_STRING &&
            type->fixed_length == (size_t)capacity) {
            *out_index = i;
            return true;
        }
    }
    {
        TypedType type = {0};

        type.kind = NYX_TYPE_STRING;
        type.name_index = TYPED_NO_NODE;
        type.signature_index = TYPED_NO_NODE;
        type.fixed_length = (size_t)capacity;
        type.declaration_node = TYPED_NO_NODE;
        type.source_name_index = TYPED_NO_NODE;
        if (!checker_add_type(ctx, &type, out_index)) {
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

        if (type != NULL && type->kind == NYX_TYPE_STRING_LITERAL) {
            *out_index = i;
            return true;
        }
    }
    {
        TypedType type = {0};

        type.kind = NYX_TYPE_STRING_LITERAL;
        type.name_index = TYPED_NO_NODE;
        type.signature_index = TYPED_NO_NODE;
        type.declaration_node = TYPED_NO_NODE;
        type.source_name_index = TYPED_NO_NODE;
        if (!checker_add_type(ctx, &type, out_index)) {
            return false;
        }
        return true;
    }
}

bool error_type(CheckContext *ctx, size_t *out_index)
{
    size_t kind;
    size_t description;
    size_t filename;
    size_t children_start;
    size_t i;

    for (i = 0; i < ctx->program->types.length; i++) {
        const TypedType *type = typed_program_type(ctx->program, i);

        if (type != NULL && type->kind == NYX_TYPE_ERROR) {
            *out_index = i;
            return true;
        }
    }
    if (!error_kind_type(ctx, &kind) ||
        !string_type(ctx, ctx->program->error_description_capacity,
                     &description) ||
        !string_type(ctx, ctx->program->error_filename_capacity, &filename)) {
        return false;
    }
    children_start = ctx->program->type_children.length;
    checker_ok(ctx->errors, typed_program_add_type_child(ctx->program, kind), MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
    checker_ok(ctx->errors, typed_program_add_type_child(ctx->program, description), MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
    checker_ok(ctx->errors, typed_program_add_type_child(ctx->program, filename), MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
    {
        TypedType type = {0};

        type.kind = NYX_TYPE_ERROR;
        type.name_index = TYPED_NO_NODE;
        type.signature_index = TYPED_NO_NODE;
        type.first_child = children_start;
        type.child_count = 3;
        type.declaration_node = TYPED_NO_NODE;
        type.source_name_index = TYPED_NO_NODE;
        if (!checker_add_type(ctx, &type, out_index)) {
            return false;
        }
        return true;
    }
}

bool error_field_type(CheckContext *ctx, size_t error_index,
                             size_t field, size_t *out_type)
{
    const TypedType *error = typed_program_type(ctx->program, error_index);

    if (error == NULL || error->kind != NYX_TYPE_ERROR ||
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
    /* The seventh field is not stored: it is the text of the kind's header,
       which is as long as an `Other` kind's header. */
    if (field == 6) {
        return string_type(ctx, ctx->program->error_header_capacity, out_type);
    }
    return builtin_type(ctx, NYX_TYPE_INT32, out_type);
}

static bool kind_in(NyxType kind, const NyxType *kinds, size_t count)
{
    size_t i;

    for (i = 0; i < count; i++) {
        if (kinds[i] == kind) {
            return true;
        }
    }
    return false;
}

/* The kinds of type that have a size, besides the numbers. */
static const NyxType sized_kinds[] = {
    NYX_TYPE_BOOL,   NYX_TYPE_NIL,      NYX_TYPE_STRUCT,
    NYX_TYPE_HANDLE, NYX_TYPE_CHANNEL,  NYX_TYPE_STRING,
    NYX_TYPE_DYNAMIC_STRING, NYX_TYPE_TEXT_BUILDER, NYX_TYPE_LIST,
    NYX_TYPE_FIXED_LIST, NYX_TYPE_SLICE, NYX_TYPE_RANGE,
    NYX_TYPE_RECORD, NYX_TYPE_TABLE,    NYX_TYPE_UNION,
    NYX_TYPE_FUNCTION, NYX_TYPE_ERROR,
};

bool is_sized_type(const TypedProgram *program, size_t type_index)
{
    const TypedType *type = typed_program_type(program, type_index);

    return type != NULL &&
           (nyx_type_is_numeric(type->kind) ||
            kind_in(type->kind, sized_kinds, NYX_COUNT_OF(sized_kinds)));
}

/* A type that has a handle-less structural identity: the one with these parts
   (and fixed length) if it exists, otherwise a new one. */
static bool intern_type(CheckContext *ctx, NyxType kind, const size_t *parts,
                        size_t count, size_t fixed_length, size_t *out_index)
{
    TypedProgram *program = ctx->program;
    TypedType type = {0};
    size_t i;
    size_t j;

    for (i = 0; i < program->types.length; i++) {
        const TypedType *existing = typed_program_type(program, i);
        bool same = existing != NULL && existing->kind == kind &&
                    existing->child_count == count &&
                    existing->fixed_length == fixed_length;

        for (j = 0; same && j < count; j++) {
            same = same_type(program,
                             *(const size_t *)list_get(&program->type_children,
                                                       existing->first_child + j),
                             parts[j]);
        }
        if (same) {
            *out_index = i;
            return true;
        }
    }
    type.kind = kind;
    type.name_index = TYPED_NO_NODE;
    type.signature_index = TYPED_NO_NODE;
    type.first_child = program->type_children.length;
    type.child_count = count;
    type.fixed_length = fixed_length;
    type.declaration_node = TYPED_NO_NODE;
    type.source_name_index = TYPED_NO_NODE;
    for (j = 0; j < count; j++) {
        checker_ok(ctx->errors,
                   typed_program_add_type_child(program, parts[j]),
                   MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
    }
    return checker_add_type(ctx, &type, out_index);
}

bool handle_type(CheckContext *ctx, size_t element, size_t *out_index)
{
    bool valid = true;

    if (ctx->handle_depth == 0 && !check_handle_payload(ctx, element, &valid)) {
        return false;
    }
    if (!valid) {
        error_report(ctx->errors, MESSAGE_CHECKER_HANDLE_FIXED);
        return false;
    }
    return intern_type(ctx, NYX_TYPE_HANDLE, &element, 1, 0, out_index);
}

/* Channel<T>: a value naming a channel in a parallel pool. */
/* Channel<T>: a value naming a channel in a parallel pool. */
bool channel_type(CheckContext *ctx, size_t element, size_t *out_index)
{
    return intern_type(ctx, NYX_TYPE_CHANNEL, &element, 1, 0, out_index);
}

bool type_is_channel(const TypedProgram *program, size_t type_index)
{
    const TypedType *type = typed_program_type(program, type_index);

    return type != NULL && type->kind == NYX_TYPE_CHANNEL;
}

bool add_type_record(CheckContext *ctx, NyxType kind,
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
    if (!checker_add_type(ctx, &type, out_index)) {
        return false;
    }
    return true;
}

bool text_builder_type(CheckContext *ctx, size_t *out_index)
{
    size_t dynamic_string;

    if (!builtin_type(ctx, NYX_TYPE_DYNAMIC_STRING, &dynamic_string)) {
        return false;
    }
    return builtin_type(ctx, NYX_TYPE_TEXT_BUILDER, out_index);
}

/* What a list, array, slice or map cannot hold. */
static const NyxType unholdable_kinds[] = {
    NYX_TYPE_STORE,  NYX_TYPE_CHANNEL, NYX_TYPE_LIST,       NYX_TYPE_FIXED_LIST,
    NYX_TYPE_SLICE,  NYX_TYPE_RECORD,  NYX_TYPE_TABLE,      NYX_TYPE_TEXT_BUILDER,
};

bool is_list_element_type(CheckContext *ctx, size_t element_type, bool fixed)
{
    const TypedType *type = typed_program_type(ctx->program, element_type);

    (void)fixed;
    return type != NULL &&
           !kind_in(type->kind, unholdable_kinds, NYX_COUNT_OF(unholdable_kinds)) &&
           is_sized_type(ctx->program, element_type);
}

bool map_type(CheckContext *ctx, size_t key_type, size_t value_type, bool fixed,
              size_t capacity, size_t *out_index)
{
    const TypedType *key = typed_program_type(ctx->program, key_type);
    size_t parts[2] = {key_type, value_type};

    if (key == NULL || key->kind != NYX_TYPE_STRING) {
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
    return intern_type(ctx, fixed ? NYX_TYPE_RECORD : NYX_TYPE_TABLE, parts, 2,
                       fixed ? capacity : 0, out_index);
}

bool list_type(CheckContext *ctx, size_t element_type, bool fixed,
               size_t capacity, size_t *out_index)
{
    if (!is_list_element_type(ctx, element_type, fixed)) {
        error_report(ctx->errors, fixed ? MESSAGE_CHECKER_ARRAY_ELEMENT
                                        : MESSAGE_CHECKER_COLLECTION_ELEMENT);
        return false;
    }
    return intern_type(ctx, fixed ? NYX_TYPE_FIXED_LIST : NYX_TYPE_LIST,
                       &element_type, 1, fixed ? capacity : 0, out_index);
}

bool slice_type(CheckContext *ctx, size_t element_type, size_t *out_index)
{
    if (!is_list_element_type(ctx, element_type, false)) {
        error_report(ctx->errors, MESSAGE_CHECKER_SLICE_ELEMENT);
        return false;
    }
    return intern_type(ctx, NYX_TYPE_SLICE, &element_type, 1, 0, out_index);
}

bool range_type(CheckContext *ctx, size_t element_type, size_t *out_index)
{
    const TypedType *element = typed_program_type(ctx->program, element_type);

    return element != NULL && nyx_type_is_integer(element->kind) &&
           intern_type(ctx, NYX_TYPE_RANGE, &element_type, 1, 0, out_index);
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

        checker_ok(ctx->errors, typed_program_add_signature_parameter(ctx->program, parameter_type), MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
    }
    if (!checker_add_signature(ctx, &signature, &signature_index)) {
        return false;
    }
    return add_type_record(ctx, NYX_TYPE_FUNCTION, TYPED_NO_NODE,
                           signature_index, out_index);
}

size_t type_parameter_index(
    char parameters[][NYX_IDENTIFIER_MAX], size_t count, const char *name)
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

bool add_builtin_type(CheckContext *ctx, NyxType kind,
                             size_t *out_index)
{
    return add_type_record(ctx, kind, TYPED_NO_NODE, TYPED_NO_NODE, out_index);
}


typedef struct {
    size_t size;
    size_t align;
} ValueLayout;

/* Match the target C ABI, including padding between members and at the end. */
static size_t layout_add_saturated(size_t a, size_t b)
{
    return a > SIZE_MAX - b ? SIZE_MAX : a + b;
}

static size_t layout_round(size_t size, size_t alignment)
{
    size_t remainder = size % alignment;
    return remainder == 0 ? size : layout_add_saturated(size, alignment - remainder);
}

static size_t layout_multiply(size_t a, size_t b)
{
    return b != 0 && a > SIZE_MAX / b ? SIZE_MAX : a * b;
}

static void layout_member(ValueLayout *aggregate, ValueLayout member)
{
    aggregate->size = layout_add_saturated(layout_round(aggregate->size, member.align),
                                           member.size);
    if (member.align > aggregate->align) aggregate->align = member.align;
}

#define SCALAR_LAYOUT(T) (ValueLayout){sizeof(T), _Alignof(T)}

/* Kinds whose layout is a fixed list of C members, in order. */
#define LAYOUT_MAX_MEMBERS 5

/* A constant initialiser; SCALAR_LAYOUT's compound literal is not one. */
#define LAYOUT_INIT(T) {sizeof(T), _Alignof(T)}

typedef struct {
    ValueLayout members[LAYOUT_MAX_MEMBERS];
    size_t count;
} LayoutRecipe;

#define U8 LAYOUT_INIT(uint8_t)
#define U32 LAYOUT_INIT(uint32_t)
#define U64 LAYOUT_INIT(uint64_t)
#define SIZE LAYOUT_INIT(size_t)
#define PTR LAYOUT_INIT(void *)

static const LayoutRecipe layout_recipes[] = {
    [NYX_TYPE_INT8] = {{LAYOUT_INIT(int8_t)}, 1},
    [NYX_TYPE_BYTE] = {{U8}, 1},
    [NYX_TYPE_NIL] = {{U8}, 1},
    [NYX_TYPE_BOOL] = {{LAYOUT_INIT(bool)}, 1},
    [NYX_TYPE_INT16] = {{LAYOUT_INIT(int16_t)}, 1},
    [NYX_TYPE_UINT16] = {{LAYOUT_INIT(uint16_t)}, 1},
    [NYX_TYPE_INT32] = {{LAYOUT_INIT(int32_t)}, 1},
    [NYX_TYPE_UINT32] = {{U32}, 1},
    [NYX_TYPE_INT64] = {{LAYOUT_INIT(int64_t)}, 1},
    [NYX_TYPE_UINT64] = {{U64}, 1},
    [NYX_TYPE_FLOAT32] = {{LAYOUT_INIT(float)}, 1},
    [NYX_TYPE_FLOAT64] = {{LAYOUT_INIT(double)}, 1},
    [NYX_TYPE_FUNCTION] = {{LAYOUT_INIT(void (*)(void))}, 1},
    [NYX_TYPE_HANDLE] = {{PTR, SIZE, U64}, 3},
    [NYX_TYPE_CHANNEL] = {{PTR, SIZE, U64}, 3},
    [NYX_TYPE_LIST] = {{PTR, SIZE, U64}, 3},
    [NYX_TYPE_TEXT_BUILDER] = {{PTR, SIZE, U64}, 3},
    [NYX_TYPE_TABLE] = {{PTR, SIZE, U64}, 3},
    [NYX_TYPE_DYNAMIC_STRING] = {{LAYOUT_INIT(const uint8_t *), PTR, SIZE,
                                  SIZE, U64}, 5},
    [NYX_TYPE_SLICE] = {{PTR, SIZE, SIZE, SIZE, U64}, 5},
};

/* The walk a layout rule continues: the checker state and the depth left. */
typedef struct {
    CheckContext *ctx;
    unsigned depth;
} LayoutWalk;

typedef bool (*LayoutRule)(const LayoutWalk *walk, const TypedType *type,
                           ValueLayout *result);

static bool type_layout_depth(CheckContext *ctx, size_t type_index,
                              unsigned depth, ValueLayout *out);

static bool layout_of_type(const LayoutWalk *walk, size_t type_index,
                           ValueLayout *out)
{
    return type_layout_depth(walk->ctx, type_index, walk->depth - 1, out);
}

/* The layouts of exactly `count` child types. */
static bool layout_children(const LayoutWalk *walk, const TypedType *type,
                            size_t count, ValueLayout *out)
{
    size_t i;

    if (type->child_count != count) {
        return false;
    }
    for (i = 0; i < count; i++) {
        size_t child = *(const size_t *)list_get(
            &walk->ctx->program->type_children, type->first_child + i);

        if (!layout_of_type(walk, child, &out[i])) {
            return false;
        }
    }
    return true;
}

static size_t layout_count(size_t length)
{
    return length == 0 ? 1 : length;
}

static bool layout_string(const LayoutWalk *walk, const TypedType *type,
                          ValueLayout *result)
{
    (void)walk;
    layout_member(result,
                  (ValueLayout){layout_add_saturated(type->fixed_length, 1), 1});
    layout_member(result, SCALAR_LAYOUT(uint32_t));
    return true;
}

static bool layout_range(const LayoutWalk *walk, const TypedType *type,
                         ValueLayout *result)
{
    ValueLayout bound;

    if (!layout_children(walk, type, 1, &bound)) {
        return false;
    }
    layout_member(result, bound);
    layout_member(result, bound);
    layout_member(result, SCALAR_LAYOUT(int64_t));
    return true;
}

static bool layout_fixed_list(const LayoutWalk *walk, const TypedType *type,
                              ValueLayout *result)
{
    ValueLayout element;

    if (!layout_children(walk, type, 1, &element)) {
        return false;
    }
    element.size = layout_multiply(element.size, layout_count(type->fixed_length));
    layout_member(result, element);
    layout_member(result, SCALAR_LAYOUT(uint32_t));
    return true;
}

static bool layout_record(const LayoutWalk *walk, const TypedType *type,
                          ValueLayout *result)
{
    ValueLayout parts[2];
    size_t buckets = type->fixed_length == 0 ? 1 : type->fixed_length + 1;

    if (!layout_children(walk, type, 2, parts)) {
        return false;
    }
    parts[0].size = layout_multiply(parts[0].size, buckets);
    parts[1].size = layout_multiply(parts[1].size, buckets);
    layout_member(result, parts[0]);
    layout_member(result, parts[1]);
    layout_member(result, (ValueLayout){buckets, 1});
    layout_member(result, SCALAR_LAYOUT(size_t));
    return true;
}

static bool layout_error(const LayoutWalk *walk, const TypedType *type,
                         ValueLayout *result)
{
    ValueLayout parts[3];
    size_t i;

    if (!layout_children(walk, type, 3, parts)) {
        return false;
    }
    for (i = 0; i < 3; i++) {
        layout_member(result, parts[i]);
    }
    for (i = 0; i < 3; i++) {
        layout_member(result, SCALAR_LAYOUT(int32_t));
    }
    return true;
}

/* A tag, then room for the largest alternative that carries a value. */
static bool layout_union(const LayoutWalk *walk, const TypedType *type,
                         ValueLayout *result)
{
    ValueLayout payload = {0, 1};
    bool has_payload = false;
    size_t i;

    for (i = 0; i < type->child_count; i++) {
        size_t alternative = *(const size_t *)list_get(
            &walk->ctx->program->type_children, type->first_child + i);
        const TypedType *alt = typed_program_type(walk->ctx->program,
                                                  alternative);
        ValueLayout child;

        if (alt == NULL) {
            return false;
        }
        if (alt->kind == NYX_TYPE_NIL) {
            continue;
        }
        if (!layout_of_type(walk, alternative, &child)) {
            return false;
        }
        payload.size = child.size > payload.size ? child.size : payload.size;
        payload.align = child.align > payload.align ? child.align : payload.align;
        has_payload = true;
    }
    layout_member(result, SCALAR_LAYOUT(uint32_t));
    if (has_payload) {
        payload.size = layout_round(payload.size, payload.align);
        layout_member(result, payload);
    }
    return true;
}

static bool layout_struct(const LayoutWalk *walk, const TypedType *type,
                          ValueLayout *result)
{
    const TypedProgram *program = walk->ctx->program;
    const TypedNode *declaration =
        typed_program_node(program, type->declaration_node);
    size_t i;

    if (declaration == NULL) {
        return false;
    }
    for (i = 0; i < declaration->children_count; i++) {
        const TypedNode *field = typed_program_node(
            program, *(const size_t *)list_get(
                         &program->children, declaration->children_start + i));
        ValueLayout member;

        if (field == NULL || field->kind != TYPED_FIELD ||
            !layout_of_type(walk, field->type_index, &member)) {
            return false;
        }
        layout_member(result, member);
    }
    return true;
}

static const LayoutRule layout_rules[] = {
    [NYX_TYPE_STRING] = layout_string,
    [NYX_TYPE_RANGE] = layout_range,
    [NYX_TYPE_FIXED_LIST] = layout_fixed_list,
    [NYX_TYPE_RECORD] = layout_record,
    [NYX_TYPE_UNION] = layout_union,
    [NYX_TYPE_STRUCT] = layout_struct,
    [NYX_TYPE_ERROR] = layout_error,
};

/* Lays the members of `type` out, as the recipe table or the kind's rule
   directs. Returns false for a kind with neither. */
static bool layout_members(const LayoutWalk *walk, const TypedType *type,
                           ValueLayout *result)
{
    size_t i;

    if ((size_t)type->kind < NYX_COUNT_OF(layout_recipes) &&
        layout_recipes[type->kind].count != 0) {
        for (i = 0; i < layout_recipes[type->kind].count; i++) {
            layout_member(result, layout_recipes[type->kind].members[i]);
        }
        return true;
    }
    if ((size_t)type->kind >= NYX_COUNT_OF(layout_rules) ||
        layout_rules[type->kind] == NULL) {
        return false;
    }
    return layout_rules[type->kind](walk, type, result);
}

static bool type_layout_depth(CheckContext *ctx, size_t type_index,
                              unsigned depth, ValueLayout *out)
{
    const TypedType *type = typed_program_type(ctx->program, type_index);
    ValueLayoutCache *slot = (ValueLayoutCache *)list_get(ctx->layout_cache, type_index);
    LayoutWalk walk = {ctx, depth};
    ValueLayout result = {0, 1};

    if (type == NULL) {
        return false;
    }
    if (slot != NULL && slot->valid) {
        *out = (ValueLayout){slot->size, slot->align};
        return true;
    }
    if (depth == 0 || !layout_members(&walk, type, &result)) {
        return false;
    }
    result.size = layout_round(result.size, result.align);
    if (slot != NULL) {
        slot->size = result.size;
        slot->align = result.align;
        slot->valid = true;
    }
    *out = result;
    return true;
}

#undef LAYOUT_INIT
#undef U8
#undef U32
#undef U64
#undef SIZE
#undef PTR
#undef SCALAR_LAYOUT

bool type_value_bytes(CheckContext *ctx, size_t type_index, size_t *out)
{
    ValueLayout layout;

    while (ctx->layout_cache->length < ctx->program->types.length) {
        ValueLayoutCache empty = {0};
        checker_ok(ctx->errors, list_push(ctx->layout_cache, &empty), MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
    }
    if (!type_layout_depth(ctx, type_index, NYX_MAX_LAYOUT_DEPTH, &layout)) {
        error_report(ctx->errors, MESSAGE_CHECKER_LAYOUT_UNAVAILABLE);
        return false;
    }
    *out = layout.size;
    return true;
}

bool check_type_value_size(CheckContext *ctx, size_t type_index,
                           SourcePos pos)
{
    ValueLayout layout;
    size_t size, i;

    while (ctx->layout_cache->length < ctx->program->types.length) {
        ValueLayoutCache empty = {0};
        checker_ok(ctx->errors, list_push(ctx->layout_cache, &empty), MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
    }
    if (!type_layout_depth(ctx, type_index, NYX_MAX_LAYOUT_DEPTH, &layout)) {
        for (i = 0; i < ctx->struct_instantiations->length; i++) {
            const StructInstantiation *record = list_get(ctx->struct_instantiations, i);
            if (record->in_progress) return true;
        }
        error_report_at(ctx->errors, pos, MESSAGE_CHECKER_LAYOUT_UNAVAILABLE);
        return false;
    }
    size = layout.size;
    if (size <= (size_t)ctx->program->max_value_bytes) {
        return true;
    }
    error_report_at(ctx->errors, pos, MESSAGE_CHECKER_VALUE_TOO_LARGE, size,
                    (size_t)ctx->program->max_value_bytes);
    return false;
}

/* T | Nil, the type of a value that may be missing. */
bool optional_type(CheckContext *ctx, size_t element, size_t *result)
{
    size_t nil_type;
    List alternatives;
    bool ok;

    if (!builtin_type(ctx, NYX_TYPE_NIL, &nil_type)) {
        return false;
    }
    if (same_type(ctx->program, element, nil_type)) {
        *result = element;
        return true;
    }
    list_init(&alternatives, sizeof(size_t));
    checker_ok(ctx->errors, list_push(&alternatives, &element),
               MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
    checker_ok(ctx->errors, list_push(&alternatives, &nil_type),
               MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
    ok = !ctx->errors->fatal && intern_union(ctx, &alternatives, result);
    list_free(&alternatives);
    return ok;
}
