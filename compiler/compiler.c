#include "compiler.h"

#include "alloc.h"
#include "analyser.h"
#include "ast.h"
#include "checker.h"
#include "error.h"
#include "generator.h"
#include "lexer.h"
#include "messages.h"
#include "parser.h"
#include "typed_program.h"

#include <stdlib.h>
#include <string.h>

static void report_lexer_errors(const LexerErrorList *errors, ErrorState *state)
{
    size_t i;

    for (i = 0; i < errors->length; i++) {
        const LexerError *error = list_get(errors, i);
        if (error->kind == LEXER_INTEGER_OUT_OF_RANGE) {
            error_report_at(state, error->pos, MESSAGE_LEXER_INTEGER_OUT_OF_RANGE);
        } else if (error->kind == LEXER_IDENTIFIER_TOO_LONG) {
            error_report_at(state, error->pos, MESSAGE_LEXER_IDENTIFIER_TOO_LONG,
                            NIPPY_IDENTIFIER_MAX - 1);
        } else if (error->character >= 32 && error->character <= 126) {
            error_report_at(state, error->pos, MESSAGE_LEXER_UNEXPECTED_CHARACTER,
                            error->character);
        } else {
            error_report_at(state, error->pos, MESSAGE_LEXER_UNEXPECTED_BYTE,
                            error->character);
        }
    }
}

static char *copy_text(const char *text)
{
    size_t length = strlen(text) + 1;
    char *copy = nippy_malloc(length);

    if (copy != NULL) {
        memcpy(copy, text, length);
    }
    return copy;
}

void compiler_result_free(CompilerResult *result)
{
    if (result == NULL) {
        return;
    }
    free(result->c_source);
    free(result->h_source);
    free(result->error_message);
    result->c_source = NULL;
    result->h_source = NULL;
    result->error_message = NULL;
    result->succeeded = false;
}

CompilerResult compiler_compile(const char *source, const CompilerConfig *config)
{
    CompilerResult result = {false, NULL, NULL, NULL};
    ErrorState errors;
    LexerResult lexed = {0};
    Ast ast;
    TypedProgram program;
    bool ast_initialized = false;

    error_init(&errors);
    errors.source_name = config == NULL ? NULL : config->source_name;
    typed_program_init(&program);

    if (source == NULL) {
        error_report(&errors, MESSAGE_COMPILER_SOURCE_IS_NULL);
        goto failed;
    }

    lexed = lexer_tokenize(source);
    if (lexed.out_of_memory) {
        error_report(&errors, MESSAGE_LEXER_OUT_OF_MEMORY);
        goto failed;
    }
    if (!lexed.succeeded) {
        report_lexer_errors(&lexed.errors, &errors);
        goto failed;
    }

    if (!parser_parse(&lexed.tokens, &ast, &errors)) {
        ast_initialized = true;
        goto failed;
    }
    ast_initialized = true;

    /* source -> tokens -> syntax tree -> typed program. The tree has done its
       job once the checker returns, and freeing it here is what proves the
       remaining stages cannot reach back to it. */
    if (!checker_check(&ast, &program, &errors)) {
        goto failed;
    }
    ast_free(&ast);
    ast_initialized = false;

    if (!analyser_analyse(&program, &errors)) {
        goto failed;
    }
    if (!generator_generate(&program, config == NULL ? NULL : config->header_name,
                            config == NULL ? NULL : config->source_name,
                            &result.c_source, &result.h_source, &errors)) {
        goto failed;
    }

    result.succeeded = true;
    lexer_result_free(&lexed);
    typed_program_free(&program);
    return result;

failed:
    error_unknown_if_none(&errors);
    if (lexed.tokens.item_size != 0 || lexed.errors.item_size != 0) {
        lexer_result_free(&lexed);
    }
    if (ast_initialized) {
        ast_free(&ast);
    }
    typed_program_free(&program);
    free(result.c_source);
    free(result.h_source);
    result.c_source = NULL;
    result.h_source = NULL;
    result.error_message = copy_text(errors.message);
    result.succeeded = false;
    return result;
}
