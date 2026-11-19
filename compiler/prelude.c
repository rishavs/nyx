#include "prelude.h"

#include "alloc.h"
#include "messages.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool prelude_is_needed(const TokenList *tokens)
{
    size_t i;

    for (i = 0; i < tokens->length; i++) {
        const Token *token = list_get(tokens, i);

        if (token->type == TOKEN_IDENTIFIER &&
            (strcmp(token->text, "Error") == 0 ||
             strcmp(token->text, "ErrorKind") == 0)) {
            return true;
        }
    }
    return false;
}

/* The source of the prelude, with the capacity of a custom header filled in. */
static char *prelude_text(int header_capacity)
{
    size_t size = sizeof(MESSAGE_PRELUDE_ERROR_KIND) + 16;
    char *text = nyx_malloc(size);

    if (text != NULL) {
        snprintf(text, size, MESSAGE_PRELUDE_ERROR_KIND, header_capacity);
    }
    return text;
}

static size_t line_count(const char *source)
{
    size_t lines = 1;

    for (; *source != '\0'; source++) {
        lines += *source == '\n';
    }
    return lines;
}

bool prelude_append(LexerResult *lexed, const char *source, int header_capacity)
{
    char *text = prelude_text(header_capacity);
    LexerResult prelude;
    Token end;
    size_t base = strlen(source) + 1;
    size_t lines = line_count(source);
    size_t i;
    bool ok;

    if (text == NULL) {
        return false;
    }
    prelude = lexer_tokenize(text);
    free(text);
    ok = prelude.succeeded && list_pop(&lexed->tokens, &end);
    for (i = 0; ok && i + 1 < prelude.tokens.length; i++) {
        Token token = *(const Token *)list_get(&prelude.tokens, i);

        token.pos.offset += base;
        token.pos.line += lines;
        ok = list_push(&lexed->tokens, &token);
    }
    ok = ok && list_push(&lexed->tokens, &end);
    lexer_result_free(&prelude);
    return ok;
}
