#ifndef NYX_MACROS_H
#define NYX_MACROS_H

/* Number of elements in an array (not a pointer). */
#define NYX_COUNT_OF(array) (sizeof(array) / sizeof((array)[0]))

/* The text of a macro's value as a string literal: NYX_STRINGIFY(X) is "10"
   when X is defined as 10. */
#define NYX_STRINGIFY_(value) #value
#define NYX_STRINGIFY(value) NYX_STRINGIFY_(value)

#endif
