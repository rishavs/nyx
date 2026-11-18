#ifndef NYX_IDENTIFIER_H
#define NYX_IDENTIFIER_H

/* Longest identifier the language accepts, including the terminating NUL.
   Names are stored inline in fixed-size records rather than behind pointers, so
   this is a hard limit shared by the lexer, the syntax tree, and the typed
   program. Raise it in one place only. */
#define NYX_IDENTIFIER_MAX 32

/* Prefix reserved for identifiers the generator invents. Source names that
   start with it are rejected by the checker, so generated and user names
   cannot collide in emitted C. */
#define NYX_GENERATED_PREFIX "nyx_"

#endif
