#ifndef NIPPY_CHECKER_H
#define NIPPY_CHECKER_H

#include "ast.h"
#include "error.h"
#include "typed_program.h"

#include <stdbool.h>

/* Read-only over the syntax tree, write-only to `program`: the checked program
   is a separate artifact, so nothing in this stage writes to `ast`. The caller
   must have initialised `program` and must free it. */
bool checker_check(const Ast *ast, TypedProgram *program, ErrorState *errors);

#endif
