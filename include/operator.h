#ifndef NYX_OPERATOR_H
#define NYX_OPERATOR_H

/* Operators are shared by the syntax tree and the typed program so that the
   checker can carry a source operator through to the generator unchanged. */
typedef enum {
    NYX_OPERATOR_NONE,
    NYX_OPERATOR_ADD,
    NYX_OPERATOR_SUB,
    NYX_OPERATOR_MUL,
    NYX_OPERATOR_DIV,
    NYX_OPERATOR_MOD,
    NYX_OPERATOR_BIT_AND,
    NYX_OPERATOR_BIT_OR,
    NYX_OPERATOR_BIT_XOR,
    NYX_OPERATOR_SHL,
    NYX_OPERATOR_SHR,
    NYX_OPERATOR_NEG,
    NYX_OPERATOR_BIT_NOT,
    NYX_OPERATOR_EQ,
    NYX_OPERATOR_STRICT_EQ,
    NYX_OPERATOR_NE,
    NYX_OPERATOR_LT,
    NYX_OPERATOR_LE,
    NYX_OPERATOR_GT,
    NYX_OPERATOR_GE,
    NYX_OPERATOR_AND,
    NYX_OPERATOR_OR,
    NYX_OPERATOR_NOT,
    NYX_OPERATOR_COUNT
} NyxOperator;

#endif
