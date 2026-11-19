#ifndef NYX_PRELUDE_H
#define NYX_PRELUDE_H

#include "lexer.h"

#include <stdbool.h>
#include <stddef.h>

/* The declarations the language provides in its own source, added to a program
   only when it names them. Today that is `ErrorKind`, the sum type an `Error`
   carries; it is written as Nyx, so it is checked and emitted like any user
   sum type. The tokens of the prelude are appended to the program's tokens,
   positioned after the end of the source, so a program's own diagnostics keep
   their positions. */

/* Whether the tokens name `Error` or `ErrorKind`. */
bool prelude_is_needed(const TokenList *tokens);

/* Appends the prelude's tokens, before the end-of-file token. The header of an
   `ErrorKind.Other` is a `String<header_capacity>`. Returns false when memory
   runs out. `source` is the program text the tokens came from, which decides
   where the prelude's positions start. */
bool prelude_append(LexerResult *lexed, const char *source,
                    int header_capacity);

#endif
