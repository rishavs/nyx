#ifndef NIPPY_TYPED_PROGRAM_H
#define NIPPY_TYPED_PROGRAM_H

#include "identifier.h"
#include "list.h"
#include "operator.h"
#include "source_pos.h"

#include <stdbool.h>
#include <stddef.h>

#define TYPED_NO_NODE ((size_t)-1)

/* Fixed-string capacity limit, and the default capacities of the built-in
   Error fields. The compiler config can override the Error capacities. */
#define NIPPY_DEFAULT_MAX_STRING_CAPACITY 4096
#define NIPPY_DEFAULT_MAX_LIST_CAPACITY 4096
#define NIPPY_DEFAULT_MAX_PRINT_DEPTH 16
#define NIPPY_MAX_PRINT_DEPTH 1024
#define NIPPY_MAX_STRING_CAPACITY (1024 * 1024)
#define NIPPY_DEFAULT_ERROR_HEADER_CAPACITY 32
#define NIPPY_DEFAULT_ERROR_DESCRIPTION_CAPACITY 128
#define NIPPY_DEFAULT_ERROR_FILENAME_CAPACITY 128

typedef enum {
    TYPED_DECLARATION,
    TYPED_LOCAL_DECLARATION,
    TYPED_TYPE_DECLARATION,
    TYPED_FUNCTION_DECLARATION,
    TYPED_FIELD,
    TYPED_PARAMETER,
    TYPED_BLOCK,
    TYPED_RETURN,
    TYPED_IF,
    TYPED_IF_EXPRESSION,
    TYPED_WHILE,
    TYPED_FOR,
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
    TYPED_EXPRESSION_STATEMENT,
    TYPED_PRINT,
    TYPED_ASSIGNMENT,
    TYPED_STRING_LITERAL,
    TYPED_STRING_LEN,
    TYPED_STRING_AT,
    TYPED_STRING_SLICE,
    TYPED_STRING_CONCAT,
    TYPED_STRING_COPY,
    TYPED_STRING_FROM_FIXED,
    TYPED_BUILDER_CREATE,
    TYPED_BUILDER_APPEND,
    TYPED_BUILDER_APPEND_BYTE,
    TYPED_BUILDER_LEN,
    TYPED_BUILDER_FINISH,
    TYPED_LIST_LITERAL,
    TYPED_RECORD_LITERAL,
    TYPED_RECORD_GET,
    TYPED_RECORD_SET,
    TYPED_RECORD_HAS,
    TYPED_RECORD_REMOVE,
    TYPED_RECORD_LEN,
    TYPED_TABLE_GET,
    TYPED_TABLE_SET,
    TYPED_TABLE_HAS,
    TYPED_TABLE_REMOVE,
    TYPED_TABLE_LEN,
    TYPED_TABLE_CREATE,
    TYPED_LIST_LEN,
    TYPED_LIST_GET,
    TYPED_LIST_SET,
    TYPED_MEMBER_ACCESS,
    TYPED_MEMBER_WRITE,
    TYPED_LIST_PUSH,
    TYPED_LIST_POP,
    TYPED_LIST_CREATE,
    TYPED_SLICE_CREATE,
    TYPED_SLICE_LEN,
    TYPED_SLICE_GET,
    TYPED_ERROR_CONSTRUCT,
    TYPED_ERROR_THROW,
    TYPED_ERROR_FIELD
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
    NIPPY_TYPE_HANDLE,
    NIPPY_TYPE_STRING,
    NIPPY_TYPE_DYNAMIC_STRING,
    NIPPY_TYPE_TEXT_BUILDER,
    NIPPY_TYPE_LIST,
    NIPPY_TYPE_FIXED_LIST,
    NIPPY_TYPE_SLICE,
    NIPPY_TYPE_RECORD,
    NIPPY_TYPE_TABLE,
    NIPPY_TYPE_STRING_LITERAL,
    NIPPY_TYPE_ERROR
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
    size_t fixed_length;      /* byte capacity for NIPPY_TYPE_STRING */
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
    /* TYPED_FOR: the value binding when an index binding is present. */
    size_t second_name_index;
    size_t left_child;
    size_t right_child;
    size_t children_start;
    size_t children_count;
    /* Secondary type link for TYPED_IS (the tested type). */
    size_t auxiliary_type;
    /* Global store id for the store/handle nodes; TYPED_NO_NODE otherwise. */
    size_t store_id;
    /* TYPED_STRING_LITERAL points into TypedProgram.bytes. */
    size_t string_offset;
    size_t string_length;
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
    /* Decoded bytes for string literals; nodes store offsets into this. */
    List bytes;
    /* Capacities used to build the built-in Error field strings. */
    int error_header_capacity;
    int error_description_capacity;
    int error_filename_capacity;
    int max_string_capacity;
    int max_list_capacity;
    int max_print_depth;
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
bool typed_program_push_byte(TypedProgram *program, unsigned char byte);
bool typed_program_push_child(TypedProgram *program, size_t index);

#endif
