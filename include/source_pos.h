#ifndef NIPPY_SOURCE_POS_H
#define NIPPY_SOURCE_POS_H

#include <stddef.h>

/* A position in a Nippy source file. `offset` is a 0-based byte offset;
   `line` and `column` are 1-based, with column counted from the start of the
   line. Every token, syntax node, and typed node records the position of its
   first character so diagnostics and emitted #line directives can point back
   at the source. */
typedef struct {
    size_t offset;
    size_t line;
    size_t column;
} SourcePos;

#endif
