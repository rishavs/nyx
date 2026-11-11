#ifndef NIPPY_TYPED_PROGRAM_H
#define NIPPY_TYPED_PROGRAM_H

#include "identifier.h"
#include "list.h"
#include "operator.h"
#include "source_pos.h"

#include <stdbool.h>
#include <stddef.h>

#define TYPED_NO_NODE ((size_t)-1)

typedef enum {
    TYPED_DECLARATION,
    TYPED_TYPE_DECLARATION,
    TYPED_FUNCTION_DECLARATION,
    TYPED_FIELD,
    TYPED_PARAMETER,
    TYPED_BLOCK,
    TYPED_RETURN,
    TYPED_IF,
    TYPED_WHILE,
    TYPED_BREAK,
    TYPED_CONTINUE,
    TYPED_INTEGER_LITERAL,
    TYPED_BOOL_LITERAL,
    TYPED_IDENTIFIER,
    TYPED_STRUCT_CONSTRUCTOR,
    TYPED_FUNCTION_CALL,
    TYPED_BINARY,
    TYPED_UNARY,
    TYPED_NIL_LITERAL,
    TYPED_SUM_INJECT,
    TYPED_SUM_PAYLOAD,
    TYPED_IS,
    TYPED_STORE_DECLARATION,
    TYPED_STORE_ALLOC,
    TYPED_STORE_RESET,
    TYPED_HANDLE_READ,
    TYPED_HANDLE_WRITE,
    TYPED_EXPRESSION_STATEMENT
} TypedNodeKind;

typedef enum {
    NIPPY_TYPE_UNKNOWN,
    NIPPY_TYPE_INT32,
    NIPPY_TYPE_BOOL,
    NIPPY_TYPE_BYTE,
    NIPPY_TYPE_NIL,
    NIPPY_TYPE_STRUCT,
    NIPPY_TYPE_FUNCTION,
    NIPPY_TYPE_UNION,
    NIPPY_TYPE_STORE,
    NIPPY_TYPE_HANDLE
} NippyType;

/* A resolved type. Struct types refer to their declaration name and, for an
   instantiated generic, a run of type arguments. Union types refer to a
   canonical run of alternatives. Function types refer to a signature record.
   The record holds indices only, never pointers, so it can be stored inline in
   a List. */
typedef struct {
    NippyType kind;
    size_t name_index;      /* TYPED_NO_NODE unless NIPPY_TYPE_STRUCT */
    size_t signature_index; /* TYPED_NO_NODE unless NIPPY_TYPE_FUNCTION */
    size_t first_child;     /* union alternatives or struct type arguments */
    size_t child_count;
    size_t declaration_node; /* TYPED_NO_NODE unless NIPPY_TYPE_STRUCT */
    size_t source_name_index; /* source struct name, TYPED_NO_NODE otherwise */
} TypedType;

typedef struct {
    size_t first_parameter; /* index into TypedProgram.signature_parameters */
    size_t parameter_count;
    size_t return_type;     /* index into TypedProgram.types */
} TypedFunctionSignature;

/* Symbols are copied, not borrowed: the typed program owns its text so that it
   outlives the syntax tree it was checked from. */
typedef struct {
    char text[NIPPY_IDENTIFIER_MAX];
} TypedSymbol;

typedef struct {
    TypedNodeKind kind;
    size_t type_index;
    SourcePos pos;
    int integer_value;
    NippyOperator op;
    size_t name_index;
    size_t left_child;
    size_t right_child;
    size_t children_start;
    size_t children_count;
    /* Secondary type link for TYPED_IS (the tested type). */
    size_t auxiliary_type;
    /* Global store id for the store/handle nodes; TYPED_NO_NODE otherwise. */
    size_t store_id;
} TypedNode;

typedef struct {
    List nodes;
    List symbols;
    /* Indices into `nodes` of the top-level declarations, in source order. */
    List roots;
    /* Indices into `nodes` for variable-arity children. */
    List children;
    /* Resolved types, function signatures, and the parameter types those
       signatures refer to. */
    List types;
    List signatures;
    List signature_parameters;
    /* Runs of type indices: union alternatives and struct type arguments. */
    List type_children;
} TypedProgram;

void typed_program_init(TypedProgram *program);
void typed_program_free(TypedProgram *program);
const TypedNode *typed_program_node(const TypedProgram *program, size_t index);
bool typed_program_push(TypedProgram *program, const TypedNode *node,
                        size_t *out_index);
size_t typed_program_copy_symbol(TypedProgram *program, const char *text,
                                 size_t length);
const char *typed_program_symbol_text(const TypedProgram *program, size_t index);
const TypedType *typed_program_type(const TypedProgram *program, size_t index);
bool typed_program_add_type(TypedProgram *program, const TypedType *type,
                            size_t *out_index);
const TypedFunctionSignature *typed_program_signature(
    const TypedProgram *program, size_t index);
bool typed_program_add_signature(TypedProgram *program,
                                 const TypedFunctionSignature *signature,
                                 size_t *out_index);
bool typed_program_add_signature_parameter(TypedProgram *program,
                                           size_t type_index);
bool typed_program_add_type_child(TypedProgram *program, size_t type_index);
bool typed_program_push_child(TypedProgram *program, size_t index);

#endif
