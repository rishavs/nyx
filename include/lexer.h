#ifndef NIPPY_LEXER_H
#define NIPPY_LEXER_H

#include "identifier.h"
#include "list.h"

#include <stdint.h>
#include "source_pos.h"

#include <stdbool.h>
#include <stddef.h>

typedef enum {
    TOKEN_KEYWORD_GLOBAL,
    TOKEN_KEYWORD_VAR,
    TOKEN_KEYWORD_CONST,
    TOKEN_KEYWORD_TYPE,
    TOKEN_KEYWORD_FUN,
    TOKEN_KEYWORD_METHOD,
    TOKEN_KEYWORD_STATIC,
    TOKEN_KEYWORD_RETURN,
    TOKEN_KEYWORD_IF,
    TOKEN_KEYWORD_THEN,
    TOKEN_KEYWORD_ELSEIF,
    TOKEN_KEYWORD_ELSE,
    TOKEN_KEYWORD_WHILE,
    TOKEN_KEYWORD_FOR,
    TOKEN_KEYWORD_TYPEOF,
    TOKEN_KEYWORD_IS,
    TOKEN_KEYWORD_MATCH,
    TOKEN_KEYWORD_WITH,
    TOKEN_KEYWORD_IN,
    TOKEN_KEYWORD_DO,
    TOKEN_KEYWORD_BREAK,
    TOKEN_KEYWORD_CONTINUE,
    TOKEN_KEYWORD_TRUE,
    TOKEN_KEYWORD_FALSE,
    TOKEN_KEYWORD_NIL,
    TOKEN_KEYWORD_END,
    TOKEN_IDENTIFIER,
    TOKEN_INTEGER,
    TOKEN_FLOAT,
    TOKEN_STRING,
    TOKEN_COLON,
    TOKEN_DOUBLE_COLON,
    TOKEN_EQUALS,
    TOKEN_EQEQ,
    TOKEN_EQEQEQ,
    TOKEN_NE,
    TOKEN_LPAREN,
    TOKEN_RPAREN,
    TOKEN_LBRACKET,
    TOKEN_RBRACKET,
    TOKEN_LBRACE,
    TOKEN_RBRACE,
    TOKEN_COMMA,
    TOKEN_PLUS,
    TOKEN_MINUS,
    TOKEN_STAR,
    TOKEN_SLASH,
    TOKEN_PERCENT,
    TOKEN_TILDE,
    TOKEN_AMP,
    TOKEN_CARET,
    TOKEN_SHL,
    TOKEN_SHR,
    TOKEN_LT,
    TOKEN_LE,
    TOKEN_GT,
    TOKEN_GE,
    TOKEN_ANDAND,
    TOKEN_OROR,
    TOKEN_PIPE,
    TOKEN_BANG,
    TOKEN_DOT,
    TOKEN_EOF
} TokenType;

/* Token values live inline in TokenList.items; tokens contain no pointers.
   An identifier carries its own text so the parser never has to reach back to
   the source string, which it is not given. */
typedef struct {
    TokenType type;
    int integer_value;
    uint64_t integer_value_u;
    double float_value;
    SourcePos pos;
    size_t length;
    char text[NIPPY_IDENTIFIER_MAX];
    /* TOKEN_STRING points into LexerResult.string_bytes. */
    size_t string_offset;
    size_t string_length;
} Token;

typedef List TokenList;

typedef enum {
    LEXER_UNEXPECTED_CHARACTER,
    LEXER_INTEGER_OUT_OF_RANGE,
    LEXER_FLOAT_OUT_OF_RANGE,
    LEXER_INVALID_DIGIT,
    LEXER_MISSING_DIGITS,
    LEXER_BAD_SEPARATOR,
    LEXER_UPPERCASE_PREFIX,
    LEXER_IDENTIFIER_TOO_LONG,
    LEXER_UNTERMINATED_STRING,
    LEXER_UNTERMINATED_COMMENT,
    LEXER_STRING_NEWLINE,
    LEXER_INVALID_ESCAPE
} LexerErrorKind;

typedef struct {
    LexerErrorKind kind;
    SourcePos pos;
    unsigned char character;
    /* The numeric base for a number-literal error. */
    unsigned base;
} LexerError;

typedef List LexerErrorList;

typedef struct {
    bool succeeded;
    bool out_of_memory;
    TokenList tokens;
    LexerErrorList errors;
    /* Decoded bytes for string literals; tokens store offsets into this. */
    List string_bytes;
} LexerResult;

LexerResult lexer_tokenize(const char *code);
void lexer_result_free(LexerResult *result);

#endif
