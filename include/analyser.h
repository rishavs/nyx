#ifndef NIPPY_ANALYSER_H
#define NIPPY_ANALYSER_H

#include "error.h"
#include "typed_program.h"

#include <stdbool.h>

/* Consumes the typed program, not the syntax tree. It validates the program's
   index links and Store capabilities (analyser_validate), then rejects any
   value from a scoped Store that could outlive its scope. It produces no
   artifact of its own. */
bool analyser_analyse(const TypedProgram *program, ErrorState *errors);
/* The link and capability validation alone, without the escape analysis. */
bool analyser_validate(const TypedProgram *program, ErrorState *errors);

#endif
