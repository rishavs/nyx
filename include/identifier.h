#ifndef NIPPY_IDENTIFIER_H
#define NIPPY_IDENTIFIER_H

/* Longest identifier the language accepts, including the terminating NUL.
   Names are stored inline in fixed-size records rather than behind pointers, so
   this is a hard limit shared by the lexer, the syntax tree, and the typed
   program. Raise it in one place only. */
#define NIPPY_IDENTIFIER_MAX 32

#endif
