#include "checker_internal.h"


size_t find_type_index(const List *types, const char *name)
{
    size_t i;

    for (i = 0; i < types->length; i++) {
        const TypeEntry *entry = list_get(types, i);

        if (entry != NULL && entry->owner[0] == '\0' &&
            strcmp(entry->name, name) == 0) {
            return i;
        }
    }
    return TYPED_NO_NODE;
}


size_t find_case_index(const List *types, const char *owner, const char *name)
{
    size_t i;

    for (i = 0; i < types->length; i++) {
        const TypeEntry *entry = list_get(types, i);

        if (entry != NULL && strcmp(entry->owner, owner) == 0 &&
            strcmp(entry->name, name) == 0) {
            return i;
        }
    }
    return TYPED_NO_NODE;
}


const TypeEntry *find_type(const List *types, const char *name)
{
    size_t index = find_type_index(types, name);

    return index == TYPED_NO_NODE ? NULL : list_get(types, index);
}


const FunctionEntry *find_function(const CheckContext *ctx,
                                          const char *name)
{
    size_t index = name_index_find(&ctx->function_index, ctx->functions,
                                   name, strlen(name));
    return index == TYPED_NO_NODE ? NULL : list_get(ctx->functions, index);
}


const FunctionEntry *find_associated_function(const List *functions,
                                               const char *owner_name,
                                               const char *method_name)
{
    size_t i;

    if (owner_name == NULL || method_name == NULL) {
        return NULL;
    }
    for (i = 0; i < functions->length; i++) {
        const FunctionEntry *entry = list_get(functions, i);

        if (entry != NULL && (entry->is_method || entry->is_static) &&
            strcmp(entry->owner_name, owner_name) == 0 &&
            strcmp(entry->method_name, method_name) == 0) {
            return entry;
        }
    }
    return NULL;
}


size_t find_function_index(const CheckContext *ctx, const char *name)
{
    return name_index_find(&ctx->function_index, ctx->functions, name,
                           strlen(name));
}


const VariableEntry *find_variable(const List *variables,
                                          const char *name)
{
    size_t i;

    for (i = 0; i < variables->length; i++) {
        const VariableEntry *entry = list_get(variables, i);

        if (entry != NULL && strcmp(entry->name, name) == 0) {
            return entry;
        }
    }
    return NULL;
}


const StoreEntry *find_store(const List *stores, const char *name)
{
    size_t i;

    for (i = 0; i < stores->length; i++) {
        const StoreEntry *entry = list_get(stores, i);

        if (entry != NULL && strcmp(entry->name, name) == 0) {
            return entry;
        }
    }
    return NULL;
}

const AtomicEntry *find_atomic(const List *atomics, const char *name)
{
    size_t i;

    for (i = 0; i < atomics->length; i++) {
        const AtomicEntry *entry = list_get(atomics, i);

        if (entry != NULL && strcmp(entry->name, name) == 0) {
            return entry;
        }
    }
    return NULL;
}

const ChannelEntry *find_channel(const List *channels, const char *name)
{
    size_t i;

    for (i = 0; i < channels->length; i++) {
        const ChannelEntry *entry = list_get(channels, i);

        if (entry != NULL && strcmp(entry->name, name) == 0) {
            return entry;
        }
    }
    return NULL;
}

bool name_is_resource(const CheckContext *ctx, const char *name)
{
    return find_store(ctx->stores, name) != NULL ||
           find_atomic(ctx->atomics, name) != NULL ||
           find_channel(ctx->channels, name) != NULL;
}

bool identifier_is_reserved(const char *name)
{
    static const char *const reserved[] = {
        "_Alignas", "_Alignof", "_Atomic", "_Bool", "_Complex",
        "_Generic", "_Imaginary", "_Noreturn", "_Static_assert",
        "_Thread_local", "auto", "break", "case", "char", "const",
        "continue", "default", "do", "double", "else", "enum",
        "extern", "float", "for", "goto", "if", "inline", "int",
        "long", "register", "restrict", "return", "short", "signed",
        "sizeof", "static", "struct", "switch", "typedef", "union",
        "unsigned", "void", "volatile", "while", "main", "bool",
        "int8_t", "uint8_t", "int16_t", "uint16_t", "int32_t",
        "uint32_t", "int64_t", "uint64_t", "size_t", "NULL", "printf",
        "fprintf", "malloc", "realloc", "free", "memcpy", "memset",
        "exit", "stdout", "stderr"
    };
    size_t i;

    if (name == NULL) {
        return false;
    }
    if (strncmp(name, NYX_GENERATED_PREFIX,
                sizeof(NYX_GENERATED_PREFIX) - 1) == 0) {
        return true;
    }
    for (i = 0; i < sizeof(reserved) / sizeof(reserved[0]); i++) {
        if (strcmp(name, reserved[i]) == 0) {
            return true;
        }
    }
    return false;
}


bool check_reserved_name(CheckContext *ctx, SourcePos pos,
                                const char *name)
{
    if (!identifier_is_reserved(name)) {
        return true;
    }
    error_report_at(ctx->errors, pos, MESSAGE_CHECKER_RESERVED_NAME, name);
    return false;
}


typedef struct {
    const char *name;
    NyxType kind;
} BuiltinScalarType;

static const BuiltinScalarType builtin_scalar_types[] = {
    {"Int8", NYX_TYPE_INT8}, {"Int16", NYX_TYPE_INT16},
    {"Int32", NYX_TYPE_INT32}, {"Int64", NYX_TYPE_INT64},
    {"Uint8", NYX_TYPE_BYTE}, {"Byte", NYX_TYPE_BYTE},
    {"Uint16", NYX_TYPE_UINT16}, {"Uint32", NYX_TYPE_UINT32},
    {"Uint64", NYX_TYPE_UINT64},
    {"Float32", NYX_TYPE_FLOAT32}, {"Float64", NYX_TYPE_FLOAT64},
    {"Bool", NYX_TYPE_BOOL}, {"Nil", NYX_TYPE_NIL},
    {"Store", NYX_TYPE_STORE}, {"Text", NYX_TYPE_DYNAMIC_STRING}
};

bool builtin_scalar_type_from_name(const char *name, NyxType *out)
{
    size_t i;

    if (name == NULL) return false;
    for (i = 0; i < sizeof(builtin_scalar_types) / sizeof(builtin_scalar_types[0]); i++) {
        if (strcmp(name, builtin_scalar_types[i].name) == 0) {
            *out = builtin_scalar_types[i].kind;
            return true;
        }
    }
    return false;
}

bool is_builtin_type_name(const char *name)
{
    static const char *const composite_names[] = {
        "Handle", "String", "TextBuilder", "List", "Array", "Slice",
        "Range", "Record", "Table", "Error"
    };
    NyxType kind;
    size_t i;

    if (builtin_scalar_type_from_name(name, &kind)) return true;
    if (name == NULL) return false;
    for (i = 0; i < sizeof(composite_names) / sizeof(composite_names[0]); i++) {
        if (strcmp(name, composite_names[i]) == 0) return true;
    }
    return false;
}

bool is_builtin_function_name(const char *name)
{
    return strcmp(name, "print") == 0 || strcmp(name, "base64") == 0 ||
           strcmp(name, "range") == 0 || strcmp(name, "now") == 0 ||
           strcmp(name, "sleep") == 0 || strcmp(name, "cores") == 0;
}


bool numeric_type_from_name(const char *name, NyxType *out)
{
    NyxType kind;

    if (!builtin_scalar_type_from_name(name, &kind) ||
        !nyx_type_is_numeric(kind)) return false;
    *out = kind;
    return true;
}

typedef bool (*NameTaken)(const CheckContext *ctx, const char *name);

static bool taken_by_local(const CheckContext *ctx, const char *name)
{
    return find_variable(ctx->locals, name) != NULL;
}

static bool taken_by_resource(const CheckContext *ctx, const char *name)
{
    return name_is_resource(ctx, name);
}

static bool taken_by_type(const CheckContext *ctx, const char *name)
{
    return find_type(ctx->types, name) != NULL;
}

static bool taken_by_function(const CheckContext *ctx, const char *name)
{
    return find_function(ctx, name) != NULL;
}

static bool taken_by_builtin_type(const CheckContext *ctx, const char *name)
{
    (void)ctx;
    return is_builtin_type_name(name);
}

static bool taken_by_builtin_function(const CheckContext *ctx, const char *name)
{
    (void)ctx;
    return is_builtin_function_name(name);
}

static const NameTaken name_takers[] = {
    taken_by_local, taken_by_resource, taken_by_type, taken_by_function,
    taken_by_builtin_type, taken_by_builtin_function,
};

/* A variable visible here: a local, or at the top level a global. */
const VariableEntry *lookup_variable(const CheckContext *ctx, const char *name)
{
    const VariableEntry *variable = find_variable(ctx->locals, name);

    if (variable == NULL && !ctx->in_function) {
        variable = find_variable(ctx->globals, name);
    }
    return variable;
}

/* Whether a new binding may not use `name`: anything already visible there.
   A new local also may not shadow a global at the top level. */
bool name_is_taken(const CheckContext *ctx, const char *name,
                   bool include_globals)
{
    size_t i;

    if (include_globals && !ctx->in_function &&
        find_variable(ctx->globals, name) != NULL) {
        return true;
    }
    for (i = 0; i < NYX_COUNT_OF(name_takers); i++) {
        if (name_takers[i](ctx, name)) {
            return true;
        }
    }
    return false;
}

/* One type parameter of a generic declaration: a fresh, unreserved name,
   appended to the declaration's parameters. */
bool add_type_parameter_name(CheckContext *ctx, size_t child,
                             char (*parameters)[NYX_IDENTIFIER_MAX],
                             size_t *count)
{
    const AstNode *parameter = ast_get_node(ctx->ast, child);
    const char *name;

    if (parameter == NULL || parameter->kind != AST_TYPE_PARAMETER) {
        error_report(ctx->errors, MESSAGE_CHECKER_MISSING_AST_NODE, child);
        return false;
    }
    name = ast_symbol_text(ctx->ast, parameter->name_index);
    if (name == NULL) {
        error_report_at(ctx->errors, parameter->pos,
                        MESSAGE_CHECKER_MISSING_NAME_OR_TYPE);
        return false;
    }
    if (!check_reserved_name(ctx, parameter->pos, name)) {
        return false;
    }
    if (type_parameter_index(parameters, *count, name) != TYPED_NO_NODE) {
        error_report_at(ctx->errors, parameter->pos,
                        MESSAGE_CHECKER_DUPLICATE_TYPE_PARAMETER, name);
        return false;
    }
    memcpy(parameters[*count], name, strlen(name) + 1);
    (*count)++;
    return true;
}
