#ifndef NIPPY_PARSER_H
#define NIPPY_PARSER_H

#include "ast.h"
#include "error.h"
#include "lexer.h"

#include <stdbool.h>

bool parser_parse(const TokenList *tokens, Ast *ast, ErrorState *errors);

#endif
