#ifndef NYX_ANALYSER_H
#define NYX_ANALYSER_H

#include "analysis.h"
#include "error.h"
#include "typed_program.h"

#include <stdbool.h>

/* Consumes the typed program, not the syntax tree. It validates the program's
   index links and Store capabilities (analyser_validate), audits parallel
   arms, then rejects any value from a scoped Store that could outlive its
   scope. On success it fills `analysis` (when not NULL) with the program-wide
   facts the generator needs, which the caller releases with analysis_free; on
   failure `analysis` is left untouched. The typed program is never written. */
bool analyser_analyse(const TypedProgram *program, Analysis *analysis,
                      ErrorState *errors);
/* The link and capability validation alone, without the escape analysis. */
bool analyser_validate(const TypedProgram *program, ErrorState *errors);
/* The facts alone, with no validation: for a caller that has validated the
   program another way, or built one by hand. Tolerates dangling links by
   skipping what it cannot read. False, with a diagnostic, if it cannot
   allocate the node index. The caller releases it with analysis_free. */
bool analysis_collect(const TypedProgram *program, Analysis *analysis,
                      ErrorState *errors);
/* The part of analysis_collect that works out the lists in `Analysis`: the
   order to define types in, and the types print and equality need helpers
   for. Needs the node index. */
bool analysis_collect_types(const TypedProgram *program, Analysis *analysis,
                            ErrorState *errors);

#endif
