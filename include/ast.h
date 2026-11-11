#ifndef NIPPY_AST_H
#define NIPPY_AST_H

#include "identifier.h"
#include "list.h"
#include "operator.h"
#include "source_pos.h"

#include <stddef.h>

#define AST_NO_NODE ((size_t)-1)

typedef enum {
    AST_DECLARATION,
    AST_TYPE_DECLARATION,
    AST_FUNCTION_DECLARATION,
    AST_FIELD,
    AST_PARAMETER,
    AST_BLOCK,
    AST_RETURN,
    AST_IF,
    AST_WHILE,
    AST_BREAK,
    AST_CONTINUE,
    AST_INTEGER_LITERAL,
    AST_BOOL_LITERAL,
    AST_IDENTIFIER,
    AST_CALL,
    AST_BINARY,
    AST_UNARY,
    AST_NAMED_ARG,
    AST_TYPE_NAME,
    AST_TYPE_FUNCTION,
    AST_TYPE_UNION,
    AST_TYPE_APPLICATION,
    AST_TYPE_PARAMETER,
    AST_TYPE_ALIAS,
    AST_IS,
    AST_MEMBER_CALL,
    AST_EXPRESSION_STATEMENT,
    AST_NIL_LITERAL
} AstNodeKind;

/* Names live inline: a List element may not hold a pointer, so a symbol is a
   fixed-size character array and nodes refer to it by index. */
typedef struct {
    char text[NIPPY_IDENTIFIER_MAX];
} AstSymbol;

/* The syntax tree carries no resolved types. A node records only syntax: what
   kind it is, what literal value it holds, and which names, children, and type
   nodes it refers to. Variable-arity children live in Ast.children and a node
   records their start and count, so a child expression can push its own nodes
   without breaking the run. */
typedef struct {
    AstNodeKind kind;
    SourcePos pos;
    int integer_value;
    NippyOperator op;
    size_t name_index;
    size_t left_child;
    size_t right_child;
    size_t children_start;
    size_t children_count;
    size_t type_parameters_start;
    size_t type_parameters_count;
} AstNode;

typedef struct {
    List nodes;
    List symbols;
    /* Indices into `nodes` of the top-level declarations, in source order. */
    List roots;
    /* Indices into `nodes` for variable-arity children: type declaration
       fields, function parameters, call arguments, union alternatives, type
       arguments, and function-type parameters. */
    List children;
    /* Indices into `nodes` of AST_TYPE_PARAMETER declarations. Kept separate
       from `children` so a generic declaration's value children and type
       parameters never share a run. */
    List type_parameters;
} Ast;

void ast_init(Ast *ast);
void ast_free(Ast *ast);
const AstNode *ast_get_node(const Ast *ast, size_t index);
size_t ast_intern_symbol(Ast *ast, const char *text, size_t length);
const char *ast_symbol_text(const Ast *ast, size_t index);
bool ast_push_child(Ast *ast, size_t index);
bool ast_push_type_parameter(Ast *ast, size_t index);

#endif
