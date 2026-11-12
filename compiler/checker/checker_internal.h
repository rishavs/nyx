#ifndef NIPPY_CHECKER_INTERNAL_H
#define NIPPY_CHECKER_INTERNAL_H

#include "checker.h"

#include "alloc.h"
#include "identifier.h"
#include "messages.h"

#include <stdbool.h>
#include <stddef.h>
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
    bool is_const;
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
    bool active;
} Narrowing;

typedef struct {
    const char *names[NIPPY_MAX_TYPE_PARAMETERS];
    size_t types[NIPPY_MAX_TYPE_PARAMETERS]; /* TYPED_NO_NODE until bound */
    size_t count;
} InferEnv;


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
    bool in_expression_statement;
    size_t loop_depth;
    const char *function_name;
    size_t depth;
    size_t generated_names;
    const TypeEnv *env; /* concrete substitutions while checking a generic body */
} CheckContext;


/* Internal helpers shared by the checker modules. */

size_t *mapping_slot(List *mapping, size_t index);
size_t find_type_index(const List *types, const char *name);
const TypeEntry *find_type(const List *types, const char *name);
const FunctionEntry *find_function(const List *functions, const char *name);
size_t find_function_index(const List *functions, const char *name);
const VariableEntry *find_variable(const List *variables, const char *name);
const StoreEntry *find_store(const List *stores, const char *name);
bool identifier_is_reserved(const char *name);
bool check_reserved_name(CheckContext *ctx, SourcePos pos, const char *name);
bool string_type(CheckContext *ctx, int capacity, size_t *out_index);
bool string_literal_type(CheckContext *ctx, size_t *out_index);
bool error_type(CheckContext *ctx, size_t *out_index);
bool error_field_type(CheckContext *ctx, size_t error_index, size_t field,
    size_t *out_type);
bool is_sized_type(const TypedProgram *program, size_t type_index);
bool is_fixed_type_depth(const TypedProgram *program, size_t type_index,
    unsigned depth);
bool is_fixed_type(const TypedProgram *program, size_t type_index);
bool handle_type(CheckContext *ctx, size_t element, size_t *out_index);
bool add_type_record(CheckContext *ctx, NippyType kind, size_t name_index,
    size_t signature_index, size_t *out_index);
bool builtin_type(CheckContext *ctx, NippyType kind, size_t *out_index);
bool text_builder_type(CheckContext *ctx, size_t *out_index);
bool is_list_element_type(CheckContext *ctx, size_t element_type, bool fixed);
bool map_type(CheckContext *ctx, size_t key_type, size_t value_type, bool fixed,
    size_t capacity, size_t *out_index);
bool list_type(CheckContext *ctx, size_t element_type, bool fixed, size_t capacity,
    size_t *out_index);
bool slice_type(CheckContext *ctx, size_t element_type, size_t *out_index);
bool add_function_type(CheckContext *ctx, const List *parameter_types,
    size_t return_type, size_t *out_index);
bool same_type_depth(const TypedProgram *program, size_t a, size_t b, unsigned depth);
bool same_type(const TypedProgram *program, size_t a, size_t b);
int type_rank(NippyType kind);
int type_compare_depth(const TypedProgram *program, size_t a, size_t b, unsigned depth);
int type_compare(const TypedProgram *program, size_t a, size_t b);
bool type_in_list(const TypedProgram *program, const List *indices, size_t type_index);
bool intern_union(CheckContext *ctx, const List *alternatives, size_t *out_index);
bool union_has_alternative(const TypedProgram *program, const TypedType *union_type,
    size_t wanted, size_t *out_tag);
size_t type_parameter_index(char parameters[][NIPPY_IDENTIFIER_MAX], size_t count,
    const char *name);
bool env_lookup(const TypeEnv *env, const char *name, size_t *out);
void type_ast_append(const char *text, char *buffer, size_t size, size_t *used);
bool type_ast_text(const Ast *ast, size_t ast_index, char *buffer, size_t size);
bool instantiate_struct(CheckContext *ctx, size_t entry_index,
    const size_t *arguments, size_t argument_count, size_t *out_type);
bool expand_alias(CheckContext *ctx, const TypeEntry *entry, const size_t *arguments,
    size_t argument_count, size_t *out_type);
bool resolve_type_name(CheckContext *ctx, const AstNode *node, const TypeEnv *env,
    const char *owner, size_t *out_index);
bool resolve_type_application(CheckContext *ctx, const AstNode *node,
    const TypeEnv *env, const char *owner, size_t *out_index);
bool resolve_type_in_env(CheckContext *ctx, size_t ast_index, const TypeEnv *env,
    const char *owner, size_t *out_index);
bool resolve_type_node(CheckContext *ctx, size_t ast_index, const char *owner,
    size_t *out_index);
bool emit_node(CheckContext *ctx, const TypedNode *node, size_t ast_index,
    size_t *out_index);
bool check_nil_literal(CheckContext *ctx, const AstNode *node, size_t ast_index);
bool push_injection(CheckContext *ctx, size_t value, size_t target, size_t tag,
    size_t *out_value);
bool inject_into(CheckContext *ctx, size_t value, size_t target, size_t *out_value);
bool lookup_field(CheckContext *ctx, size_t receiver_type, const char *name,
    size_t *out_type, bool *out_error);
bool field_is_lvalue(const Ast *ast, size_t ast_index);
bool check_member_access(CheckContext *ctx, const AstNode *node, size_t ast_index);
bool check_member_assignment(CheckContext *ctx, const AstNode *node, size_t ast_index,
    size_t *out_typed);
bool check_index_expression(CheckContext *ctx, const AstNode *node, size_t ast_index);
bool check_index_assignment(CheckContext *ctx, const AstNode *node, size_t ast_index,
    size_t *out_typed);
bool check_record_literal(CheckContext *ctx, const AstNode *node, size_t ast_index);
bool check_list_literal(CheckContext *ctx, const AstNode *node, size_t ast_index);
bool check_string_literal(CheckContext *ctx, const AstNode *node, size_t ast_index);
bool check_is(CheckContext *ctx, const AstNode *node, size_t ast_index);
bool check_literal(CheckContext *ctx, const AstNode *node, size_t ast_index);
bool check_identifier(CheckContext *ctx, const AstNode *node, size_t ast_index);
const char *operator_text(NippyOperator op);
bool add_builtin_type(CheckContext *ctx, NippyType kind, size_t *out_index);
bool check_bool_literal(CheckContext *ctx, const AstNode *node, size_t ast_index);
bool check_binary(CheckContext *ctx, const AstNode *node, size_t ast_index);
bool check_unary(CheckContext *ctx, const AstNode *node, size_t ast_index);
const char *struct_field_name(const TypedProgram *program, const TypedNode *type_decl,
    size_t field_index);
size_t struct_field_type(const TypedProgram *program, const TypedNode *type_decl,
    size_t field_index);
size_t struct_declaration_node(const CheckContext *ctx, const TypeEntry *entry);
size_t infer_find(const InferEnv *infer, const char *name);
void infer_type_env(const InferEnv *infer, TypeEnv *env);
bool ast_type_has_unbound(CheckContext *ctx, size_t ast_index, const InferEnv *infer,
    unsigned depth);
bool infer_ast_type(CheckContext *ctx, size_t ast_type, size_t concrete,
    InferEnv *infer, const char *owner);
const char *infer_first_unbound(const InferEnv *infer);
bool infer_finish(CheckContext *ctx, InferEnv *infer, TypeEnv *env, const char *owner);
bool constructor_value_fits(CheckContext *ctx, size_t value, const TypeEntry *entry,
    size_t field_index, size_t argument_index, SourcePos pos, size_t *out_value);
const AstNode *template_field(const CheckContext *ctx, const AstNode *declaration,
    size_t index, size_t *out_ast);
size_t template_field_index(const CheckContext *ctx, const AstNode *declaration,
    const char *name);
bool check_generic_constructor(CheckContext *ctx, const AstNode *node,
    size_t entry_index, size_t ast_index);
bool check_constructor(CheckContext *ctx, const AstNode *node, const TypeEntry *entry,
    size_t ast_index);
bool check_function_call(CheckContext *ctx, const AstNode *node, const char *name,
    size_t type_index, size_t ast_index);
bool check_error_throw(CheckContext *ctx, const AstNode *node, size_t ast_index);
bool check_error_constructor(CheckContext *ctx, const AstNode *node, size_t ast_index);
bool is_printable_type_depth(const TypedProgram *program, size_t type_index,
    unsigned depth);
bool is_printable_type(const TypedProgram *program, size_t type_index);
bool check_print(CheckContext *ctx, const AstNode *node, size_t ast_index);
bool check_call(CheckContext *ctx, const AstNode *node, size_t ast_index);
FunctionInstantiation *find_function_instantiation(CheckContext *ctx,
    size_t entry_index, const size_t *arguments, size_t argument_count);
bool check_generic_body(CheckContext *ctx, size_t entry_index, size_t name_index,
    const TypeEnv *env, size_t function_type, size_t return_type);
bool check_generic_call(CheckContext *ctx, const AstNode *node, size_t function_index,
    size_t ast_index);
bool member_store(CheckContext *ctx, const AstNode *node, const char *method,
    const StoreEntry **out_store);
bool check_member_call(CheckContext *ctx, const AstNode *node, size_t ast_index);
bool check_expression(CheckContext *ctx, size_t ast_index, size_t *out_typed);
bool eval_const_int(CheckContext *ctx, size_t ast_index, int *out);
bool check_store_declaration(CheckContext *ctx, const AstNode *node, const char *name,
    size_t ast_index, size_t store_type);
bool check_var_declaration(CheckContext *ctx, const AstNode *node, size_t ast_index);
bool check_type_declaration(CheckContext *ctx, const AstNode *node, size_t ast_index);
bool check_condition(CheckContext *ctx, size_t ast_index, size_t *out_typed);
bool typed_statement_returns(const TypedProgram *program, size_t index);
bool typed_statement_terminates(const TypedProgram *program, size_t index);
bool typed_block_returns(const TypedProgram *program, size_t index);
bool push_if_narrowing(CheckContext *ctx, size_t condition_ast);
void invalidate_narrowing(CheckContext *ctx, const char *name);
bool is_builtin_type_name(const char *name);
bool check_assignment(CheckContext *ctx, const AstNode *node, size_t ast_index,
    size_t *out_typed);
bool check_expression_statement(CheckContext *ctx, size_t ast_index, size_t *out_typed);
bool check_for_binding(CheckContext *ctx, const char *name, size_t type_index,
    SourcePos pos);
bool check_if_expression(CheckContext *ctx, const AstNode *node, size_t ast_index);
bool push_match_narrowing(CheckContext *ctx, size_t head_ast, size_t tested,
    size_t tag);
bool check_match(CheckContext *ctx, const AstNode *node, size_t ast_index,
    bool expression_form, size_t return_type, size_t *out_typed);
bool check_for(CheckContext *ctx, const AstNode *node, size_t ast_index,
    size_t return_type, size_t *out_typed);
bool check_local_declaration(CheckContext *ctx, const AstNode *node, size_t ast_index,
    size_t *out_typed);
bool check_statement(CheckContext *ctx, size_t ast_index, size_t return_type,
    size_t *out_typed);
bool check_block(CheckContext *ctx, size_t ast_index, size_t return_type,
    size_t *out_typed);
bool check_function_declaration(CheckContext *ctx, const AstNode *node,
    size_t ast_index);
bool checker_check(const Ast *ast, TypedProgram *program, ErrorState *errors);
bool check_collection_method(CheckContext *ctx, const AstNode *node,
    const char *method, size_t ast_index, bool *handled);
bool check_string_method(CheckContext *ctx, const AstNode *node, const char *method,
    size_t ast_index, bool *handled);
bool check_error_method(CheckContext *ctx, const AstNode *node, const char *method,
    size_t ast_index, bool *handled);
bool check_store_method(CheckContext *ctx, const AstNode *node, const char *method,
    size_t ast_index, bool *handled);

#endif
