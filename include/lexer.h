#ifndef NIPPY_LEXER_H
#define NIPPY_LEXER_H

#include "identifier.h"
#include "list.h"
#include "source_pos.h"

#include <stdbool.h>
#include <stddef.h>

typedef enum {
    TOKEN_KEYWORD_VAR,
    TOKEN_KEYWORD_TYPE,
    TOKEN_KEYWORD_FUN,
    TOKEN_KEYWORD_RETURN,
    TOKEN_KEYWORD_IF,
    TOKEN_KEYWORD_THEN,
    TOKEN_KEYWORD_ELSEIF,
    TOKEN_KEYWORD_ELSE,
    TOKEN_KEYWORD_WHILE,
    TOKEN_KEYWORD_BREAK,
    TOKEN_KEYWORD_CONTINUE,
    TOKEN_KEYWORD_TRUE,
    TOKEN_KEYWORD_FALSE,
    TOKEN_KEYWORD_END,
    TOKEN_IDENTIFIER,
    TOKEN_INTEGER,
    TOKEN_COLON,
    TOKEN_EQUALS,
    TOKEN_EQEQ,
    TOKEN_NE,
    TOKEN_LPAREN,
    TOKEN_RPAREN,
    TOKEN_COMMA,
    TOKEN_PLUS,
    TOKEN_LT,
    TOKEN_LE,
    TOKEN_GT,
    TOKEN_GE,
    TOKEN_ANDAND,
    TOKEN_OROR,
    TOKEN_BANG,
    TOKEN_EOF
} TokenType;

/* Token values live inline in TokenList.items; tokens contain no pointers.
   An identifier carries its own text so the parser never has to reach back to
   the source string, which it is not given. */
typedef struct {
    TokenType type;
    int integer_value;
    SourcePos pos;
    size_t length;
    char text[NIPPY_IDENTIFIER_MAX];
} Token;

typedef List TokenList;

typedef enum {
    LEXER_UNEXPECTED_CHARACTER,
    LEXER_INTEGER_OUT_OF_RANGE,
    LEXER_IDENTIFIER_TOO_LONG
} LexerErrorKind;

typedef struct {
    LexerErrorKind kind;
    SourcePos pos;
    unsigned char character;
} LexerError;

typedef List LexerErrorList;

typedef struct {
    bool succeeded;
    bool out_of_memory;
    TokenList tokens;
    LexerErrorList errors;
} LexerResult;

LexerResult lexer_tokenize(const char *code);
void lexer_result_free(LexerResult *result);

#endif
