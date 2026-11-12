#ifndef NIPPY_TOOL_SUPPORT_H
#define NIPPY_TOOL_SUPPORT_H

#include "test_harness.h"
#include "compiler.h"
#include "messages.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

void cleanup(void);
int write_text(const char *path, const char *text);
int write_bytes(const char *path, const unsigned char *data, size_t length);
int file_has_content(const char *path);
int file_contains(const char *path, const char *needle);
int run_compiler(const char *source_path, const char *binary, const char *log);
int run_command(const char *command, const char *log);
int file_equals(const char *path, const char *expected);
TestResult run_generated(const char *source, const char *expected,
                         const char *what);
TestResult run_generated_driver(const char *source, const char *driver,
                                const char *expected, const char *what);
int run_with_alloc_failure(size_t index, const char *source_path,
                           const char *binary, const char *log);
TestResult run_generated_failure(const char *source, const char *what,
                                 const char *log_needle);

#endif
