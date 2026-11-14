#ifndef NIPPY_CHECKER_INTERNAL_H
#define NIPPY_CHECKER_INTERNAL_H

#include "checker.h"

#include "alloc.h"
#include "identifier.h"
#include "messages.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


#define NIPPY_MAX_TYPE_PARAMETERS 8
#define NIPPY_TYPE_DEPTH 64
/* The most distinct instantiations of generic functions in one program. It
   stops a generic that instantiates itself with ever-growing types from
   running away; it is a count and has nothing to do with nesting depth. */
#define NIPPY_MAX_INSTANTIATIONS 4096

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
    char owner_name[NIPPY_IDENTIFIER_MAX];
    char method_name[NIPPY_IDENTIFIER_MAX];
    size_t type_index; /* TYPED_NO_NODE for a generic template */
    bool generic;
    bool is_method;
    bool is_static;
    size_t ast_index;
    char parameters[NIPPY_MAX_TYPE_PARAMETERS][NIPPY_IDENTIFIER_MAX];
    size_t parameter_count;
    size_t owner_parameter_count;
} FunctionEntry;

typedef struct {
    char name[NIPPY_IDENTIFIER_MAX];
    size_t type_index;
    bool is_const;
    bool has_constant;
    int constant_value;
    bool is_self_pointer;
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
    size_t path_ast; /* AST_NO_NODE for a bare variable */
    size_t type_index; /* narrowed type */
    size_t original_type; /* type of the underlying storage */
    size_t tag;
    bool active;
    bool alias_sensitive;
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
    NameIndex function_index;
    List *globals;                /* VariableEntry, program-level names */
    List *locals;                 /* VariableEntry, current function's parameters */
    List *struct_instantiations;  /* StructInstantiation */
    List *function_instantiations;/* FunctionInstantiation */
    List *narrowings;             /* Narrowing, scoped to a then block */
    List *stores;                 /* StoreEntry, program-level global stores */
    bool in_function;
    bool in_expression_statement;
    size_t statement_call_ast;
    size_t loop_depth;
    const char *function_name;
    size_t depth;
    size_t generated_names;
    size_t expected_ast;
    size_t expected_type;
    size_t assignment_target_ast;
    size_t match_scope;
    const TypeEnv *env; /* concrete substitutions while checking a generic body */
} CheckContext;


/* Internal helpers shared by the checker modules. */

bool emit_narrowed_value(CheckContext *ctx, size_t source,
                         const Narrowing *narrowing, size_t *out);
size_t *mapping_slot(List *mapping, size_t index);
size_t find_type_index(const List *types, const char *name);
const TypeEntry *find_type(const List *types, const char *name);
const FunctionEntry *find_function(const CheckContext *ctx, const char *name);
const FunctionEntry *find_associated_function(const List *functions,
    const char *owner_name, const char *method_name);
size_t find_function_index(const CheckContext *ctx, const char *name);
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
bool range_type(CheckContext *ctx, size_t element_type, size_t *out_index);
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
bool check_int32_expression(CheckContext *ctx, size_t ast_index, size_t *out);
bool check_expression_coerced(CheckContext *ctx, size_t ast_index,
                              size_t expected, size_t *out);
bool check_default_numeric(CheckContext *ctx, size_t value);
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
bool equality_type_supported(const TypedProgram *program, size_t type_index,
                             unsigned depth);
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
bool check_associated_function_call(CheckContext *ctx, const AstNode *node,
    const FunctionEntry *function, size_t ast_index, bool is_method);
bool check_expression(CheckContext *ctx, size_t ast_index, size_t *out_typed);
bool check_expression_expected(CheckContext *ctx, size_t ast_index,
    size_t expected_type, size_t *out_typed);
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
bool push_else_narrowing(CheckContext *ctx, size_t condition_ast);
void invalidate_narrowing(CheckContext *ctx, const char *name);
void invalidate_access_narrowing(CheckContext *ctx, size_t ast_index);
bool apply_access_narrowing(CheckContext *ctx, size_t ast_index);
void invalidate_accesses_for_expression(CheckContext *ctx, size_t ast_index);
void invalidate_aliased_narrowings(CheckContext *ctx);
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

bool literal_int_value(const Ast *ast, size_t ast_index, int *out);
bool check_negative_capacity(CheckContext *ctx, size_t capacity_ast);
bool check_fixed_index_bounds(CheckContext *ctx, size_t index_ast,
                              size_t receiver_type_index);

bool const_int_eval(CheckContext *ctx, size_t ast_index, int *out,
                    bool *is_constant);

bool check_inline_const_mutation(CheckContext *ctx, size_t ast_index);

bool type_contains_error(const TypedProgram *program, size_t type_index);

bool check_type_value_size(CheckContext *ctx, size_t type_index,
                           SourcePos pos);

bool numeric_type_from_name(const char *name, NippyType *out);

/* base64("...") builtin: decodes its string-literal argument at compile time. */
bool check_base64(CheckContext *ctx, const AstNode *node, size_t ast_index);
/* The builtins that are called like functions and so cannot be redeclared. */
bool is_builtin_function_name(const char *name);

/* A numeric literal, possibly negated, read back out of the typed program.
   Integers keep a sign and a 64-bit magnitude so every integer type's range is
   decidable; floats keep the value with its sign. */
typedef struct {
    bool is_float;
    bool negative;
    uint64_t magnitude;
    double real;
} NumericConstant;

/* Reads a literal or a negated literal. With `allow_converted`, also reads
   through an explicit conversion that was folded at compile time: such a
   constant already has its final type and must not be retyped by context. */
bool numeric_constant_read(const TypedProgram *program, size_t node_index,
                           bool allow_converted, NumericConstant *out);
bool integer_constant_fits(NippyType kind, bool negative, uint64_t magnitude);
bool float_constant_fits(NippyType kind, double value);
NippyType unsigned_counterpart(NippyType kind);
/* Converts a constant to `target`, truncating a float toward zero. Returns
   false when the result is not representable in the target. */
bool numeric_constant_convert(const NumericConstant *in, NippyType target,
                              NumericConstant *out);
/* Reports a range error at `pos` and returns false when `constant` does not
   fit `kind`. */
bool numeric_constant_check_range(CheckContext *ctx, SourcePos pos,
                                  const NumericConstant *constant,
                                  NippyType kind);
/* Gives a contextual literal (or negated literal) its final numeric type. The
   caller has already checked the range. */
bool numeric_constant_retype(CheckContext *ctx, size_t node_index,
                             size_t target_index, NippyType target_kind);
/* Appends a synthesized literal node holding `value` as `kind`. A negative
   integer becomes a negation of a literal typed as the unsigned counterpart. */
bool numeric_constant_emit(CheckContext *ctx, const NumericConstant *value,
                           NippyType kind, size_t target_index, SourcePos pos,
                           size_t *out_index);

#endif
