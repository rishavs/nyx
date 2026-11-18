#ifndef NYX_ANALYSER_INTERNAL_H
#define NYX_ANALYSER_INTERNAL_H

#include "analysis.h"
#include "macros.h"
#include "error.h"
#include "typed_program.h"

#include <stdbool.h>

/* Rejects programs in which a value from a scoped Store could outlive the
   scope. Does nothing for programs without a `scope`. */
bool analyser_check_regions(const TypedProgram *program,
                            const Analysis *analysis, ErrorState *errors);

/* Every Store use names a Store that exists: a global by id, a local by name. */
bool analyser_validate_stores(const TypedProgram *program, size_t store_count,
                              ErrorState *errors);

/* Rejects parallel arms that reach a global Store or an indirect call. */
bool analyser_audit_parallel(const TypedProgram *program, ErrorState *errors);

/* Whether the node allocates from, or names, a Store. */
bool analyser_node_uses_store(const TypedProgram *program, const TypedNode *node);

/* The call a parallel arm makes, under the temporaries that order its
   arguments; NULL when the arm holds no call. */
const TypedNode *analyser_parallel_arm_call(const TypedProgram *program,
                                            const TypedNode *arm);

#endif
