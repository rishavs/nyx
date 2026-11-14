/* Lexer errors: what the lexer rejects, and how it reports it. */

#include "compile_support.h"
#include "lexer.h"

TEST("UTF-8 byte order mark")
{
    return compile_accepts("\xef\xbb\xbfvar value: Int32 = 3\n",
                           "a UTF-8 BOM at the start of the source");
}

TEST("integer literal out of range")
{
    return compile_fails("var x : Int32 = 99999999999999999999999999\n",
                             "an out-of-range integer literal",
                             MESSAGE_LEXER_INTEGER_OUT_OF_RANGE);
}

TEST("identifier too long")
{
    return compile_fails(
        "var abcdefghijklmnopqrstuvwxyzABCDEFG : Int32 = 1\n",
        "an over-long identifier",
        MESSAGE_LEXER_IDENTIFIER_TOO_LONG, NIPPY_IDENTIFIER_MAX - 1);
}

TEST("unexpected character")
{
    return compile_fails("var x : Int32 = 13 @\n",
                             "an unexpected printable character",
                             MESSAGE_LEXER_UNEXPECTED_CHARACTER, '@');
}

TEST("single-line comments")
{
    return compile_accepts(
        "-- an entire comment\n"
        "var x: Int32 = 1 -- trailing comment\n",
        "single-line comments");
}

TEST("multiline comments")
{
    return compile_accepts(
        "--[ comment with -- and operators\n"
        "var ignored = @\n"
        "]--\n"
        "var x: Int32 = 1\n",
        "multiline comments");
}

TEST("unterminated multiline comment")
{
    return compile_fails("--[ unfinished\n", "an unterminated comment",
                         MESSAGE_LEXER_UNTERMINATED_COMMENT);
}

TEST("unexpected byte")
{
    return compile_fails("var x : Int32 = 13 \x80\n",
                             "an unexpected non-printable byte",
                             MESSAGE_LEXER_UNEXPECTED_BYTE, 0x80);
}

TEST("string: an unterminated string literal")
{
    return compile_fails(
        "var s: String<4> = \"ab",
        "an unterminated string literal",
        MESSAGE_LEXER_UNTERMINATED_STRING);
}

TEST("string: a newline inside a string literal")
{
    return compile_fails(
        "var s: String<8> = \"a\nb\"\n",
        "a newline inside a string literal",
        MESSAGE_LEXER_STRING_NEWLINE);
}

TEST("string: an invalid string escape")
{
    return compile_fails(
        "var s: String<8> = \"a\\qb\"\n",
        "an invalid string escape",
        MESSAGE_LEXER_INVALID_ESCAPE, 'q');
}

TEST("keywords and identifiers with keyword prefixes")
{
    static const char *const words[] = {
        "global", "var", "const", "type", "fun", "method", "static", "return",
        "if", "then", "elseif", "else", "while", "for", "typeof", "is",
        "match", "with", "in", "do", "break", "continue", "true", "false",
        "nil", "end"
    };
    size_t i;

    for (i = 0; i < sizeof(words) / sizeof(words[0]); i++) {
        char input[64];
        LexerResult result;
        const Token *first;
        const Token *second;
        bool ok;

        snprintf(input, sizeof(input), "%s %sx", words[i], words[i]);
        result = lexer_tokenize(input);
        first = list_get(&result.tokens, 0);
        second = list_get(&result.tokens, 1);
        ok = result.succeeded && first != NULL && second != NULL &&
             first->type == (TokenType)(TOKEN_KEYWORD_GLOBAL + i) &&
             second->type == TOKEN_IDENTIFIER;
        lexer_result_free(&result);
        if (!ok) {
            fprintf(stderr, "  incorrect tokenization for %s\n", words[i]);
            return TEST_FAIL;
        }
    }
    return TEST_PASS;
}
