#include "lexer.h"

#include <ctype.h>
#include <float.h>
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

/* The value of a digit character in any base up to 16, or UINT_MAX when the
   character is not a digit. */
static unsigned digit_value(unsigned char character)
{
    if (character >= '0' && character <= '9') {
        return (unsigned)(character - '0');
    }
    if (character >= 'a' && character <= 'f') {
        return (unsigned)(character - 'a') + 10;
    }
    if (character >= 'A' && character <= 'F') {
        return (unsigned)(character - 'A') + 10;
    }
    return UINT_MAX;
}


static bool push_lexer_error(LexerResult *result, LexerErrorKind kind,
                             SourcePos pos, unsigned char character,
                             unsigned base)
{
    LexerError error = {kind, pos, character, base};

    if (!list_push(&result->errors, &error)) {
        result->out_of_memory = true;
        return false;
    }
    return true;
}


/* Scans a run of digits of `base`. A single '_' is allowed between two digits;
   any other '_' sets *bad_separator but is still consumed so the whole literal
   is skipped once. The digits, without separators, are appended to `digits`.
   Returns false only when out of memory. */
static bool scan_digits(const char *code, size_t *scan, unsigned base,
                        List *digits, size_t *count, bool *bad_separator)
{
    bool previous_is_digit = false;

    for (;;) {
        unsigned char character = (unsigned char)code[*scan];

        if (digit_value(character) < base) {
            if (!list_push(digits, &character)) {
                return false;
            }
            (*count)++;
            previous_is_digit = true;
        } else if (character == '_') {
            if (!previous_is_digit ||
                digit_value((unsigned char)code[*scan + 1]) >= base) {
                *bad_separator = true;
            }
            previous_is_digit = false;
        } else {
            return true;
        }
        (*scan)++;
    }
}


/* The integer value of collected digits, or false when it needs more than 64
   bits. Accumulating by hand keeps separators out of the parse and makes the
   overflow test exact for every base. */
static bool digits_to_integer(const List *digits, unsigned base,
                              uint64_t *out)
{
    const unsigned char *items = digits->items;
    uint64_t value = 0;
    size_t i;

    for (i = 0; i < digits->length; i++) {
        unsigned digit = digit_value(items[i]);

        if (value > (UINT64_MAX - digit) / base) {
            return false;
        }
        value = value * base + digit;
    }
    *out = value;
    return true;
}


static bool is_identifier_character(unsigned char character)
{
    return isalnum(character) || character == '_';
}


/* Lexes one numeric literal starting at code[*cursor], which is a digit.

     integer = digits | "0x" hex | "0b" binary | "0o" octal
     float   = digits "." digits [exponent] | digits exponent

   Digits of every kind may be grouped with single underscores. A literal that
   is malformed is reported once and skipped whole. Returns false only when out
   of memory, after setting out_of_memory. */
static bool lex_number(const char *code, LexerResult *result, size_t *cursor,
                       size_t line, size_t *column)
{
    SourcePos start = {*cursor, line, *column};
    size_t scan = *cursor;
    unsigned base = 10;
    bool radix = false;
    bool is_float = false;
    bool bad_separator = false;
    size_t count = 0;
    List digits;
    Token token = {0};
    bool ok = false;

    list_init(&digits, sizeof(unsigned char));
    if (code[scan] == '0') {
        unsigned char marker = (unsigned char)code[scan + 1];

        if (marker == 'x' || marker == 'b' || marker == 'o') {
            base = marker == 'x' ? 16 : marker == 'b' ? 2 : 8;
            radix = true;
            scan += 2;
        } else if (marker == 'X' || marker == 'B' || marker == 'O') {
            /* One spelling: the prefix is lowercase. */
            while (is_identifier_character((unsigned char)code[scan])) {
                scan++;
            }
            ok = push_lexer_error(result, LEXER_UPPERCASE_PREFIX, start,
                                  marker, 0);
            goto finished;
        }
    }

    if (!scan_digits(code, &scan, base, &digits, &count, &bad_separator)) {
        result->out_of_memory = true;
        goto finished;
    }
    if (radix) {
        unsigned char next = (unsigned char)code[scan];
        bool fraction = next == '.' && isdigit((unsigned char)code[scan + 1]);

        if (isalnum(next) || fraction) {
            /* A digit that does not belong to the base, such as 0b102, or a
               fraction on a radix literal. Skip the rest of the literal. */
            while (is_identifier_character((unsigned char)code[scan])) {
                scan++;
            }
            ok = push_lexer_error(result, LEXER_INVALID_DIGIT, start, next,
                                  base);
            goto finished;
        }
        if (!bad_separator && count == 0) {
            ok = push_lexer_error(result, LEXER_MISSING_DIGITS, start, 0, base);
            goto finished;
        }
    } else {
        if (code[scan] == '.' && isdigit((unsigned char)code[scan + 1])) {
            unsigned char point = '.';

            is_float = true;
            if (!list_push(&digits, &point)) {
                result->out_of_memory = true;
                goto finished;
            }
            scan++;
            if (!scan_digits(code, &scan, 10, &digits, &count,
                             &bad_separator)) {
                result->out_of_memory = true;
                goto finished;
            }
        }
        if (code[scan] == 'e' || code[scan] == 'E') {
            size_t exponent = scan + 1;
            unsigned char sign = 0;

            if (code[exponent] == '+' || code[exponent] == '-') {
                sign = (unsigned char)code[exponent];
                exponent++;
            }
            if (isdigit((unsigned char)code[exponent])) {
                unsigned char marker = 'e';

                is_float = true;
                if (!list_push(&digits, &marker) ||
                    (sign != 0 && !list_push(&digits, &sign))) {
                    result->out_of_memory = true;
                    goto finished;
                }
                scan = exponent;
                if (!scan_digits(code, &scan, 10, &digits, &count,
                                 &bad_separator)) {
                    result->out_of_memory = true;
                    goto finished;
                }
            }
        }
    }

    if (bad_separator) {
        ok = push_lexer_error(result, LEXER_BAD_SEPARATOR, start, 0, base);
        goto finished;
    }
    if (is_float) {
        unsigned char terminator = '\0';
        double parsed;

        if (!list_push(&digits, &terminator)) {
            result->out_of_memory = true;
            goto finished;
        }
        parsed = strtod(digits.items, NULL);
        /* Only overflow is an error. strtod also sets ERANGE for a denormal
           result, which is a valid literal. */
        if (parsed > DBL_MAX) {
            ok = push_lexer_error(result, LEXER_FLOAT_OUT_OF_RANGE, start, 0,
                                  10);
            goto finished;
        }
        token.type = TOKEN_FLOAT;
        token.float_value = parsed;
    } else {
        uint64_t value;

        if (!digits_to_integer(&digits, base, &value)) {
            ok = push_lexer_error(result, LEXER_INTEGER_OUT_OF_RANGE, start, 0,
                                  base);
            goto finished;
        }
        token.type = TOKEN_INTEGER;
        token.integer_value_u = value;
        token.integer_value = value <= INT_MAX ? (int)value : 0;
    }
    token.pos = start;
    if (!list_push(&result->tokens, &token)) {
        result->out_of_memory = true;
        goto finished;
    }
    ok = true;

finished:
    list_free(&digits);
    /* Numbers hold no newline, so the column advances by the bytes consumed. */
    *column += scan - *cursor;
    *cursor = scan;
    return ok;
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

        if (current == '-' && code[cursor + 1] == '-') {
            SourcePos start = {cursor, line, column};

            advance(code, &cursor, &line, &column);
            advance(code, &cursor, &line, &column);
            if (code[cursor] == '[') {
                bool terminated = false;

                advance(code, &cursor, &line, &column);
                while (code[cursor] != '\0') {
                    if (code[cursor] == ']' && code[cursor + 1] == '-' &&
                        code[cursor + 2] == '-') {
                        advance(code, &cursor, &line, &column);
                        advance(code, &cursor, &line, &column);
                        advance(code, &cursor, &line, &column);
                        terminated = true;
                        break;
                    }
                    advance(code, &cursor, &line, &column);
                }
                if (!terminated) {
                    LexerError error = {LEXER_UNTERMINATED_COMMENT, start, 0,
                                        0};
                    if (!list_push(&result.errors, &error)) {
                        result.out_of_memory = true;
                        goto failed;
                    }
                    break;
                }
            } else {
                while (code[cursor] != '\0' && code[cursor] != '\n') {
                    advance(code, &cursor, &line, &column);
                }
            }
            continue;
        }

        if (isspace(current)) {
            advance(code, &cursor, &line, &column);
            continue;
        }

        if (isdigit(current)) {
            if (!lex_number(code, &result, &cursor, line, &column)) {
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
                    LexerError error = {LEXER_UNTERMINATED_STRING, start, 0, 0};
                    if (!list_push(&result.errors, &error)) {
                        result.out_of_memory = true;
                        goto failed;
                    }
                    literal_ok = false;
                    break;
                }
                if (byte == '\n' || byte == '\r') {
                    LexerError error = {LEXER_STRING_NEWLINE,
                                        (SourcePos){cursor, line, column}, 0,
                                        0};
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
                                            0, 0};
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
                                            escaped, 0};
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
                LexerError error = {LEXER_IDENTIFIER_TOO_LONG, start, 0, 0};
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
            LexerError error = {LEXER_UNEXPECTED_CHARACTER, start, current, 0};

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
