#include "error.h"

#include "messages.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

void error_init(ErrorState *state)
{
    state->reported = false;
    state->source_name = NULL;
    state->length = 0;
    state->message[0] = '\0';
}

/* Append formatted text, truncating at the buffer's last byte so `message`
   always ends with a NUL and `length` never reaches the capacity. */
static void append_format(ErrorState *state, const char *format, va_list args)
{
    size_t remaining;
    int written;

    if (state->length >= sizeof(state->message) - 1) {
        return;
    }
    remaining = sizeof(state->message) - state->length;
    written = vsnprintf(state->message + state->length, remaining, format, args);
    if (written < 0) {
        return;
    }
    if ((size_t)written >= remaining) {
        state->length = sizeof(state->message) - 1;
        return;
    }
    state->length += (size_t)written;
}

static void append_text(ErrorState *state, const char *text)
{
    size_t length = 0;

    if (state->length >= sizeof(state->message) - 1) {
        return;
    }
    while (text[length] != '\0' &&
           state->length + length < sizeof(state->message) - 1) {
        state->message[state->length + length] = text[length];
        length++;
    }
    state->length += length;
    state->message[state->length] = '\0';
}

static void finish_message(ErrorState *state)
{
    if (state->length < sizeof(state->message) - 1) {
        state->message[state->length++] = '\n';
        state->message[state->length] = '\0';
    }
}

void error_report(ErrorState *state, const char *format, ...)
{
    va_list args;

    state->reported = true;
    va_start(args, format);
    append_format(state, format, args);
    va_end(args);
    finish_message(state);
}

void error_report_at(ErrorState *state, SourcePos pos, const char *format, ...)
{
    va_list args;
    char prefix[128];

    state->reported = true;
    snprintf(prefix, sizeof(prefix), "%s:%zu:%zu (byte %zu): ",
             state->source_name == NULL ? "<source>" : state->source_name,
             pos.line, pos.column, pos.offset);
    append_text(state, prefix);

    va_start(args, format);
    append_format(state, format, args);
    va_end(args);
    finish_message(state);
}

void error_unknown_if_none(ErrorState *state)
{
    if (!state->reported) {
        error_report(state, MESSAGE_UNKNOWN_ERROR);
    }
}
