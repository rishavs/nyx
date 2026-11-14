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


static bool ast_depth_within_limit(const Ast *ast, ErrorState *errors)
{
    size_t *depths;
    size_t i;

    if (ast->nodes.length > SIZE_MAX / sizeof(*depths)) {
        error_report(errors, MESSAGE_PARSER_OUT_OF_MEMORY_BUILDING_TREE);
        return false;
    }
    depths = nippy_malloc(ast->nodes.length * sizeof(*depths));
    if (depths == NULL && ast->nodes.length != 0) {
        error_report(errors, MESSAGE_PARSER_OUT_OF_MEMORY_BUILDING_TREE);
        return false;
    }

    for (i = 0; i < ast->nodes.length; i++) {
        const AstNode *node = ast_get_node(ast, i);
        size_t depth = 1;
        size_t j;
        size_t refs[3] = {
            node->left_child,
            node->right_child,
            ((node->kind == AST_FUNCTION_DECLARATION &&
              (node->is_method || node->is_static)) ||
             (node->kind == AST_CALL && node->is_static))
                ? node->owner_type_index : AST_NO_NODE
        };

        for (j = 0; j < 3; j++) {
            size_t child = refs[j];

            if (child == AST_NO_NODE) {
                continue;
            }
            if (child >= i) {
                free(depths);
                error_report_at(errors, node->pos,
                                MESSAGE_PARSER_TREE_DEPTH_LIMIT,
                                NIPPY_MAX_AST_DEPTH);
                return false;
            }
            if (depths[child] >= depth) {
                depth = depths[child] + 1;
            }
        }
        for (j = 0; j < node->children_count; j++) {
            const size_t *child = list_get(&ast->children,
                                           node->children_start + j);

            if (child == NULL || *child >= i) {
                free(depths);
                error_report_at(errors, node->pos,
                                MESSAGE_PARSER_TREE_DEPTH_LIMIT,
                                NIPPY_MAX_AST_DEPTH);
                return false;
            }
            if (depths[*child] >= depth) {
                depth = depths[*child] + 1;
            }
        }
        for (j = 0; j < node->type_parameters_count; j++) {
            const size_t *child = list_get(&ast->type_parameters,
                                           node->type_parameters_start + j);

            if (child == NULL || *child >= i) {
                free(depths);
                error_report_at(errors, node->pos,
                                MESSAGE_PARSER_TREE_DEPTH_LIMIT,
                                NIPPY_MAX_AST_DEPTH);
                return false;
            }
            if (depths[*child] >= depth) {
                depth = depths[*child] + 1;
            }
        }
        depths[i] = depth;
        if (depth > NIPPY_MAX_AST_DEPTH) {
            free(depths);
            error_report_at(errors, node->pos,
                            MESSAGE_PARSER_TREE_DEPTH_LIMIT,
                            NIPPY_MAX_AST_DEPTH);
            return false;
        }
    }

    free(depths);
    return true;
}

static void report_lexer_errors(const LexerErrorList *errors, ErrorState *state)
{
    size_t i;

    for (i = 0; i < errors->length; i++) {
        const LexerError *error = list_get(errors, i);
        if (error->kind == LEXER_INTEGER_OUT_OF_RANGE) {
            error_report_at(state, error->pos, MESSAGE_LEXER_INTEGER_OUT_OF_RANGE);
        } else if (error->kind == LEXER_FLOAT_OUT_OF_RANGE) {
            error_report_at(state, error->pos, MESSAGE_LEXER_FLOAT_OUT_OF_RANGE);
        } else if (error->kind == LEXER_INVALID_DIGIT) {
            error_report_at(state, error->pos, MESSAGE_LEXER_INVALID_DIGIT,
                            error->character, error->base);
        } else if (error->kind == LEXER_MISSING_DIGITS) {
            error_report_at(state, error->pos, MESSAGE_LEXER_MISSING_DIGITS);
        } else if (error->kind == LEXER_BAD_SEPARATOR) {
            error_report_at(state, error->pos, MESSAGE_LEXER_BAD_SEPARATOR);
        } else if (error->kind == LEXER_UPPERCASE_PREFIX) {
            error_report_at(state, error->pos, MESSAGE_LEXER_UPPERCASE_PREFIX,
                            error->character);
        } else if (error->kind == LEXER_IDENTIFIER_TOO_LONG) {
            error_report_at(state, error->pos, MESSAGE_LEXER_IDENTIFIER_TOO_LONG,
                            NIPPY_IDENTIFIER_MAX - 1);
        } else if (error->kind == LEXER_UNTERMINATED_STRING) {
            error_report_at(state, error->pos,
                            MESSAGE_LEXER_UNTERMINATED_STRING);
        } else if (error->kind == LEXER_UNTERMINATED_COMMENT) {
            error_report_at(state, error->pos,
                            MESSAGE_LEXER_UNTERMINATED_COMMENT);
        } else if (error->kind == LEXER_STRING_NEWLINE) {
            error_report_at(state, error->pos,
                            MESSAGE_LEXER_STRING_NEWLINE);
        } else if (error->kind == LEXER_INVALID_ESCAPE) {
            error_report_at(state, error->pos, MESSAGE_LEXER_INVALID_ESCAPE,
                            error->character);
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

static bool resolve_error_capacity(int value, int fallback, const char *field,
                                   int maximum, int *out_capacity,
                                   ErrorState *errors)
{
    if (value == 0) {
        value = fallback > maximum ? maximum : fallback;
    }
    if (value < 1 || value > maximum) {
        error_report(errors, MESSAGE_COMPILER_ERROR_CAPACITY, field, maximum);
        return false;
    }
    *out_capacity = value;
    return true;
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
    if (config != NULL) {
        int max_string = config->max_string_capacity;

        if (max_string == 0) {
            max_string = NIPPY_DEFAULT_MAX_STRING_CAPACITY;
        }
        if (max_string < 1 || max_string > NIPPY_MAX_STRING_CAPACITY) {
            error_report(&errors, MESSAGE_COMPILER_STRING_CAPACITY,
                         NIPPY_MAX_STRING_CAPACITY);
            goto failed;
        }
        program.max_string_capacity = max_string;
        {
            int max_list = config->max_list_capacity;

            if (max_list == 0) {
                max_list = NIPPY_DEFAULT_MAX_LIST_CAPACITY;
            }
            if (max_list < 1 || max_list > NIPPY_MAX_LIST_CAPACITY) {
                error_report(&errors, MESSAGE_COMPILER_LIST_CAPACITY,
                             NIPPY_MAX_LIST_CAPACITY);
                goto failed;
            }
            program.max_list_capacity = max_list;
        }
        {
            int max_print = config->max_print_depth;

            if (max_print == 0) {
                max_print = NIPPY_DEFAULT_MAX_PRINT_DEPTH;
            }
            if (max_print < 1 || max_print > NIPPY_MAX_PRINT_DEPTH) {
                error_report(&errors, MESSAGE_COMPILER_PRINT_DEPTH,
                             NIPPY_MAX_PRINT_DEPTH);
                goto failed;
            }
            program.max_print_depth = max_print;
        }
        if (!resolve_error_capacity(
                config->error_header_capacity,
                NIPPY_DEFAULT_ERROR_HEADER_CAPACITY, "header", max_string,
                &program.error_header_capacity, &errors) ||
            !resolve_error_capacity(
                config->error_description_capacity,
                NIPPY_DEFAULT_ERROR_DESCRIPTION_CAPACITY, "description",
                max_string, &program.error_description_capacity, &errors) ||
            !resolve_error_capacity(
                config->error_filename_capacity,
                NIPPY_DEFAULT_ERROR_FILENAME_CAPACITY, "filename", max_string,
                &program.error_filename_capacity, &errors)) {
            goto failed;
        }
    }

    if (source == NULL) {
        error_report(&errors, MESSAGE_COMPILER_SOURCE_IS_NULL);
        goto failed;
    }
    {
        int max_source = config == NULL ? 0 : config->max_source_bytes;

        if (max_source <= 0) {
            max_source = NIPPY_DEFAULT_MAX_SOURCE_BYTES;
        }
        if (strlen(source) > (size_t)max_source) {
            error_report(&errors, MESSAGE_COMPILER_SOURCE_TOO_LARGE,
                         max_source);
            goto failed;
        }
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

    if (!parser_parse(&lexed.tokens, &lexed.string_bytes, &ast, &errors)) {
        ast_initialized = true;
        goto failed;
    }
    ast_initialized = true;

    if (!ast_depth_within_limit(&ast, &errors)) {
        goto failed;
    }

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
