#ifndef NYX_GENERATED_H
#define NYX_GENERATED_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#line 5 "test.nip"
typedef struct {
    char nyx_unit;
} nyx_u_nyx_gen_0;

#line 5 "test.nip"
typedef struct {
    char nyx_unit;
} nyx_u_nyx_gen_1;

#line 5 "test.nip"
typedef struct {
    char nyx_unit;
} nyx_u_nyx_gen_2;

#line 5 "test.nip"
typedef struct {
    char nyx_unit;
} nyx_u_nyx_gen_3;

#line 6 "test.nip"
typedef struct {
    char nyx_unit;
} nyx_u_nyx_gen_4;

#line 6 "test.nip"
typedef struct {
    char nyx_unit;
} nyx_u_nyx_gen_5;

#line 6 "test.nip"
typedef struct {
    char nyx_unit;
} nyx_u_nyx_gen_6;

#line 6 "test.nip"
typedef struct {
    char nyx_unit;
} nyx_u_nyx_gen_7;

#line 7 "test.nip"
typedef struct {
    char nyx_unit;
} nyx_u_nyx_gen_8;

#line 7 "test.nip"
typedef struct {
    char nyx_unit;
} nyx_u_nyx_gen_9;

#line 7 "test.nip"
typedef struct {
    char nyx_unit;
} nyx_u_nyx_gen_10;

#line 7 "test.nip"
typedef struct {
    char nyx_unit;
} nyx_u_nyx_gen_11;

#line 7 "test.nip"
typedef struct {
    char nyx_unit;
} nyx_u_nyx_gen_12;

#line 7 "test.nip"
typedef struct {
    char nyx_unit;
} nyx_u_nyx_gen_13;

#line 8 "test.nip"
typedef struct {
    char nyx_unit;
} nyx_u_nyx_gen_14;

#line 8 "test.nip"
typedef struct {
    char nyx_unit;
} nyx_u_nyx_gen_15;

#line 8 "test.nip"
typedef struct {
    char nyx_unit;
} nyx_u_nyx_gen_16;

#line 8 "test.nip"
typedef struct {
    char nyx_unit;
} nyx_u_nyx_gen_17;

#line 9 "test.nip"
typedef struct {
    char nyx_unit;
} nyx_u_nyx_gen_18;

#line 9 "test.nip"
typedef struct {
    char nyx_unit;
} nyx_u_nyx_gen_19;

#line 9 "test.nip"
typedef struct {
    char nyx_unit;
} nyx_u_nyx_gen_20;

#line 10 "test.nip"
typedef struct {
    char nyx_unit;
} nyx_u_nyx_gen_21;

#line 10 "test.nip"
typedef struct {
    char nyx_unit;
} nyx_u_nyx_gen_22;

#line 10 "test.nip"
typedef struct {
    char nyx_unit;
} nyx_u_nyx_gen_23;

#line 10 "test.nip"
typedef struct {
    char nyx_unit;
} nyx_u_nyx_gen_24;

typedef struct {
    uint8_t data[33];
    uint32_t length;
} nyx_string_32;

static inline uint8_t nyx_string_32_at(nyx_string_32 s, int32_t i, const char *file, size_t line, size_t column, size_t offset);

#line 11 "test.nip"
typedef struct {
    nyx_string_32 nyx_u_header;
} nyx_u_nyx_gen_25;

typedef struct {
    uint32_t tag;
    union {
        nyx_u_nyx_gen_0 v0;
        nyx_u_nyx_gen_1 v1;
        nyx_u_nyx_gen_10 v2;
        nyx_u_nyx_gen_11 v3;
        nyx_u_nyx_gen_12 v4;
        nyx_u_nyx_gen_13 v5;
        nyx_u_nyx_gen_14 v6;
        nyx_u_nyx_gen_15 v7;
        nyx_u_nyx_gen_16 v8;
        nyx_u_nyx_gen_17 v9;
        nyx_u_nyx_gen_18 v10;
        nyx_u_nyx_gen_19 v11;
        nyx_u_nyx_gen_2 v12;
        nyx_u_nyx_gen_20 v13;
        nyx_u_nyx_gen_21 v14;
        nyx_u_nyx_gen_22 v15;
        nyx_u_nyx_gen_23 v16;
        nyx_u_nyx_gen_24 v17;
        nyx_u_nyx_gen_25 v18;
        nyx_u_nyx_gen_3 v19;
        nyx_u_nyx_gen_4 v20;
        nyx_u_nyx_gen_5 v21;
        nyx_u_nyx_gen_6 v22;
        nyx_u_nyx_gen_7 v23;
        nyx_u_nyx_gen_8 v24;
        nyx_u_nyx_gen_9 v25;
    } value;
} nyx_sum_27;

typedef struct {
    uint8_t data[129];
    uint32_t length;
} nyx_string_128;

static inline uint8_t nyx_string_128_at(nyx_string_128 s, int32_t i, const char *file, size_t line, size_t column, size_t offset);

typedef struct {
    nyx_sum_27 kind;
    nyx_string_128 description;
    nyx_string_128 filename;
    int32_t line;
    int32_t col;
    int32_t pos;
} nyx_error;

static inline nyx_string_32 nyx_error_header(nyx_error e)
{
    nyx_string_32 r = { {0}, 0 };
    const char *text = "";
    switch (e.kind.tag) {
    case 0: text = "NotFound"; break;
    case 1: text = "PermissionDenied"; break;
    case 2: text = "Interrupted"; break;
    case 3: text = "Cancelled"; break;
    case 4: text = "TimedOut"; break;
    case 5: text = "Unsupported"; break;
    case 6: text = "ResourceExhausted"; break;
    case 7: text = "Closed"; break;
    case 8: text = "AddressInUse"; break;
    case 9: text = "AddressUnavailable"; break;
    case 10: text = "ConnectionRefused"; break;
    case 11: text = "ConnectionReset"; break;
    case 12: text = "AlreadyExists"; break;
    case 13: text = "ConnectionAborted"; break;
    case 14: text = "HostUnreachable"; break;
    case 15: text = "NetworkUnreachable"; break;
    case 16: text = "BrokenPipe"; break;
    case 17: text = "NotConnected"; break;
    case 18: return e.kind.value.v18.nyx_u_header;
    case 19: text = "InvalidInput"; break;
    case 20: text = "InvalidPath"; break;
    case 21: text = "NotADirectory"; break;
    case 22: text = "IsADirectory"; break;
    case 23: text = "DirectoryNotEmpty"; break;
    case 24: text = "ReadOnly"; break;
    case 25: text = "Busy"; break;
    default: break;
    }
    while (text[r.length] != '\0' && r.length < 32) {
        r.data[r.length] = (uint8_t)text[r.length];
        r.length++;
    }
    return r;
}

typedef uint8_t nyx_nil;

#endif
