#ifndef NIPPY_ANALYSER_INTERNAL_H
#define NIPPY_ANALYSER_INTERNAL_H

#include "macros.h"
#include "error.h"
#include "typed_program.h"

#include <stdbool.h>

/* Rejects programs in which a value from a scoped Store could outlive the
   scope. Does nothing for programs without a `scope`. */
bool analyser_check_regions(const TypedProgram *program, ErrorState *errors);

#endif
