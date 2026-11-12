#include "lexer.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

static TokenType keyword_type(const char *text, size_t length)
{
    if (length == 6 && memcmp(text, "global", 6) == 0) {
        return TOKEN_KEYWORD_GLOBAL;
    }
    if (length == 3 && memcmp(text, "var", 3) == 0) {
        return TOKEN_KEYWORD_VAR;
    }
    if (length == 5 && memcmp(text, "const", 5) == 0) {
        return TOKEN_KEYWORD_CONST;
    }
    if (length == 4 && memcmp(text, "type", 4) == 0) {
        return TOKEN_KEYWORD_TYPE;
    }
    if (length == 3 && memcmp(text, "fun", 3) == 0) {
        return TOKEN_KEYWORD_FUN;
    }
    if (length == 6 && memcmp(text, "return", 6) == 0) {
        return TOKEN_KEYWORD_RETURN;
    }
    if (length == 2 && memcmp(text, "if", 2) == 0) {
        return TOKEN_KEYWORD_IF;
    }
    if (length == 4 && memcmp(text, "then", 4) == 0) {
        return TOKEN_KEYWORD_THEN;
    }
    if (length == 6 && memcmp(text, "elseif", 6) == 0) {
        return TOKEN_KEYWORD_ELSEIF;
    }
    if (length == 4 && memcmp(text, "else", 4) == 0) {
        return TOKEN_KEYWORD_ELSE;
    }
    if (length == 5 && memcmp(text, "while", 5) == 0) {
        return TOKEN_KEYWORD_WHILE;
    }
    if (length == 3 && memcmp(text, "for", 3) == 0) {
        return TOKEN_KEYWORD_FOR;
    }
    if (length == 6 && memcmp(text, "typeof", 6) == 0) {
        return TOKEN_KEYWORD_TYPEOF;
    }
    if (length == 2 && memcmp(text, "is", 2) == 0) {
        return TOKEN_KEYWORD_IS;
    }
    if (length == 5 && memcmp(text, "match", 5) == 0) {
        return TOKEN_KEYWORD_MATCH;
    }
    if (length == 4 && memcmp(text, "with", 4) == 0) {
        return TOKEN_KEYWORD_WITH;
    }
    if (length == 2 && memcmp(text, "in", 2) == 0) {
        return TOKEN_KEYWORD_IN;
    }
    if (length == 2 && memcmp(text, "do", 2) == 0) {
        return TOKEN_KEYWORD_DO;
    }
    if (length == 5 && memcmp(text, "break", 5) == 0) {
        return TOKEN_KEYWORD_BREAK;
    }
    if (length == 8 && memcmp(text, "continue", 8) == 0) {
        return TOKEN_KEYWORD_CONTINUE;
    }
    if (length == 4 && memcmp(text, "true", 4) == 0) {
        return TOKEN_KEYWORD_TRUE;
    }
    if (length == 5 && memcmp(text, "false", 5) == 0) {
        return TOKEN_KEYWORD_FALSE;
    }
    if (length == 3 && memcmp(text, "nil", 3) == 0) {
        return TOKEN_KEYWORD_NIL;
    }
    if (length == 3 && memcmp(text, "end", 3) == 0) {
        return TOKEN_KEYWORD_END;
    }
    return TOKEN_IDENTIFIER;
}

/* Advance one character, keeping line and column in step so a position can be
   recorded for every token and error without a second pass over the text. */
static void advance(const char *code, size_t *cursor, size_t *line,
                    size_t *column)
{
    if (code[*cursor] == '\n') {
        (*line)++;
        *column = 1;
    } else {
        (*column)++;
    }
    (*cursor)++;
}

static bool push_token(TokenList *tokens, TokenType type, SourcePos pos)
{
    Token token = {0};

    token.type = type;
    token.pos = pos;
    return list_push(tokens, &token);
}

void lexer_result_free(LexerResult *result)
{
    list_free(&result->tokens);
    list_free(&result->errors);
    list_free(&result->string_bytes);
}

LexerResult lexer_tokenize(const char *code)
{
    LexerResult result = {0};
    size_t cursor = 0;
    size_t line = 1;
    size_t column = 1;

    list_init(&result.tokens, sizeof(Token));
    list_init(&result.errors, sizeof(LexerError));
    list_init(&result.string_bytes, sizeof(unsigned char));

    while (code[cursor] != '\0') {
        unsigned char current = (unsigned char)code[cursor];

        if (isspace(current)) {
            advance(code, &cursor, &line, &column);
            continue;
        }

        if (isdigit(current)) {
            char *end;
            long parsed;
            SourcePos start = {cursor, line, column};
            Token token = {0};

            errno = 0;
            parsed = strtol(code + cursor, &end, 10);
            cursor = (size_t)(end - code);
            /* Integer literals contain no newlines, so column advances by the
               number of digits consumed. */
            column += cursor - start.offset;

            if (errno == ERANGE || parsed > INT_MAX) {
                LexerError error = {LEXER_INTEGER_OUT_OF_RANGE, start, 0};
                if (!list_push(&result.errors, &error)) {
                    result.out_of_memory = true;
                    goto failed;
                }
                continue;
            }

            token.type = TOKEN_INTEGER;
            token.integer_value = (int)parsed;
            token.pos = start;
            if (!list_push(&result.tokens, &token)) {
                result.out_of_memory = true;
                goto failed;
            }
            continue;
        }

        if (current == '"') {
            SourcePos start = {cursor, line, column};
            size_t byte_start = result.string_bytes.length;
            bool literal_ok = true;
            Token token = {0};

            advance(code, &cursor, &line, &column);
            for (;;) {
                unsigned char byte = (unsigned char)code[cursor];

                if (byte == '\0') {
                    LexerError error = {LEXER_UNTERMINATED_STRING, start, 0};
                    if (!list_push(&result.errors, &error)) {
                        result.out_of_memory = true;
                        goto failed;
                    }
                    literal_ok = false;
                    break;
                }
                if (byte == '\n' || byte == '\r') {
                    LexerError error = {LEXER_STRING_NEWLINE,
                                        (SourcePos){cursor, line, column}, 0};
                    if (!list_push(&result.errors, &error)) {
                        result.out_of_memory = true;
                        goto failed;
                    }
                    literal_ok = false;
                    break;
                }
                if (byte == '"') {
                    advance(code, &cursor, &line, &column);
                    break;
                }
                if (byte == '\\') {
                    unsigned char escaped;

                    advance(code, &cursor, &line, &column);
                    escaped = (unsigned char)code[cursor];
                    if (escaped == '\0') {
                        LexerError error = {LEXER_UNTERMINATED_STRING, start,
                                            0};
                        if (!list_push(&result.errors, &error)) {
                            result.out_of_memory = true;
                            goto failed;
                        }
                        literal_ok = false;
                        break;
                    }
                    if (escaped == 'n') {
                        byte = '\n';
                    } else if (escaped == 't') {
                        byte = '\t';
                    } else if (escaped == 'r') {
                        byte = '\r';
                    } else if (escaped == '\\') {
                        byte = '\\';
                    } else if (escaped == '"') {
                        byte = '"';
                    } else {
                        LexerError error = {LEXER_INVALID_ESCAPE,
                                            (SourcePos){cursor, line, column},
                                            escaped};
                        if (!list_push(&result.errors, &error)) {
                            result.out_of_memory = true;
                            goto failed;
                        }
                        literal_ok = false;
                        advance(code, &cursor, &line, &column);
                        continue;
                    }
                    advance(code, &cursor, &line, &column);
                } else {
                    advance(code, &cursor, &line, &column);
                }
                if (!list_push(&result.string_bytes, &byte)) {
                    result.out_of_memory = true;
                    goto failed;
                }
            }
            if (!literal_ok) {
                result.string_bytes.length = byte_start;
                continue;
            }
            token.type = TOKEN_STRING;
            token.pos = start;
            token.string_offset = byte_start;
            token.string_length = result.string_bytes.length - byte_start;
            if (!list_push(&result.tokens, &token)) {
                result.out_of_memory = true;
                goto failed;
            }
            continue;
        }

        if (isalpha(current) || current == '_') {
            SourcePos start = {cursor, line, column};
            size_t length;
            Token token = {0};

            while (isalnum((unsigned char)code[cursor]) || code[cursor] == '_') {
                cursor++;
            }
            length = cursor - start.offset;
            column += length;

            /* The buffer also needs room for the terminating NUL. */
            if (length >= NIPPY_IDENTIFIER_MAX) {
                LexerError error = {LEXER_IDENTIFIER_TOO_LONG, start, 0};
                if (!list_push(&result.errors, &error)) {
                    result.out_of_memory = true;
                    goto failed;
                }
                continue;
            }

            token.type = keyword_type(code + start.offset, length);
            token.pos = start;
            token.length = length;
            memcpy(token.text, code + start.offset, length);
            token.text[length] = '\0';
            if (!list_push(&result.tokens, &token)) {
                result.out_of_memory = true;
                goto failed;
            }
            continue;
        }

        if (current == ':') {
            SourcePos start = {cursor, line, column};

            if (!push_token(&result.tokens, TOKEN_COLON, start)) {
                result.out_of_memory = true;
                goto failed;
            }
            advance(code, &cursor, &line, &column);
            continue;
        }

        if (current == '=') {
            SourcePos start = {cursor, line, column};
            bool doubled = code[cursor + 1] == '=';

            if (!push_token(&result.tokens,
                            doubled ? TOKEN_EQEQ : TOKEN_EQUALS, start)) {
                result.out_of_memory = true;
                goto failed;
            }
            advance(code, &cursor, &line, &column);
            if (doubled) {
                advance(code, &cursor, &line, &column);
            }
            continue;
        }

        if (current == '(') {
            SourcePos start = {cursor, line, column};

            if (!push_token(&result.tokens, TOKEN_LPAREN, start)) {
                result.out_of_memory = true;
                goto failed;
            }
            advance(code, &cursor, &line, &column);
            continue;
        }

        if (current == ')') {
            SourcePos start = {cursor, line, column};

            if (!push_token(&result.tokens, TOKEN_RPAREN, start)) {
                result.out_of_memory = true;
                goto failed;
            }
            advance(code, &cursor, &line, &column);
            continue;
        }

        if (current == '[') {
            SourcePos start = {cursor, line, column};

            if (!push_token(&result.tokens, TOKEN_LBRACKET, start)) {
                result.out_of_memory = true;
                goto failed;
            }
            advance(code, &cursor, &line, &column);
            continue;
        }

        if (current == ']') {
            SourcePos start = {cursor, line, column};

            if (!push_token(&result.tokens, TOKEN_RBRACKET, start)) {
                result.out_of_memory = true;
                goto failed;
            }
            advance(code, &cursor, &line, &column);
            continue;
        }

        if (current == '{') {
            SourcePos start = {cursor, line, column};

            if (!push_token(&result.tokens, TOKEN_LBRACE, start)) {
                result.out_of_memory = true;
                goto failed;
            }
            advance(code, &cursor, &line, &column);
            continue;
        }

        if (current == '}') {
            SourcePos start = {cursor, line, column};

            if (!push_token(&result.tokens, TOKEN_RBRACE, start)) {
                result.out_of_memory = true;
                goto failed;
            }
            advance(code, &cursor, &line, &column);
            continue;
        }

        if (current == ',') {
            SourcePos start = {cursor, line, column};

            if (!push_token(&result.tokens, TOKEN_COMMA, start)) {
                result.out_of_memory = true;
                goto failed;
            }
            advance(code, &cursor, &line, &column);
            continue;
        }

        if (current == '.') {
            SourcePos start = {cursor, line, column};

            if (!push_token(&result.tokens, TOKEN_DOT, start)) {
                result.out_of_memory = true;
                goto failed;
            }
            advance(code, &cursor, &line, &column);
            continue;
        }

        if (current == '+') {
            SourcePos start = {cursor, line, column};

            if (!push_token(&result.tokens, TOKEN_PLUS, start)) {
                result.out_of_memory = true;
                goto failed;
            }
            advance(code, &cursor, &line, &column);
            continue;
        }

        if (current == '-') {
            SourcePos start = {cursor, line, column};

            if (!push_token(&result.tokens, TOKEN_MINUS, start)) {
                result.out_of_memory = true;
                goto failed;
            }
            advance(code, &cursor, &line, &column);
            continue;
        }

        if (current == '*') {
            SourcePos start = {cursor, line, column};

            if (!push_token(&result.tokens, TOKEN_STAR, start)) {
                result.out_of_memory = true;
                goto failed;
            }
            advance(code, &cursor, &line, &column);
            continue;
        }

        if (current == '/') {
            SourcePos start = {cursor, line, column};

            if (!push_token(&result.tokens, TOKEN_SLASH, start)) {
                result.out_of_memory = true;
                goto failed;
            }
            advance(code, &cursor, &line, &column);
            continue;
        }

        if (current == '%') {
            SourcePos start = {cursor, line, column};

            if (!push_token(&result.tokens, TOKEN_PERCENT, start)) {
                result.out_of_memory = true;
                goto failed;
            }
            advance(code, &cursor, &line, &column);
            continue;
        }

        if (current == '~') {
            SourcePos start = {cursor, line, column};

            if (!push_token(&result.tokens, TOKEN_TILDE, start)) {
                result.out_of_memory = true;
                goto failed;
            }
            advance(code, &cursor, &line, &column);
            continue;
        }

        if (current == '^') {
            SourcePos start = {cursor, line, column};

            if (!push_token(&result.tokens, TOKEN_CARET, start)) {
                result.out_of_memory = true;
                goto failed;
            }
            advance(code, &cursor, &line, &column);
            continue;
        }

        if (current == '<') {
            SourcePos start = {cursor, line, column};
            bool with_equals = code[cursor + 1] == '=';

            if (!push_token(&result.tokens,
                            with_equals ? TOKEN_LE : TOKEN_LT, start)) {
                result.out_of_memory = true;
                goto failed;
            }
            advance(code, &cursor, &line, &column);
            if (with_equals) {
                advance(code, &cursor, &line, &column);
            }
            continue;
        }

        if (current == '>') {
            SourcePos start = {cursor, line, column};
            bool with_equals = code[cursor + 1] == '=';

            if (!push_token(&result.tokens,
                            with_equals ? TOKEN_GE : TOKEN_GT, start)) {
                result.out_of_memory = true;
                goto failed;
            }
            advance(code, &cursor, &line, &column);
            if (with_equals) {
                advance(code, &cursor, &line, &column);
            }
            continue;
        }

        if (current == '!') {
            SourcePos start = {cursor, line, column};
            bool with_equals = code[cursor + 1] == '=';

            if (!push_token(&result.tokens,
                            with_equals ? TOKEN_NE : TOKEN_BANG, start)) {
                result.out_of_memory = true;
                goto failed;
            }
            advance(code, &cursor, &line, &column);
            if (with_equals) {
                advance(code, &cursor, &line, &column);
            }
            continue;
        }

        if (current == '&' && code[cursor + 1] == '&') {
            SourcePos start = {cursor, line, column};

            if (!push_token(&result.tokens, TOKEN_ANDAND, start)) {
                result.out_of_memory = true;
                goto failed;
            }
            advance(code, &cursor, &line, &column);
            advance(code, &cursor, &line, &column);
            continue;
        }

        if (current == '&') {
            SourcePos start = {cursor, line, column};

            if (!push_token(&result.tokens, TOKEN_AMP, start)) {
                result.out_of_memory = true;
                goto failed;
            }
            advance(code, &cursor, &line, &column);
            continue;
        }

        if (current == '|') {
            SourcePos start = {cursor, line, column};
            bool doubled = code[cursor + 1] == '|';

            if (!push_token(&result.tokens, doubled ? TOKEN_OROR : TOKEN_PIPE,
                            start)) {
                result.out_of_memory = true;
                goto failed;
            }
            advance(code, &cursor, &line, &column);
            if (doubled) {
                advance(code, &cursor, &line, &column);
            }
            continue;
        }

        {
            SourcePos start = {cursor, line, column};
            LexerError error = {LEXER_UNEXPECTED_CHARACTER, start, current};

            if (!list_push(&result.errors, &error)) {
                result.out_of_memory = true;
                goto failed;
            }
        }
        advance(code, &cursor, &line, &column);
    }

    if (result.errors.length != 0) {
        list_free(&result.tokens);
        return result;
    }

    {
        SourcePos eof = {cursor, line, column};

        if (!push_token(&result.tokens, TOKEN_EOF, eof)) {
            result.out_of_memory = true;
            goto failed;
        }
    }

    result.succeeded = true;
    return result;

failed:
    list_free(&result.tokens);
    list_free(&result.errors);
    list_free(&result.string_bytes);
    return result;
}
