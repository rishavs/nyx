#ifndef NIPPY_ERROR_H
#define NIPPY_ERROR_H

#include "source_pos.h"

#include <stdbool.h>
#include <stddef.h>

#define ERROR_MESSAGE_CAPACITY 4096

typedef struct {
    bool reported;
    bool fatal;
    /* Name of the source file being compiled, shown in positioned messages.
       Owned elsewhere; ErrorState never frees it. */
    const char *source_name;
    size_t length;
    char message[ERROR_MESSAGE_CAPACITY];
} ErrorState;

void error_init(ErrorState *state);
/* Append a message with no source position. */
void error_report(ErrorState *state, const char *format, ...);
/* Append a message prefixed with `file:line:column (byte offset): `. */
void error_report_at(ErrorState *state, SourcePos pos, const char *format, ...);
void error_report_fatal(ErrorState *state, const char *format, ...);
void error_report_fatal_at(ErrorState *state, SourcePos pos, const char *format, ...);
void error_unknown_if_none(ErrorState *state);

#endif
