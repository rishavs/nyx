#include "checker.h"

#include "alloc.h"
#include "messages.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NIPPY_MAX_TYPE_PARAMETERS 8
#define NIPPY_TYPE_DEPTH 64

typedef enum {
    TYPE_ENTRY_STRUCT,
    TYPE_ENTRY_ALIAS
} TypeEntryKind;

/* A declared type. Instantiations live in CheckContext.struct_instantiations
   rather than inside this record, because a List element may not own another
   List. */
typedef struct {
    char name[NIPPY_IDENTIFIER_MAX];
    TypeEntryKind kind;
    size_t ast_index;  /* AST_TYPE_DECLARATION or AST_TYPE_ALIAS */
    size_t type_index; /* concrete non-generic struct, or TYPED_NO_NODE */
    char parameters[NIPPY_MAX_TYPE_PARAMETERS][NIPPY_IDENTIFIER_MAX];
    size_t parameter_count;
} TypeEntry;

typedef struct {
    char name[NIPPY_IDENTIFIER_MAX];
    size_t type_index; /* TYPED_NO_NODE for a generic template */
    bool generic;
    size_t ast_index;
    char parameters[NIPPY_MAX_TYPE_PARAMETERS][NIPPY_IDENTIFIER_MAX];
    size_t parameter_count;
} FunctionEntry;

typedef struct {
    char name[NIPPY_IDENTIFIER_MAX];
    size_t type_index;
} VariableEntry;

typedef struct {
    char name[NIPPY_IDENTIFIER_MAX];
    size_t id;
    int initial_size;
} StoreEntry;

typedef struct {
    size_t entry; /* index into CheckContext.types */
    size_t arguments[NIPPY_MAX_TYPE_PARAMETERS];
    size_t argument_count;
    size_t type_index;
    bool in_progress;
} StructInstantiation;

typedef struct {
    size_t entry; /* index into CheckContext.functions */
    size_t arguments[NIPPY_MAX_TYPE_PARAMETERS];
    size_t argument_count;
    size_t type_index;
    size_t name_index;
    bool in_progress;
    bool emitted;
} FunctionInstantiation;

/* A concrete substitution for a type declaration's parameters. */
typedef struct {
    const char *names[NIPPY_MAX_TYPE_PARAMETERS];
    size_t types[NIPPY_MAX_TYPE_PARAMETERS];
    size_t count;
} TypeEnv;

typedef struct {
    const char *name;
    size_t type_index; /* narrowed type */
    size_t tag;
} Narrowing;

typedef struct {
    const Ast *ast;
    TypedProgram *program;
    ErrorState *errors;
    List *mapping;
    List *types;                  /* TypeEntry */
    List *functions;              /* FunctionEntry */
    List *globals;                /* VariableEntry, program-level names */
    List *locals;                 /* VariableEntry, current function's parameters */
    List *struct_instantiations;  /* StructInstantiation */
    List *function_instantiations;/* FunctionInstantiation */
    List *narrowings;             /* Narrowing, scoped to a then block */
    List *stores;                 /* StoreEntry, program-level global stores */
    bool in_function;
    size_t loop_depth;
    const char *function_name;
    size_t depth;
    size_t generated_names;
    const TypeEnv *env; /* concrete substitutions while checking a generic body */
} CheckContext;

static bool check_expression(CheckContext *ctx, size_t ast_index,
                             size_t *out_typed);
static bool check_block(CheckContext *ctx, size_t ast_index,
                        size_t return_type, size_t *out_typed);
static bool resolve_type_in_env(CheckContext *ctx, size_t ast_index,
                                const TypeEnv *env, const char *owner,
                                size_t *out_index);
static bool check_constructor(CheckContext *ctx, const AstNode *node,
                              const TypeEntry *entry, size_t ast_index);
static bool check_generic_call(CheckContext *ctx, const AstNode *node,
                               size_t function_index, size_t ast_index);
static bool typed_block_returns(const TypedProgram *program, size_t index);
static bool same_type(const TypedProgram *program, size_t a, size_t b);

static size_t *mapping_slot(List *mapping, size_t index)
{
    return (size_t *)list_get(mapping, index);
}

static size_t find_type_index(const List *types, const char *name)
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

static const TypeEntry *find_type(const List *types, const char *name)
{
    size_t index = find_type_index(types, name);

    return index == TYPED_NO_NODE ? NULL : list_get(types, index);
}

static const FunctionEntry *find_function(const List *functions,
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

static size_t find_function_index(const List *functions, const char *name)
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

static const VariableEntry *find_variable(const List *variables,
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

static const StoreEntry *find_store(const List *stores, const char *name)
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

static bool is_sized_type(const TypedProgram *program, size_t type_index)
{
    const TypedType *type = typed_program_type(program, type_index);

    if (type == NULL) {
        return false;
    }
    return type->kind == NIPPY_TYPE_INT32 || type->kind == NIPPY_TYPE_BOOL ||
           type->kind == NIPPY_TYPE_BYTE || type->kind == NIPPY_TYPE_NIL ||
           type->kind == NIPPY_TYPE_STRUCT ||
           type->kind == NIPPY_TYPE_HANDLE;
}

static bool handle_type(CheckContext *ctx, size_t element, size_t *out_index)
{
    size_t i;

    for (i = 0; i < ctx->program->types.length; i++) {
        const TypedType *type = typed_program_type(ctx->program, i);

        if (type == NULL || type->kind != NIPPY_TYPE_HANDLE ||
            type->child_count != 1) {
            continue;
        }
        {
            size_t existing = *(const size_t *)list_get(
                &ctx->program->type_children, type->first_child);

            if (same_type(ctx->program, existing, element)) {
                *out_index = i;
                return true;
            }
        }
    }
    {
        TypedType type = {0};
        size_t first = ctx->program->type_children.length;

        if (!typed_program_add_type_child(ctx->program, element)) {
            error_report(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
            return false;
        }
        type.kind = NIPPY_TYPE_HANDLE;
        type.name_index = TYPED_NO_NODE;
        type.signature_index = TYPED_NO_NODE;
        type.first_child = first;
        type.child_count = 1;
        type.declaration_node = TYPED_NO_NODE;
        type.source_name_index = TYPED_NO_NODE;
        if (!typed_program_add_type(ctx->program, &type, out_index)) {
            error_report(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
            return false;
        }
        return true;
    }
}

static bool add_type_record(CheckContext *ctx, NippyType kind,
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
    if (!typed_program_add_type(ctx->program, &type, out_index)) {
        error_report(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
        return false;
    }
    return true;
}

/* Builtins are interned so a program does not accumulate one Int32 record per
   literal. */
static bool builtin_type(CheckContext *ctx, NippyType kind, size_t *out_index)
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

static bool add_function_type(CheckContext *ctx, const List *parameter_types,
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

        if (!typed_program_add_signature_parameter(ctx->program,
                                                   parameter_type)) {
            error_report(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
            return false;
        }
    }
    if (!typed_program_add_signature(ctx->program, &signature,
                                     &signature_index)) {
        error_report(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
        return false;
    }
    return add_type_record(ctx, NIPPY_TYPE_FUNCTION, TYPED_NO_NODE,
                           signature_index, out_index);
}

static bool same_type_depth(const TypedProgram *program, size_t a, size_t b,
                            unsigned depth)
{
    const TypedType *left;
    const TypedType *right;
    size_t i;

    if (a == b) {
        return true;
    }
    if (depth == 0) {
        return false;
    }
    left = typed_program_type(program, a);
    right = typed_program_type(program, b);
    if (left == NULL || right == NULL || left->kind != right->kind) {
        return false;
    }
    if (left->kind == NIPPY_TYPE_INT32 || left->kind == NIPPY_TYPE_BOOL ||
        left->kind == NIPPY_TYPE_BYTE || left->kind == NIPPY_TYPE_NIL ||
        left->kind == NIPPY_TYPE_STORE) {
        return true;
    }
    if (left->kind == NIPPY_TYPE_HANDLE) {
        if (left->child_count != 1 || right->child_count != 1) {
            return false;
        }
        {
            size_t left_element = *(const size_t *)list_get(
                &program->type_children, left->first_child);
            size_t right_element = *(const size_t *)list_get(
                &program->type_children, right->first_child);

            return same_type_depth(program, left_element, right_element,
                                   depth - 1);
        }
    }
    if (left->kind == NIPPY_TYPE_STRUCT) {
        if (left->name_index != right->name_index ||
            left->child_count != right->child_count) {
            return false;
        }
        for (i = 0; i < left->child_count; i++) {
            size_t left_arg = *(const size_t *)list_get(
                &program->type_children, left->first_child + i);
            size_t right_arg = *(const size_t *)list_get(
                &program->type_children, right->first_child + i);

            if (!same_type_depth(program, left_arg, right_arg, depth - 1)) {
                return false;
            }
        }
        return true;
    }
    if (left->kind == NIPPY_TYPE_UNION) {
        if (left->child_count != right->child_count) {
            return false;
        }
        for (i = 0; i < left->child_count; i++) {
            size_t left_alt = *(const size_t *)list_get(
                &program->type_children, left->first_child + i);
            size_t right_alt = *(const size_t *)list_get(
                &program->type_children, right->first_child + i);

            if (!same_type_depth(program, left_alt, right_alt, depth - 1)) {
                return false;
            }
        }
        return true;
    }
    if (left->kind == NIPPY_TYPE_FUNCTION) {
        const TypedFunctionSignature *left_signature =
            typed_program_signature(program, left->signature_index);
        const TypedFunctionSignature *right_signature =
            typed_program_signature(program, right->signature_index);

        if (left_signature == NULL || right_signature == NULL ||
            left_signature->parameter_count !=
                right_signature->parameter_count ||
            !same_type_depth(program, left_signature->return_type,
                             right_signature->return_type, depth - 1)) {
            return false;
        }
        for (i = 0; i < left_signature->parameter_count; i++) {
            size_t left_parameter = *(const size_t *)list_get(
                &program->signature_parameters,
                left_signature->first_parameter + i);
            size_t right_parameter = *(const size_t *)list_get(
                &program->signature_parameters,
                right_signature->first_parameter + i);

            if (!same_type_depth(program, left_parameter, right_parameter,
                                 depth - 1)) {
                return false;
            }
        }
        return true;
    }
    return false;
}

static bool same_type(const TypedProgram *program, size_t a, size_t b)
{
    return same_type_depth(program, a, b, NIPPY_TYPE_DEPTH);
}

static int type_rank(NippyType kind)
{
    switch (kind) {
    case NIPPY_TYPE_INT32:
        return 0;
    case NIPPY_TYPE_BOOL:
        return 1;
    case NIPPY_TYPE_NIL:
        return 2;
    case NIPPY_TYPE_STRUCT:
        return 3;
    case NIPPY_TYPE_UNION:
        return 4;
    case NIPPY_TYPE_FUNCTION:
        return 5;
    case NIPPY_TYPE_BYTE:
        return 6;
    case NIPPY_TYPE_STORE:
        return 7;
    case NIPPY_TYPE_HANDLE:
        return 8;
    default:
        return 9;
    }
}

/* A total order over resolved types. Union alternatives are sorted with it so
   two spellings of the same union intern to one record. */
static int type_compare_depth(const TypedProgram *program, size_t a, size_t b,
                              unsigned depth)
{
    const TypedType *left = typed_program_type(program, a);
    const TypedType *right = typed_program_type(program, b);
    int left_rank;
    int right_rank;
    size_t i;

    if (a == b) {
        return 0;
    }
    if (left == NULL || right == NULL) {
        return left == NULL ? -1 : 1;
    }
    left_rank = type_rank(left->kind);
    right_rank = type_rank(right->kind);
    if (left_rank != right_rank) {
        return left_rank < right_rank ? -1 : 1;
    }
    if (left->kind == NIPPY_TYPE_STRUCT) {
        const char *left_name = typed_program_symbol_text(program,
                                                          left->name_index);
        const char *right_name = typed_program_symbol_text(program,
                                                           right->name_index);
        int by_name = strcmp(left_name == NULL ? "" : left_name,
                             right_name == NULL ? "" : right_name);

        if (by_name != 0) {
            return by_name;
        }
        if (left->child_count != right->child_count) {
            return left->child_count < right->child_count ? -1 : 1;
        }
        for (i = 0; i < left->child_count && depth > 0; i++) {
            size_t left_arg = *(const size_t *)list_get(
                &program->type_children, left->first_child + i);
            size_t right_arg = *(const size_t *)list_get(
                &program->type_children, right->first_child + i);
            int by_arg = type_compare_depth(program, left_arg, right_arg,
                                            depth - 1);

            if (by_arg != 0) {
                return by_arg;
            }
        }
        return 0;
    }
    if (left->kind == NIPPY_TYPE_UNION) {
        if (left->child_count != right->child_count) {
            return left->child_count < right->child_count ? -1 : 1;
        }
        for (i = 0; i < left->child_count && depth > 0; i++) {
            size_t left_alt = *(const size_t *)list_get(
                &program->type_children, left->first_child + i);
            size_t right_alt = *(const size_t *)list_get(
                &program->type_children, right->first_child + i);
            int by_alt = type_compare_depth(program, left_alt, right_alt,
                                            depth - 1);

            if (by_alt != 0) {
                return by_alt;
            }
        }
        return 0;
    }
    if (left->kind == NIPPY_TYPE_HANDLE) {
        size_t left_element;
        size_t right_element;

        if (left->child_count != 1 || right->child_count != 1) {
            return left->child_count < right->child_count ? -1 : 1;
        }
        left_element = *(const size_t *)list_get(&program->type_children,
                                                 left->first_child);
        right_element = *(const size_t *)list_get(&program->type_children,
                                                  right->first_child);
        return type_compare_depth(program, left_element, right_element,
                                  depth - 1);
    }
    if (left->kind == NIPPY_TYPE_FUNCTION) {
        const TypedFunctionSignature *ls =
            typed_program_signature(program, left->signature_index);
        const TypedFunctionSignature *rs =
            typed_program_signature(program, right->signature_index);
        int by_return;

        if (ls == NULL || rs == NULL) {
            return ls == NULL ? -1 : 1;
        }
        if (ls->parameter_count != rs->parameter_count) {
            return ls->parameter_count < rs->parameter_count ? -1 : 1;
        }
        for (i = 0; i < ls->parameter_count && depth > 0; i++) {
            size_t lp = *(const size_t *)list_get(
                &program->signature_parameters, ls->first_parameter + i);
            size_t rp = *(const size_t *)list_get(
                &program->signature_parameters, rs->first_parameter + i);
            int by_parameter = type_compare_depth(program, lp, rp, depth - 1);

            if (by_parameter != 0) {
                return by_parameter;
            }
        }
        by_return = type_compare_depth(program, ls->return_type,
                                       rs->return_type, depth - 1);
        return by_return;
    }
    return 0;
}

static int type_compare(const TypedProgram *program, size_t a, size_t b)
{
    return type_compare_depth(program, a, b, NIPPY_TYPE_DEPTH);
}

static bool type_in_list(const TypedProgram *program, const List *indices,
                         size_t type_index)
{
    size_t i;

    for (i = 0; i < indices->length; i++) {
        size_t existing = *(const size_t *)list_get(indices, i);

        if (same_type(program, existing, type_index)) {
            return true;
        }
    }
    return false;
}

/* Flattened alternatives are deduplicated and sorted before interning. */
static bool intern_union(CheckContext *ctx, const List *alternatives,
                         size_t *out_index)
{
    List unique;
    size_t i;
    size_t *items;

    list_init(&unique, sizeof(size_t));
    for (i = 0; i < alternatives->length; i++) {
        size_t alternative = *(const size_t *)list_get(alternatives, i);
        const TypedType *type = typed_program_type(ctx->program, alternative);

        if (type == NULL) {
            error_report(ctx->errors, MESSAGE_CHECKER_UNSUPPORTED_AST_NODE);
            list_free(&unique);
            return false;
        }
        if (type->kind == NIPPY_TYPE_UNION) {
            size_t j;

            for (j = 0; j < type->child_count; j++) {
                size_t nested = *(const size_t *)list_get(
                    &ctx->program->type_children, type->first_child + j);

                if (!type_in_list(ctx->program, &unique, nested) &&
                    !list_push(&unique, &nested)) {
                    error_report(ctx->errors,
                                 MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
                    list_free(&unique);
                    return false;
                }
            }
        } else if (!type_in_list(ctx->program, &unique, alternative) &&
                   !list_push(&unique, &alternative)) {
            error_report(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
            list_free(&unique);
            return false;
        }
    }
    if (unique.length == 0) {
        list_free(&unique);
        error_report(ctx->errors, MESSAGE_CHECKER_UNSUPPORTED_AST_NODE);
        return false;
    }
    if (unique.length == 1) {
        *out_index = *(const size_t *)list_get(&unique, 0);
        list_free(&unique);
        return true;
    }

    items = nippy_malloc(unique.length * sizeof(size_t));
    if (items == NULL) {
        list_free(&unique);
        error_report(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
        return false;
    }
    for (i = 0; i < unique.length; i++) {
        items[i] = *(const size_t *)list_get(&unique, i);
    }
    for (i = 1; i < unique.length; i++) {
        size_t key = items[i];
        size_t j = i;

        while (j > 0 && type_compare(ctx->program, items[j - 1], key) > 0) {
            items[j] = items[j - 1];
            j--;
        }
        items[j] = key;
    }

    /* Reuse an existing union with the same canonical alternatives. */
    for (i = 0; i < ctx->program->types.length; i++) {
        const TypedType *type = typed_program_type(ctx->program, i);
        size_t j;
        bool match = true;

        if (type == NULL || type->kind != NIPPY_TYPE_UNION ||
            type->child_count != unique.length) {
            continue;
        }
        for (j = 0; j < unique.length; j++) {
            size_t existing = *(const size_t *)list_get(
                &ctx->program->type_children, type->first_child + j);

            if (!same_type(ctx->program, existing, items[j])) {
                match = false;
                break;
            }
        }
        if (match) {
            *out_index = i;
            free(items);
            list_free(&unique);
            return true;
        }
    }

    {
        TypedType type = {0};
        size_t first = ctx->program->type_children.length;

        for (i = 0; i < unique.length; i++) {
            if (!typed_program_add_type_child(ctx->program, items[i])) {
                free(items);
                list_free(&unique);
                error_report(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
                return false;
            }
        }
        type.kind = NIPPY_TYPE_UNION;
        type.name_index = TYPED_NO_NODE;
        type.signature_index = TYPED_NO_NODE;
        type.first_child = first;
        type.child_count = unique.length;
        type.declaration_node = TYPED_NO_NODE;
        type.source_name_index = TYPED_NO_NODE;
        if (!typed_program_add_type(ctx->program, &type, out_index)) {
            free(items);
            list_free(&unique);
            error_report(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
            return false;
        }
    }
    free(items);
    list_free(&unique);
    return true;
}

static bool union_has_alternative(const TypedProgram *program,
                                  const TypedType *union_type,
                                  size_t wanted, size_t *out_tag)
{
    size_t i;

    for (i = 0; i < union_type->child_count; i++) {
        size_t alternative = *(const size_t *)list_get(
            &program->type_children, union_type->first_child + i);

        if (same_type(program, alternative, wanted)) {
            if (out_tag != NULL) {
                *out_tag = i;
            }
            return true;
        }
    }
    return false;
}

static size_t type_parameter_index(
    char parameters[][NIPPY_IDENTIFIER_MAX], size_t count, const char *name)
{
    size_t i;

    for (i = 0; i < count; i++) {
        if (strcmp(parameters[i], name) == 0) {
            return i;
        }
    }
    return TYPED_NO_NODE;
}

static bool env_lookup(const TypeEnv *env, const char *name, size_t *out)
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

static bool type_ast_text(const Ast *ast, size_t ast_index, char *buffer,
                          size_t size);

static void type_ast_append(const char *text, char *buffer, size_t size,
                            size_t *used)
{
    size_t length = strlen(text);

    if (*used < size) {
        size_t available = size - *used - 1;

        if (length > available) {
            length = available;
        }
        memcpy(buffer + *used, text, length);
        *used += length;
        buffer[*used] = '\0';
    }
}

static bool type_ast_text(const Ast *ast, size_t ast_index, char *buffer,
                          size_t size)
{
    const AstNode *node = ast_get_node(ast, ast_index);
    size_t used = 0;

    buffer[0] = '\0';
    if (node == NULL) {
        type_ast_append("value", buffer, size, &used);
        return true;
    }
    if (node->kind == AST_TYPE_NAME) {
        const char *name = ast_symbol_text(ast, node->name_index);

        type_ast_append(name == NULL ? "value" : name, buffer, size, &used);
        return true;
    }
    if (node->kind == AST_TYPE_FUNCTION) {
        type_ast_append("Fun", buffer, size, &used);
        return true;
    }
    if (node->kind == AST_TYPE_APPLICATION) {
        const char *name = ast_symbol_text(ast, node->name_index);
        size_t i;

        type_ast_append(name == NULL ? "value" : name, buffer, size, &used);
        type_ast_append("<", buffer, size, &used);
        for (i = 0; i < node->children_count; i++) {
            size_t child = *(const size_t *)list_get(
                &ast->children, node->children_start + i);
            char child_text[128];

            if (i > 0) {
                type_ast_append(", ", buffer, size, &used);
            }
            type_ast_text(ast, child, child_text, sizeof(child_text));
            type_ast_append(child_text, buffer, size, &used);
        }
        type_ast_append(">", buffer, size, &used);
        return true;
    }
    if (node->kind == AST_TYPE_UNION) {
        size_t i;

        for (i = 0; i < node->children_count; i++) {
            size_t child = *(const size_t *)list_get(
                &ast->children, node->children_start + i);
            char child_text[128];

            if (i > 0) {
                type_ast_append(" | ", buffer, size, &used);
            }
            type_ast_text(ast, child, child_text, sizeof(child_text));
            type_ast_append(child_text, buffer, size, &used);
        }
        return true;
    }
    type_ast_append("value", buffer, size, &used);
    return true;
}

static bool instantiate_struct(CheckContext *ctx, size_t entry_index,
                               const size_t *arguments, size_t argument_count,
                               size_t *out_type)
{
    const TypeEntry *entry = list_get(ctx->types, entry_index);
    size_t i;

    if (entry == NULL) {
        error_report(ctx->errors, MESSAGE_CHECKER_UNSUPPORTED_AST_NODE);
        return false;
    }
    for (i = 0; i < ctx->struct_instantiations->length; i++) {
        StructInstantiation *record =
            (StructInstantiation *)list_get(ctx->struct_instantiations, i);

        if (record->entry == entry_index &&
            record->argument_count == argument_count) {
            size_t j;
            bool same = true;

            for (j = 0; j < argument_count; j++) {
                if (!same_type(ctx->program, record->arguments[j],
                               arguments[j])) {
                    same = false;
                    break;
                }
            }
            if (same) {
                if (record->in_progress) {
                    error_report(ctx->errors, MESSAGE_CHECKER_RECURSIVE_TYPE,
                                 entry->name);
                    return false;
                }
                *out_type = record->type_index;
                return true;
            }
        }
    }

    {
        StructInstantiation record = {0};
        TypeEnv env = {0};
        size_t record_index;
        size_t first_argument;
        size_t declaration_index;
        size_t type_index;
        size_t typed_fields_start = ctx->program->children.length;
        const AstNode *declaration;
        char generated[32];
        size_t name_index;

        if (argument_count != entry->parameter_count) {
            error_report(ctx->errors, MESSAGE_CHECKER_TYPE_ARGUMENT_COUNT,
                         entry->name, entry->parameter_count, argument_count);
            return false;
        }
        record.entry = entry_index;
        record.argument_count = argument_count;
        record.in_progress = true;
        for (i = 0; i < argument_count; i++) {
            record.arguments[i] = arguments[i];
            env.names[i] = entry->parameters[i];
            env.types[i] = arguments[i];
        }
        env.count = argument_count;
        if (!list_push(ctx->struct_instantiations, &record)) {
            error_report(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
            return false;
        }
        record_index = ctx->struct_instantiations->length - 1;

        snprintf(generated, sizeof(generated), "nippy_gen_%zu",
                 ctx->generated_names++);
        name_index = typed_program_copy_symbol(ctx->program, generated,
                                               strlen(generated));
        if (name_index == TYPED_NO_NODE) {
            error_report(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_COPYING_NAME);
            return false;
        }

        declaration = ast_get_node(ctx->ast, entry->ast_index);
        if (declaration == NULL ||
            declaration->kind != AST_TYPE_DECLARATION) {
            error_report(ctx->errors, MESSAGE_CHECKER_UNSUPPORTED_AST_NODE);
            return false;
        }
        for (i = 0; i < declaration->children_count; i++) {
            size_t field_ast = *(const size_t *)list_get(
                &ctx->ast->children, declaration->children_start + i);
            const AstNode *field = ast_get_node(ctx->ast, field_ast);
            const char *field_name;
            size_t field_type;
            TypedNode typed_field = {0};
            size_t field_index;

            if (field == NULL || field->kind != AST_FIELD) {
                error_report(ctx->errors, MESSAGE_CHECKER_MISSING_AST_NODE,
                             field_ast);
                return false;
            }
            field_name = ast_symbol_text(ctx->ast, field->name_index);
            if (!resolve_type_in_env(ctx, field->left_child, &env, field_name,
                                     &field_type)) {
                return false;
            }
            {
                const TypedType *resolved =
                    typed_program_type(ctx->program, field_type);

                if (resolved != NULL &&
                    (resolved->kind == NIPPY_TYPE_FUNCTION ||
                     resolved->kind == NIPPY_TYPE_STORE)) {
                    error_report_at(ctx->errors, field->pos,
                                    MESSAGE_CHECKER_FIELD_TYPE, field_name);
                    return false;
                }
            }
            typed_field.kind = TYPED_FIELD;
            typed_field.type_index = field_type;
            typed_field.pos = field->pos;
            typed_field.name_index = typed_program_copy_symbol(
                ctx->program, field_name, strlen(field_name));
            if (typed_field.name_index == TYPED_NO_NODE) {
                error_report(ctx->errors,
                             MESSAGE_CHECKER_OUT_OF_MEMORY_COPYING_NAME);
                return false;
            }
            typed_field.left_child = TYPED_NO_NODE;
            typed_field.right_child = TYPED_NO_NODE;
            typed_field.children_start = 0;
            typed_field.children_count = 0;
            if (!typed_program_push(ctx->program, &typed_field, &field_index) ||
                !typed_program_push_child(ctx->program, field_index)) {
                error_report(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_PROGRAM);
                return false;
            }
        }

        {
            TypedNode typed_declaration = {0};

            typed_declaration.kind = TYPED_TYPE_DECLARATION;
            typed_declaration.type_index = TYPED_NO_NODE;
            typed_declaration.pos = declaration->pos;
            typed_declaration.name_index = name_index;
            typed_declaration.left_child = TYPED_NO_NODE;
            typed_declaration.right_child = TYPED_NO_NODE;
            typed_declaration.children_start = typed_fields_start;
            typed_declaration.children_count = declaration->children_count;
            if (!typed_program_push(ctx->program, &typed_declaration,
                                    &declaration_index)) {
                error_report(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_PROGRAM);
                return false;
            }
        }

        first_argument = ctx->program->type_children.length;
        for (i = 0; i < argument_count; i++) {
            if (!typed_program_add_type_child(ctx->program, arguments[i])) {
                error_report(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
                return false;
            }
        }

        {
            TypedType type = {0};
            size_t source_name = typed_program_copy_symbol(
                ctx->program, entry->name, strlen(entry->name));

            if (source_name == TYPED_NO_NODE) {
                error_report(ctx->errors,
                             MESSAGE_CHECKER_OUT_OF_MEMORY_COPYING_NAME);
                return false;
            }
            type.kind = NIPPY_TYPE_STRUCT;
            type.name_index = name_index;
            type.signature_index = TYPED_NO_NODE;
            type.first_child = first_argument;
            type.child_count = argument_count;
            type.declaration_node = declaration_index;
            type.source_name_index = source_name;
            if (!typed_program_add_type(ctx->program, &type, &type_index)) {
                error_report(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
                return false;
            }
        }

        {
            StructInstantiation *record =
                (StructInstantiation *)list_get(ctx->struct_instantiations,
                                                record_index);
            record->type_index = type_index;
            record->in_progress = false;
        }
        *out_type = type_index;
        return true;
    }
}

static bool expand_alias(CheckContext *ctx, const TypeEntry *entry,
                         const size_t *arguments, size_t argument_count,
                         size_t *out_type)
{
    TypeEnv env = {0};
    const AstNode *alias;
    size_t i;

    if (argument_count != entry->parameter_count) {
        error_report(ctx->errors, MESSAGE_CHECKER_TYPE_ARGUMENT_COUNT,
                     entry->name, entry->parameter_count, argument_count);
        return false;
    }
    alias = ast_get_node(ctx->ast, entry->ast_index);
    if (alias == NULL || alias->kind != AST_TYPE_ALIAS) {
        error_report(ctx->errors, MESSAGE_CHECKER_UNSUPPORTED_AST_NODE);
        return false;
    }
    for (i = 0; i < argument_count; i++) {
        env.names[i] = entry->parameters[i];
        env.types[i] = arguments[i];
    }
    env.count = argument_count;
    if (ctx->depth >= NIPPY_TYPE_DEPTH) {
        error_report(ctx->errors, MESSAGE_CHECKER_RECURSIVE_TYPE, entry->name);
        return false;
    }
    ctx->depth++;
    if (!resolve_type_in_env(ctx, alias->right_child, &env, entry->name,
                             out_type)) {
        ctx->depth--;
        return false;
    }
    ctx->depth--;
    return true;
}

static bool resolve_type_name(CheckContext *ctx, const AstNode *node,
                              const TypeEnv *env, const char *owner,
                              size_t *out_index)
{
    const char *name = ast_symbol_text(ctx->ast, node->name_index);
    size_t entry_index;

    if (name == NULL) {
        error_report_at(ctx->errors, node->pos,
                        MESSAGE_CHECKER_MISSING_NAME_OR_TYPE);
        return false;
    }
    if (env != NULL && env_lookup(env, name, out_index)) {
        return true;
    }
    if (strcmp(name, "Int32") == 0) {
        return builtin_type(ctx, NIPPY_TYPE_INT32, out_index);
    }
    if (strcmp(name, "Bool") == 0) {
        return builtin_type(ctx, NIPPY_TYPE_BOOL, out_index);
    }
    if (strcmp(name, "Nil") == 0) {
        return builtin_type(ctx, NIPPY_TYPE_NIL, out_index);
    }
    if (strcmp(name, "Byte") == 0) {
        return builtin_type(ctx, NIPPY_TYPE_BYTE, out_index);
    }
    if (strcmp(name, "Store") == 0) {
        return builtin_type(ctx, NIPPY_TYPE_STORE, out_index);
    }
    if (strcmp(name, "Handle") == 0) {
        error_report_at(ctx->errors, node->pos,
                        MESSAGE_CHECKER_MISSING_TYPE_ARGUMENTS, name);
        return false;
    }
    entry_index = find_type_index(ctx->types, name);
    if (entry_index == TYPED_NO_NODE) {
        error_report_at(ctx->errors, node->pos, MESSAGE_CHECKER_UNKNOWN_TYPE,
                        name, owner == NULL ? "value" : owner);
        return false;
    }
    {
        const TypeEntry *entry = list_get(ctx->types, entry_index);

        if (entry->kind == TYPE_ENTRY_ALIAS) {
            if (entry->parameter_count != 0) {
                error_report_at(ctx->errors, node->pos,
                                MESSAGE_CHECKER_MISSING_TYPE_ARGUMENTS, name);
                return false;
            }
            if (entry->type_index != TYPED_NO_NODE) {
                *out_index = entry->type_index;
                return true;
            }
            return expand_alias(ctx, entry, NULL, 0, out_index);
        }
        if (entry->parameter_count != 0) {
            error_report_at(ctx->errors, node->pos,
                            MESSAGE_CHECKER_MISSING_TYPE_ARGUMENTS, name);
            return false;
        }
        if (entry->type_index == TYPED_NO_NODE) {
            return instantiate_struct(ctx, entry_index, NULL, 0, out_index);
        }
        *out_index = entry->type_index;
        return true;
    }
}

static bool resolve_type_application(CheckContext *ctx, const AstNode *node,
                                     const TypeEnv *env, const char *owner,
                                     size_t *out_index)
{
    const char *name = ast_symbol_text(ctx->ast, node->name_index);
    size_t entry_index;
    List arguments;
    size_t i;
    bool ok = false;

    if (name == NULL) {
        error_report_at(ctx->errors, node->pos,
                        MESSAGE_CHECKER_MISSING_NAME_OR_TYPE);
        return false;
    }
    if (strcmp(name, "Handle") == 0) {
        size_t element;

        if (node->children_count != 1) {
            error_report_at(ctx->errors, node->pos,
                            MESSAGE_CHECKER_HANDLE_ELEMENT);
            return false;
        }
        {
            size_t child = *(const size_t *)list_get(
                &ctx->ast->children, node->children_start);

            if (!resolve_type_in_env(ctx, child, env, owner, &element)) {
                return false;
            }
        }
        if (!is_sized_type(ctx->program, element)) {
            error_report_at(ctx->errors, node->pos,
                            MESSAGE_CHECKER_HANDLE_ELEMENT);
            return false;
        }
        return handle_type(ctx, element, out_index);
    }
    if (strcmp(name, "Int32") == 0 || strcmp(name, "Bool") == 0 ||
        strcmp(name, "Nil") == 0 || strcmp(name, "Byte") == 0 ||
        strcmp(name, "Store") == 0) {
        error_report_at(ctx->errors, node->pos,
                        MESSAGE_CHECKER_TYPE_NOT_GENERIC, name);
        return false;
    }
    entry_index = find_type_index(ctx->types, name);
    if (entry_index == TYPED_NO_NODE) {
        error_report_at(ctx->errors, node->pos, MESSAGE_CHECKER_UNKNOWN_TYPE,
                        name, owner == NULL ? "value" : owner);
        return false;
    }

    list_init(&arguments, sizeof(size_t));
    for (i = 0; i < node->children_count; i++) {
        size_t child = *(const size_t *)list_get(&ctx->ast->children,
                                                 node->children_start + i);
        size_t argument;

        if (!resolve_type_in_env(ctx, child, env, owner, &argument) ||
            !list_push(&arguments, &argument)) {
            if (!ctx->errors->reported) {
                error_report(ctx->errors,
                             MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
            }
            list_free(&arguments);
            return false;
        }
    }

    {
        const TypeEntry *entry = list_get(ctx->types, entry_index);

        if (entry->parameter_count == 0) {
            error_report_at(ctx->errors, node->pos,
                            MESSAGE_CHECKER_TYPE_NOT_GENERIC, name);
        } else if (entry->kind == TYPE_ENTRY_ALIAS) {
            ok = expand_alias(ctx, entry,
                              (const size_t *)arguments.items,
                              arguments.length, out_index);
        } else {
            ok = instantiate_struct(ctx, entry_index,
                                    (const size_t *)arguments.items,
                                    arguments.length, out_index);
        }
    }
    list_free(&arguments);
    return ok;
}

static bool resolve_type_in_env(CheckContext *ctx, size_t ast_index,
                                const TypeEnv *env, const char *owner,
                                size_t *out_index)
{
    const AstNode *node = ast_get_node(ctx->ast, ast_index);
    size_t i;

    if (node == NULL) {
        error_report(ctx->errors, MESSAGE_CHECKER_MISSING_AST_NODE, ast_index);
        return false;
    }
    if (node->kind == AST_TYPE_NAME) {
        return resolve_type_name(ctx, node, env, owner, out_index);
    }
    if (node->kind == AST_TYPE_APPLICATION) {
        return resolve_type_application(ctx, node, env, owner, out_index);
    }
    if (node->kind == AST_TYPE_UNION) {
        List alternatives;

        list_init(&alternatives, sizeof(size_t));
        for (i = 0; i < node->children_count; i++) {
            size_t child = *(const size_t *)list_get(
                &ctx->ast->children, node->children_start + i);
            size_t alternative;

            if (!resolve_type_in_env(ctx, child, env, owner, &alternative) ||
                !list_push(&alternatives, &alternative)) {
                if (!ctx->errors->reported) {
                    error_report(ctx->errors,
                                 MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
                }
                list_free(&alternatives);
                return false;
            }
            {
                const TypedType *resolved =
                    typed_program_type(ctx->program, alternative);

                if (resolved != NULL &&
                    (resolved->kind == NIPPY_TYPE_FUNCTION ||
                     resolved->kind == NIPPY_TYPE_STORE)) {
                    error_report_at(ctx->errors, node->pos,
                                    MESSAGE_CHECKER_UNION_FUNCTION);
                    list_free(&alternatives);
                    return false;
                }
            }
        }
        {
            bool ok = intern_union(ctx, &alternatives, out_index);
            list_free(&alternatives);
            return ok;
        }
    }
    if (node->kind == AST_TYPE_FUNCTION) {
        List parameter_types;
        size_t return_type;
        bool ok = false;

        list_init(&parameter_types, sizeof(size_t));
        for (i = 0; i < node->children_count; i++) {
            size_t child = *(const size_t *)list_get(
                &ctx->ast->children, node->children_start + i);
            size_t parameter_type;

            if (!resolve_type_in_env(ctx, child, env, owner,
                                     &parameter_type)) {
                goto function_type_done;
            }
            if (!list_push(&parameter_types, &parameter_type)) {
                error_report(ctx->errors,
                             MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
                goto function_type_done;
            }
        }
        if (!resolve_type_in_env(ctx, node->right_child, env, owner,
                                 &return_type)) {
            goto function_type_done;
        }
        ok = add_function_type(ctx, &parameter_types, return_type, out_index);

    function_type_done:
        list_free(&parameter_types);
        return ok;
    }

    error_report_at(ctx->errors, node->pos, MESSAGE_CHECKER_UNSUPPORTED_AST_NODE);
    return false;
}

static bool resolve_type_node(CheckContext *ctx, size_t ast_index,
                              const char *owner, size_t *out_index)
{
    return resolve_type_in_env(ctx, ast_index, ctx->env, owner, out_index);
}


static bool emit_node(CheckContext *ctx, const TypedNode *node,
                      size_t ast_index, size_t *out_index)
{
    if (!typed_program_push(ctx->program, node, out_index)) {
        error_report(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_PROGRAM);
        return false;
    }
    *mapping_slot(ctx->mapping, ast_index) = *out_index;
    return true;
}


static bool check_nil_literal(CheckContext *ctx, const AstNode *node,
                              size_t ast_index)
{
    size_t nil_type;
    TypedNode typed = {0};

    if (!builtin_type(ctx, NIPPY_TYPE_NIL, &nil_type)) {
        return false;
    }
    typed.kind = TYPED_NIL_LITERAL;
    typed.type_index = nil_type;
    typed.pos = node->pos;
    typed.name_index = TYPED_NO_NODE;
    typed.left_child = TYPED_NO_NODE;
    typed.right_child = TYPED_NO_NODE;
    return emit_node(ctx, &typed, ast_index, &(size_t){0});
}

/* A value of an alternative type becomes a union value at an assignment-like
   boundary. This is widening, not a general conversion. */
static bool push_injection(CheckContext *ctx, size_t value, size_t target,
                           size_t tag, size_t *out_value)
{
    const TypedNode *value_node = typed_program_node(ctx->program, value);
    TypedNode inject = {0};

    inject.kind = TYPED_SUM_INJECT;
    inject.type_index = target;
    inject.pos = value_node == NULL ? (SourcePos){0, 1, 1} : value_node->pos;
    inject.integer_value = (int)tag;
    inject.name_index = TYPED_NO_NODE;
    inject.left_child = value;
    inject.right_child = TYPED_NO_NODE;
    if (!typed_program_push(ctx->program, &inject, out_value)) {
        error_report(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_PROGRAM);
        return false;
    }
    return true;
}

static bool inject_into(CheckContext *ctx, size_t value, size_t target,
                        size_t *out_value)
{
    TypedNode *value_node =
        (TypedNode *)typed_program_node(ctx->program, value);
    const TypedType *target_type = typed_program_type(ctx->program, target);
    size_t tag;

    if (value_node == NULL || target_type == NULL) {
        return false;
    }
    if (same_type(ctx->program, value_node->type_index, target)) {
        *out_value = value;
        return true;
    }

    /* Integer literals are contextual Bytes. This is literal typing, not a
       conversion from an Int32 value. */
    if (target_type->kind == NIPPY_TYPE_BYTE) {
        if (value_node->kind != TYPED_INTEGER_LITERAL) {
            return false;
        }
        if (value_node->integer_value < 0 || value_node->integer_value > 255) {
            error_report_at(ctx->errors, value_node->pos,
                            MESSAGE_CHECKER_BYTE_RANGE,
                            value_node->integer_value);
            return false;
        }
        value_node->type_index = target;
        *out_value = value;
        return true;
    }

    if (target_type->kind != NIPPY_TYPE_UNION) {
        return false;
    }
    if (union_has_alternative(ctx->program, target_type,
                              value_node->type_index, &tag)) {
        return push_injection(ctx, value, target, tag, out_value);
    }
    if (value_node->kind == TYPED_INTEGER_LITERAL) {
        size_t byte_type;

        if (!builtin_type(ctx, NIPPY_TYPE_BYTE, &byte_type)) {
            return false;
        }
        if (union_has_alternative(ctx->program, target_type, byte_type,
                                  &tag)) {
            if (value_node->integer_value < 0 ||
                value_node->integer_value > 255) {
                error_report_at(ctx->errors, value_node->pos,
                                MESSAGE_CHECKER_BYTE_RANGE,
                                value_node->integer_value);
                return false;
            }
            value_node->type_index = byte_type;
            return push_injection(ctx, value, target, tag, out_value);
        }
    }
    return false;
}

static bool check_is(CheckContext *ctx, const AstNode *node, size_t ast_index)
{
    size_t value;
    size_t tested;
    const TypedNode *value_node;
    const TypedType *value_type;
    size_t tested_type;
    size_t bool_type;
    int result;
    TypedNode typed = {0};

    if (!check_expression(ctx, node->left_child, &value)) {
        return false;
    }
    if (!resolve_type_in_env(ctx, node->right_child, ctx->env, "is", &tested)) {
        return false;
    }
    tested_type = tested;
    {
        const TypedType *target = typed_program_type(ctx->program, tested_type);

        if (target != NULL && target->kind == NIPPY_TYPE_UNION) {
            error_report_at(ctx->errors, node->pos,
                            MESSAGE_CHECKER_IS_TYPE_IS_UNION);
            return false;
        }
    }
    value_node = typed_program_node(ctx->program, value);
    value_type = value_node == NULL
                     ? NULL
                     : typed_program_type(ctx->program, value_node->type_index);
    if (value_type == NULL) {
        error_report_at(ctx->errors, node->pos,
                        MESSAGE_CHECKER_UNSUPPORTED_AST_NODE);
        return false;
    }

    if (same_type(ctx->program, value_node->type_index, tested_type)) {
        result = 1;
    } else if (value_type->kind == NIPPY_TYPE_UNION) {
        size_t tag;

        if (union_has_alternative(ctx->program, value_type, tested_type, &tag)) {
            result = (int)tag;
        } else {
            result = 0;
        }
    } else {
        result = 0;
    }

    if (!builtin_type(ctx, NIPPY_TYPE_BOOL, &bool_type)) {
        return false;
    }
    typed.kind = TYPED_IS;
    typed.type_index = bool_type;
    typed.auxiliary_type = tested_type;
    typed.pos = node->pos;
    typed.integer_value = result;
    typed.name_index = TYPED_NO_NODE;
    typed.left_child = value;
    typed.right_child = TYPED_NO_NODE;
    return emit_node(ctx, &typed, ast_index, &(size_t){0});
}

static bool check_literal(CheckContext *ctx, const AstNode *node,
                          size_t ast_index)
{
    size_t int32_type;
    TypedNode typed = {0};

    if (!add_type_record(ctx, NIPPY_TYPE_INT32, TYPED_NO_NODE, TYPED_NO_NODE,
                         &int32_type)) {
        return false;
    }

    typed.kind = TYPED_INTEGER_LITERAL;
    typed.type_index = int32_type;
    typed.pos = node->pos;
    typed.integer_value = node->integer_value;
    typed.name_index = TYPED_NO_NODE;
    typed.left_child = TYPED_NO_NODE;
    typed.right_child = TYPED_NO_NODE;
    typed.children_start = 0;
    typed.children_count = 0;

    return emit_node(ctx, &typed, ast_index, &(size_t){0});
}

static bool check_identifier(CheckContext *ctx, const AstNode *node,
                             size_t ast_index)
{
    const char *name = ast_symbol_text(ctx->ast, node->name_index);
    const VariableEntry *variable;
    const FunctionEntry *function;
    size_t type_index;
    TypedNode typed = {0};

    if (name == NULL) {
        error_report_at(ctx->errors, node->pos,
                        MESSAGE_CHECKER_MISSING_NAME_OR_TYPE);
        return false;
    }
    if (find_store(ctx->stores, name) != NULL) {
        error_report_at(ctx->errors, node->pos,
                        MESSAGE_CHECKER_STORE_NOT_VALUE, name);
        return false;
    }

    variable = find_variable(ctx->locals, name);
    if (variable == NULL && !ctx->in_function) {
        variable = find_variable(ctx->globals, name);
    }
    if (variable != NULL) {
        type_index = variable->type_index;
    } else {
        function = find_function(ctx->functions, name);
        if (function != NULL) {
            if (function->generic) {
                error_report_at(ctx->errors, node->pos,
                                MESSAGE_CHECKER_GENERIC_AS_VALUE, name);
                return false;
            }
            type_index = function->type_index;
        } else if (ctx->in_function && find_variable(ctx->globals, name)) {
            error_report_at(ctx->errors, node->pos,
                            MESSAGE_CHECKER_GLOBAL_IN_FUNCTION, name);
            return false;
        } else if (find_type(ctx->types, name) != NULL ||
                   strcmp(name, "Int32") == 0 || strcmp(name, "Bool") == 0 ||
                   strcmp(name, "Nil") == 0) {
            error_report_at(ctx->errors, node->pos,
                            MESSAGE_CHECKER_TYPE_AS_VALUE, name);
            return false;
        } else {
            error_report_at(ctx->errors, node->pos,
                            MESSAGE_CHECKER_UNKNOWN_NAME, name);
            return false;
        }
    }

    if (variable != NULL) {
        size_t i;

        for (i = 0; i < ctx->narrowings->length; i++) {
            const Narrowing *narrowing =
                (const Narrowing *)list_get(ctx->narrowings, i);
            const TypedType *declared;

            if (narrowing == NULL || strcmp(narrowing->name, name) != 0) {
                continue;
            }
            declared = typed_program_type(ctx->program, variable->type_index);
            if (declared == NULL || declared->kind != NIPPY_TYPE_UNION) {
                continue;
            }
            {
                TypedNode base = {0};
                TypedNode payload = {0};
                size_t base_index;

                base.kind = TYPED_IDENTIFIER;
                base.type_index = variable->type_index;
                base.pos = node->pos;
                base.name_index = typed_program_copy_symbol(
                    ctx->program, name, strlen(name));
                if (base.name_index == TYPED_NO_NODE) {
                    error_report(ctx->errors,
                                 MESSAGE_CHECKER_OUT_OF_MEMORY_COPYING_NAME);
                    return false;
                }
                base.left_child = TYPED_NO_NODE;
                base.right_child = TYPED_NO_NODE;
                if (!typed_program_push(ctx->program, &base, &base_index)) {
                    error_report(ctx->errors,
                                 MESSAGE_CHECKER_OUT_OF_MEMORY_PROGRAM);
                    return false;
                }
                payload.kind = TYPED_SUM_PAYLOAD;
                payload.type_index = narrowing->type_index;
                payload.pos = node->pos;
                payload.name_index = TYPED_NO_NODE;
                payload.integer_value = (int)narrowing->tag;
                payload.left_child = base_index;
                payload.right_child = TYPED_NO_NODE;
                return emit_node(ctx, &payload, ast_index, &(size_t){0});
            }
        }
    }

    typed.kind = TYPED_IDENTIFIER;
    typed.type_index = type_index;
    typed.pos = node->pos;
    typed.name_index = typed_program_copy_symbol(ctx->program, name,
                                                 strlen(name));
    if (typed.name_index == TYPED_NO_NODE) {
        error_report(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_COPYING_NAME);
        return false;
    }
    typed.left_child = TYPED_NO_NODE;
    typed.right_child = TYPED_NO_NODE;
    typed.children_start = 0;
    typed.children_count = 0;

    return emit_node(ctx, &typed, ast_index, &(size_t){0});
}

static const char *operator_text(NippyOperator op)
{
    switch (op) {
    case NIPPY_OPERATOR_ADD:
        return "+";
    case NIPPY_OPERATOR_EQ:
        return "==";
    case NIPPY_OPERATOR_NE:
        return "!=";
    case NIPPY_OPERATOR_LT:
        return "<";
    case NIPPY_OPERATOR_LE:
        return "<=";
    case NIPPY_OPERATOR_GT:
        return ">";
    case NIPPY_OPERATOR_GE:
        return ">=";
    case NIPPY_OPERATOR_AND:
        return "&&";
    case NIPPY_OPERATOR_OR:
        return "||";
    case NIPPY_OPERATOR_NOT:
        return "!";
    default:
        return "?";
    }
}

static bool add_builtin_type(CheckContext *ctx, NippyType kind,
                             size_t *out_index)
{
    return add_type_record(ctx, kind, TYPED_NO_NODE, TYPED_NO_NODE, out_index);
}

static bool check_bool_literal(CheckContext *ctx, const AstNode *node,
                               size_t ast_index)
{
    size_t bool_type;
    TypedNode typed = {0};

    if (!add_builtin_type(ctx, NIPPY_TYPE_BOOL, &bool_type)) {
        return false;
    }
    typed.kind = TYPED_BOOL_LITERAL;
    typed.type_index = bool_type;
    typed.pos = node->pos;
    typed.integer_value = node->integer_value;
    typed.name_index = TYPED_NO_NODE;
    typed.left_child = TYPED_NO_NODE;
    typed.right_child = TYPED_NO_NODE;
    typed.children_start = 0;
    typed.children_count = 0;
    return emit_node(ctx, &typed, ast_index, &(size_t){0});
}

static bool check_binary(CheckContext *ctx, const AstNode *node,
                         size_t ast_index)
{
    size_t left;
    size_t right;
    size_t result_type;
    const TypedNode *left_node;
    const TypedNode *right_node;
    const TypedType *left_type;
    const TypedType *right_type;
    bool operands_ok = false;
    TypedNode typed = {0};

    if (!check_expression(ctx, node->left_child, &left) ||
        !check_expression(ctx, node->right_child, &right)) {
        return false;
    }
    left_node = typed_program_node(ctx->program, left);
    right_node = typed_program_node(ctx->program, right);
    if (left_node == NULL || right_node == NULL) {
        error_report(ctx->errors, MESSAGE_CHECKER_MISSING_AST_NODE,
                     node->left_child);
        return false;
    }
    left_type = typed_program_type(ctx->program, left_node->type_index);
    right_type = typed_program_type(ctx->program, right_node->type_index);
    if (left_type == NULL || right_type == NULL) {
        error_report_at(ctx->errors, node->pos,
                        MESSAGE_CHECKER_UNSUPPORTED_AST_NODE);
        return false;
    }

    /* A Byte can be compared with an integer literal: the literal is typed as
       a Byte in that context. A Byte variable is never implicitly compared with
       an Int32 variable. */
    if (node->op == NIPPY_OPERATOR_EQ || node->op == NIPPY_OPERATOR_NE) {
        if (left_type->kind == NIPPY_TYPE_BYTE &&
            right_node->kind == TYPED_INTEGER_LITERAL) {
            size_t coerced;

            if (!inject_into(ctx, right, left_node->type_index, &coerced)) {
                error_report_at(ctx->errors, node->pos,
                                MESSAGE_CHECKER_OPERATOR_TYPE,
                                operator_text(node->op));
                return false;
            }
            right = coerced;
            right_node = typed_program_node(ctx->program, right);
            right_type = typed_program_type(ctx->program,
                                            right_node->type_index);
        } else if (right_type->kind == NIPPY_TYPE_BYTE &&
                   left_node->kind == TYPED_INTEGER_LITERAL) {
            size_t coerced;

            if (!inject_into(ctx, left, right_node->type_index, &coerced)) {
                error_report_at(ctx->errors, node->pos,
                                MESSAGE_CHECKER_OPERATOR_TYPE,
                                operator_text(node->op));
                return false;
            }
            left = coerced;
            left_node = typed_program_node(ctx->program, left);
            left_type = typed_program_type(ctx->program,
                                           left_node->type_index);
        }
    }

    switch (node->op) {
    case NIPPY_OPERATOR_ADD:
        operands_ok = left_type->kind == NIPPY_TYPE_INT32 &&
                      right_type->kind == NIPPY_TYPE_INT32;
        result_type = NIPPY_TYPE_INT32;
        break;
    case NIPPY_OPERATOR_LT:
    case NIPPY_OPERATOR_LE:
    case NIPPY_OPERATOR_GT:
    case NIPPY_OPERATOR_GE:
        operands_ok = left_type->kind == NIPPY_TYPE_INT32 &&
                      right_type->kind == NIPPY_TYPE_INT32;
        result_type = NIPPY_TYPE_BOOL;
        break;
    case NIPPY_OPERATOR_EQ:
    case NIPPY_OPERATOR_NE:
        operands_ok = same_type(ctx->program, left_node->type_index,
                                right_node->type_index) &&
                      (left_type->kind == NIPPY_TYPE_INT32 ||
                       left_type->kind == NIPPY_TYPE_BOOL ||
                       left_type->kind == NIPPY_TYPE_BYTE);
        result_type = NIPPY_TYPE_BOOL;
        break;
    case NIPPY_OPERATOR_AND:
    case NIPPY_OPERATOR_OR:
        operands_ok = left_type->kind == NIPPY_TYPE_BOOL &&
                      right_type->kind == NIPPY_TYPE_BOOL;
        result_type = NIPPY_TYPE_BOOL;
        break;
    default:
        operands_ok = false;
        result_type = NIPPY_TYPE_UNKNOWN;
        break;
    }
    if (!operands_ok) {
        error_report_at(ctx->errors, node->pos, MESSAGE_CHECKER_OPERATOR_TYPE,
                        operator_text(node->op));
        return false;
    }

    {
        size_t result_index;

        if (!add_builtin_type(ctx, result_type, &result_index)) {
            return false;
        }
        typed.kind = TYPED_BINARY;
        typed.type_index = result_index;
        typed.pos = node->pos;
        typed.op = node->op;
        typed.name_index = TYPED_NO_NODE;
        typed.left_child = left;
        typed.right_child = right;
        typed.children_start = 0;
        typed.children_count = 0;
    }

    return emit_node(ctx, &typed, ast_index, &(size_t){0});
}

static bool check_unary(CheckContext *ctx, const AstNode *node,
                        size_t ast_index)
{
    size_t operand;
    const TypedNode *operand_node;
    const TypedType *operand_type;
    size_t bool_type;
    TypedNode typed = {0};

    if (node->op != NIPPY_OPERATOR_NOT) {
        error_report_at(ctx->errors, node->pos,
                        MESSAGE_CHECKER_UNSUPPORTED_AST_NODE);
        return false;
    }
    if (!check_expression(ctx, node->left_child, &operand)) {
        return false;
    }
    operand_node = typed_program_node(ctx->program, operand);
    operand_type = operand_node == NULL
                       ? NULL
                       : typed_program_type(ctx->program,
                                            operand_node->type_index);
    if (operand_type == NULL || operand_type->kind != NIPPY_TYPE_BOOL) {
        error_report_at(ctx->errors, node->pos, MESSAGE_CHECKER_OPERATOR_TYPE,
                        operator_text(node->op));
        return false;
    }
    if (!add_builtin_type(ctx, NIPPY_TYPE_BOOL, &bool_type)) {
        return false;
    }

    typed.kind = TYPED_UNARY;
    typed.type_index = bool_type;
    typed.pos = node->pos;
    typed.op = node->op;
    typed.name_index = TYPED_NO_NODE;
    typed.left_child = operand;
    typed.right_child = TYPED_NO_NODE;
    typed.children_start = 0;
    typed.children_count = 0;
    return emit_node(ctx, &typed, ast_index, &(size_t){0});
}

static const char *struct_field_name(const TypedProgram *program,
                                     const TypedNode *type_decl,
                                     size_t field_index)
{
    size_t child = *(const size_t *)list_get(
        &program->children, type_decl->children_start + field_index);
    const TypedNode *field = typed_program_node(program, child);

    return field == NULL ? NULL
                         : typed_program_symbol_text(program, field->name_index);
}

static size_t struct_field_type(const TypedProgram *program,
                                const TypedNode *type_decl,
                                size_t field_index)
{
    size_t child = *(const size_t *)list_get(
        &program->children, type_decl->children_start + field_index);
    const TypedNode *field = typed_program_node(program, child);

    return field == NULL ? TYPED_NO_NODE : field->type_index;
}

static size_t struct_declaration_node(const CheckContext *ctx,
                                    const TypeEntry *entry)
{
    const TypedType *type;

    if (entry == NULL || entry->type_index == TYPED_NO_NODE) {
        return TYPED_NO_NODE;
    }
    type = typed_program_type(ctx->program, entry->type_index);
    return type == NULL ? TYPED_NO_NODE : type->declaration_node;
}

typedef struct {
    const char *names[NIPPY_MAX_TYPE_PARAMETERS];
    size_t types[NIPPY_MAX_TYPE_PARAMETERS]; /* TYPED_NO_NODE until bound */
    size_t count;
} InferEnv;

static size_t infer_find(const InferEnv *infer, const char *name)
{
    size_t i;

    for (i = 0; i < infer->count; i++) {
        if (infer->names[i] != NULL && strcmp(infer->names[i], name) == 0) {
            return i;
        }
    }
    return TYPED_NO_NODE;
}

static void infer_type_env(const InferEnv *infer, TypeEnv *env)
{
    size_t i;

    env->count = 0;
    for (i = 0; i < infer->count; i++) {
        if (infer->types[i] != TYPED_NO_NODE) {
            env->names[env->count] = infer->names[i];
            env->types[env->count] = infer->types[i];
            env->count++;
        }
    }
}

/* Does this type expression still contain an unbound parameter? Resolving such
   an expression would report a misleading "unknown type", so inference refuses
   before it gets that far. */
static bool ast_type_has_unbound(CheckContext *ctx, size_t ast_index,
                                 const InferEnv *infer, unsigned depth)
{
    const AstNode *node = ast_get_node(ctx->ast, ast_index);
    size_t i;

    if (node == NULL || depth == 0) {
        return true;
    }
    if (node->kind == AST_TYPE_NAME) {
        const char *name = ast_symbol_text(ctx->ast, node->name_index);
        size_t index = name == NULL ? TYPED_NO_NODE : infer_find(infer, name);

        return index != TYPED_NO_NODE && infer->types[index] == TYPED_NO_NODE;
    }
    if (node->kind == AST_TYPE_UNION || node->kind == AST_TYPE_FUNCTION) {
        for (i = 0; i < node->children_count; i++) {
            size_t child = *(const size_t *)list_get(
                &ctx->ast->children, node->children_start + i);

            if (ast_type_has_unbound(ctx, child, infer, depth - 1)) {
                return true;
            }
        }
        if (node->kind == AST_TYPE_FUNCTION &&
            ast_type_has_unbound(ctx, node->right_child, infer, depth - 1)) {
            return true;
        }
        return false;
    }
    if (node->kind == AST_TYPE_APPLICATION) {
        const char *name = ast_symbol_text(ctx->ast, node->name_index);

        if (name != NULL && infer_find(infer, name) != TYPED_NO_NODE) {
            return true;
        }
        for (i = 0; i < node->children_count; i++) {
            size_t child = *(const size_t *)list_get(
                &ctx->ast->children, node->children_start + i);

            if (ast_type_has_unbound(ctx, child, infer, depth - 1)) {
                return true;
            }
        }
    }
    return false;
}

static bool infer_ast_type(CheckContext *ctx, size_t ast_type,
                           size_t concrete, InferEnv *infer,
                           const char *owner)
{
    const AstNode *node = ast_get_node(ctx->ast, ast_type);
    const TypedType *concrete_type = typed_program_type(ctx->program,
                                                        concrete);
    size_t i;

    if (node == NULL || concrete_type == NULL) {
        return false;
    }
    if (node->kind == AST_TYPE_NAME) {
        const char *name = ast_symbol_text(ctx->ast, node->name_index);
        size_t index = name == NULL ? TYPED_NO_NODE : infer_find(infer, name);

        if (index != TYPED_NO_NODE) {
            if (infer->types[index] == TYPED_NO_NODE) {
                infer->types[index] = concrete;
                return true;
            }
            return same_type(ctx->program, infer->types[index], concrete);
        }
        {
            TypeEnv env = {0};
            size_t expected;

            infer_type_env(infer, &env);
            if (!resolve_type_in_env(ctx, ast_type, &env, owner, &expected)) {
                return false;
            }
            return same_type(ctx->program, expected, concrete);
        }
    }
    if (node->kind == AST_TYPE_APPLICATION) {
        const char *name = ast_symbol_text(ctx->ast, node->name_index);
        const TypeEntry *entry;

        if (name == NULL || infer_find(infer, name) != TYPED_NO_NODE) {
            return false;
        }
        if (strcmp(name, "Handle") == 0) {
            size_t child;
            size_t element;

            if (concrete_type->kind != NIPPY_TYPE_HANDLE ||
                concrete_type->child_count != 1 ||
                node->children_count != 1) {
                return false;
            }
            child = *(const size_t *)list_get(&ctx->ast->children,
                                              node->children_start);
            element = *(const size_t *)list_get(&ctx->program->type_children,
                                                concrete_type->first_child);
            return infer_ast_type(ctx, child, element, infer, owner);
        }
        entry = find_type(ctx->types, name);
        if (entry == NULL) {
            return false;
        }
        if (entry->kind == TYPE_ENTRY_STRUCT) {
            const char *source_name =
                concrete_type->source_name_index == TYPED_NO_NODE
                    ? NULL
                    : typed_program_symbol_text(
                          ctx->program, concrete_type->source_name_index);

            if (concrete_type->kind != NIPPY_TYPE_STRUCT ||
                source_name == NULL ||
                strcmp(source_name, entry->name) != 0 ||
                concrete_type->child_count != node->children_count) {
                return false;
            }
            for (i = 0; i < node->children_count; i++) {
                size_t child = *(const size_t *)list_get(
                    &ctx->ast->children, node->children_start + i);
                size_t argument = *(const size_t *)list_get(
                    &ctx->program->type_children,
                    concrete_type->first_child + i);

                if (!infer_ast_type(ctx, child, argument, infer, owner)) {
                    return false;
                }
            }
            return true;
        }
        if (ast_type_has_unbound(ctx, ast_type, infer, NIPPY_TYPE_DEPTH)) {
            return false;
        }
        {
            TypeEnv env = {0};
            size_t expected;

            infer_type_env(infer, &env);
            if (!resolve_type_in_env(ctx, ast_type, &env, owner, &expected)) {
                return false;
            }
            return same_type(ctx->program, expected, concrete);
        }
    }
    if (node->kind == AST_TYPE_UNION) {
        if (ast_type_has_unbound(ctx, ast_type, infer, NIPPY_TYPE_DEPTH)) {
            return false;
        }
        {
            TypeEnv env = {0};
            size_t expected;

            infer_type_env(infer, &env);
            if (!resolve_type_in_env(ctx, ast_type, &env, owner, &expected)) {
                return false;
            }
            return same_type(ctx->program, expected, concrete);
        }
    }
    if (node->kind == AST_TYPE_FUNCTION) {
        const TypedFunctionSignature *signature;

        if (concrete_type->kind != NIPPY_TYPE_FUNCTION) {
            return false;
        }
        signature = typed_program_signature(ctx->program,
                                            concrete_type->signature_index);
        if (signature == NULL ||
            signature->parameter_count != node->children_count) {
            return false;
        }
        for (i = 0; i < node->children_count; i++) {
            size_t child = *(const size_t *)list_get(
                &ctx->ast->children, node->children_start + i);
            size_t parameter = *(const size_t *)list_get(
                &ctx->program->signature_parameters,
                signature->first_parameter + i);

            if (!infer_ast_type(ctx, child, parameter, infer, owner)) {
                return false;
            }
        }
        return infer_ast_type(ctx, node->right_child, signature->return_type,
                              infer, owner);
    }
    return false;
}

static const char *infer_first_unbound(const InferEnv *infer)
{
    size_t i;

    for (i = 0; i < infer->count; i++) {
        if (infer->types[i] == TYPED_NO_NODE) {
            return infer->names[i];
        }
    }
    return infer->count == 0 ? "?" : infer->names[0];
}

static bool infer_finish(CheckContext *ctx, InferEnv *infer, TypeEnv *env,
                         const char *owner)
{
    size_t i;

    env->count = 0;
    for (i = 0; i < infer->count; i++) {
        if (infer->types[i] == TYPED_NO_NODE) {
            error_report(ctx->errors, MESSAGE_CHECKER_CANNOT_INFER_TYPE,
                         infer->names[i], owner);
            return false;
        }
        env->names[env->count] = infer->names[i];
        env->types[env->count] = infer->types[i];
        env->count++;
    }
    return true;
}

static bool constructor_value_fits(CheckContext *ctx, size_t value,
                                   const TypeEntry *entry, size_t field_index,
                                   size_t argument_index, SourcePos pos,
                                   size_t *out_value)
{
    size_t field_type_index = struct_field_type(ctx->program,
                                                typed_program_node(
                                                    ctx->program,
                                                    struct_declaration_node(
                                                        ctx, entry)),
                                                field_index);

    if (!inject_into(ctx, value, field_type_index, out_value)) {
        error_report_at(ctx->errors, pos,
                        MESSAGE_CHECKER_CONSTRUCTOR_ARG_TYPE,
                        argument_index, entry->name);
        return false;
    }
    return true;
}

static const AstNode *template_field(const CheckContext *ctx,
                                     const AstNode *declaration, size_t index,
                                     size_t *out_ast)
{
    size_t child = *(const size_t *)list_get(&ctx->ast->children,
                                             declaration->children_start +
                                                 index);
    if (out_ast != NULL) {
        *out_ast = child;
    }
    return ast_get_node(ctx->ast, child);
}

static size_t template_field_index(const CheckContext *ctx,
                                   const AstNode *declaration,
                                   const char *name)
{
    size_t i;

    for (i = 0; i < declaration->children_count; i++) {
        const AstNode *field = template_field(ctx, declaration, i, NULL);
        const char *field_name;

        if (field == NULL) {
            continue;
        }
        field_name = ast_symbol_text(ctx->ast, field->name_index);
        if (field_name != NULL && strcmp(field_name, name) == 0) {
            return i;
        }
    }
    return TYPED_NO_NODE;
}

/* Constructing a generic struct infers its type arguments from the field types
   and the argument expressions, then hands the concrete declaration to the
   ordinary constructor checker. */
static bool check_generic_constructor(CheckContext *ctx, const AstNode *node,
                                      size_t entry_index, size_t ast_index)
{
    const TypeEntry *entry = list_get(ctx->types, entry_index);
    const AstNode *declaration = ast_get_node(ctx->ast, entry->ast_index);
    InferEnv infer = {0};
    TypeEnv env = {0};
    size_t arguments[NIPPY_MAX_TYPE_PARAMETERS];
    size_t type_index;
    TypeEntry concrete;
    size_t i;
    bool named = false;

    if (entry == NULL || declaration == NULL ||
        declaration->kind != AST_TYPE_DECLARATION) {
        error_report_at(ctx->errors, node->pos,
                        MESSAGE_CHECKER_UNSUPPORTED_AST_NODE);
        return false;
    }
    for (i = 0; i < entry->parameter_count; i++) {
        infer.names[i] = entry->parameters[i];
        infer.types[i] = TYPED_NO_NODE;
    }
    infer.count = entry->parameter_count;

    if (node->children_count > 0) {
        size_t first = *(const size_t *)list_get(&ctx->ast->children,
                                                 node->children_start);
        const AstNode *argument = ast_get_node(ctx->ast, first);

        named = argument != NULL && argument->kind == AST_NAMED_ARG;
    }

    if (!named) {
        if (node->children_count != declaration->children_count) {
            error_report_at(ctx->errors, node->pos,
                            MESSAGE_CHECKER_CONSTRUCTOR_ARG_COUNT,
                            entry->name, declaration->children_count,
                            node->children_count);
            return false;
        }
        for (i = 0; i < declaration->children_count; i++) {
            const AstNode *field = template_field(ctx, declaration, i, NULL);
            size_t argument_ast = *(const size_t *)list_get(
                &ctx->ast->children, node->children_start + i);
            size_t value;
            const TypedNode *value_node;

            if (field == NULL || !check_expression(ctx, argument_ast, &value)) {
                return false;
            }
            value_node = typed_program_node(ctx->program, value);
            if (value_node == NULL ||
                !infer_ast_type(ctx, field->left_child,
                                value_node->type_index, &infer,
                                entry->name)) {
                error_report_at(ctx->errors, node->pos,
                                MESSAGE_CHECKER_CANNOT_INFER_TYPE,
                                infer_first_unbound(&infer), entry->name);
                return false;
            }
        }
    } else {
        for (i = 0; i < node->children_count; i++) {
            size_t argument_ast = *(const size_t *)list_get(
                &ctx->ast->children, node->children_start + i);
            const AstNode *argument = ast_get_node(ctx->ast, argument_ast);
            const char *argument_name;

            if (argument == NULL || argument->kind != AST_NAMED_ARG) {
                error_report_at(ctx->errors, node->pos,
                                MESSAGE_CHECKER_UNSUPPORTED_AST_NODE);
                return false;
            }
            argument_name = ast_symbol_text(ctx->ast, argument->name_index);
            if (template_field_index(ctx, declaration, argument_name) ==
                TYPED_NO_NODE) {
                error_report_at(ctx->errors, argument->pos,
                                MESSAGE_CHECKER_UNKNOWN_CONSTRUCTOR_FIELD,
                                argument_name);
                return false;
            }
            {
                size_t j;

                for (j = 0; j < i; j++) {
                    size_t other_ast = *(const size_t *)list_get(
                        &ctx->ast->children, node->children_start + j);
                    const AstNode *other = ast_get_node(ctx->ast, other_ast);
                    const char *other_name = ast_symbol_text(
                        ctx->ast, other->name_index);

                    if (other_name != NULL &&
                        strcmp(other_name, argument_name) == 0) {
                        error_report_at(ctx->errors, argument->pos,
                                        MESSAGE_CHECKER_DUPLICATE_CONSTRUCTOR_ARG,
                                        argument_name);
                        return false;
                    }
                }
            }
        }
        for (i = 0; i < declaration->children_count; i++) {
            const AstNode *field = template_field(ctx, declaration, i, NULL);
            const char *field_name = field == NULL
                                         ? NULL
                                         : ast_symbol_text(ctx->ast,
                                                           field->name_index);
            const AstNode *found = NULL;
            size_t j;

            for (j = 0; j < node->children_count; j++) {
                size_t argument_ast = *(const size_t *)list_get(
                    &ctx->ast->children, node->children_start + j);
                const AstNode *argument = ast_get_node(ctx->ast, argument_ast);
                const char *argument_name = ast_symbol_text(
                    ctx->ast, argument->name_index);

                if (argument_name != NULL && field_name != NULL &&
                    strcmp(argument_name, field_name) == 0) {
                    found = argument;
                    break;
                }
            }
            if (found == NULL) {
                error_report_at(ctx->errors, node->pos,
                                MESSAGE_CHECKER_MISSING_CONSTRUCTOR_ARG,
                                field_name);
                return false;
            }
            {
                size_t value;
                const TypedNode *value_node;

                if (!check_expression(ctx, found->left_child, &value)) {
                    return false;
                }
                value_node = typed_program_node(ctx->program, value);
                if (value_node == NULL ||
                    !infer_ast_type(ctx, field->left_child,
                                    value_node->type_index, &infer,
                                    entry->name)) {
                    error_report_at(ctx->errors, node->pos,
                                    MESSAGE_CHECKER_CANNOT_INFER_TYPE,
                                    infer_first_unbound(&infer), entry->name);
                    return false;
                }
            }
        }
    }

    if (!infer_finish(ctx, &infer, &env, entry->name)) {
        return false;
    }
    for (i = 0; i < env.count; i++) {
        arguments[i] = env.types[i];
    }
    if (!instantiate_struct(ctx, entry_index, arguments, env.count,
                            &type_index)) {
        return false;
    }
    concrete = *entry;
    concrete.type_index = type_index;
    return check_constructor(ctx, node, &concrete, ast_index);
}

static bool check_constructor(CheckContext *ctx, const AstNode *node,
                              const TypeEntry *entry, size_t ast_index)
{
    const TypedNode *type_decl = typed_program_node(
        ctx->program, struct_declaration_node(ctx, entry));
    size_t field_count;
    bool named = false;
    size_t i;
    List values;
    TypedNode typed = {0};
    bool ok = false;

    if (type_decl == NULL || type_decl->kind != TYPED_TYPE_DECLARATION) {
        error_report_at(ctx->errors, node->pos,
                        MESSAGE_CHECKER_UNSUPPORTED_AST_NODE);
        return false;
    }
    field_count = type_decl->children_count;
    if (node->children_count > 0) {
        size_t first = *(const size_t *)list_get(&ctx->ast->children,
                                                 node->children_start);
        const AstNode *first_argument = ast_get_node(ctx->ast, first);

        if (first_argument == NULL) {
            error_report(ctx->errors, MESSAGE_CHECKER_MISSING_AST_NODE, first);
            return false;
        }
        named = first_argument->kind == AST_NAMED_ARG;
    }

    list_init(&values, sizeof(size_t));

    if (!named) {
        if (node->children_count != field_count) {
            error_report_at(ctx->errors, node->pos,
                            MESSAGE_CHECKER_CONSTRUCTOR_ARG_COUNT,
                            entry->name, field_count, node->children_count);
            goto done;
        }
        for (i = 0; i < field_count; i++) {
            size_t child = *(const size_t *)list_get(
                &ctx->ast->children, node->children_start + i);
            size_t value;
            size_t coerced;

            if (!check_expression(ctx, child, &value)) {
                goto done;
            }
            if (!constructor_value_fits(ctx, value, entry, i, i + 1,
                                         node->pos, &coerced)) {
                goto done;
            }
            if (!list_push(&values, &coerced)) {
                error_report(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
                goto done;
            }
        }
    } else {
        /* Reject unknown field names before reporting missing ones, so a
           typo reads as "unknown field" rather than "missing field". */
        for (i = 0; i < node->children_count; i++) {
            size_t child = *(const size_t *)list_get(
                &ctx->ast->children, node->children_start + i);
            const AstNode *argument = ast_get_node(ctx->ast, child);
            const char *argument_name;
            bool known = false;
            size_t j;

            if (argument == NULL || argument->kind != AST_NAMED_ARG) {
                error_report_at(ctx->errors, node->pos,
                                MESSAGE_CHECKER_UNSUPPORTED_AST_NODE);
                goto done;
            }
            argument_name = ast_symbol_text(ctx->ast, argument->name_index);
            for (j = 0; j < field_count; j++) {
                const char *field_name = struct_field_name(ctx->program,
                                                           type_decl, j);

                if (argument_name != NULL && field_name != NULL &&
                    strcmp(argument_name, field_name) == 0) {
                    known = true;
                    break;
                }
            }
            if (!known) {
                error_report_at(ctx->errors, argument->pos,
                                MESSAGE_CHECKER_UNKNOWN_CONSTRUCTOR_FIELD,
                                argument_name);
                goto done;
            }
        }

        for (i = 0; i < field_count; i++) {
            const char *field_name = struct_field_name(ctx->program,
                                                       type_decl, i);
            const AstNode *found = NULL;
            size_t j;

            for (j = 0; j < node->children_count; j++) {
                size_t child = *(const size_t *)list_get(
                    &ctx->ast->children, node->children_start + j);
                const AstNode *argument = ast_get_node(ctx->ast, child);
                const char *argument_name = ast_symbol_text(
                    ctx->ast, argument->name_index);

                if (argument_name != NULL && field_name != NULL &&
                    strcmp(argument_name, field_name) == 0) {
                    if (found != NULL) {
                        error_report_at(ctx->errors, argument->pos,
                                        MESSAGE_CHECKER_DUPLICATE_CONSTRUCTOR_ARG,
                                        field_name);
                        goto done;
                    }
                    found = argument;
                }
            }
            if (found == NULL) {
                error_report_at(ctx->errors, node->pos,
                                MESSAGE_CHECKER_MISSING_CONSTRUCTOR_ARG,
                                field_name);
                goto done;
            }
            {
                size_t value;
                size_t coerced;

                if (!check_expression(ctx, found->left_child, &value)) {
                    goto done;
                }
                if (!constructor_value_fits(ctx, value, entry, i, i + 1,
                                            found->pos, &coerced)) {
                    goto done;
                }
                if (!list_push(&values, &coerced)) {
                    error_report(ctx->errors,
                                 MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
                    goto done;
                }
            }
        }
    }

    {
        size_t children_start = ctx->program->children.length;

        /* Nested constructors in the arguments have appended their own runs
           already, so this constructor's values become one contiguous run. */
        for (i = 0; i < values.length; i++) {
            size_t value = *(const size_t *)list_get(&values, i);

            if (!typed_program_push_child(ctx->program, value)) {
                error_report(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
                goto done;
            }
        }

        typed.kind = TYPED_STRUCT_CONSTRUCTOR;
        typed.type_index = entry->type_index;
        typed.pos = node->pos;
        typed.name_index = TYPED_NO_NODE;
        typed.left_child = TYPED_NO_NODE;
        typed.right_child = TYPED_NO_NODE;
        typed.children_start = children_start;
        typed.children_count = field_count;

        ok = emit_node(ctx, &typed, ast_index, &(size_t){0});
    }

done:
    list_free(&values);
    return ok;
}

static bool check_function_call(CheckContext *ctx, const AstNode *node,
                                const char *name, size_t type_index,
                                size_t ast_index)
{
    const TypedType *callee_type = typed_program_type(ctx->program, type_index);
    const TypedFunctionSignature *signature;
    size_t children_start;
    size_t i;
    List values;
    TypedNode typed = {0};
    bool ok = false;

    if (callee_type == NULL || callee_type->kind != NIPPY_TYPE_FUNCTION) {
        error_report_at(ctx->errors, node->pos, MESSAGE_CHECKER_NOT_CALLABLE,
                        name);
        return false;
    }
    signature = typed_program_signature(ctx->program,
                                        callee_type->signature_index);
    if (signature == NULL) {
        error_report_at(ctx->errors, node->pos,
                        MESSAGE_CHECKER_UNSUPPORTED_AST_NODE);
        return false;
    }

    for (i = 0; i < node->children_count; i++) {
        size_t child = *(const size_t *)list_get(&ctx->ast->children,
                                                 node->children_start + i);
        const AstNode *argument = ast_get_node(ctx->ast, child);

        if (argument == NULL) {
            error_report(ctx->errors, MESSAGE_CHECKER_MISSING_AST_NODE, child);
            return false;
        }
        if (argument->kind == AST_NAMED_ARG) {
            error_report_at(ctx->errors, argument->pos,
                            MESSAGE_CHECKER_NAMED_FUNCTION_ARG);
            return false;
        }
    }

    if (node->children_count != signature->parameter_count) {
        error_report_at(ctx->errors, node->pos,
                        MESSAGE_CHECKER_FUNCTION_ARG_COUNT, name,
                        signature->parameter_count, node->children_count);
        return false;
    }

    list_init(&values, sizeof(size_t));
    for (i = 0; i < node->children_count; i++) {
        size_t child = *(const size_t *)list_get(&ctx->ast->children,
                                                 node->children_start + i);
        size_t value;
        size_t parameter_type;
        size_t coerced;
        const TypedNode *value_node;

        if (!check_expression(ctx, child, &value)) {
            goto done;
        }
        value_node = typed_program_node(ctx->program, value);
        parameter_type = *(const size_t *)list_get(
            &ctx->program->signature_parameters,
            signature->first_parameter + i);
        if (value_node == NULL ||
            !inject_into(ctx, value, parameter_type, &coerced)) {
            error_report_at(ctx->errors, node->pos,
                            MESSAGE_CHECKER_FUNCTION_ARG_TYPE, i + 1, name);
            goto done;
        }
        value = coerced;
        if (!list_push(&values, &value)) {
            error_report(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
            goto done;
        }
    }

    /* Append the argument run only now, after nested calls have appended
       theirs, so this call's children stay contiguous. */
    children_start = ctx->program->children.length;
    for (i = 0; i < values.length; i++) {
        size_t value = *(const size_t *)list_get(&values, i);

        if (!typed_program_push_child(ctx->program, value)) {
            error_report(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
            goto done;
        }
    }

    typed.kind = TYPED_FUNCTION_CALL;
    typed.type_index = signature->return_type;
    typed.pos = node->pos;
    typed.name_index = typed_program_copy_symbol(ctx->program, name,
                                                 strlen(name));
    if (typed.name_index == TYPED_NO_NODE) {
        error_report(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_COPYING_NAME);
        goto done;
    }
    typed.left_child = TYPED_NO_NODE;
    typed.right_child = TYPED_NO_NODE;
    typed.children_start = children_start;
    typed.children_count = values.length;

    ok = emit_node(ctx, &typed, ast_index, &(size_t){0});

done:
    list_free(&values);
    return ok;
}

static bool check_call(CheckContext *ctx, const AstNode *node,
                       size_t ast_index)
{
    const char *name = ast_symbol_text(ctx->ast, node->name_index);
    const VariableEntry *variable;
    const FunctionEntry *function;

    if (name == NULL) {
        error_report_at(ctx->errors, node->pos,
                        MESSAGE_CHECKER_MISSING_NAME_OR_TYPE);
        return false;
    }
    if (strcmp(name, "Store") == 0 || find_store(ctx->stores, name) != NULL) {
        error_report_at(ctx->errors, node->pos,
                        MESSAGE_CHECKER_STORE_TYPE);
        return false;
    }

    variable = find_variable(ctx->locals, name);
    if (variable == NULL && !ctx->in_function) {
        variable = find_variable(ctx->globals, name);
    }
    if (variable != NULL) {
        return check_function_call(ctx, node, name, variable->type_index,
                                   ast_index);
    }

    {
        size_t function_index = find_function_index(ctx->functions, name);

        if (function_index != TYPED_NO_NODE) {
            function = list_get(ctx->functions, function_index);
            if (function->generic) {
                return check_generic_call(ctx, node, function_index,
                                          ast_index);
            }
            return check_function_call(ctx, node, name, function->type_index,
                                       ast_index);
        }
    }

    if (ctx->in_function && find_variable(ctx->globals, name) != NULL) {
        error_report_at(ctx->errors, node->pos,
                        MESSAGE_CHECKER_GLOBAL_IN_FUNCTION, name);
        return false;
    }

    {
        size_t type_index_entry = find_type_index(ctx->types, name);

        if (type_index_entry != TYPED_NO_NODE) {
            const TypeEntry *entry =
                (const TypeEntry *)list_get(ctx->types, type_index_entry);

            if (entry->parameter_count > 0) {
                return check_generic_constructor(ctx, node,
                                                 type_index_entry, ast_index);
            }
            {
                const TypedType *constructed =
                    typed_program_type(ctx->program, entry->type_index);

                if (constructed == NULL ||
                    constructed->kind != NIPPY_TYPE_STRUCT) {
                    error_report_at(ctx->errors, node->pos,
                                    MESSAGE_CHECKER_CONSTRUCTOR_NOT_STRUCT,
                                    name);
                    return false;
                }
            }
            return check_constructor(ctx, node, entry, ast_index);
        }
    }
    if (strcmp(name, "Int32") == 0 || strcmp(name, "Bool") == 0 ||
        strcmp(name, "Nil") == 0) {
        error_report_at(ctx->errors, node->pos,
                        MESSAGE_CHECKER_CONSTRUCTOR_NOT_STRUCT, name);
        return false;
    }
    error_report_at(ctx->errors, node->pos, MESSAGE_CHECKER_UNKNOWN_CALL,
                    name);
    return false;
}

static FunctionInstantiation *find_function_instantiation(
    CheckContext *ctx, size_t entry_index, const size_t *arguments,
    size_t argument_count)
{
    size_t i;

    for (i = 0; i < ctx->function_instantiations->length; i++) {
        FunctionInstantiation *record =
            (FunctionInstantiation *)list_get(ctx->function_instantiations, i);
        size_t j;
        bool same = true;

        if (record->entry != entry_index ||
            record->argument_count != argument_count) {
            continue;
        }
        for (j = 0; j < argument_count; j++) {
            if (!same_type(ctx->program, record->arguments[j], arguments[j])) {
                same = false;
                break;
            }
        }
        if (same) {
            return record;
        }
    }
    return NULL;
}

/* Check a generic body once per concrete binding. The body AST is shared
   between instantiations, so it gets a fresh AST-to-typed mapping and a local
   parameter scope; the resulting function is fully concrete. */
static bool check_generic_body(CheckContext *ctx, size_t entry_index,
                               size_t name_index, const TypeEnv *env,
                               size_t function_type, size_t return_type)
{
    const FunctionEntry *entry =
        (const FunctionEntry *)list_get(ctx->functions, entry_index);
    const AstNode *declaration = ast_get_node(ctx->ast, entry->ast_index);
    const TypeEnv *previous_env = ctx->env;
    List *previous_mapping = ctx->mapping;
    List *previous_locals = ctx->locals;
    bool previous_in_function = ctx->in_function;
    size_t previous_loop = ctx->loop_depth;
    const char *previous_name = ctx->function_name;
    size_t previous_narrowings = ctx->narrowings->length;
    List fresh_mapping;
    List locals;
    size_t children_start = ctx->program->children.length;
    size_t typed_body;
    size_t typed_index;
    size_t i;
    bool ok = false;

    list_init(&fresh_mapping, sizeof(size_t));
    list_init(&locals, sizeof(VariableEntry));
    for (i = 0; i < ctx->ast->nodes.length; i++) {
        size_t none = TYPED_NO_NODE;

        if (!list_push(&fresh_mapping, &none)) {
            error_report(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
            goto done;
        }
    }
    ctx->mapping = &fresh_mapping;
    ctx->env = env;
    ctx->in_function = true;
    ctx->locals = &locals;
    ctx->loop_depth = 0;
    ctx->function_name = entry->name;
    ctx->narrowings->length = 0;

    for (i = 0; i < declaration->children_count; i++) {
        size_t parameter_ast = *(const size_t *)list_get(
            &ctx->ast->children, declaration->children_start + i);
        const AstNode *parameter = ast_get_node(ctx->ast, parameter_ast);
        const char *parameter_name;
        size_t parameter_type;
        size_t parameter_index;
        VariableEntry local;
        TypedNode typed_parameter = {0};

        if (parameter == NULL || parameter->kind != AST_PARAMETER) {
            error_report(ctx->errors, MESSAGE_CHECKER_MISSING_AST_NODE,
                         parameter_ast);
            goto done;
        }
        parameter_name = ast_symbol_text(ctx->ast, parameter->name_index);
        if (parameter_name == NULL) {
            error_report_at(ctx->errors, parameter->pos,
                            MESSAGE_CHECKER_MISSING_NAME_OR_TYPE);
            goto done;
        }
        if (find_variable(&locals, parameter_name) != NULL) {
            error_report_at(ctx->errors, parameter->pos,
                            MESSAGE_CHECKER_DUPLICATE_PARAMETER,
                            parameter_name);
            goto done;
        }
        if (!resolve_type_in_env(ctx, parameter->left_child, env,
                                 parameter_name, &parameter_type)) {
            goto done;
        }
        {
            const TypedType *resolved =
                typed_program_type(ctx->program, parameter_type);

            if (resolved != NULL && resolved->kind == NIPPY_TYPE_STORE) {
                error_report_at(ctx->errors, parameter->pos,
                                MESSAGE_CHECKER_STORE_TYPE);
                goto done;
            }
        }
        typed_parameter.kind = TYPED_PARAMETER;
        typed_parameter.type_index = parameter_type;
        typed_parameter.pos = parameter->pos;
        typed_parameter.name_index = typed_program_copy_symbol(
            ctx->program, parameter_name, strlen(parameter_name));
        if (typed_parameter.name_index == TYPED_NO_NODE) {
            error_report(ctx->errors,
                         MESSAGE_CHECKER_OUT_OF_MEMORY_COPYING_NAME);
            goto done;
        }
        typed_parameter.left_child = TYPED_NO_NODE;
        typed_parameter.right_child = TYPED_NO_NODE;
        if (!typed_program_push(ctx->program, &typed_parameter,
                                &parameter_index) ||
            !typed_program_push_child(ctx->program, parameter_index)) {
            error_report(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_PROGRAM);
            goto done;
        }
        memset(&local, 0, sizeof(local));
        memcpy(local.name, parameter_name, strlen(parameter_name) + 1);
        local.type_index = parameter_type;
        if (!list_push(&locals, &local)) {
            error_report(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
            goto done;
        }
    }

    if (!check_block(ctx, declaration->left_child, return_type, &typed_body)) {
        goto done;
    }
    if (!typed_block_returns(ctx->program, typed_body)) {
        error_report_at(ctx->errors, declaration->pos,
                        MESSAGE_CHECKER_FUNCTION_MAY_NOT_RETURN, entry->name);
        goto done;
    }

    {
        TypedNode typed = {0};

        typed.kind = TYPED_FUNCTION_DECLARATION;
        typed.type_index = function_type;
        typed.pos = declaration->pos;
        typed.name_index = name_index;
        typed.left_child = typed_body;
        typed.right_child = TYPED_NO_NODE;
        typed.children_start = children_start;
        typed.children_count = declaration->children_count;
        if (!typed_program_push(ctx->program, &typed, &typed_index)) {
            error_report(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_PROGRAM);
            goto done;
        }
        if (!list_push(&ctx->program->roots, &typed_index)) {
            error_report(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
            goto done;
        }
    }
    ok = true;

done:
    ctx->mapping = previous_mapping;
    ctx->env = previous_env;
    ctx->locals = previous_locals;
    ctx->in_function = previous_in_function;
    ctx->loop_depth = previous_loop;
    ctx->function_name = previous_name;
    ctx->narrowings->length = previous_narrowings;
    list_free(&fresh_mapping);
    list_free(&locals);
    return ok;
}

static bool check_generic_call(CheckContext *ctx, const AstNode *node,
                               size_t function_index, size_t ast_index)
{
    const FunctionEntry *entry =
        (const FunctionEntry *)list_get(ctx->functions, function_index);
    const AstNode *declaration = ast_get_node(ctx->ast, entry->ast_index);
    InferEnv infer = {0};
    TypeEnv env = {0};
    List values;
    List parameter_types;
    size_t return_type;
    size_t function_type;
    FunctionInstantiation *record;
    size_t i;
    bool ok = false;

    if (declaration == NULL ||
        declaration->kind != AST_FUNCTION_DECLARATION) {
        error_report_at(ctx->errors, node->pos,
                        MESSAGE_CHECKER_UNSUPPORTED_AST_NODE);
        return false;
    }
    for (i = 0; i < node->children_count; i++) {
        size_t child = *(const size_t *)list_get(&ctx->ast->children,
                                                 node->children_start + i);
        const AstNode *argument = ast_get_node(ctx->ast, child);

        if (argument == NULL) {
            error_report(ctx->errors, MESSAGE_CHECKER_MISSING_AST_NODE, child);
            return false;
        }
        if (argument->kind == AST_NAMED_ARG) {
            error_report_at(ctx->errors, argument->pos,
                            MESSAGE_CHECKER_NAMED_FUNCTION_ARG);
            return false;
        }
    }
    if (node->children_count != declaration->children_count) {
        error_report_at(ctx->errors, node->pos,
                        MESSAGE_CHECKER_FUNCTION_ARG_COUNT, entry->name,
                        declaration->children_count, node->children_count);
        return false;
    }

    for (i = 0; i < entry->parameter_count; i++) {
        infer.names[i] = entry->parameters[i];
        infer.types[i] = TYPED_NO_NODE;
    }
    infer.count = entry->parameter_count;

    list_init(&values, sizeof(size_t));
    list_init(&parameter_types, sizeof(size_t));
    for (i = 0; i < node->children_count; i++) {
        size_t argument_ast = *(const size_t *)list_get(
            &ctx->ast->children, node->children_start + i);
        size_t parameter_ast = *(const size_t *)list_get(
            &ctx->ast->children, declaration->children_start + i);
        const AstNode *parameter = ast_get_node(ctx->ast, parameter_ast);
        size_t value;
        const TypedNode *value_node;

        if (parameter == NULL || parameter->kind != AST_PARAMETER) {
            error_report(ctx->errors, MESSAGE_CHECKER_MISSING_AST_NODE,
                         parameter_ast);
            goto done;
        }
        if (!check_expression(ctx, argument_ast, &value)) {
            goto done;
        }
        value_node = typed_program_node(ctx->program, value);
        if (value_node == NULL ||
            !infer_ast_type(ctx, parameter->left_child,
                            value_node->type_index, &infer, entry->name)) {
            error_report_at(ctx->errors, node->pos,
                            MESSAGE_CHECKER_CANNOT_INFER_TYPE,
                            infer_first_unbound(&infer), entry->name);
            goto done;
        }
        if (!list_push(&values, &value)) {
            error_report(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
            goto done;
        }
    }
    if (!infer_finish(ctx, &infer, &env, entry->name)) {
        goto done;
    }

    for (i = 0; i < declaration->children_count; i++) {
        size_t parameter_ast = *(const size_t *)list_get(
            &ctx->ast->children, declaration->children_start + i);
        const AstNode *parameter = ast_get_node(ctx->ast, parameter_ast);
        size_t parameter_type;

        if (!resolve_type_in_env(ctx, parameter->left_child, &env,
                                 entry->name, &parameter_type) ||
            !list_push(&parameter_types, &parameter_type)) {
            if (!ctx->errors->reported) {
                error_report(ctx->errors,
                             MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
            }
            goto done;
        }
    }
    if (!resolve_type_in_env(ctx, declaration->right_child, &env, entry->name,
                             &return_type)) {
        goto done;
    }
    {
        const TypedType *resolved =
            typed_program_type(ctx->program, return_type);

        if (resolved != NULL && resolved->kind == NIPPY_TYPE_STORE) {
            error_report_at(ctx->errors, declaration->pos,
                            MESSAGE_CHECKER_STORE_TYPE);
            goto done;
        }
    }
    if (!add_function_type(ctx, &parameter_types, return_type,
                           &function_type)) {
        goto done;
    }

    record = find_function_instantiation(ctx, function_index, env.types,
                                         env.count);
    if (record == NULL) {
        FunctionInstantiation fresh = {0};
        size_t record_index;
        size_t generated_name;
        char generated[32];

        if (ctx->function_instantiations->length >= NIPPY_TYPE_DEPTH) {
            error_report(ctx->errors, MESSAGE_CHECKER_RECURSIVE_TYPE,
                         entry->name);
            goto done;
        }
        fresh.entry = function_index;
        fresh.argument_count = env.count;
        for (i = 0; i < env.count; i++) {
            fresh.arguments[i] = env.types[i];
        }
        fresh.type_index = function_type;
        fresh.in_progress = true;
        snprintf(generated, sizeof(generated), "nippy_genfn_%zu",
                 ctx->generated_names++);
        fresh.name_index = typed_program_copy_symbol(
            ctx->program, generated, strlen(generated));
        if (fresh.name_index == TYPED_NO_NODE) {
            error_report(ctx->errors,
                         MESSAGE_CHECKER_OUT_OF_MEMORY_COPYING_NAME);
            goto done;
        }
        if (!list_push(ctx->function_instantiations, &fresh)) {
            error_report(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
            goto done;
        }
        record_index = ctx->function_instantiations->length - 1;
        generated_name = fresh.name_index;
        if (!check_generic_body(ctx, function_index, generated_name, &env,
                                function_type, return_type)) {
            goto done;
        }
        record = (FunctionInstantiation *)list_get(
            ctx->function_instantiations, record_index);
        record->in_progress = false;
        record->emitted = true;
    }

    {
        size_t record_type_index;
        size_t record_name_index;
        const TypedType *callee;
        const TypedFunctionSignature *signature;
        List coerced;
        size_t children_start;

        record_type_index = record->type_index;
        record_name_index = record->name_index;
        callee = typed_program_type(ctx->program, record_type_index);
        signature =
            callee == NULL || callee->kind != NIPPY_TYPE_FUNCTION
                ? NULL
                : typed_program_signature(ctx->program,
                                          callee->signature_index);

        if (signature == NULL ||
            signature->parameter_count != values.length) {
            error_report_at(ctx->errors, node->pos,
                            MESSAGE_CHECKER_UNSUPPORTED_AST_NODE);
            goto done;
        }
        list_init(&coerced, sizeof(size_t));
        for (i = 0; i < values.length; i++) {
            size_t value = *(const size_t *)list_get(&values, i);
            size_t parameter_type = *(const size_t *)list_get(
                &ctx->program->signature_parameters,
                signature->first_parameter + i);
            size_t injected;

            if (!inject_into(ctx, value, parameter_type, &injected) ||
                !list_push(&coerced, &injected)) {
                if (!ctx->errors->reported) {
                    error_report_at(ctx->errors, node->pos,
                                    MESSAGE_CHECKER_FUNCTION_ARG_TYPE,
                                    i + 1, entry->name);
                }
                list_free(&coerced);
                goto done;
            }
        }
        children_start = ctx->program->children.length;
        for (i = 0; i < coerced.length; i++) {
            size_t value = *(const size_t *)list_get(&coerced, i);

            if (!typed_program_push_child(ctx->program, value)) {
                error_report(ctx->errors,
                             MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
                list_free(&coerced);
                goto done;
            }
        }
        {
            TypedNode typed = {0};

            typed.kind = TYPED_FUNCTION_CALL;
            typed.type_index = signature->return_type;
            typed.pos = node->pos;
            typed.name_index = record_name_index;
            typed.left_child = TYPED_NO_NODE;
            typed.right_child = TYPED_NO_NODE;
            typed.children_start = children_start;
            typed.children_count = coerced.length;
            if (!emit_node(ctx, &typed, ast_index, &(size_t){0})) {
                list_free(&coerced);
                goto done;
            }
        }
        list_free(&coerced);
    }
    ok = true;

done:
    list_free(&values);
    list_free(&parameter_types);
    return ok;
}

static bool member_store(CheckContext *ctx, const AstNode *node,
                         const char *method, const StoreEntry **out_store)
{
    const AstNode *receiver = ast_get_node(ctx->ast, node->left_child);
    const char *name;

    if (receiver == NULL || receiver->kind != AST_IDENTIFIER) {
        error_report_at(ctx->errors, node->pos,
                        MESSAGE_CHECKER_MEMBER_RECEIVER, method);
        return false;
    }
    name = ast_symbol_text(ctx->ast, receiver->name_index);
    if (name == NULL) {
        error_report_at(ctx->errors, receiver->pos,
                        MESSAGE_CHECKER_MISSING_NAME_OR_TYPE);
        return false;
    }
    *out_store = find_store(ctx->stores, name);
    if (*out_store == NULL) {
        error_report_at(ctx->errors, receiver->pos,
                        MESSAGE_CHECKER_UNKNOWN_STORE, name);
        return false;
    }
    return true;
}

static bool check_member_call(CheckContext *ctx, const AstNode *node,
                              size_t ast_index)
{
    const char *method = ast_symbol_text(ctx->ast, node->name_index);
    const StoreEntry *store = NULL;
    TypedNode typed = {0};

    if (method == NULL) {
        error_report_at(ctx->errors, node->pos,
                        MESSAGE_CHECKER_MISSING_NAME_OR_TYPE);
        return false;
    }

    if (strcmp(method, "alloc") == 0) {
        size_t element;
        size_t value;
        size_t coerced;
        size_t handle;
        size_t argument_ast;

        if (!member_store(ctx, node, method, &store)) {
            return false;
        }
        if (node->right_child == AST_NO_NODE) {
            error_report_at(ctx->errors, node->pos,
                            MESSAGE_CHECKER_ALLOC_TYPE_ARG);
            return false;
        }
        if (!resolve_type_in_env(ctx, node->right_child, ctx->env, "alloc",
                                 &element)) {
            return false;
        }
        if (!is_sized_type(ctx->program, element)) {
            error_report_at(ctx->errors, node->pos,
                            MESSAGE_CHECKER_HANDLE_ELEMENT);
            return false;
        }
        if (node->children_count != 1) {
            error_report_at(ctx->errors, node->pos,
                            MESSAGE_CHECKER_ALLOC_ARG_COUNT);
            return false;
        }
        argument_ast = *(const size_t *)list_get(&ctx->ast->children,
                                                 node->children_start);
        if (!check_expression(ctx, argument_ast, &value)) {
            return false;
        }
        if (!inject_into(ctx, value, element, &coerced)) {
            char element_text[128];

            type_ast_text(ctx->ast, node->right_child, element_text,
                          sizeof(element_text));
            error_report_at(ctx->errors, node->pos,
                            MESSAGE_CHECKER_ALLOC_ARG_TYPE, element_text);
            return false;
        }
        if (!handle_type(ctx, element, &handle)) {
            return false;
        }
        typed.kind = TYPED_STORE_ALLOC;
        typed.type_index = handle;
        typed.store_id = store->id;
        typed.integer_value = 0;
        typed.pos = node->pos;
        typed.name_index = TYPED_NO_NODE;
        typed.left_child = coerced;
        typed.right_child = TYPED_NO_NODE;
        typed.children_start = 0;
        typed.children_count = 0;
        return emit_node(ctx, &typed, ast_index, &(size_t){0});
    }

    if (strcmp(method, "reset") == 0) {
        size_t nil_type;

        if (!member_store(ctx, node, method, &store)) {
            return false;
        }
        if (node->right_child != AST_NO_NODE) {
            error_report_at(ctx->errors, node->pos,
                            MESSAGE_CHECKER_MEMBER_TYPE_ARG, method);
            return false;
        }
        if (node->children_count != 0) {
            error_report_at(ctx->errors, node->pos,
                            MESSAGE_CHECKER_MEMBER_ARGS, method);
            return false;
        }
        if (!builtin_type(ctx, NIPPY_TYPE_NIL, &nil_type)) {
            return false;
        }
        typed.kind = TYPED_STORE_RESET;
        typed.type_index = nil_type;
        typed.store_id = store->id;
        typed.pos = node->pos;
        typed.name_index = TYPED_NO_NODE;
        typed.left_child = TYPED_NO_NODE;
        typed.right_child = TYPED_NO_NODE;
        typed.children_start = 0;
        typed.children_count = 0;
        return emit_node(ctx, &typed, ast_index, &(size_t){0});
    }

    if (strcmp(method, "read") == 0) {
        size_t receiver;
        const TypedNode *receiver_node;
        const TypedType *receiver_type;
        size_t element;

        if (node->right_child != AST_NO_NODE) {
            error_report_at(ctx->errors, node->pos,
                            MESSAGE_CHECKER_MEMBER_TYPE_ARG, method);
            return false;
        }
        if (node->children_count != 0) {
            error_report_at(ctx->errors, node->pos,
                            MESSAGE_CHECKER_MEMBER_ARGS, method);
            return false;
        }
        if (!check_expression(ctx, node->left_child, &receiver)) {
            return false;
        }
        receiver_node = typed_program_node(ctx->program, receiver);
        receiver_type = receiver_node == NULL
                            ? NULL
                            : typed_program_type(ctx->program,
                                                 receiver_node->type_index);
        if (receiver_type == NULL ||
            receiver_type->kind != NIPPY_TYPE_HANDLE ||
            receiver_type->child_count != 1) {
            error_report_at(ctx->errors, node->pos,
                            MESSAGE_CHECKER_MEMBER_RECEIVER, method);
            return false;
        }
        element = *(const size_t *)list_get(&ctx->program->type_children,
                                            receiver_type->first_child);
        typed.kind = TYPED_HANDLE_READ;
        typed.type_index = element;
        typed.pos = node->pos;
        typed.name_index = TYPED_NO_NODE;
        typed.left_child = receiver;
        typed.right_child = TYPED_NO_NODE;
        typed.children_start = 0;
        typed.children_count = 0;
        return emit_node(ctx, &typed, ast_index, &(size_t){0});
    }

    if (strcmp(method, "write") == 0) {
        size_t receiver;
        const TypedNode *receiver_node;
        const TypedType *receiver_type;
        size_t element;
        size_t value;
        size_t coerced;
        size_t nil_type;
        size_t argument_ast;

        if (node->right_child != AST_NO_NODE) {
            error_report_at(ctx->errors, node->pos,
                            MESSAGE_CHECKER_MEMBER_TYPE_ARG, method);
            return false;
        }
        if (node->children_count != 1) {
            error_report_at(ctx->errors, node->pos,
                            MESSAGE_CHECKER_MEMBER_ARGS, method);
            return false;
        }
        if (!check_expression(ctx, node->left_child, &receiver)) {
            return false;
        }
        receiver_node = typed_program_node(ctx->program, receiver);
        receiver_type = receiver_node == NULL
                            ? NULL
                            : typed_program_type(ctx->program,
                                                 receiver_node->type_index);
        if (receiver_type == NULL ||
            receiver_type->kind != NIPPY_TYPE_HANDLE ||
            receiver_type->child_count != 1) {
            error_report_at(ctx->errors, node->pos,
                            MESSAGE_CHECKER_MEMBER_RECEIVER, method);
            return false;
        }
        element = *(const size_t *)list_get(&ctx->program->type_children,
                                            receiver_type->first_child);
        argument_ast = *(const size_t *)list_get(&ctx->ast->children,
                                                 node->children_start);
        if (!check_expression(ctx, argument_ast, &value)) {
            return false;
        }
        if (!inject_into(ctx, value, element, &coerced)) {
            error_report_at(ctx->errors, node->pos,
                            MESSAGE_CHECKER_WRITE_TYPE);
            return false;
        }
        if (!builtin_type(ctx, NIPPY_TYPE_NIL, &nil_type)) {
            return false;
        }
        typed.kind = TYPED_HANDLE_WRITE;
        typed.type_index = nil_type;
        typed.pos = node->pos;
        typed.name_index = TYPED_NO_NODE;
        typed.left_child = receiver;
        typed.right_child = coerced;
        typed.children_start = 0;
        typed.children_count = 0;
        return emit_node(ctx, &typed, ast_index, &(size_t){0});
    }

    error_report_at(ctx->errors, node->pos, MESSAGE_CHECKER_UNKNOWN_MEMBER,
                    method);
    return false;
}

static bool check_expression(CheckContext *ctx, size_t ast_index,
                             size_t *out_typed)
{
    const AstNode *node = ast_get_node(ctx->ast, ast_index);
    size_t *slot;

    if (node == NULL) {
        error_report(ctx->errors, MESSAGE_CHECKER_MISSING_AST_NODE, ast_index);
        return false;
    }
    slot = mapping_slot(ctx->mapping, ast_index);
    if (*slot != TYPED_NO_NODE) {
        *out_typed = *slot;
        return true;
    }

    {
        bool ok;

        switch (node->kind) {
        case AST_INTEGER_LITERAL:
            ok = check_literal(ctx, node, ast_index);
            break;
        case AST_BOOL_LITERAL:
            ok = check_bool_literal(ctx, node, ast_index);
            break;
        case AST_NIL_LITERAL:
            ok = check_nil_literal(ctx, node, ast_index);
            break;
        case AST_IS:
            ok = check_is(ctx, node, ast_index);
            break;
        case AST_MEMBER_CALL:
            ok = check_member_call(ctx, node, ast_index);
            break;
        case AST_UNARY:
            ok = check_unary(ctx, node, ast_index);
            break;
        case AST_IDENTIFIER:
            ok = check_identifier(ctx, node, ast_index);
            break;
        case AST_BINARY:
            ok = check_binary(ctx, node, ast_index);
            break;
        case AST_CALL:
            ok = check_call(ctx, node, ast_index);
            break;
        default:
            error_report_at(ctx->errors, node->pos,
                            MESSAGE_CHECKER_UNSUPPORTED_AST_NODE);
            return false;
        }
        if (ok) {
            *out_typed = *slot;
        }
        return ok;
    }
}

static bool eval_const_int(CheckContext *ctx, size_t ast_index, int *out)
{
    const AstNode *node = ast_get_node(ctx->ast, ast_index);

    if (node == NULL) {
        error_report(ctx->errors, MESSAGE_CHECKER_MISSING_AST_NODE, ast_index);
        return false;
    }
    if (node->kind == AST_INTEGER_LITERAL) {
        *out = node->integer_value;
        return true;
    }
    if (node->kind == AST_BINARY && node->op == NIPPY_OPERATOR_ADD) {
        int left;
        int right;
        long long sum;

        if (!eval_const_int(ctx, node->left_child, &left) ||
            !eval_const_int(ctx, node->right_child, &right)) {
            return false;
        }
        sum = (long long)left + (long long)right;
        if (sum < 0 || sum > 2147483647LL) {
            error_report_at(ctx->errors, node->pos,
                            MESSAGE_CHECKER_STORE_SIZE);
            return false;
        }
        *out = (int)sum;
        return true;
    }
    error_report_at(ctx->errors, node->pos, MESSAGE_CHECKER_STORE_SIZE);
    return false;
}

static bool check_store_declaration(CheckContext *ctx, const AstNode *node,
                                    const char *name, size_t ast_index,
                                    size_t store_type)
{
    const AstNode *initialiser = ast_get_node(ctx->ast, node->left_child);
    const char *constructor;
    size_t size_ast;
    int size;
    StoreEntry entry;
    TypedNode typed = {0};

    if (initialiser == NULL || initialiser->kind != AST_CALL) {
        error_report_at(ctx->errors, node->pos,
                        MESSAGE_CHECKER_STORE_INITIALIZER, name);
        return false;
    }
    constructor = ast_symbol_text(ctx->ast, initialiser->name_index);
    if (constructor == NULL || strcmp(constructor, "Store") != 0 ||
        initialiser->children_count != 1) {
        error_report_at(ctx->errors, node->pos,
                        MESSAGE_CHECKER_STORE_INITIALIZER, name);
        return false;
    }
    size_ast = *(const size_t *)list_get(&ctx->ast->children,
                                         initialiser->children_start);
    {
        const AstNode *argument = ast_get_node(ctx->ast, size_ast);

        if (argument == NULL || argument->kind == AST_NAMED_ARG) {
            error_report_at(ctx->errors, node->pos,
                            MESSAGE_CHECKER_STORE_INITIALIZER, name);
            return false;
        }
    }
    if (!eval_const_int(ctx, size_ast, &size)) {
        return false;
    }
    if (find_store(ctx->stores, name) != NULL) {
        error_report_at(ctx->errors, node->pos,
                        MESSAGE_CHECKER_DUPLICATE_NAME, name);
        return false;
    }

    memset(&entry, 0, sizeof(entry));
    memcpy(entry.name, name, strlen(name) + 1);
    entry.id = ctx->stores->length;
    entry.initial_size = size;
    if (!list_push(ctx->stores, &entry)) {
        error_report(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
        return false;
    }

    typed.kind = TYPED_STORE_DECLARATION;
    typed.type_index = store_type;
    typed.pos = node->pos;
    typed.name_index = typed_program_copy_symbol(ctx->program, name,
                                                 strlen(name));
    if (typed.name_index == TYPED_NO_NODE) {
        error_report(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_COPYING_NAME);
        return false;
    }
    typed.store_id = entry.id;
    typed.integer_value = size;
    typed.left_child = TYPED_NO_NODE;
    typed.right_child = TYPED_NO_NODE;
    typed.children_start = 0;
    typed.children_count = 0;
    return emit_node(ctx, &typed, ast_index, &(size_t){0});
}

static bool check_var_declaration(CheckContext *ctx, const AstNode *node,
                                  size_t ast_index)
{
    const char *name = ast_symbol_text(ctx->ast, node->name_index);
    size_t declared_type = TYPED_NO_NODE;
    size_t initialiser;
    const TypedNode *initialiser_node;
    size_t variable_type;
    TypedNode typed = {0};

    if (name == NULL) {
        error_report_at(ctx->errors, node->pos,
                        MESSAGE_CHECKER_MISSING_NAME_OR_TYPE);
        return false;
    }
    if (find_variable(ctx->globals, name) != NULL ||
        find_function(ctx->functions, name) != NULL ||
        find_type(ctx->types, name) != NULL ||
        find_store(ctx->stores, name) != NULL ||
        strcmp(name, "Int32") == 0 || strcmp(name, "Bool") == 0 ||
        strcmp(name, "Nil") == 0 || strcmp(name, "Byte") == 0 ||
        strcmp(name, "Store") == 0 || strcmp(name, "Handle") == 0) {
        error_report_at(ctx->errors, node->pos, MESSAGE_CHECKER_DUPLICATE_NAME,
                        name);
        return false;
    }
    if (node->left_child == AST_NO_NODE) {
        error_report_at(ctx->errors, node->pos, MESSAGE_CHECKER_NO_INITIALISER,
                        name);
        return false;
    }

    if (node->right_child != AST_NO_NODE &&
        !resolve_type_node(ctx, node->right_child, name, &declared_type)) {
        return false;
    }
    if (declared_type != TYPED_NO_NODE) {
        const TypedType *annotated =
            typed_program_type(ctx->program, declared_type);

        if (annotated != NULL && annotated->kind == NIPPY_TYPE_STORE) {
            return check_store_declaration(ctx, node, name, ast_index,
                                           declared_type);
        }
    }
    if (!check_expression(ctx, node->left_child, &initialiser)) {
        return false;
    }
    initialiser_node = typed_program_node(ctx->program, initialiser);
    if (initialiser_node == NULL) {
        error_report_at(ctx->errors, node->pos,
                        MESSAGE_CHECKER_MISSING_AST_NODE, node->left_child);
        return false;
    }

    if (declared_type == TYPED_NO_NODE) {
        const TypedNode *inferred = typed_program_node(ctx->program,
                                                       initialiser);

        if (inferred != NULL) {
            const TypedType *inferred_type = typed_program_type(
                ctx->program, inferred->type_index);

            if (inferred_type != NULL &&
                inferred_type->kind == NIPPY_TYPE_STORE) {
                error_report_at(ctx->errors, node->pos,
                                MESSAGE_CHECKER_STORE_INITIALIZER, name);
                return false;
            }
        }
    }

    if (declared_type != TYPED_NO_NODE) {
        size_t coerced;

        if (!inject_into(ctx, initialiser, declared_type, &coerced)) {
            char declared_text[128];

            type_ast_text(ctx->ast, node->right_child, declared_text,
                          sizeof(declared_text));
            error_report_at(ctx->errors, node->pos,
                            MESSAGE_CHECKER_INITIALISER_TYPE_MISMATCH, name,
                            declared_text);
            return false;
        }
        initialiser = coerced;
        variable_type = declared_type;
    } else {
        variable_type = initialiser_node->type_index;
    }

    typed.kind = TYPED_DECLARATION;
    typed.type_index = variable_type;
    typed.pos = node->pos;
    typed.name_index = typed_program_copy_symbol(ctx->program, name,
                                                 strlen(name));
    if (typed.name_index == TYPED_NO_NODE) {
        error_report(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_COPYING_NAME);
        return false;
    }
    typed.left_child = initialiser;
    typed.right_child = TYPED_NO_NODE;
    typed.children_start = 0;
    typed.children_count = 0;

    if (!emit_node(ctx, &typed, ast_index, &(size_t){0})) {
        return false;
    }

    {
        VariableEntry entry;

        memset(&entry, 0, sizeof(entry));
        memcpy(entry.name, name, strlen(name) + 1);
        entry.type_index = variable_type;
        if (!list_push(ctx->globals, &entry)) {
            error_report(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
            return false;
        }
    }
    return true;
}

static bool check_type_declaration(CheckContext *ctx, const AstNode *node,
                                   size_t ast_index)
{
    const char *name = ast_symbol_text(ctx->ast, node->name_index);
    TypeEntry entry = {0};
    size_t entry_index;
    size_t i;

    if (name == NULL) {
        error_report_at(ctx->errors, node->pos,
                        MESSAGE_CHECKER_MISSING_NAME_OR_TYPE);
        return false;
    }
    if (strcmp(name, "Int32") == 0) {
        error_report_at(ctx->errors, node->pos,
                        MESSAGE_CHECKER_REDEFINES_INT32);
        return false;
    }
    if (strcmp(name, "Bool") == 0) {
        error_report_at(ctx->errors, node->pos,
                        MESSAGE_CHECKER_REDEFINES_BOOL);
        return false;
    }
    if (strcmp(name, "Nil") == 0) {
        error_report_at(ctx->errors, node->pos,
                        MESSAGE_CHECKER_REDEFINES_NIL);
        return false;
    }
    if (find_type_index(ctx->types, name) != TYPED_NO_NODE) {
        error_report_at(ctx->errors, node->pos, MESSAGE_CHECKER_DUPLICATE_TYPE,
                        name);
        return false;
    }
    if (find_variable(ctx->globals, name) != NULL ||
        find_function(ctx->functions, name) != NULL ||
        find_store(ctx->stores, name) != NULL) {
        error_report_at(ctx->errors, node->pos, MESSAGE_CHECKER_DUPLICATE_NAME,
                        name);
        return false;
    }
    if (node->type_parameters_count > NIPPY_MAX_TYPE_PARAMETERS) {
        error_report_at(ctx->errors, node->pos,
                        MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
        return false;
    }

    memcpy(entry.name, name, strlen(name) + 1);
    entry.kind = node->kind == AST_TYPE_ALIAS ? TYPE_ENTRY_ALIAS
                                              : TYPE_ENTRY_STRUCT;
    entry.ast_index = ast_index;
    entry.type_index = TYPED_NO_NODE;
    for (i = 0; i < node->type_parameters_count; i++) {
        size_t child = *(const size_t *)list_get(
            &ctx->ast->type_parameters,
            node->type_parameters_start + i);
        const AstNode *parameter = ast_get_node(ctx->ast, child);
        const char *parameter_name;

        if (parameter == NULL || parameter->kind != AST_TYPE_PARAMETER) {
            error_report(ctx->errors, MESSAGE_CHECKER_MISSING_AST_NODE, child);
            return false;
        }
        parameter_name = ast_symbol_text(ctx->ast, parameter->name_index);
        if (parameter_name == NULL) {
            error_report_at(ctx->errors, parameter->pos,
                            MESSAGE_CHECKER_MISSING_NAME_OR_TYPE);
            return false;
        }
        if (type_parameter_index(entry.parameters, entry.parameter_count,
                                 parameter_name) != TYPED_NO_NODE) {
            error_report_at(ctx->errors, parameter->pos,
                            MESSAGE_CHECKER_DUPLICATE_TYPE_PARAMETER,
                            parameter_name);
            return false;
        }
        memcpy(entry.parameters[entry.parameter_count], parameter_name,
               strlen(parameter_name) + 1);
        entry.parameter_count++;
    }

    if (!list_push(ctx->types, &entry)) {
        error_report(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
        return false;
    }
    entry_index = ctx->types->length - 1;

    if (entry.kind == TYPE_ENTRY_ALIAS) {
        /* A non-generic alias is resolved at its declaration so unknown or
           invalid types are diagnosed even when the alias is never used. It
           has no C type and no root. Generic aliases expand per application. */
        if (entry.parameter_count == 0) {
            size_t type_index;

            if (!expand_alias(ctx, &entry, NULL, 0, &type_index)) {
                return false;
            }
            ((TypeEntry *)list_get(ctx->types, entry_index))->type_index =
                type_index;
        }
        return true;
    }
    if (node->children_count == 0) {
        error_report_at(ctx->errors, node->pos,
                        MESSAGE_CHECKER_TYPE_HAS_NO_FIELDS);
        return false;
    }
    if (entry.parameter_count == 0) {
        size_t type_index;

        if (!instantiate_struct(ctx, entry_index, NULL, 0, &type_index)) {
            return false;
        }
        ((TypeEntry *)list_get(ctx->types, entry_index))->type_index =
            type_index;
    }
    return true;
}

static bool check_condition(CheckContext *ctx, size_t ast_index,
                            size_t *out_typed)
{
    size_t expression;
    const TypedNode *node;
    const TypedType *type;

    if (!check_expression(ctx, ast_index, &expression)) {
        return false;
    }
    node = typed_program_node(ctx->program, expression);
    type = node == NULL ? NULL
                        : typed_program_type(ctx->program, node->type_index);
    if (type == NULL || type->kind != NIPPY_TYPE_BOOL) {
        const AstNode *ast_node = ast_get_node(ctx->ast, ast_index);

        error_report_at(ctx->errors,
                        ast_node == NULL ? (SourcePos){0, 1, 1} : ast_node->pos,
                        MESSAGE_CHECKER_CONDITION_TYPE);
        return false;
    }
    *out_typed = expression;
    return true;
}

static bool typed_block_returns(const TypedProgram *program, size_t index);
static bool typed_statement_returns(const TypedProgram *program, size_t index);
static bool typed_statement_terminates(const TypedProgram *program,
                                       size_t index);

static bool typed_statement_returns(const TypedProgram *program, size_t index)
{
    const TypedNode *node = typed_program_node(program, index);

    if (node == NULL) {
        return false;
    }
    if (node->kind == TYPED_RETURN) {
        return true;
    }
    if (node->kind == TYPED_BLOCK) {
        return typed_block_returns(program, index);
    }
    if (node->kind == TYPED_IF) {
        size_t then_block;
        size_t else_block;

        if (node->children_count != 2) {
            return false;
        }
        then_block = *(const size_t *)list_get(&program->children,
                                               node->children_start);
        else_block = *(const size_t *)list_get(&program->children,
                                               node->children_start + 1);
        return typed_statement_returns(program, then_block) &&
               typed_statement_returns(program, else_block);
    }
    return false;
}

static bool typed_statement_terminates(const TypedProgram *program,
                                       size_t index)
{
    const TypedNode *node = typed_program_node(program, index);

    if (node == NULL) {
        return false;
    }
    if (node->kind == TYPED_RETURN || node->kind == TYPED_BREAK ||
        node->kind == TYPED_CONTINUE) {
        return true;
    }
    if (node->kind == TYPED_BLOCK) {
        size_t i;

        for (i = 0; i < node->children_count; i++) {
            size_t child = *(const size_t *)list_get(&program->children,
                                                     node->children_start + i);

            if (typed_statement_terminates(program, child)) {
                return true;
            }
        }
        return false;
    }
    if (node->kind == TYPED_IF) {
        size_t then_block;
        size_t else_block;

        if (node->children_count != 2) {
            return false;
        }
        then_block = *(const size_t *)list_get(&program->children,
                                               node->children_start);
        else_block = *(const size_t *)list_get(&program->children,
                                               node->children_start + 1);
        return typed_statement_terminates(program, then_block) &&
               typed_statement_terminates(program, else_block);
    }
    return false;
}

static bool typed_block_returns(const TypedProgram *program, size_t index)
{
    const TypedNode *block = typed_program_node(program, index);
    size_t i;

    if (block == NULL || block->kind != TYPED_BLOCK) {
        return false;
    }
    for (i = 0; i < block->children_count; i++) {
        size_t child = *(const size_t *)list_get(&program->children,
                                                 block->children_start + i);

        if (typed_statement_returns(program, child)) {
            return true;
        }
        if (typed_statement_terminates(program, child)) {
            return false;
        }
    }
    return false;
}

/* If the condition is exactly `name.is<T>()` and `name` is a union that has T,
   the then block sees `name` as T. The else block and everything after the if
   keep the declared type. */
static bool push_if_narrowing(CheckContext *ctx, size_t condition_ast)
{
    const AstNode *condition = ast_get_node(ctx->ast, condition_ast);
    const AstNode *receiver;
    const char *name;
    const VariableEntry *variable;
    const TypedType *declared;
    size_t tested;
    size_t tag;

    if (condition == NULL || condition->kind != AST_IS) {
        return true;
    }
    receiver = ast_get_node(ctx->ast, condition->left_child);
    if (receiver == NULL || receiver->kind != AST_IDENTIFIER) {
        return true;
    }
    name = ast_symbol_text(ctx->ast, receiver->name_index);
    if (name == NULL) {
        return true;
    }
    variable = find_variable(ctx->locals, name);
    if (variable == NULL && !ctx->in_function) {
        variable = find_variable(ctx->globals, name);
    }
    if (variable == NULL) {
        return true;
    }
    declared = typed_program_type(ctx->program, variable->type_index);
    if (declared == NULL || declared->kind != NIPPY_TYPE_UNION) {
        return true;
    }
    if (!resolve_type_in_env(ctx, condition->right_child, ctx->env, "is",
                             &tested)) {
        return false;
    }
    if (!union_has_alternative(ctx->program, declared, tested, &tag)) {
        return true;
    }
    {
        Narrowing narrowing = {0};

        narrowing.name = name;
        narrowing.type_index = tested;
        narrowing.tag = tag;
        if (!list_push(ctx->narrowings, &narrowing)) {
            error_report(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
            return false;
        }
    }
    return true;
}

static bool check_expression_statement(CheckContext *ctx, size_t ast_index,
                                       size_t *out_typed)
{
    const AstNode *node = ast_get_node(ctx->ast, ast_index);
    size_t expression;
    const TypedNode *expression_node;
    TypedNode typed = {0};

    if (node == NULL || node->kind != AST_EXPRESSION_STATEMENT) {
        error_report_at(ctx->errors,
                        node == NULL ? (SourcePos){0, 1, 1} : node->pos,
                        MESSAGE_CHECKER_UNSUPPORTED_AST_NODE);
        return false;
    }
    {
        const AstNode *value = ast_get_node(ctx->ast, node->left_child);

        if (value == NULL ||
            (value->kind != AST_CALL && value->kind != AST_MEMBER_CALL)) {
            error_report_at(ctx->errors,
                            value == NULL ? node->pos : value->pos,
                            MESSAGE_CHECKER_EXPRESSION_STATEMENT);
            return false;
        }
    }
    if (!check_expression(ctx, node->left_child, &expression)) {
        return false;
    }
    expression_node = typed_program_node(ctx->program, expression);
    typed.kind = TYPED_EXPRESSION_STATEMENT;
    typed.type_index = expression_node == NULL
                           ? TYPED_NO_NODE
                           : expression_node->type_index;
    typed.pos = node->pos;
    typed.name_index = TYPED_NO_NODE;
    typed.left_child = expression;
    typed.right_child = TYPED_NO_NODE;
    typed.children_start = 0;
    typed.children_count = 0;
    return emit_node(ctx, &typed, ast_index, out_typed);
}

static bool check_statement(CheckContext *ctx, size_t ast_index,
                            size_t return_type, size_t *out_typed)
{
    const AstNode *node = ast_get_node(ctx->ast, ast_index);
    TypedNode typed = {0};

    if (node == NULL) {
        error_report(ctx->errors, MESSAGE_CHECKER_MISSING_AST_NODE, ast_index);
        return false;
    }

    if (node->kind == AST_RETURN) {
        size_t expression;
        const TypedNode *expression_node;

        size_t coerced;

        if (!check_expression(ctx, node->left_child, &expression)) {
            return false;
        }
        expression_node = typed_program_node(ctx->program, expression);
        if (expression_node == NULL ||
            !inject_into(ctx, expression, return_type, &coerced)) {
            error_report_at(ctx->errors, node->pos,
                            MESSAGE_CHECKER_RETURN_TYPE_MISMATCH,
                            ctx->function_name == NULL ? "function"
                                                       : ctx->function_name);
            return false;
        }
        expression = coerced;
        typed.kind = TYPED_RETURN;
        typed.type_index = return_type;
        typed.pos = node->pos;
        typed.left_child = expression;
        typed.right_child = TYPED_NO_NODE;
        return emit_node(ctx, &typed, ast_index, out_typed);
    }

    if (node->kind == AST_IF) {
        size_t condition;
        size_t then_block;
        size_t else_block = TYPED_NO_NODE;
        size_t children_start;

        size_t narrowing_count;

        if (!check_condition(ctx, node->left_child, &condition)) {
            return false;
        }
        if (node->children_count == 0) {
            error_report_at(ctx->errors, node->pos,
                            MESSAGE_CHECKER_UNSUPPORTED_AST_NODE);
            return false;
        }
        narrowing_count = ctx->narrowings->length;
        if (!push_if_narrowing(ctx, node->left_child)) {
            return false;
        }
        {
            size_t then_ast = *(const size_t *)list_get(
                &ctx->ast->children, node->children_start);

            if (!check_block(ctx, then_ast, return_type, &then_block)) {
                ctx->narrowings->length = narrowing_count;
                return false;
            }
        }
        ctx->narrowings->length = narrowing_count;
        if (node->children_count > 1) {
            size_t else_ast = *(const size_t *)list_get(
                &ctx->ast->children, node->children_start + 1);

            if (!check_block(ctx, else_ast, return_type, &else_block)) {
                return false;
            }
        }

        children_start = ctx->program->children.length;
        if (!typed_program_push_child(ctx->program, then_block)) {
            error_report(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
            return false;
        }
        if (else_block != TYPED_NO_NODE &&
            !typed_program_push_child(ctx->program, else_block)) {
            error_report(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
            return false;
        }

        typed.kind = TYPED_IF;
        typed.type_index = TYPED_NO_NODE;
        typed.pos = node->pos;
        typed.left_child = condition;
        typed.right_child = TYPED_NO_NODE;
        typed.children_start = children_start;
        typed.children_count = node->children_count > 1 ? 2 : 1;
        return emit_node(ctx, &typed, ast_index, out_typed);
    }

    if (node->kind == AST_WHILE) {
        size_t condition;
        size_t body;

        if (!check_condition(ctx, node->left_child, &condition)) {
            return false;
        }
        ctx->loop_depth++;
        if (!check_block(ctx, node->right_child, return_type, &body)) {
            ctx->loop_depth--;
            return false;
        }
        ctx->loop_depth--;

        typed.kind = TYPED_WHILE;
        typed.type_index = TYPED_NO_NODE;
        typed.pos = node->pos;
        typed.left_child = condition;
        typed.right_child = body;
        return emit_node(ctx, &typed, ast_index, out_typed);
    }

    if (node->kind == AST_EXPRESSION_STATEMENT) {
        return check_expression_statement(ctx, ast_index, out_typed);
    }

    if (node->kind == AST_BREAK || node->kind == AST_CONTINUE) {
        if (ctx->loop_depth == 0) {
            error_report_at(ctx->errors, node->pos,
                            node->kind == AST_BREAK
                                ? MESSAGE_CHECKER_BREAK_OUTSIDE_LOOP
                                : MESSAGE_CHECKER_CONTINUE_OUTSIDE_LOOP);
            return false;
        }
        typed.kind = node->kind == AST_BREAK ? TYPED_BREAK : TYPED_CONTINUE;
        typed.type_index = TYPED_NO_NODE;
        typed.pos = node->pos;
        typed.left_child = TYPED_NO_NODE;
        typed.right_child = TYPED_NO_NODE;
        return emit_node(ctx, &typed, ast_index, out_typed);
    }

    error_report_at(ctx->errors, node->pos,
                    MESSAGE_CHECKER_UNSUPPORTED_AST_NODE);
    return false;
}

static bool check_block(CheckContext *ctx, size_t ast_index,
                        size_t return_type, size_t *out_typed)
{
    const AstNode *node = ast_get_node(ctx->ast, ast_index);
    List statements;
    size_t children_start;
    size_t i;
    TypedNode typed = {0};
    bool ok = false;

    if (node == NULL || node->kind != AST_BLOCK) {
        error_report_at(ctx->errors,
                        node == NULL ? (SourcePos){0, 1, 1} : node->pos,
                        MESSAGE_CHECKER_UNSUPPORTED_AST_NODE);
        return false;
    }

    list_init(&statements, sizeof(size_t));
    for (i = 0; i < node->children_count; i++) {
        size_t child = *(const size_t *)list_get(&ctx->ast->children,
                                                 node->children_start + i);
        size_t statement;

        if (!check_statement(ctx, child, return_type, &statement)) {
            goto done;
        }
        if (!list_push(&statements, &statement)) {
            error_report(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
            goto done;
        }
    }

    children_start = ctx->program->children.length;
    for (i = 0; i < statements.length; i++) {
        size_t statement = *(const size_t *)list_get(&statements, i);

        if (!typed_program_push_child(ctx->program, statement)) {
            error_report(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
            goto done;
        }
    }

    typed.kind = TYPED_BLOCK;
    typed.type_index = TYPED_NO_NODE;
    typed.pos = node->pos;
    typed.left_child = TYPED_NO_NODE;
    typed.right_child = TYPED_NO_NODE;
    typed.children_start = children_start;
    typed.children_count = statements.length;
    ok = emit_node(ctx, &typed, ast_index, out_typed);

done:
    list_free(&statements);
    return ok;
}

static bool check_function_declaration(CheckContext *ctx, const AstNode *node,
                                       size_t ast_index)
{
    const char *name = ast_symbol_text(ctx->ast, node->name_index);
    List parameter_types;
    size_t children_start = ctx->program->children.length;
    size_t return_type;
    size_t function_type;
    size_t typed_body;
    size_t typed_index;
    size_t i;
    TypedNode typed = {0};
    bool ok = false;

    if (name == NULL) {
        error_report_at(ctx->errors, node->pos,
                        MESSAGE_CHECKER_MISSING_NAME_OR_TYPE);
        return false;
    }
    if (find_type(ctx->types, name) != NULL ||
        find_function(ctx->functions, name) != NULL ||
        find_variable(ctx->globals, name) != NULL ||
        find_store(ctx->stores, name) != NULL ||
        strcmp(name, "Int32") == 0 || strcmp(name, "Bool") == 0 ||
        strcmp(name, "Nil") == 0 || strcmp(name, "Byte") == 0 ||
        strcmp(name, "Store") == 0 || strcmp(name, "Handle") == 0) {
        error_report_at(ctx->errors, node->pos, MESSAGE_CHECKER_DUPLICATE_NAME,
                        name);
        return false;
    }
    if (node->right_child == AST_NO_NODE) {
        error_report_at(ctx->errors, node->pos,
                        MESSAGE_CHECKER_MISSING_NAME_OR_TYPE);
        return false;
    }

    if (node->type_parameters_count > 0) {
        FunctionEntry generic = {0};

        if (node->type_parameters_count > NIPPY_MAX_TYPE_PARAMETERS) {
            error_report_at(ctx->errors, node->pos,
                            MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
            return false;
        }
        memcpy(generic.name, name, strlen(name) + 1);
        generic.type_index = TYPED_NO_NODE;
        generic.generic = true;
        generic.ast_index = ast_index;
        for (i = 0; i < node->type_parameters_count; i++) {
            size_t child = *(const size_t *)list_get(
                &ctx->ast->type_parameters,
                node->type_parameters_start + i);
            const AstNode *parameter = ast_get_node(ctx->ast, child);
            const char *parameter_name;

            if (parameter == NULL ||
                parameter->kind != AST_TYPE_PARAMETER) {
                error_report(ctx->errors, MESSAGE_CHECKER_MISSING_AST_NODE,
                             child);
                return false;
            }
            parameter_name = ast_symbol_text(ctx->ast,
                                             parameter->name_index);
            if (parameter_name == NULL) {
                error_report_at(ctx->errors, parameter->pos,
                                MESSAGE_CHECKER_MISSING_NAME_OR_TYPE);
                return false;
            }
            if (type_parameter_index(generic.parameters,
                                     generic.parameter_count,
                                     parameter_name) != TYPED_NO_NODE) {
                error_report_at(ctx->errors, parameter->pos,
                                MESSAGE_CHECKER_DUPLICATE_TYPE_PARAMETER,
                                parameter_name);
                return false;
            }
            memcpy(generic.parameters[generic.parameter_count],
                   parameter_name, strlen(parameter_name) + 1);
            generic.parameter_count++;
        }
        if (!list_push(ctx->functions, &generic)) {
            error_report(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
            return false;
        }
        return true;
    }

    list_init(&parameter_types, sizeof(size_t));
    ctx->in_function = true;
    ctx->locals->length = 0;

    for (i = 0; i < node->children_count; i++) {
        size_t child = *(const size_t *)list_get(&ctx->ast->children,
                                                 node->children_start + i);
        const AstNode *parameter = ast_get_node(ctx->ast, child);
        const char *parameter_name;
        size_t parameter_type;
        size_t parameter_index;
        VariableEntry entry;
        TypedNode typed_parameter = {0};

        if (parameter == NULL || parameter->kind != AST_PARAMETER) {
            error_report(ctx->errors, MESSAGE_CHECKER_MISSING_AST_NODE, child);
            goto done;
        }
        parameter_name = ast_symbol_text(ctx->ast, parameter->name_index);
        if (parameter_name == NULL) {
            error_report_at(ctx->errors, parameter->pos,
                            MESSAGE_CHECKER_MISSING_NAME_OR_TYPE);
            goto done;
        }
        if (find_variable(ctx->locals, parameter_name) != NULL ||
            find_store(ctx->stores, parameter_name) != NULL) {
            error_report_at(ctx->errors, parameter->pos,
                            MESSAGE_CHECKER_DUPLICATE_PARAMETER,
                            parameter_name);
            goto done;
        }
        if (!resolve_type_node(ctx, parameter->left_child, parameter_name,
                               &parameter_type)) {
            goto done;
        }
        {
            const TypedType *resolved =
                typed_program_type(ctx->program, parameter_type);

            if (resolved != NULL && resolved->kind == NIPPY_TYPE_STORE) {
                error_report_at(ctx->errors, parameter->pos,
                                MESSAGE_CHECKER_STORE_TYPE);
                goto done;
            }
        }

        typed_parameter.kind = TYPED_PARAMETER;
        typed_parameter.type_index = parameter_type;
        typed_parameter.pos = parameter->pos;
        typed_parameter.name_index = typed_program_copy_symbol(
            ctx->program, parameter_name, strlen(parameter_name));
        if (typed_parameter.name_index == TYPED_NO_NODE) {
            error_report(ctx->errors,
                         MESSAGE_CHECKER_OUT_OF_MEMORY_COPYING_NAME);
            goto done;
        }
        typed_parameter.left_child = TYPED_NO_NODE;
        typed_parameter.right_child = TYPED_NO_NODE;
        typed_parameter.children_start = 0;
        typed_parameter.children_count = 0;
        if (!typed_program_push(ctx->program, &typed_parameter,
                                &parameter_index) ||
            !typed_program_push_child(ctx->program, parameter_index) ||
            !list_push(&parameter_types, &parameter_type)) {
            error_report(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_PROGRAM);
            goto done;
        }

        memset(&entry, 0, sizeof(entry));
        memcpy(entry.name, parameter_name, strlen(parameter_name) + 1);
        entry.type_index = parameter_type;
        if (!list_push(ctx->locals, &entry)) {
            error_report(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
            goto done;
        }
    }

    if (!resolve_type_node(ctx, node->right_child, name, &return_type)) {
        goto done;
    }
    {
        const TypedType *resolved =
            typed_program_type(ctx->program, return_type);

        if (resolved != NULL && resolved->kind == NIPPY_TYPE_STORE) {
            error_report_at(ctx->errors, node->pos,
                            MESSAGE_CHECKER_STORE_TYPE);
            goto done;
        }
    }
    if (!add_function_type(ctx, &parameter_types, return_type, &function_type)) {
        goto done;
    }

    {
        FunctionEntry entry;

        memset(&entry, 0, sizeof(entry));
        memcpy(entry.name, name, strlen(name) + 1);
        entry.type_index = function_type;
        entry.generic = false;
        entry.ast_index = ast_index;
        if (!list_push(ctx->functions, &entry)) {
            error_report(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
            goto done;
        }
    }

    if (node->left_child == AST_NO_NODE) {
        error_report_at(ctx->errors, node->pos, MESSAGE_CHECKER_MISSING_RETURN,
                        name);
        goto done;
    }
    ctx->loop_depth = 0;
    ctx->function_name = name;
    if (!check_block(ctx, node->left_child, return_type, &typed_body)) {
        goto done;
    }
    if (!typed_block_returns(ctx->program, typed_body)) {
        error_report_at(ctx->errors, node->pos,
                        MESSAGE_CHECKER_FUNCTION_MAY_NOT_RETURN, name);
        goto done;
    }

    typed.kind = TYPED_FUNCTION_DECLARATION;
    typed.type_index = function_type;
    typed.pos = node->pos;
    typed.name_index = typed_program_copy_symbol(ctx->program, name,
                                                 strlen(name));
    if (typed.name_index == TYPED_NO_NODE) {
        error_report(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_COPYING_NAME);
        goto done;
    }
    typed.left_child = typed_body;
    typed.right_child = TYPED_NO_NODE;
    typed.children_start = children_start;
    typed.children_count = node->children_count;

    if (!emit_node(ctx, &typed, ast_index, &typed_index)) {
        goto done;
    }
    ok = true;

done:
    ctx->in_function = false;
    ctx->loop_depth = 0;
    ctx->function_name = NULL;
    ctx->locals->length = 0;
    list_free(&parameter_types);
    return ok;
}

bool checker_check(const Ast *ast, TypedProgram *program, ErrorState *errors)
{
    CheckContext ctx;
    TypeEnv empty_env = {0};
    List mapping;
    List types;
    List functions;
    List globals;
    List locals;
    List struct_instantiations;
    List function_instantiations;
    List narrowings;
    List stores;
    size_t i;
    bool ok = false;

    if (ast->roots.length == 0) {
        error_report(errors, MESSAGE_CHECKER_MISSING_AST_ROOT);
        return false;
    }

    list_init(&mapping, sizeof(size_t));
    list_init(&types, sizeof(TypeEntry));
    list_init(&functions, sizeof(FunctionEntry));
    list_init(&globals, sizeof(VariableEntry));
    list_init(&locals, sizeof(VariableEntry));
    list_init(&struct_instantiations, sizeof(StructInstantiation));
    list_init(&function_instantiations, sizeof(FunctionInstantiation));
    list_init(&narrowings, sizeof(Narrowing));
    list_init(&stores, sizeof(StoreEntry));

    ctx.ast = ast;
    ctx.program = program;
    ctx.errors = errors;
    ctx.mapping = &mapping;
    ctx.types = &types;
    ctx.functions = &functions;
    ctx.globals = &globals;
    ctx.locals = &locals;
    ctx.struct_instantiations = &struct_instantiations;
    ctx.function_instantiations = &function_instantiations;
    ctx.narrowings = &narrowings;
    ctx.stores = &stores;
    ctx.in_function = false;
    ctx.loop_depth = 0;
    ctx.function_name = NULL;
    ctx.depth = 0;
    ctx.generated_names = 0;
    ctx.env = &empty_env;

    for (i = 0; i < ast->nodes.length; i++) {
        size_t none = TYPED_NO_NODE;

        if (!list_push(&mapping, &none)) {
            error_report(errors, MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
            goto done;
        }
    }

    for (i = 0; i < ast->roots.length; i++) {
        size_t ast_root = *(const size_t *)list_get(&ast->roots, i);
        const AstNode *node = ast_get_node(ast, ast_root);
        size_t typed_root;
        bool checked = false;

        if (node == NULL) {
            error_report(errors, MESSAGE_CHECKER_MISSING_AST_NODE, ast_root);
            goto done;
        }

        switch (node->kind) {
        case AST_DECLARATION:
            checked = check_var_declaration(&ctx, node, ast_root);
            break;
        case AST_TYPE_DECLARATION:
        case AST_TYPE_ALIAS:
            checked = check_type_declaration(&ctx, node, ast_root);
            break;
        case AST_FUNCTION_DECLARATION:
            checked = check_function_declaration(&ctx, node, ast_root);
            break;
        case AST_EXPRESSION_STATEMENT:
            checked = check_expression_statement(&ctx, ast_root,
                                                 &(size_t){0});
            break;
        default:
            error_report_at(errors, node->pos,
                            MESSAGE_CHECKER_UNSUPPORTED_AST_NODE);
            goto done;
        }
        if (!checked) {
            goto done;
        }
        if (node->kind == AST_TYPE_DECLARATION ||
            node->kind == AST_TYPE_ALIAS) {
            continue;
        }

        typed_root = *mapping_slot(&mapping, ast_root);
        if (typed_root == TYPED_NO_NODE) {
            /* A generic function is a template; its instantiations add their
               own concrete roots when called. */
            continue;
        }
        if (!list_push(&program->roots, &typed_root)) {
            error_report(errors, MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
            goto done;
        }
    }
    ok = true;

done:
    list_free(&mapping);
    list_free(&types);
    list_free(&functions);
    list_free(&globals);
    list_free(&locals);
    list_free(&struct_instantiations);
    list_free(&function_instantiations);
    list_free(&narrowings);
    list_free(&stores);
    return ok;
}
