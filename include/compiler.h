#ifndef NIPPY_COMPILER_H
#define NIPPY_COMPILER_H

#include <stdbool.h>

typedef struct {
    /* Header filename used by the generated C source; defaults to nippy.h. */
    const char *header_name;
    /* Source filename shown in diagnostics and emitted #line directives. */
    const char *source_name;
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
