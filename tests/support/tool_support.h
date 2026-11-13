#ifndef NIPPY_TOOL_SUPPORT_H
#define NIPPY_TOOL_SUPPORT_H

/* Helpers for tests that run the compiler or its generated programs as
   separate processes. Every file such a test writes goes through test_path(),
   which names a private directory per test, so nothing needs cleaning up and
   tests can run in parallel. */

#include "harness.h"
#include "compiler.h"
#include "messages.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

int write_text(const char *path, const char *text);
int write_bytes(const char *path, const unsigned char *data, size_t length);
int file_has_content(const char *path);
int file_contains(const char *path, const char *needle);
int file_equals(const char *path, const char *expected);

/* The compiler binary the tool tests run: $NIPPY_BIN, else ./bin/nippy. The
   poisoned-list build (`make test-poison`) points it at its own binary. */
const char *nippy_binary(void);

/* Runs the compiler on `source binary`, sending its output to `log`. */
int run_compiler(const char *source_path, const char *binary, const char *log);
/* The same with NIPPY_FAIL_ALLOC_AFTER set, to fail the nth allocation. */
int run_with_alloc_failure(size_t index, const char *source_path,
                           const char *binary, const char *log);
int run_command(const char *command, const char *log);

/* Compile `source`, build the generated C, run it, and require exit status 0
   and standard output exactly equal to `expected`. */
TestResult run_generated(const char *source, const char *expected,
                         const char *what);
/* As run_generated, but links the program's `main` renamed to
   nyx_generated_main with `driver`, a C file that supplies its own main. */
TestResult run_generated_driver(const char *source, const char *driver,
                                const char *expected, const char *what);
/* Compile `source`, build and run it, and require a non-zero exit status with
   `log_needle` somewhere in its output. */
TestResult run_generated_failure(const char *source, const char *log_needle,
                                 const char *what);

#endif
