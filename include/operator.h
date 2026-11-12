#ifndef NIPPY_OPERATOR_H
#define NIPPY_OPERATOR_H

/* Operators are shared by the syntax tree and the typed program so that the
   checker can carry a source operator through to the generator unchanged. */
typedef enum {
    NIPPY_OPERATOR_NONE,
    NIPPY_OPERATOR_ADD,
    NIPPY_OPERATOR_SUB,
    NIPPY_OPERATOR_MUL,
    NIPPY_OPERATOR_DIV,
    NIPPY_OPERATOR_MOD,
    NIPPY_OPERATOR_BIT_AND,
    NIPPY_OPERATOR_BIT_OR,
    NIPPY_OPERATOR_BIT_XOR,
    NIPPY_OPERATOR_SHL,
    NIPPY_OPERATOR_SHR,
    NIPPY_OPERATOR_NEG,
    NIPPY_OPERATOR_BIT_NOT,
    NIPPY_OPERATOR_EQ,
    NIPPY_OPERATOR_NE,
    NIPPY_OPERATOR_LT,
    NIPPY_OPERATOR_LE,
    NIPPY_OPERATOR_GT,
    NIPPY_OPERATOR_GE,
    NIPPY_OPERATOR_AND,
    NIPPY_OPERATOR_OR,
    NIPPY_OPERATOR_NOT
} NippyOperator;

#endif
