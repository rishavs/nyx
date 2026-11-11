#ifndef NIPPY_GENERATOR_H
#define NIPPY_GENERATOR_H

#include "error.h"
#include "typed_program.h"

#include <stdbool.h>

/* Consumes the typed program only. The syntax tree and the token list are not
   reachable from here. `source_name` is interpolated into the emitted #line
   directive; it defaults to nippy.nip when NULL. */
bool generator_generate(const TypedProgram *program, const char *header_name,
                        const char *source_name, char **c_source,
                        char **h_source, ErrorState *errors);

#endif
