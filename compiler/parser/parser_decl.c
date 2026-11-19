#include "parser_internal.h"

static bool parse_default_literal(const TokenList *tokens, const List *string_bytes,
                                  Ast *ast, size_t *cursor, size_t *out,
                                  ErrorState *errors)
{
    const AstNode *value;
    if (!parse_expression(tokens, string_bytes, ast, cursor, out, errors))
        return false;
    value = ast_get_node(ast, *out);
    if (value != NULL && value->kind == AST_UNARY &&
        value->op == NYX_OPERATOR_NEG) {
        value = ast_get_node(ast, value->left_child);
        if (value != NULL && (value->kind == AST_INTEGER_LITERAL ||
                              value->kind == AST_FLOAT_LITERAL)) return true;
    } else if (value != NULL &&
               (value->kind == AST_INTEGER_LITERAL || value->kind == AST_FLOAT_LITERAL ||
                value->kind == AST_BOOL_LITERAL || value->kind == AST_NIL_LITERAL ||
                value->kind == AST_STRING_LITERAL)) {
        return true;
    }
    error_report_at(errors, position_at(tokens, *cursor), MESSAGE_PARSER_DEFAULT_LITERAL);
    return false;
}

/* A type declaration is one of three shapes after its name and type
   parameters: `= Type` (an alias), `is | Case ... end` (a sum type of cases),
   or fields up to `end` (a struct). */

typedef struct {
    const TokenList *tokens;
    const List *string_bytes;
    Ast *ast;
    size_t *cursor;
    ErrorState *errors;
} TypeParse;

static bool type_parse_oom(const TypeParse *p)
{
    error_report_fatal(p->errors, MESSAGE_PARSER_OUT_OF_MEMORY_BUILDING_TREE);
    return false;
}

static bool type_parse_error(const TypeParse *p, const char *message)
{
    error_report_at(p->errors, position_at(p->tokens, *p->cursor), message);
    return false;
}

static bool at_token(const TypeParse *p, TokenType type)
{
    const Token *token = token_at(p->tokens, *p->cursor);

    return token != NULL && token->type == type;
}

/* `: Type` and an optional `= default`, after a field's name. */
static bool parse_field_tail(const TypeParse *p, AstNode *field)
{
    if (!at_token(p, TOKEN_COLON)) {
        return type_parse_error(p, MESSAGE_PARSER_EXPECTED_FIELD_COLON);
    }
    (*p->cursor)++;
    if (!at_token(p, TOKEN_IDENTIFIER) && !at_token(p, TOKEN_LPAREN)) {
        return type_parse_error(p, MESSAGE_PARSER_EXPECTED_FIELD_TYPE);
    }
    if (!parse_type(p->tokens, p->string_bytes, p->ast, p->cursor,
                    &field->left_child, p->errors)) {
        return false;
    }
    if (!at_token(p, TOKEN_EQUALS)) {
        return true;
    }
    (*p->cursor)++;
    return parse_default_literal(p->tokens, p->string_bytes, p->ast, p->cursor,
                                 &field->right_child, p->errors);
}

/* `name: Type`, pushed, with its index returned. */
static bool parse_field(const TypeParse *p, size_t *out_field)
{
    const Token *name = token_at(p->tokens, *p->cursor);
    AstNode field = {0};

    if (name == NULL || name->type != TOKEN_IDENTIFIER) {
        return type_parse_error(p, MESSAGE_PARSER_EXPECTED_FIELD_NAME);
    }
    field.kind = AST_FIELD;
    field.pos = name->pos;
    field.right_child = AST_NO_NODE;
    if (!intern_token(p->ast, name, &field.name_index, p->errors)) {
        return false;
    }
    (*p->cursor)++;
    return parse_field_tail(p, &field) &&
           (push_node(p->ast, &field, out_field) || type_parse_oom(p));
}

/* What follows a field: `end` closes the list, a comma separates it from the
   next field (and may also trail), and anything else must start a field. */
typedef enum {
    FIELD_MORE,
    FIELD_DONE,
    FIELD_BAD
} FieldNext;

static FieldNext field_next(const TypeParse *p)
{
    if (at_token(p, TOKEN_COMMA)) {
        (*p->cursor)++;
    }
    if (at_token(p, TOKEN_KEYWORD_END)) {
        (*p->cursor)++;
        return FIELD_DONE;
    }
    return at_token(p, TOKEN_IDENTIFIER) ? FIELD_MORE : FIELD_BAD;
}

/* The fields in `fields`, in order, as the node's children. */
static bool attach_children(const TypeParse *p, AstNode *owner,
                            const List *indices)
{
    size_t i;

    owner->children_start = p->ast->children.length;
    owner->children_count = indices->length;
    for (i = 0; i < indices->length; i++) {
        if (!ast_push_child(p->ast, *(const size_t *)list_get(indices, i))) {
            return type_parse_oom(p);
        }
    }
    return true;
}

static bool collect_fields(const TypeParse *p, List *fields)
{
    FieldNext next = FIELD_MORE;

    while (next == FIELD_MORE) {
        size_t field;

        if (!parse_field(p, &field) ||
            !(list_push(fields, &field) || type_parse_oom(p))) {
            return false;
        }
        next = token_at(p->tokens, *p->cursor) == NULL ? FIELD_BAD
                                                       : field_next(p);
    }
    return next == FIELD_DONE ||
           type_parse_error(p, MESSAGE_PARSER_EXPECTED_FIELD_OR_END);
}

/* One or more fields up to `end`, which is consumed. The fields become the
   node's children. */
static bool parse_fields_until_end(const TypeParse *p, AstNode *owner)
{
    List fields;
    bool ok = !at_token(p, TOKEN_KEYWORD_END) &&
              token_at(p->tokens, *p->cursor) != NULL;

    if (!ok) {
        return type_parse_error(p, MESSAGE_PARSER_EXPECTED_AT_LEAST_ONE_FIELD);
    }
    list_init(&fields, sizeof(size_t));
    ok = collect_fields(p, &fields) && attach_children(p, owner, &fields);
    list_free(&fields);
    return ok;
}

/* `Case` or `Case is fields end`: one case of a sum type. */
static bool parse_case(const TypeParse *p, size_t *out_case)
{
    const Token *name = token_at(p->tokens, *p->cursor);
    AstNode node = {0};

    if (name == NULL || name->type != TOKEN_IDENTIFIER) {
        return type_parse_error(p, MESSAGE_PARSER_EXPECTED_CASE_NAME);
    }
    node.kind = AST_ADT_CASE;
    node.pos = name->pos;
    node.left_child = AST_NO_NODE;
    node.right_child = AST_NO_NODE;
    if (!intern_token(p->ast, name, &node.name_index, p->errors)) {
        return false;
    }
    (*p->cursor)++;
    if (at_token(p, TOKEN_KEYWORD_IS)) {
        (*p->cursor)++;
        if (!parse_fields_until_end(p, &node)) {
            return false;
        }
    }
    return push_node(p->ast, &node, out_case) || type_parse_oom(p);
}

/* The cases up to the type's `end`: each one, and a pipe before the next. A
   pipe may also trail the last. */
static bool collect_cases(const TypeParse *p, List *cases)
{
    bool more = true;

    while (more) {
        size_t node;

        if (!parse_case(p, &node) ||
            !(list_push(cases, &node) || type_parse_oom(p))) {
            return false;
        }
        more = at_token(p, TOKEN_PIPE);
        if (more) {
            (*p->cursor)++;
            more = !at_token(p, TOKEN_KEYWORD_END);
        }
    }
    return true;
}

/* `| A | B is x: T end | C end`: the cases of a sum type, up to its `end`. A
   pipe may lead or trail. */
static bool parse_cases(const TypeParse *p, AstNode *declaration)
{
    List cases;
    bool ok;

    if (at_token(p, TOKEN_PIPE)) {
        (*p->cursor)++;
    }
    list_init(&cases, sizeof(size_t));
    ok = collect_cases(p, &cases) &&
         (at_token(p, TOKEN_KEYWORD_END) ||
          type_parse_error(p, MESSAGE_PARSER_EXPECTED_CASE_OR_END)) &&
         attach_children(p, declaration, &cases);
    list_free(&cases);
    if (ok) {
        (*p->cursor)++;
    }
    return ok;
}

/* The name and type parameters after `type`. */
static bool parse_type_head(const TypeParse *p, AstNode *declaration)
{
    const Token *name = token_at(p->tokens, *p->cursor);

    if (name == NULL || name->type != TOKEN_IDENTIFIER) {
        return type_parse_error(p, MESSAGE_PARSER_EXPECTED_TYPE_NAME_AFTER_TYPE);
    }
    declaration->left_child = AST_NO_NODE;
    declaration->right_child = AST_NO_NODE;
    if (!intern_token(p->ast, name, &declaration->name_index, p->errors)) {
        return false;
    }
    (*p->cursor)++;
    return parse_type_parameters(p->tokens, p->string_bytes, p->ast, p->cursor,
                                 &declaration->type_parameters_start,
                                 &declaration->type_parameters_count, p->errors);
}

/* What follows the head: `= Type`, `is` and cases, or fields. */
static bool parse_type_body(const TypeParse *p, AstNode *declaration)
{
    if (at_token(p, TOKEN_EQUALS)) {
        (*p->cursor)++;
        declaration->kind = AST_TYPE_ALIAS;
        return parse_type(p->tokens, p->string_bytes, p->ast, p->cursor,
                          &declaration->right_child, p->errors);
    }
    if (at_token(p, TOKEN_KEYWORD_IS)) {
        (*p->cursor)++;
        declaration->kind = AST_ADT_DECLARATION;
        return parse_cases(p, declaration);
    }
    declaration->kind = AST_TYPE_DECLARATION;
    return parse_fields_until_end(p, declaration);
}

bool parse_type_declaration(
    const TokenList *tokens, const List *string_bytes, Ast *ast, size_t *cursor,
    SourcePos declaration_pos, ErrorState *errors
)
{
    TypeParse p = {tokens, string_bytes, ast, cursor, errors};
    AstNode declaration = {0};

    declaration.pos = declaration_pos;
    return parse_type_head(&p, &declaration) &&
           parse_type_body(&p, &declaration) &&
           push_root(ast, &declaration, errors);
}


static bool parse_function_tail(
    const TokenList *tokens, const List *string_bytes, Ast *ast, size_t *cursor,
    SourcePos declaration_pos, size_t name_index, size_t owner_name_index,
    size_t owner_type_index, bool is_method, bool is_static, ErrorState *errors
);


bool parse_function_declaration(
    const TokenList *tokens, const List *string_bytes, Ast *ast, size_t *cursor,
    SourcePos declaration_pos, ErrorState *errors
)
{
    const Token *token = token_at(tokens, *cursor);
    size_t name_index;

    if (token == NULL || token->type != TOKEN_IDENTIFIER) {
        error_report_at(errors, position_at(tokens, *cursor),
                        MESSAGE_PARSER_EXPECTED_FUNCTION_NAME);
        return false;
    }
    if (!intern_token(ast, token, &name_index, errors)) {
        return false;
    }
    (*cursor)++;
    return parse_function_tail(tokens, string_bytes, ast, cursor,
                               declaration_pos, name_index, AST_NO_NODE,
                               AST_NO_NODE, false, false, errors);
}


bool parse_method_declaration(
    const TokenList *tokens, const List *string_bytes, Ast *ast, size_t *cursor,
    SourcePos declaration_pos, bool is_static, ErrorState *errors
)
{
    const Token *owner = token_at(tokens, *cursor);
    const Token *separator;
    const Token *member;
    size_t owner_name_index;
    size_t owner_type_index;
    size_t method_name_index;

    if (owner == NULL || owner->type != TOKEN_IDENTIFIER) {
        error_report_at(errors, position_at(tokens, *cursor),
                        MESSAGE_PARSER_EXPECTED_TYPE_NAME_AFTER_TYPE);
        return false;
    }
    if (!intern_token(ast, owner, &owner_name_index, errors) ||
        !parse_type_primary(tokens, string_bytes, ast, cursor,
                            &owner_type_index, errors)) {
        return false;
    }
    separator = token_at(tokens, *cursor);
    if (separator == NULL ||
        (is_static ? separator->type != TOKEN_DOUBLE_COLON
                   : separator->type != TOKEN_DOT)) {
        error_report_at(errors, position_at(tokens, *cursor),
                        MESSAGE_PARSER_EXPECTED_MEMBER_NAME);
        return false;
    }
    (*cursor)++;
    member = token_at(tokens, *cursor);
    if (member == NULL || member->type != TOKEN_IDENTIFIER) {
        error_report_at(errors, position_at(tokens, *cursor),
                        MESSAGE_PARSER_EXPECTED_FUNCTION_NAME);
        return false;
    }
    if (!intern_token(ast, member, &method_name_index, errors)) {
        return false;
    }
    (*cursor)++;
    return parse_function_tail(tokens, string_bytes, ast, cursor,
                               declaration_pos, method_name_index,
                               owner_name_index, owner_type_index, !is_static,
                               is_static, errors);
}

static bool parse_function_tail_token_at(const Token **token, const TokenList *tokens, size_t *cursor, bool is_method, size_t *return_type_index, size_t owner_type_index, const List *string_bytes, Ast *ast, ErrorState *errors, List *parameters, size_t *body_index)
{
    (*token) = token_at(tokens, *cursor);
    if ((*token) != NULL && (*token)->type == TOKEN_COLON) {
        (*cursor)++;
        (*token) = token_at(tokens, *cursor);
        if (is_method && (*token) != NULL && (*token)->type == TOKEN_IDENTIFIER &&
            (*token)->length == 4 && memcmp((*token)->text, "self", 4) == 0) {
            (*return_type_index) = owner_type_index;
            (*cursor)++;
        } else if (!parse_type(tokens, string_bytes, ast, cursor,
                               return_type_index, errors)) {
            list_free(parameters);
            return false;
        }
    }

    if (!parse_block(tokens, string_bytes, ast, cursor, body_index, errors)) {
        list_free(parameters);
        return false;
    }
    (*token) = token_at(tokens, *cursor);
    if ((*token) == NULL || (*token)->type != TOKEN_KEYWORD_END) {
        error_report_at(errors, position_at(tokens, *cursor),
                        MESSAGE_PARSER_EXPECTED_STATEMENT_OR_END);
        list_free(parameters);
        return false;
    }
    return true;
}

static bool parse_function_tail_token_at4(const Token **token, const TokenList *tokens, size_t *cursor, ErrorState *errors, List *parameters, AstNode *parameter, Ast *ast, size_t *parameter_name_index, const List *string_bytes, size_t *parameter_type_index, size_t *parameter_index)
{
    (*token) = token_at(tokens, *cursor);
    if ((*token) == NULL || (*token)->type != TOKEN_IDENTIFIER) {
        error_report_at(errors, position_at(tokens, *cursor),
                        MESSAGE_PARSER_EXPECTED_PARAMETER_NAME);
        list_free(parameters);
        return false;
    }
    (*parameter).pos = (*token)->pos;
    if (!intern_token(ast, (*token), parameter_name_index, errors)) {
        list_free(parameters);
        return false;
    }
    (*cursor)++;

    (*token) = token_at(tokens, *cursor);
    if ((*token) == NULL || (*token)->type != TOKEN_COLON) {
        error_report_at(errors, position_at(tokens, *cursor),
                        MESSAGE_PARSER_EXPECTED_PARAMETER_COLON);
        list_free(parameters);
        return false;
    }
    (*cursor)++;

    if (!parse_type(tokens, string_bytes, ast, cursor, parameter_type_index,
                    errors)) {
        list_free(parameters);
        return false;
    }

    (*parameter).kind = AST_PARAMETER;
    (*parameter).name_index = (*parameter_name_index);
    (*parameter).left_child = (*parameter_type_index);
    (*parameter).right_child = AST_NO_NODE;
    if (!push_node(ast, parameter, parameter_index) ||
        !list_push(parameters, parameter_index)) {
        error_report_fatal(errors, MESSAGE_PARSER_OUT_OF_MEMORY_BUILDING_TREE);
        list_free(parameters);
        return false;
    }
    return true;
}

static bool parse_function_tail_self_name(Ast *ast, ErrorState *errors, List *parameters, AstNode *self_parameter, SourcePos *declaration_pos, size_t owner_type_index, size_t *i)
{
    size_t self_name_index;
    self_name_index = ast_intern_symbol(ast, "self", 4);
    if (self_name_index == AST_NO_NODE) {
        error_report_fatal(errors, MESSAGE_PARSER_OUT_OF_MEMORY_RECORDING_NAME);
        list_free(parameters);
        return false;
    }
    (*self_parameter).kind = AST_PARAMETER;
    (*self_parameter).pos = (*declaration_pos);
    (*self_parameter).name_index = self_name_index;
    (*self_parameter).left_child = owner_type_index;
    (*self_parameter).right_child = AST_NO_NODE;
    (*self_parameter).is_self = true;
    if (!push_node(ast, self_parameter, i) ||
        !ast_push_child(ast, (*i))) {
        error_report_fatal(errors, MESSAGE_PARSER_OUT_OF_MEMORY_BUILDING_TREE);
        list_free(parameters);
        return false;
    }
    return true;
}

static bool parse_function_tail_children_start(const TokenList *tokens, const List *string_bytes, Ast *ast, size_t *cursor, size_t *type_parameters_start, size_t *type_parameters_count, ErrorState *errors, const Token **token, size_t *return_type_index, AstNode *declaration)
{
    (*return_type_index) = AST_NO_NODE;
    
    
    
    (*declaration) = (AstNode){0};

    if (!parse_type_parameters(tokens, string_bytes, ast, cursor,
                               type_parameters_start,
                               type_parameters_count, errors)) {
        return false;
    }

    (*token) = token_at(tokens, *cursor);
    if ((*token) == NULL || (*token)->type != TOKEN_LPAREN) {
        error_report_at(errors, position_at(tokens, *cursor),
                        MESSAGE_PARSER_EXPECTED_FUNCTION_LPAREN);
        return false;
    }
    return true;
}

static bool parse_function_tail(
    const TokenList *tokens, const List *string_bytes, Ast *ast, size_t *cursor,
    SourcePos declaration_pos, size_t name_index, size_t owner_name_index,
    size_t owner_type_index, bool is_method, bool is_static, ErrorState *errors
)
{
    const Token *token;
    size_t type_parameters_start;
    size_t type_parameters_count;
    List parameters = {0};
    size_t return_type_index = {0};
    size_t body_index = {0};
    size_t children_start = {0};
    size_t i = {0};
    AstNode declaration = {0};
    if (!parse_function_tail_children_start(tokens, string_bytes, ast, cursor, &type_parameters_start, &type_parameters_count, errors, &token, &return_type_index, &declaration)) {
        return false;
    }
    (*cursor)++;

    list_init(&parameters, sizeof(size_t));
    token = token_at(tokens, *cursor);
    if (token != NULL && token->type == TOKEN_RPAREN) {
        (*cursor)++;
    } else {
        for (;;) {
            size_t parameter_name_index;
            size_t parameter_type_index;
            size_t parameter_index;
            AstNode parameter = {0};

            if (!parse_function_tail_token_at4(&token, tokens, cursor, errors, &parameters, &parameter, ast, &parameter_name_index, string_bytes, &parameter_type_index, &parameter_index)) {
        return false;
    }

            token = token_at(tokens, *cursor);
            if (token == NULL) {
                error_report_at(errors, position_at(tokens, *cursor),
                                MESSAGE_PARSER_EXPECTED_PARAMETER_SEPARATOR);
                list_free(&parameters);
                return false;
            }
            if (token->type == TOKEN_COMMA) {
                (*cursor)++;
                continue;
            }
            if (token->type == TOKEN_RPAREN) {
                (*cursor)++;
                break;
            }
            error_report_at(errors, token->pos,
                            MESSAGE_PARSER_EXPECTED_PARAMETER_SEPARATOR);
            list_free(&parameters);
            return false;
        }
    }

    if (!parse_function_tail_token_at(&token, tokens, cursor, is_method, &return_type_index, owner_type_index, string_bytes, ast, errors, &parameters, &body_index)) {
        return false;
    }
    (*cursor)++;

    /* Parameters are appended after the body so a parameter type that is
       itself a function type cannot split the run. */
    children_start = ast->children.length;
    if (is_method) {
        AstNode self_parameter = {0};
        if (!parse_function_tail_self_name(ast, errors, &parameters, &self_parameter, &declaration_pos, owner_type_index, &i)) {
        return false;
    }
        declaration.children_count = 1;
    }
    for (i = 0; i < parameters.length; i++) {
        size_t parameter_index = *(const size_t *)list_get(&parameters, i);

        if (!ast_push_child(ast, parameter_index)) {
            error_report_fatal(errors, MESSAGE_PARSER_OUT_OF_MEMORY_BUILDING_TREE);
            list_free(&parameters);
            return false;
        }
    }
    declaration.children_count += parameters.length;
    list_free(&parameters);

    declaration.kind = AST_FUNCTION_DECLARATION;
    declaration.pos = declaration_pos;
    declaration.name_index = name_index;
    declaration.second_name_index = owner_name_index;
    declaration.owner_type_index = owner_type_index;
    declaration.is_method = is_method;
    declaration.is_static = is_static;
    declaration.left_child = body_index;
    declaration.right_child = return_type_index;
    declaration.children_start = children_start;
    declaration.type_parameters_start = type_parameters_start;
    declaration.type_parameters_count = type_parameters_count;
    return push_root(ast, &declaration, errors);
}
