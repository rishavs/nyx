#ifndef NYX_LIMITS_CONFIG_H
#define NYX_LIMITS_CONFIG_H

/* Defaults and hard limits shared across compiler stages. */
#define NYX_DEFAULT_MAX_SOURCE_BYTES 1048576
#define NYX_DEFAULT_MAX_STRING_CAPACITY 4096
#define NYX_DEFAULT_MAX_LIST_CAPACITY 4096
#define NYX_DEFAULT_MAX_PRINT_DEPTH 16
#define NYX_DEFAULT_ERROR_HEADER_CAPACITY 32
#define NYX_DEFAULT_ERROR_DESCRIPTION_CAPACITY 128
#define NYX_DEFAULT_ERROR_FILENAME_CAPACITY 128
#define NYX_MAX_STRING_CAPACITY (1024 * 1024)
#define NYX_MAX_LIST_CAPACITY (1024 * 1024)
#define NYX_MAX_PRINT_DEPTH 1024
/* The most a value of one type may occupy by value. This is the default; a
   program may raise or lower it through CompilerConfig.max_value_bytes, up to
   the hard limit. Every by-value copy of a value lives on the C stack, so a
   larger limit is a choice to spend more of it. */
#define NYX_MAX_VALUE_BYTES (64 * 1024)
#define NYX_MAX_VALUE_BYTES_HARD (16 * 1024 * 1024)
/* A channel's buffer is static storage, so both its slot count and its total
   size are bounded. */
#define NYX_MAX_CHANNEL_CAPACITY 65536
#define NYX_MAX_CHANNEL_BYTES (64 * 1024 * 1024)

/* Fixed safety limits; some also determine internal array sizes. */
#define NYX_MAX_PARSE_NESTING 256
#define NYX_MAX_AST_DEPTH 1024
#define NYX_MAX_LAYOUT_DEPTH 1024
#define NYX_MAX_TYPE_PARAMETERS 8
#define NYX_TYPE_DEPTH 64
#define NYX_MAX_INSTANTIATIONS 4096
#define NYX_RUNTIME_RECURSION_LIMIT 10000

#endif
