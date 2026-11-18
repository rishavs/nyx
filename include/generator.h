#ifndef NYX_GENERATOR_H
#define NYX_GENERATOR_H

#include "analysis.h"
#include "error.h"
#include "typed_program.h"

#include <stdbool.h>

/* Consumes the typed program and the analyser's facts about it. The syntax
   tree and the token list are not reachable from here. `analysis` must have
   been built from `program` (analyser_analyse or analysis_collect) and must
   outlive the call. `source_name` is interpolated into the emitted #line
   directive; it defaults to nyx.nip when NULL. */
bool generator_generate(const TypedProgram *program, const Analysis *analysis,
                        const char *header_name, const char *source_name,
                        char **c_source, char **h_source, ErrorState *errors);

#endif
