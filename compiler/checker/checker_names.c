#include "checker_internal.h"


size_t find_type_index(const List *types, const char *name)
{
    size_t i;

    for (i = 0; i < types->length; i++) {
        const TypeEntry *entry = list_get(types, i);

        if (entry != NULL && strcmp(entry->name, name) == 0) {
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
    if (strncmp(name, NIPPY_GENERATED_PREFIX,
                sizeof(NIPPY_GENERATED_PREFIX) - 1) == 0) {
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
    NippyType kind;
} BuiltinScalarType;

static const BuiltinScalarType builtin_scalar_types[] = {
    {"Int8", NIPPY_TYPE_INT8}, {"Int16", NIPPY_TYPE_INT16},
    {"Int32", NIPPY_TYPE_INT32}, {"Int64", NIPPY_TYPE_INT64},
    {"Uint8", NIPPY_TYPE_BYTE}, {"Byte", NIPPY_TYPE_BYTE},
    {"Uint16", NIPPY_TYPE_UINT16}, {"Uint32", NIPPY_TYPE_UINT32},
    {"Uint64", NIPPY_TYPE_UINT64},
    {"Float32", NIPPY_TYPE_FLOAT32}, {"Float64", NIPPY_TYPE_FLOAT64},
    {"Bool", NIPPY_TYPE_BOOL}, {"Nil", NIPPY_TYPE_NIL},
    {"Store", NIPPY_TYPE_STORE}, {"Text", NIPPY_TYPE_DYNAMIC_STRING}
};

bool builtin_scalar_type_from_name(const char *name, NippyType *out)
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
    NippyType kind;
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
           strcmp(name, "range") == 0;
}


bool numeric_type_from_name(const char *name, NippyType *out)
{
    NippyType kind;

    if (!builtin_scalar_type_from_name(name, &kind) ||
        !nippy_type_is_numeric(kind)) return false;
    *out = kind;
    return true;
}
