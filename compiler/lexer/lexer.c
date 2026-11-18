#include "lexer.h"

#include <ctype.h>
#include <float.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *text;
    size_t length;
    TokenType type;
} Keyword;

#define KEYWORD(text, type) {text, sizeof(text) - 1, type}

static const Keyword keywords[] = {
    KEYWORD("global", TOKEN_KEYWORD_GLOBAL),
    KEYWORD("var", TOKEN_KEYWORD_VAR),
    KEYWORD("const", TOKEN_KEYWORD_CONST),
    KEYWORD("type", TOKEN_KEYWORD_TYPE),
    KEYWORD("fun", TOKEN_KEYWORD_FUN),
    KEYWORD("method", TOKEN_KEYWORD_METHOD),
    KEYWORD("static", TOKEN_KEYWORD_STATIC),
    KEYWORD("return", TOKEN_KEYWORD_RETURN),
    KEYWORD("if", TOKEN_KEYWORD_IF),
    KEYWORD("then", TOKEN_KEYWORD_THEN),
    KEYWORD("elseif", TOKEN_KEYWORD_ELSEIF),
    KEYWORD("else", TOKEN_KEYWORD_ELSE),
    KEYWORD("while", TOKEN_KEYWORD_WHILE),
    KEYWORD("for", TOKEN_KEYWORD_FOR),
    KEYWORD("typeof", TOKEN_KEYWORD_TYPEOF),
    KEYWORD("is", TOKEN_KEYWORD_IS),
    KEYWORD("match", TOKEN_KEYWORD_MATCH),
    KEYWORD("with", TOKEN_KEYWORD_WITH),
    KEYWORD("in", TOKEN_KEYWORD_IN),
    KEYWORD("do", TOKEN_KEYWORD_DO),
    KEYWORD("break", TOKEN_KEYWORD_BREAK),
    KEYWORD("continue", TOKEN_KEYWORD_CONTINUE),
    KEYWORD("true", TOKEN_KEYWORD_TRUE),
    KEYWORD("false", TOKEN_KEYWORD_FALSE),
    KEYWORD("nil", TOKEN_KEYWORD_NIL),
    KEYWORD("end", TOKEN_KEYWORD_END),
    KEYWORD("scope", TOKEN_KEYWORD_SCOPE),
    KEYWORD("defer", TOKEN_KEYWORD_DEFER),
    KEYWORD("parallel", TOKEN_KEYWORD_PARALLEL),
    KEYWORD("when", TOKEN_KEYWORD_WHEN),
};

#undef KEYWORD

static TokenType keyword_type(const char *text, size_t length)
{
    size_t i;

    for (i = 0; i < sizeof(keywords) / sizeof(keywords[0]); i++) {
        if (keywords[i].length == length &&
            memcmp(text, keywords[i].text, length) == 0) {
            return keywords[i].type;
        }
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


/* Everything the scanners share: the text, where the cursor is, and the
   result being built. Line and column move with the cursor so a position can
   be recorded for any token or error. */
typedef struct {
    const char *code;
    size_t cursor;
    size_t line;
    size_t column;
    LexerResult *result;
    bool stopped; /* an unterminated comment ends the scan */
} Lexer;

static unsigned char peek(const Lexer *lexer, size_t ahead)
{
    return (unsigned char)lexer->code[lexer->cursor + ahead];
}

static SourcePos here(const Lexer *lexer)
{
    return (SourcePos){lexer->cursor, lexer->line, lexer->column};
}

static void step(Lexer *lexer)
{
    advance(lexer->code, &lexer->cursor, &lexer->line, &lexer->column);
}

static bool starts_with(const Lexer *lexer, const char *text, size_t length)
{
    return strncmp(lexer->code + lexer->cursor, text, length) == 0;
}

/* The emitters return false only when out of memory, after recording it. */
static bool emit_token(Lexer *lexer, const Token *token)
{
    if (!list_push(&lexer->result->tokens, token)) {
        lexer->result->out_of_memory = true;
        return false;
    }
    return true;
}

static bool emit_error(Lexer *lexer, LexerErrorKind kind, SourcePos pos,
                       unsigned char character, unsigned base)
{
    return push_lexer_error(lexer->result, kind, pos, character, base);
}

/* A number being scanned. `scan` runs ahead of the lexer's cursor, which moves
   once the whole literal has been consumed. */
typedef struct {
    SourcePos start;
    size_t scan;
    unsigned base;
    bool radix;
    bool is_float;
    bool bad_separator;
    size_t count;
    List digits;
} Number;

typedef enum {
    NUMBER_CONTINUE, /* the literal may be valid so far */
    NUMBER_SKIPPED,  /* an error was reported; the literal is skipped whole */
    NUMBER_OOM
} NumberStatus;

static NumberStatus number_error(Lexer *lexer, const Number *number,
                                 LexerErrorKind kind, unsigned char character,
                                 unsigned base)
{
    return emit_error(lexer, kind, number->start, character, base)
               ? NUMBER_SKIPPED
               : NUMBER_OOM;
}

static unsigned char number_at(const Lexer *lexer, const Number *number,
                               size_t ahead)
{
    return (unsigned char)lexer->code[number->scan + ahead];
}

static void skip_word(const Lexer *lexer, Number *number)
{
    while (is_identifier_character(number_at(lexer, number, 0))) {
        number->scan++;
    }
}

static bool number_digits(Lexer *lexer, Number *number, unsigned base)
{
    if (scan_digits(lexer->code, &number->scan, base, &number->digits,
                    &number->count, &number->bad_separator)) {
        return true;
    }
    lexer->result->out_of_memory = true;
    return false;
}

/* The optional 0x, 0b or 0o prefix, then the leading digits. */
static NumberStatus number_prefix(Lexer *lexer, Number *number)
{
    unsigned char marker = number_at(lexer, number, 1);

    if (number_at(lexer, number, 0) == '0' && strchr("xbo", marker) != NULL &&
        marker != '\0') {
        number->base = marker == 'x' ? 16 : marker == 'b' ? 2 : 8;
        number->radix = true;
        number->scan += 2;
    } else if (number_at(lexer, number, 0) == '0' &&
               strchr("XBO", marker) != NULL && marker != '\0') {
        /* One spelling: the prefix is lowercase. */
        skip_word(lexer, number);
        return number_error(lexer, number, LEXER_UPPERCASE_PREFIX, marker, 0);
    }
    return number_digits(lexer, number, number->base) ? NUMBER_CONTINUE
                                                      : NUMBER_OOM;
}

/* A radix literal ends at its digits: no fraction, no stray letter. */
static NumberStatus number_radix_end(Lexer *lexer, Number *number)
{
    unsigned char next = number_at(lexer, number, 0);
    bool fraction = next == '.' && isdigit(number_at(lexer, number, 1));

    if (isalnum(next) || fraction) {
        /* A digit that does not belong to the base, such as 0b102, or a
           fraction on a radix literal. Skip the rest of the literal. */
        skip_word(lexer, number);
        return number_error(lexer, number, LEXER_INVALID_DIGIT, next,
                            number->base);
    }
    if (!number->bad_separator && number->count == 0) {
        return number_error(lexer, number, LEXER_MISSING_DIGITS, 0,
                            number->base);
    }
    return NUMBER_CONTINUE;
}

static bool number_push(Lexer *lexer, Number *number, unsigned char character)
{
    if (list_push(&number->digits, &character)) {
        return true;
    }
    lexer->result->out_of_memory = true;
    return false;
}

static bool number_fraction(Lexer *lexer, Number *number)
{
    if (number_at(lexer, number, 0) != '.' ||
        !isdigit(number_at(lexer, number, 1))) {
        return true;
    }
    number->is_float = true;
    number->scan++;
    return number_push(lexer, number, '.') &&
           number_digits(lexer, number, 10);
}

static bool number_exponent(Lexer *lexer, Number *number)
{
    size_t exponent = number->scan + 1;
    unsigned char sign = 0;

    if (number_at(lexer, number, 0) != 'e' &&
        number_at(lexer, number, 0) != 'E') {
        return true;
    }
    if (lexer->code[exponent] == '+' || lexer->code[exponent] == '-') {
        sign = (unsigned char)lexer->code[exponent];
        exponent++;
    }
    if (!isdigit((unsigned char)lexer->code[exponent])) {
        return true;
    }
    number->is_float = true;
    number->scan = exponent;
    return number_push(lexer, number, 'e') &&
           (sign == 0 || number_push(lexer, number, sign)) &&
           number_digits(lexer, number, 10);
}

static NumberStatus number_float_token(Lexer *lexer, Number *number,
                                       Token *token)
{
    double parsed;

    if (!number_push(lexer, number, '\0')) {
        return NUMBER_OOM;
    }
    parsed = strtod(number->digits.items, NULL);
    /* Only overflow is an error. strtod also sets ERANGE for a denormal
       result, which is a valid literal. */
    if (parsed > DBL_MAX) {
        return number_error(lexer, number, LEXER_FLOAT_OUT_OF_RANGE, 0, 10);
    }
    token->type = TOKEN_FLOAT;
    token->float_value = parsed;
    return NUMBER_CONTINUE;
}

static NumberStatus number_integer_token(Lexer *lexer, Number *number,
                                         Token *token)
{
    uint64_t value;

    if (!digits_to_integer(&number->digits, number->base, &value)) {
        return number_error(lexer, number, LEXER_INTEGER_OUT_OF_RANGE, 0,
                            number->base);
    }
    token->type = TOKEN_INTEGER;
    token->integer_value_u = value;
    token->integer_value = value <= INT_MAX ? (int)value : 0;
    return NUMBER_CONTINUE;
}

static NumberStatus number_finish(Lexer *lexer, Number *number)
{
    Token token = {0};
    NumberStatus status;

    if (number->bad_separator) {
        return number_error(lexer, number, LEXER_BAD_SEPARATOR, 0,
                            number->base);
    }
    status = number->is_float ? number_float_token(lexer, number, &token)
                              : number_integer_token(lexer, number, &token);
    if (status != NUMBER_CONTINUE) {
        return status;
    }
    token.pos = number->start;
    return emit_token(lexer, &token) ? NUMBER_CONTINUE : NUMBER_OOM;
}

/* Scans a float's fraction and exponent, which a radix literal has none of. */
static NumberStatus number_decimal_tail(Lexer *lexer, Number *number)
{
    return number_fraction(lexer, number) && number_exponent(lexer, number)
               ? NUMBER_CONTINUE
               : NUMBER_OOM;
}

static NumberStatus number_scan(Lexer *lexer, Number *number)
{
    NumberStatus status = number_prefix(lexer, number);

    if (status != NUMBER_CONTINUE) {
        return status;
    }
    status = number->radix ? number_radix_end(lexer, number)
                           : number_decimal_tail(lexer, number);
    return status == NUMBER_CONTINUE ? number_finish(lexer, number) : status;
}

/* Lexes one numeric literal starting at the cursor, which is a digit.

     integer = digits | "0x" hex | "0b" binary | "0o" octal
     float   = digits "." digits [exponent] | digits exponent

   Digits of every kind may be grouped with single underscores. A literal that
   is malformed is reported once and skipped whole. Returns false only when out
   of memory, after setting out_of_memory. */
static bool lex_number(Lexer *lexer)
{
    Number number = {here(lexer), lexer->cursor, 10, false, false, false, 0,
                     {0}};
    NumberStatus status;

    list_init(&number.digits, sizeof(unsigned char));
    status = number_scan(lexer, &number);
    list_free(&number.digits);
    /* Numbers hold no newline, so the column advances by the bytes consumed. */
    lexer->column += number.scan - lexer->cursor;
    lexer->cursor = number.scan;
    return status != NUMBER_OOM;
}

void lexer_result_free(LexerResult *result)
{
    list_free(&result->tokens);
    list_free(&result->errors);
    list_free(&result->string_bytes);
}

/* Punctuation, longest spelling first within each starting character. */
typedef struct {
    const char *text;
    size_t length;
    TokenType type;
} Punctuation;

#define PUNCT(text, type) {text, sizeof(text) - 1, type}

static const Punctuation punctuation[] = {
    PUNCT("===", TOKEN_EQEQEQ), PUNCT("==", TOKEN_EQEQ),
    PUNCT("=", TOKEN_EQUALS), PUNCT("::", TOKEN_DOUBLE_COLON),
    PUNCT(":", TOKEN_COLON), PUNCT("<=", TOKEN_LE), PUNCT("<", TOKEN_LT),
    PUNCT(">=", TOKEN_GE), PUNCT(">", TOKEN_GT), PUNCT("!=", TOKEN_NE),
    PUNCT("!", TOKEN_BANG), PUNCT("&&", TOKEN_ANDAND), PUNCT("&", TOKEN_AMP),
    PUNCT("||", TOKEN_OROR), PUNCT("|", TOKEN_PIPE),
    PUNCT("(", TOKEN_LPAREN), PUNCT(")", TOKEN_RPAREN),
    PUNCT("[", TOKEN_LBRACKET), PUNCT("]", TOKEN_RBRACKET),
    PUNCT("{", TOKEN_LBRACE), PUNCT("}", TOKEN_RBRACE),
    PUNCT(",", TOKEN_COMMA), PUNCT(".", TOKEN_DOT), PUNCT("+", TOKEN_PLUS),
    PUNCT("-", TOKEN_MINUS), PUNCT("*", TOKEN_STAR), PUNCT("/", TOKEN_SLASH),
    PUNCT("%", TOKEN_PERCENT), PUNCT("~", TOKEN_TILDE),
    PUNCT("^", TOKEN_CARET),
};

#undef PUNCT

static bool lex_punctuation(Lexer *lexer)
{
    SourcePos start = here(lexer);
    size_t i;

    for (i = 0; i < sizeof(punctuation) / sizeof(punctuation[0]); i++) {
        const Punctuation *entry = &punctuation[i];
        size_t consumed;

        if (!starts_with(lexer, entry->text, entry->length)) {
            continue;
        }
        if (!push_token(&lexer->result->tokens, entry->type, start)) {
            lexer->result->out_of_memory = true;
            return false;
        }
        for (consumed = 0; consumed < entry->length; consumed++) {
            step(lexer);
        }
        return true;
    }
    step(lexer);
    return emit_error(lexer, LEXER_UNEXPECTED_CHARACTER, start,
                      (unsigned char)lexer->code[start.offset], 0);
}

/* A comment: "--" to the end of the line, or "--[" to "]--". */
static bool lex_comment(Lexer *lexer)
{
    SourcePos start = here(lexer);
    bool block;

    step(lexer);
    step(lexer);
    block = peek(lexer, 0) == '[';
    if (!block) {
        while (peek(lexer, 0) != '\0' && peek(lexer, 0) != '\n') {
            step(lexer);
        }
        return true;
    }
    step(lexer);
    while (peek(lexer, 0) != '\0' && !starts_with(lexer, "]--", 3)) {
        step(lexer);
    }
    if (peek(lexer, 0) == '\0') {
        lexer->stopped = true;
        return emit_error(lexer, LEXER_UNTERMINATED_COMMENT, start, 0, 0);
    }
    step(lexer);
    step(lexer);
    step(lexer);
    return true;
}

typedef struct {
    unsigned char escape;
    unsigned char value;
} Escape;

static const Escape escapes[] = {
    {'n', '\n'}, {'t', '\t'}, {'r', '\r'}, {'\\', '\\'}, {'"', '"'},
};

static bool escape_value(unsigned char escape, unsigned char *value)
{
    size_t i;

    for (i = 0; i < sizeof(escapes) / sizeof(escapes[0]); i++) {
        if (escapes[i].escape == escape) {
            *value = escapes[i].value;
            return true;
        }
    }
    return false;
}

typedef enum { STRING_MORE, STRING_END, STRING_OOM } StringStep;

/* Reports a string error. `valid` records that the literal is spoiled; the
   caller decides whether scanning goes on. */
static bool string_error(Lexer *lexer, LexerErrorKind kind, SourcePos pos,
                         unsigned char character, bool *valid)
{
    *valid = false;
    return emit_error(lexer, kind, pos, character, 0);
}

static bool string_push(Lexer *lexer, unsigned char byte)
{
    if (list_push(&lexer->result->string_bytes, &byte)) {
        return true;
    }
    lexer->result->out_of_memory = true;
    return false;
}

/* The byte after a backslash. An unknown escape is reported and dropped, but
   the string is scanned on so later errors are found too. */
static StringStep string_escape(Lexer *lexer, SourcePos start, bool *valid)
{
    unsigned char escaped;
    unsigned char value;

    step(lexer);
    escaped = peek(lexer, 0);
    if (escaped == '\0') {
        return string_error(lexer, LEXER_UNTERMINATED_STRING, start, 0, valid)
                   ? STRING_END
                   : STRING_OOM;
    }
    if (!escape_value(escaped, &value)) {
        bool reported = string_error(lexer, LEXER_INVALID_ESCAPE, here(lexer),
                                     escaped, valid);

        step(lexer);
        return reported ? STRING_MORE : STRING_OOM;
    }
    step(lexer);
    return string_push(lexer, value) ? STRING_MORE : STRING_OOM;
}

static StringStep string_step(Lexer *lexer, SourcePos start, bool *valid)
{
    unsigned char byte = peek(lexer, 0);
    bool reported;

    if (byte == '\0' || byte == '\n' || byte == '\r') {
        reported = byte == '\0'
                       ? string_error(lexer, LEXER_UNTERMINATED_STRING, start,
                                      0, valid)
                       : string_error(lexer, LEXER_STRING_NEWLINE, here(lexer),
                                      0, valid);
        return reported ? STRING_END : STRING_OOM;
    }
    if (byte == '"') {
        step(lexer);
        return STRING_END;
    }
    if (byte == '\\') {
        return string_escape(lexer, start, valid);
    }
    step(lexer);
    return string_push(lexer, byte) ? STRING_MORE : STRING_OOM;
}

static bool lex_string(Lexer *lexer)
{
    SourcePos start = here(lexer);
    List *bytes = &lexer->result->string_bytes;
    size_t byte_start = bytes->length;
    bool valid = true;
    StringStep status;
    Token token = {0};

    step(lexer);
    do {
        status = string_step(lexer, start, &valid);
    } while (status == STRING_MORE);
    if (status == STRING_OOM) {
        return false;
    }
    if (!valid) {
        bytes->length = byte_start;
        return true;
    }
    token.type = TOKEN_STRING;
    token.pos = start;
    token.string_offset = byte_start;
    token.string_length = bytes->length - byte_start;
    return emit_token(lexer, &token);
}

/* An identifier or keyword. */
static bool lex_word(Lexer *lexer)
{
    SourcePos start = here(lexer);
    Token token = {0};
    size_t length;

    while (is_identifier_character(peek(lexer, 0))) {
        lexer->cursor++;
    }
    length = lexer->cursor - start.offset;
    lexer->column += length;
    /* The buffer also needs room for the terminating NUL. */
    if (length >= NYX_IDENTIFIER_MAX) {
        return emit_error(lexer, LEXER_IDENTIFIER_TOO_LONG, start, 0, 0);
    }
    token.type = keyword_type(lexer->code + start.offset, length);
    token.pos = start;
    token.length = length;
    memcpy(token.text, lexer->code + start.offset, length);
    token.text[length] = '\0';
    return emit_token(lexer, &token);
}

/* Scans one token, comment, or run of whitespace. Returns false only when out
   of memory. */
static bool lex_one(Lexer *lexer)
{
    unsigned char current = peek(lexer, 0);

    if (current == '-' && peek(lexer, 1) == '-') {
        return lex_comment(lexer);
    }
    if (isspace(current)) {
        step(lexer);
        return true;
    }
    if (isdigit(current)) {
        return lex_number(lexer);
    }
    if (current == '"') {
        return lex_string(lexer);
    }
    if (isalpha(current) || current == '_') {
        return lex_word(lexer);
    }
    return lex_punctuation(lexer);
}

static void lexer_free_all(LexerResult *result)
{
    list_free(&result->tokens);
    list_free(&result->errors);
    list_free(&result->string_bytes);
}

LexerResult lexer_tokenize(const char *code)
{
    LexerResult result = {0};
    Lexer lexer = {code, 0, 1, 1, &result, false};

    list_init(&result.tokens, sizeof(Token));
    list_init(&result.errors, sizeof(LexerError));
    list_init(&result.string_bytes, sizeof(unsigned char));
    if (peek(&lexer, 0) == 0xef && peek(&lexer, 1) == 0xbb &&
        peek(&lexer, 2) == 0xbf) {
        lexer.cursor = 3;
    }
    while (peek(&lexer, 0) != '\0' && !lexer.stopped) {
        if (!lex_one(&lexer)) {
            lexer_free_all(&result);
            return result;
        }
    }
    if (result.errors.length != 0) {
        list_free(&result.tokens);
        return result;
    }
    if (!push_token(&result.tokens, TOKEN_EOF, here(&lexer))) {
        result.out_of_memory = true;
        lexer_free_all(&result);
        return result;
    }
    result.succeeded = true;
    return result;
}
