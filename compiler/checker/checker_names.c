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


const FunctionEntry *find_function(const List *functions,
                                          const char *name)
{
    size_t i;

    for (i = 0; i < functions->length; i++) {
        const FunctionEntry *entry = list_get(functions, i);

        if (entry != NULL && strcmp(entry->name, name) == 0) {
            return entry;
        }
    }
    return NULL;
}


size_t find_function_index(const List *functions, const char *name)
{
    size_t i;

    for (i = 0; i < functions->length; i++) {
        const FunctionEntry *entry = list_get(functions, i);

        if (entry != NULL && strcmp(entry->name, name) == 0) {
            return i;
        }
    }
    return TYPED_NO_NODE;
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


bool is_builtin_type_name(const char *name)
{
    return strcmp(name, "Int32") == 0 || strcmp(name, "Bool") == 0 ||
           strcmp(name, "Nil") == 0 || strcmp(name, "Byte") == 0 ||
           strcmp(name, "Store") == 0 || strcmp(name, "Handle") == 0 ||
           strcmp(name, "String") == 0 ||
           strcmp(name, "Text") == 0 ||
           strcmp(name, "TextBuilder") == 0 ||
           strcmp(name, "List") == 0 ||
           strcmp(name, "Array") == 0 ||
           strcmp(name, "Slice") == 0 ||
           strcmp(name, "Record") == 0 ||
           strcmp(name, "Table") == 0 ||
           strcmp(name, "Error") == 0;
}
