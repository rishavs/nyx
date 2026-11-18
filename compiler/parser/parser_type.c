#include "parser_internal.h"

#include "limits_config.h"

/* Type arguments nest without any token the pre-scan in parser.c can count
   (`<` is also the comparison operator), so the type parser counts its own
   recursion. The depth is a parameter of every function in the cycle, so it
   cannot outlive a parse that fails midway. */

static bool parse_type_primary_at(
    size_t depth, const TokenList *tokens, const List *string_bytes, Ast *ast,
    size_t *cursor, size_t *out_index, ErrorState *errors
);

static bool parse_type_body(size_t depth, 
    const TokenList *tokens, const List *string_bytes, Ast *ast, size_t *cursor,
    size_t *out_index, ErrorState *errors
);

static bool parse_type_at(
    size_t depth, const TokenList *tokens, const List *string_bytes, Ast *ast,
    size_t *cursor, size_t *out_index, ErrorState *errors
)
{
    if (depth >= NYX_MAX_PARSE_NESTING) {
        error_report_at(errors, position_at(tokens, *cursor),
                        MESSAGE_PARSER_NESTING_LIMIT, NYX_MAX_PARSE_NESTING);
        return false;
    }
    return parse_type_body(depth + 1, tokens, string_bytes, ast, cursor,
                           out_index, errors);
}

static bool parse_type_body(size_t depth, 
    const TokenList *tokens, const List *string_bytes, Ast *ast, size_t *cursor,
    size_t *out_index, ErrorState *errors
)
{
    size_t start = *cursor;
    size_t first;
    size_t children_start;
    size_t i;
    List alternatives;
    AstNode node = {0};

    if (!parse_type_primary_at(depth, tokens, string_bytes, ast, cursor, &first, errors)) {
        return false;
    }

    list_init(&alternatives, sizeof(size_t));
    if (!list_push(&alternatives, &first)) {
        error_report_fatal(errors, MESSAGE_PARSER_OUT_OF_MEMORY_BUILDING_TREE);
        list_free(&alternatives);
        return false;
    }

    for (;;) {
        const Token *token = token_at(tokens, *cursor);
        size_t alternative;

        if (token == NULL || token->type != TOKEN_PIPE) {
            break;
        }
        (*cursor)++;
        token = token_at(tokens, *cursor);
        if (token == NULL ||
            (token->type != TOKEN_IDENTIFIER && token->type != TOKEN_LPAREN)) {
            error_report_at(errors, position_at(tokens, *cursor),
                            MESSAGE_PARSER_EXPECTED_UNION_ALTERNATIVE);
            list_free(&alternatives);
            return false;
        }
        if (!parse_type_primary_at(depth, tokens, string_bytes, ast, cursor,
                               &alternative, errors)) {
            list_free(&alternatives);
            return false;
        }
        if (!list_push(&alternatives, &alternative)) {
            error_report_fatal(errors, MESSAGE_PARSER_OUT_OF_MEMORY_BUILDING_TREE);
            list_free(&alternatives);
            return false;
        }
    }

    if (alternatives.length == 1) {
        *out_index = first;
        list_free(&alternatives);
        return true;
    }

    children_start = ast->children.length;
    for (i = 0; i < alternatives.length; i++) {
        size_t alternative = *(const size_t *)list_get(&alternatives, i);

        if (!ast_push_child(ast, alternative)) {
            error_report_fatal(errors, MESSAGE_PARSER_OUT_OF_MEMORY_BUILDING_TREE);
            list_free(&alternatives);
            return false;
        }
    }
    node.kind = AST_TYPE_UNION;
    node.pos = position_at(tokens, start);
    node.name_index = AST_NO_NODE;
    node.left_child = AST_NO_NODE;
    node.right_child = AST_NO_NODE;
    node.children_start = children_start;
    node.children_count = alternatives.length;
    list_free(&alternatives);
    return push_node(ast, &node, out_index);
}

/* Appends the types collected in `types` as one contiguous run of children. It
   happens only after every nested type has been parsed, so the node's
   children stay together. */
static bool push_type_run(Ast *ast, const List *types, size_t *start,
                          ErrorState *errors)
{
    size_t i;

    *start = ast->children.length;
    for (i = 0; i < types->length; i++) {
        if (!ast_push_child(ast, *(const size_t *)list_get(types, i))) {
            error_report_fatal(errors,
                               MESSAGE_PARSER_OUT_OF_MEMORY_BUILDING_TREE);
            return false;
        }
    }
    return true;
}

static void expected(ErrorState *errors, const TokenList *tokens,
                     size_t cursor, const char *message)
{
    error_report_at(errors, position_at(tokens, cursor), message);
}

static bool token_is(const TokenList *tokens, size_t cursor, TokenType type)
{
    const Token *token = token_at(tokens, cursor);

    return token != NULL && token->type == type;
}

/* An integer literal used as a size, stored in the node; -1 when it is too
   large for the field, which the checker rejects. */
static bool parse_type_size(const TokenList *tokens, size_t *cursor,
                            AstNode *node, const char *message,
                            ErrorState *errors)
{
    const Token *token = token_at(tokens, *cursor);

    if (token == NULL || token->type != TOKEN_INTEGER) {
        expected(errors, tokens, *cursor, message);
        return false;
    }
    node->integer_value = token->integer_value_u > (uint64_t)INT_MAX
                              ? -1
                              : (int)token->integer_value_u;
    (*cursor)++;
    return true;
}

/* The built-in types written Head<...>: zero to two type arguments, then an
   optional size, then '>'. The row says which parts a form has and what to
   say when one is missing. */
typedef struct {
    const char *name;
    AstNodeKind kind;
    unsigned types;               /* type arguments: 0 (String), 1, or 2 */
    bool sized;                   /* a size follows: String<N>, Array<T, N> */
    const char *separator_message; /* no ',' between two types */
    const char *size_message;      /* the size is missing or not an integer */
    const char *comma_message;     /* a size where none is allowed, or NULL */
    const char *gt_message;        /* no closing '>' */
} TypeForm;

static const TypeForm type_forms[] = {
    {"String", AST_TYPE_FIXED_STRING, 0, true, NULL,
     MESSAGE_PARSER_EXPECTED_STRING_LENGTH, NULL,
     MESSAGE_PARSER_EXPECTED_STRING_GT},
    {"List", AST_TYPE_LIST, 1, false, NULL, NULL,
     MESSAGE_PARSER_LIST_CAPACITY, MESSAGE_PARSER_EXPECTED_LIST_GT},
    {"Array", AST_TYPE_FIXED_LIST, 1, true, NULL,
     MESSAGE_PARSER_EXPECTED_ARRAY_CAPACITY, NULL,
     MESSAGE_PARSER_EXPECTED_ARRAY_GT},
    {"Slice", AST_TYPE_SLICE, 1, false, NULL, NULL, NULL,
     MESSAGE_PARSER_EXPECTED_TYPE_ARGUMENT_GT},
    {"Record", AST_TYPE_RECORD, 2, true, MESSAGE_PARSER_EXPECTED_RECORD_CAPACITY,
     MESSAGE_PARSER_EXPECTED_RECORD_CAPACITY, NULL,
     MESSAGE_PARSER_EXPECTED_RECORD_GT},
    {"Table", AST_TYPE_TABLE, 2, false, MESSAGE_PARSER_EXPECTED_TABLE_GT, NULL,
     NULL, MESSAGE_PARSER_EXPECTED_TABLE_GT},
};

/* The form named by `token`, when a '<' follows it. */
static const TypeForm *find_type_form(const TokenList *tokens, size_t cursor)
{
    const Token *token = token_at(tokens, cursor);
    size_t i;

    if (!token_is(tokens, cursor + 1, TOKEN_LT)) {
        return NULL;
    }
    for (i = 0; i < NYX_COUNT_OF(type_forms); i++) {
        if (strcmp(token->text, type_forms[i].name) == 0) {
            return &type_forms[i];
        }
    }
    return NULL;
}

/* The type arguments of a form: the first, and for two, a comma and the second. */
static bool parse_form_types(size_t depth, const TypeForm *form, const TokenList *tokens,
                             const List *string_bytes, Ast *ast,
                             size_t *cursor, AstNode *node,
                             ErrorState *errors)
{
    if (!parse_type_at(depth, tokens, string_bytes, ast, cursor, &node->left_child,
                    errors)) {
        return false;
    }
    if (form->types < 2) {
        return true;
    }
    if (!token_is(tokens, *cursor, TOKEN_COMMA)) {
        expected(errors, tokens, *cursor, form->separator_message);
        return false;
    }
    (*cursor)++;
    return parse_type_at(depth, tokens, string_bytes, ast, cursor, &node->right_child,
                      errors);
}

/* What follows the type arguments: a size when the form has one. */
static bool parse_form_size(const TypeForm *form, const TokenList *tokens,
                            size_t *cursor, AstNode *node, ErrorState *errors)
{
    if (form->types == 0) {
        return parse_type_size(tokens, cursor, node, form->size_message,
                               errors);
    }
    if (!form->sized) {
        if (form->comma_message != NULL &&
            token_is(tokens, *cursor, TOKEN_COMMA)) {
            expected(errors, tokens, *cursor, form->comma_message);
            return false;
        }
        return true;
    }
    if (!token_is(tokens, *cursor, TOKEN_COMMA)) {
        expected(errors, tokens, *cursor, form->size_message);
        return false;
    }
    (*cursor)++;
    return parse_type_size(tokens, cursor, node, form->size_message, errors);
}

static bool parse_type_form(size_t depth, const TypeForm *form, const TokenList *tokens,
                            const List *string_bytes, Ast *ast,
                            size_t *cursor, size_t *out_index,
                            ErrorState *errors)
{
    AstNode node = {0};

    node.kind = form->kind;
    node.pos = position_at(tokens, *cursor);
    node.name_index = AST_NO_NODE;
    node.left_child = AST_NO_NODE;
    node.right_child = AST_NO_NODE;
    *cursor += 2; /* past the name and '<' */
    if (form->types > 0 &&
        !parse_form_types(depth, form, tokens, string_bytes, ast, cursor, &node,
                          errors)) {
        return false;
    }
    if (!parse_form_size(form, tokens, cursor, &node, errors)) {
        return false;
    }
    if (!token_is(tokens, *cursor, TOKEN_GT)) {
        expected(errors, tokens, *cursor, form->gt_message);
        return false;
    }
    (*cursor)++;
    return push_node(ast, &node, out_index);
}

/* How a comma-separated run of types is written and ends. */
typedef struct {
    TokenType close;
    const char *start_message;     /* not the start of a type, or NULL */
    const char *separator_message; /* neither ',' nor the closing token */
    const char *end_message;       /* the tokens ran out */
} TypeListSyntax;

static bool parse_type_list(size_t depth, const TypeListSyntax *syntax,
                            const TokenList *tokens, const List *string_bytes,
                            Ast *ast, size_t *cursor, List *types,
                            ErrorState *errors)
{
    for (;;) {
        const Token *token = token_at(tokens, *cursor);
        size_t type;

        if (syntax->start_message != NULL &&
            (token == NULL || (token->type != TOKEN_IDENTIFIER &&
                               token->type != TOKEN_LPAREN))) {
            expected(errors, tokens, *cursor, syntax->start_message);
            return false;
        }
        if (!parse_type_at(depth, tokens, string_bytes, ast, cursor, &type, errors)) {
            return false;
        }
        if (!list_push(types, &type)) {
            error_report_fatal(errors,
                               MESSAGE_PARSER_OUT_OF_MEMORY_BUILDING_TREE);
            return false;
        }
        token = token_at(tokens, *cursor);
        if (token == NULL || token->type == TOKEN_EOF) {
            expected(errors, tokens, *cursor, syntax->end_message);
            return false;
        }
        (*cursor)++;
        if (token->type == syntax->close) {
            return true;
        }
        if (token->type != TOKEN_COMMA) {
            error_report_at(errors, token->pos,
                            syntax->separator_message);
            return false;
        }
    }
}

static const TypeListSyntax argument_syntax = {
    TOKEN_GT, MESSAGE_PARSER_EXPECTED_TYPE_ARGUMENT,
    MESSAGE_PARSER_EXPECTED_TYPE_ARGUMENT_SEPARATOR,
    MESSAGE_PARSER_EXPECTED_TYPE_ARGUMENT_GT};

static const TypeListSyntax parameter_syntax = {
    TOKEN_RPAREN, NULL, MESSAGE_PARSER_EXPECTED_FUNCTION_TYPE_SEPARATOR,
    MESSAGE_PARSER_EXPECTED_FUNCTION_TYPE_SEPARATOR};

/* Fun<(T, U): R>, the return type being optional. */
static bool parse_function_type(size_t depth, const TokenList *tokens,
                                const List *string_bytes, Ast *ast,
                                size_t *cursor, size_t *out_index,
                                ErrorState *errors)
{
    AstNode node = {0};
    List parameters;
    bool ok;

    node.kind = AST_TYPE_FUNCTION;
    node.pos = position_at(tokens, *cursor);
    node.name_index = AST_NO_NODE;
    node.left_child = AST_NO_NODE;
    node.right_child = AST_NO_NODE;
    *cursor += 2; /* past 'Fun' and '<' */
    if (!token_is(tokens, *cursor, TOKEN_LPAREN)) {
        expected(errors, tokens, *cursor,
                 MESSAGE_PARSER_EXPECTED_FUNCTION_TYPE_LPAREN);
        return false;
    }
    (*cursor)++;
    list_init(&parameters, sizeof(size_t));
    if (token_is(tokens, *cursor, TOKEN_RPAREN)) {
        (*cursor)++;
        ok = true;
    } else {
        ok = parse_type_list(depth, &parameter_syntax, tokens, string_bytes, ast,
                             cursor, &parameters, errors);
    }
    if (ok && token_is(tokens, *cursor, TOKEN_COLON)) {
        (*cursor)++;
        ok = parse_type_at(depth, tokens, string_bytes, ast, cursor, &node.right_child,
                        errors);
    }
    if (ok && !token_is(tokens, *cursor, TOKEN_GT)) {
        expected(errors, tokens, *cursor,
                 MESSAGE_PARSER_EXPECTED_FUNCTION_TYPE_GT);
        ok = false;
    }
    if (ok) {
        (*cursor)++;
        node.children_count = parameters.length;
        ok = push_type_run(ast, &parameters, &node.children_start, errors) &&
             push_node(ast, &node, out_index);
    }
    list_free(&parameters);
    return ok;
}

/* A name, or a name applied to type arguments: Box<Int32, Bool>. */
static bool parse_type_name(size_t depth, const TokenList *tokens, const List *string_bytes,
                            Ast *ast, size_t *cursor, size_t *out_index,
                            ErrorState *errors)
{
    const Token *token = token_at(tokens, *cursor);
    AstNode node = {0};
    List arguments;
    bool ok = true;

    node.kind = AST_TYPE_NAME;
    node.pos = token->pos;
    node.left_child = AST_NO_NODE;
    node.right_child = AST_NO_NODE;
    if (!intern_token(ast, token, &node.name_index, errors)) {
        return false;
    }
    (*cursor)++;
    if (token_is(tokens, *cursor, TOKEN_LT)) {
        (*cursor)++;
        list_init(&arguments, sizeof(size_t));
        node.kind = AST_TYPE_APPLICATION;
        ok = parse_type_list(depth, &argument_syntax, tokens, string_bytes, ast,
                             cursor, &arguments, errors);
        node.children_count = arguments.length;
        ok = ok && push_type_run(ast, &arguments, &node.children_start,
                                 errors);
        list_free(&arguments);
    }
    return ok && push_node(ast, &node, out_index);
}

/* ( type ) groups a type, which matters inside a union. */
static bool parse_parenthesised_type(size_t depth, const TokenList *tokens,
                                     const List *string_bytes, Ast *ast,
                                     size_t *cursor, size_t *out_index,
                                     ErrorState *errors)
{
    (*cursor)++;
    if (!parse_type_at(depth, tokens, string_bytes, ast, cursor, out_index, errors)) {
        return false;
    }
    if (!token_is(tokens, *cursor, TOKEN_RPAREN)) {
        expected(errors, tokens, *cursor, MESSAGE_PARSER_EXPECTED_RPAREN);
        return false;
    }
    (*cursor)++;
    return true;
}

static bool parse_type_primary_at(size_t depth, 
    const TokenList *tokens, const List *string_bytes, Ast *ast, size_t *cursor,
    size_t *out_index, ErrorState *errors
)
{
    const Token *token = token_at(tokens, *cursor);
    const TypeForm *form;

    if (token == NULL) {
        expected(errors, tokens, *cursor, MESSAGE_PARSER_EXPECTED_TYPE);
        return false;
    }
    if (token->type == TOKEN_LPAREN) {
        return parse_parenthesised_type(depth, tokens, string_bytes, ast, cursor,
                                        out_index, errors);
    }
    if (token->type != TOKEN_IDENTIFIER) {
        error_report_at(errors, token->pos, MESSAGE_PARSER_EXPECTED_TYPE);
        return false;
    }
    form = find_type_form(tokens, *cursor);
    if (form != NULL) {
        return parse_type_form(depth, form, tokens, string_bytes, ast, cursor,
                               out_index, errors);
    }
    if (strcmp(token->text, "Fun") == 0 &&
        token_is(tokens, *cursor + 1, TOKEN_LT)) {
        return parse_function_type(depth, tokens, string_bytes, ast, cursor,
                                   out_index, errors);
    }
    return parse_type_name(depth, tokens, string_bytes, ast, cursor, out_index,
                           errors);
}

bool parse_type_parameters(
    const TokenList *tokens, const List *string_bytes, Ast *ast, size_t *cursor,
    size_t *out_start, size_t *out_count, ErrorState *errors
)
{
    const Token *token = token_at(tokens, *cursor);
    size_t start;

    (void)string_bytes;

    *out_start = 0;
    *out_count = 0;
    if (token == NULL || token->type != TOKEN_LT) {
        return true;
    }
    (*cursor)++;
    start = ast->type_parameters.length;
    for (;;) {
        AstNode parameter = {0};
        size_t index;

        token = token_at(tokens, *cursor);
        if (token == NULL || token->type != TOKEN_IDENTIFIER) {
            error_report_at(errors, position_at(tokens, *cursor),
                            MESSAGE_PARSER_EXPECTED_TYPE_PARAMETER_NAME);
            return false;
        }
        parameter.kind = AST_TYPE_PARAMETER;
        parameter.pos = token->pos;
        parameter.left_child = AST_NO_NODE;
        parameter.right_child = AST_NO_NODE;
        if (!intern_token(ast, token, &parameter.name_index, errors)) {
            return false;
        }
        if (!push_node(ast, &parameter, &index) ||
            !ast_push_type_parameter(ast, index)) {
            error_report_fatal(errors, MESSAGE_PARSER_OUT_OF_MEMORY_BUILDING_TREE);
            return false;
        }
        (*cursor)++;

        token = token_at(tokens, *cursor);
        if (token == NULL || token->type == TOKEN_EOF) {
            error_report_at(errors, position_at(tokens, *cursor),
                            MESSAGE_PARSER_EXPECTED_TYPE_PARAMETER_GT);
            return false;
        }
        if (token->type == TOKEN_COMMA) {
            (*cursor)++;
            continue;
        }
        if (token->type == TOKEN_GT) {
            (*cursor)++;
            break;
        }
        error_report_at(errors, token->pos,
                        MESSAGE_PARSER_EXPECTED_TYPE_PARAMETER_SEPARATOR);
        return false;
    }
    *out_start = start;
    *out_count = ast->type_parameters.length - start;
    return true;
}

/* The entry points the other parser modules use: each starts a type at depth
   zero. */
bool parse_type(const TokenList *tokens, const List *string_bytes, Ast *ast,
                size_t *cursor, size_t *out_index, ErrorState *errors)
{
    return parse_type_at(0, tokens, string_bytes, ast, cursor, out_index,
                         errors);
}

bool parse_type_primary(const TokenList *tokens, const List *string_bytes,
                        Ast *ast, size_t *cursor, size_t *out_index,
                        ErrorState *errors)
{
    return parse_type_primary_at(0, tokens, string_bytes, ast, cursor,
                                 out_index, errors);
}
