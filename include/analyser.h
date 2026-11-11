#ifndef NIPPY_ANALYSER_H
#define NIPPY_ANALYSER_H

#include "error.h"
#include "typed_program.h"

#include <stdbool.h>

/* Consumes the typed program, not the syntax tree. Today it validates the
   program's index links; memory and ownership rules belong here once the
   language has storage. It produces no artifact of its own. */
bool analyser_analyse(const TypedProgram *program, ErrorState *errors);

#endif
