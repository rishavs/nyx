#ifndef NIPPY_COMPILE_SUPPORT_H
#define NIPPY_COMPILE_SUPPORT_H

#include "harness.h"
#include "alloc.h"
#include "analyser.h"
#include "ast.h"
#include "checker.h"
#include "compiler.h"
#include "error.h"
#include "generator.h"
#include "identifier.h"
#include "messages.h"
#include "typed_program.h"

#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* A large golden is split into several C string literals so no single literal
   exceeds the ISO C minimum. */
typedef struct {
    const char *const *parts;
    size_t count;
} GoldenText;

extern const CompilerConfig test_config;

int error_contains(const ErrorState *errors, const char *needle);
TestResult compile_succeeds_parts(const char *source,
                                  const GoldenText *expected_c,
                                  const char *expected_h, const char *what);
TestResult compile_succeeds(const char *source, const char *expected_c,
                            const char *expected_h, const char *what);
TestResult compile_fails(const char *source, const char *what,
                         const char *format, ...);
/* Passes when `source` compiles; otherwise prints the diagnostic. */
TestResult compile_accepts(const char *source, const char *what);

#endif
