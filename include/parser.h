#ifndef NYX_PARSER_H
#define NYX_PARSER_H

#include "ast.h"
#include "error.h"
#include "lexer.h"

#include <stdbool.h>

bool parser_parse(const TokenList *tokens, const List *string_bytes, Ast *ast,
                  ErrorState *errors);

#endif
