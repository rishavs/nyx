#ifndef NIPPY_MACROS_H
#define NIPPY_MACROS_H

/* Number of elements in an array (not a pointer). */
#define NIPPY_COUNT_OF(array) (sizeof(array) / sizeof((array)[0]))

#endif
