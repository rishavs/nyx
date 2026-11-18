#include "analyser.h"
#include "analyser_internal.h"

/* The analyser validates typed-program links (analyser_validate.c) and Store
   capability ownership (analyser_stores.c), audits parallel arms
   (analyser_parallel.c), collects the facts the generator reads
   (analyser_facts.c, analyser_types.c), then runs the scoped-Store escape
   analysis (analyser_regions.c). The syntax tree is freed before this stage
   runs. */

bool analyser_analyse(const TypedProgram *program, Analysis *analysis,
                      ErrorState *errors)
{
    Analysis facts;

    if (!analyser_validate(program, errors)) {
        return false;
    }
    if (!analysis_collect(program, &facts, errors)) {
        return false;
    }
    if (!analyser_audit_parallel(program, errors) ||
        !analyser_check_regions(program, &facts, errors)) {
        analysis_free(&facts);
        return false;
    }
    if (analysis != NULL) {
        *analysis = facts;
    } else {
        analysis_free(&facts);
    }
    return true;
}
