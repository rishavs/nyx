#ifndef NIPPY_COMPILER_H
#define NIPPY_COMPILER_H

#include "limits_config.h"

#include <stdbool.h>

typedef struct {
    /* Header filename used by the generated C source; defaults to nippy.h. */
    const char *header_name;
    /* Source filename shown in diagnostics and emitted #line directives. */
    const char *source_name;
    /* Error field capacities in bytes. 0 selects the default (32, 128, 128),
       clamped to max_string_capacity; explicit values must be in
       1..max_string_capacity. */
    int error_header_capacity;
    int error_description_capacity;
    int error_filename_capacity;
    /* Source-size limit in bytes. 0 selects 4096. Enforced by
       compiler_compile and by the command-line driver's read. */
    int max_source_bytes;
    /* Upper bound for String<N> capacities. 0 selects 4096. */
    int max_string_capacity;
    /* Upper bound for fixed List<T,N> capacities. 0 selects 4096. */
    int max_list_capacity;
    /* Maximum pretty-print nesting depth. 0 selects 16. */
    int max_print_depth;
} CompilerConfig;

typedef struct {
    bool succeeded;
    char *c_source;
    char *h_source;
    char *error_message;
} CompilerResult;

CompilerResult compiler_compile(const char *source, const CompilerConfig *config);
void compiler_result_free(CompilerResult *result);

#endif
