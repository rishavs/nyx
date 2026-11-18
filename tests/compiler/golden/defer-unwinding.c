#include "test.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static inline int64_t nyx_range_step(int64_t step, const char *file, size_t line, size_t col, size_t pos) {
    if (step == 0) { fflush(stdout); fprintf(stderr, "nyx: %s:%zu:%zu (byte %zu): %s\n", file, line, col, pos, "range step cannot be zero"); exit(1); }
    return step;
}

#if defined(__GNUC__) || defined(__clang__)
#define NYX_UNUSED __attribute__((unused))
#else
#define NYX_UNUSED
#endif

static void nyx_panic(const char *message)
{
    fflush(stdout);
    fprintf(stderr, "%s\n", message);
    exit(1);
}

static inline void nyx_panic_at(const char *message, const char *file, size_t line, size_t column, size_t pos)
{
    if (file == NULL) nyx_panic(message);
    fflush(stdout);
    fprintf(stderr, "nyx: %s:%zu:%zu (byte %zu): %s\n", file, line, column, pos, message + sizeof("nyx: ") - 1);
    exit(1);
}

static inline size_t nyx_store_align_at(size_t offset, size_t alignment, const char *file, size_t line, size_t column, size_t pos)
{
    size_t remainder;

    if (alignment <= 1) {
        return offset;
    }
    remainder = offset % alignment;
    if (remainder == 0) {
        return offset;
    }
    if (offset > (size_t)-1 - (alignment - remainder)) {
        nyx_panic_at("nyx: out of memory", file, line, column, pos);
    }
    return offset + (alignment - remainder);
}

static inline size_t nyx_store_align(size_t offset, size_t alignment)
{ return nyx_store_align_at(offset, alignment, NULL, 0, 0, 0); }

static inline void nyx_store_init_at(nyx_store *store, size_t initial_bytes, const char *file, size_t line, size_t column, size_t pos)
{
    if (initial_bytes == 0) {
        initial_bytes = 1;
    }
    store->data = malloc(initial_bytes);
    if (store->data == NULL) {
        nyx_panic_at("nyx: out of memory", file, line, column, pos);
    }
    store->used = 0;
    store->capacity = initial_bytes;
    store->generation = 1;
}

static inline void nyx_store_init(nyx_store *store, size_t size)
{ nyx_store_init_at(store, size, NULL, 0, 0, 0); }

static inline void nyx_store_reserve_at(nyx_store *store, size_t required, const char *file, size_t line, size_t column, size_t pos)
{
    size_t capacity;
    unsigned char *grown;

    if (required <= store->capacity) {
        return;
    }
    capacity = store->capacity == 0 ? 1 : store->capacity;
    while (capacity < required) {
        if (capacity > (size_t)-1 / 2) {
            nyx_panic_at("nyx: out of memory", file, line, column, pos);
        }
        capacity *= 2;
    }
    grown = realloc(store->data, capacity);
    if (grown == NULL) {
        nyx_panic_at("nyx: out of memory", file, line, column, pos);
    }
    store->data = grown;
    store->capacity = capacity;
}

static inline void nyx_store_reserve(nyx_store *store, size_t required)
{ nyx_store_reserve_at(store, required, NULL, 0, 0, 0); }

static inline nyx_handle nyx_store_alloc_at(nyx_store *store, const void *value, size_t size,
                                      size_t align, const char *file, size_t line, size_t column, size_t pos)
{
    nyx_handle handle;
    size_t offset;
    size_t end;

    if (store == NULL) {
        nyx_panic_at("nyx: invalid store id", file, line, column, pos);
    }
    if (size == 0) {
        nyx_panic_at("nyx: handle is out of range", file, line, column, pos);
    }
    offset = nyx_store_align_at(store->used, align, file, line, column, pos);
    if (offset > (size_t)-1 - size) {
        nyx_panic_at("nyx: handle is out of range", file, line, column, pos);
    }
    end = offset + size;
    nyx_store_reserve_at(store, end, file, line, column, pos);
    memcpy(store->data + offset, value, size);
    store->used = end;
    handle.store = store;
    handle.offset = offset;
    handle.generation = store->generation;
    return handle;
}

static inline nyx_handle nyx_store_alloc(nyx_store *store, const void *value, size_t size, size_t align)
{ return nyx_store_alloc_at(store, value, size, align, NULL, 0, 0, 0); }

static inline unsigned char *nyx_handle_pointer_at(nyx_handle handle,
                                           size_t size, const char *file, size_t line, size_t column, size_t pos)
{
    nyx_store *store;

    if (handle.store == NULL) {
        nyx_panic_at("nyx: invalid store id", file, line, column, pos);
    }
    store = handle.store;
    if (handle.offset > store->used ||
        size > store->used - handle.offset) {
        nyx_panic_at("nyx: handle is out of range", file, line, column, pos);
    }
    return store->data + handle.offset;
}

static inline unsigned char *nyx_handle_pointer(nyx_handle handle, size_t size)
{ return nyx_handle_pointer_at(handle, size, NULL, 0, 0, 0); }

static inline void nyx_handle_write_at(nyx_handle handle, const void *value,
                               size_t size, const char *file, size_t line, size_t column, size_t pos)
{
    memcpy(nyx_handle_pointer_at(handle, size, file, line, column, pos), value, size);
}

static inline void nyx_handle_write(nyx_handle handle, const void *value, size_t size)
{ nyx_handle_write_at(handle, value, size, NULL, 0, 0, 0); }

static inline size_t nyx_store_alloc_bytes_at(nyx_store *store, size_t size, const char *file, size_t line, size_t column, size_t pos)
{
    size_t offset;

    if (store == NULL) {
        nyx_panic_at("nyx: invalid store id", file, line, column, pos);
    }
    if (size == 0) {
        nyx_panic_at("nyx: handle is out of range", file, line, column, pos);
    }
    offset = store->used;
    if (offset > (size_t)-1 - size) {
        nyx_panic_at("nyx: out of memory", file, line, column, pos);
    }
    nyx_store_reserve_at(store, offset + size, file, line, column, pos);
    store->used = offset + size;
    return offset;
}

static inline size_t nyx_store_alloc_bytes(nyx_store *store, size_t size)
{ return nyx_store_alloc_bytes_at(store, size, NULL, 0, 0, 0); }

static NYX_UNUSED void nyx_store_free(nyx_store *store)
{
    free(store->data);
    store->data = NULL;
    store->used = 0;
    store->capacity = 0;
    store->generation = 0;
    (void)nyx_store_reserve;
    (void)nyx_store_alloc;
    (void)nyx_handle_pointer;
    (void)nyx_handle_write;
}

static NYX_UNUSED void nyx_string_fail(const char *message)
{
    fflush(stdout);
    fprintf(stderr, "%s\n", message);
    exit(1);
}

static NYX_UNUSED void nyx_string_fail_at(const char *message, const char *file, size_t line, size_t column, size_t pos)
{
    if (file == NULL) nyx_string_fail(message);
    fflush(stdout);
    fprintf(stderr, "nyx: %s:%zu:%zu (byte %zu): %s\n", file, line, column, pos, message + sizeof("nyx: ") - 1);
    exit(1);
}

static NYX_UNUSED const uint8_t *nyx_string_data_at(nyx_string s, const char *file, size_t line, size_t column, size_t pos)
{
    nyx_store *store;

    if (s.static_data != NULL) {
        return s.static_data;
    }
    if (s.store == NULL) {
        nyx_string_fail_at("nyx: invalid store id", file, line, column, pos);
    }
    store = s.store;
    if (s.offset > store->used ||
        s.length > store->used - s.offset ||
        s.length > (size_t)-1 - s.offset - 1 ||
        s.offset + s.length + 1 > store->used) {
        nyx_string_fail_at("nyx: string index out of bounds", file, line, column, pos);
    }
    return store->data + s.offset;
}

static NYX_UNUSED const uint8_t *nyx_string_data(nyx_string s)
{ return nyx_string_data_at(s, NULL, 0, 0, 0); }

static NYX_UNUSED int32_t nyx_string_len_at(nyx_string s, const char *file, size_t line, size_t column, size_t pos)
{
    (void)nyx_string_data_at(s, file, line, column, pos);
    return (int32_t)s.length;
}

static NYX_UNUSED uint8_t nyx_string_byte_at(nyx_string s, int32_t i, const char *file, size_t line, size_t column, size_t pos)
{
    if (i < 0 || (size_t)i >= s.length) {
        nyx_string_fail_at("nyx: string index out of bounds", file, line, column, pos);
    }
    return nyx_string_data_at(s, file, line, column, pos)[i];
}

static NYX_UNUSED nyx_string nyx_string_slice(nyx_string s, int32_t start, int32_t length, const char *file, size_t line, size_t column, size_t pos)
{
    nyx_string result;

    if (start < 0 || length < 0 ||
        (size_t)start > s.length ||
        (size_t)length > s.length - (size_t)start) {
        nyx_string_fail_at("nyx: string index out of bounds", file, line, column, pos);
    }
    (void)nyx_string_data_at(s, file, line, column, pos);
    result = s;
    if (result.static_data != NULL) {
        result.static_data += start;
    } else {
        result.offset += (size_t)start;
    }
    result.length = (size_t)length;
    return result;
}

static NYX_UNUSED bool nyx_string_equal_at(nyx_string a, nyx_string b, const char *file, size_t line, size_t column, size_t pos)
{
    const uint8_t *a_data = nyx_string_data_at(a, file, line, column, pos);
    const uint8_t *b_data = nyx_string_data_at(b, file, line, column, pos);
    return a.length == b.length && memcmp(a_data, b_data, a.length) == 0;
}

static NYX_UNUSED bool nyx_string_equal(nyx_string a, nyx_string b)
{
    return nyx_string_equal_at(a, b, NULL, 0, 0, 0);
}

static NYX_UNUSED nyx_string nyx_string_from_bytes(nyx_store *store,
                                             const uint8_t *bytes, size_t length, const char *file, size_t line, size_t column, size_t pos)
{
    size_t offset;
    nyx_string result;

    if (length > (size_t)-1 - 1) {
        nyx_string_fail_at("nyx: out of memory", file, line, column, pos);
    }
    offset = nyx_store_alloc_bytes_at(store, length + 1, file, line, column, pos);
    memcpy(store->data + offset, bytes, length);
    store->data[offset + length] = 0;
    result.static_data = NULL;
    result.store = store;
    result.offset = offset;
    result.length = length;
    result.generation = store->generation;
    return result;
}

static NYX_UNUSED nyx_string nyx_string_concat(nyx_store *store,
                                        nyx_string a, nyx_string b, const char *file, size_t line, size_t column, size_t pos)
{
    size_t total;
    size_t offset;
    nyx_string result;
    const uint8_t *a_data;
    const uint8_t *b_data;

    if (a.length > (size_t)-1 - 1 ||
        b.length > (size_t)-1 - 1 - a.length) {
        nyx_string_fail_at("nyx: out of memory", file, line, column, pos);
    }
    total = a.length + b.length;
    offset = nyx_store_alloc_bytes_at(store, total + 1, file, line, column, pos);
    a_data = nyx_string_data_at(a, file, line, column, pos);
    b_data = nyx_string_data_at(b, file, line, column, pos);
    memcpy(store->data + offset, a_data, a.length);
    memcpy(store->data + offset + a.length, b_data, b.length);
    store->data[offset + total] = 0;
    result.static_data = NULL;
    result.store = store;
    result.offset = offset;
    result.length = total;
    result.generation = store->generation;
    return result;
}

static NYX_UNUSED nyx_string nyx_string_copy(nyx_store *store,
                                      nyx_string s, const char *file, size_t line, size_t column, size_t pos)
{
    size_t offset;
    nyx_string result;
    const uint8_t *data;

    if (s.length > (size_t)-1 - 1) {
        nyx_string_fail_at("nyx: out of memory", file, line, column, pos);
    }
    offset = nyx_store_alloc_bytes_at(store, s.length + 1, file, line, column, pos);
    data = nyx_string_data_at(s, file, line, column, pos);
    memcpy(store->data + offset, data, s.length);
    store->data[offset + s.length] = 0;
    result.static_data = NULL;
    result.store = store;
    result.offset = offset;
    result.length = s.length;
    result.generation = store->generation;
    return result;
}

static const uint8_t nyx_dyn_str_1[] = { 111, 117, 116, 101, 114, 0 };
static const uint8_t nyx_dyn_str_13[] = { 115, 116, 101, 112, 0 };
static const uint8_t nyx_dyn_str_36[] = { 98, 105, 103, 0 };
static inline void nyx_print_4(nyx_string value, int depth, const char *file, size_t line, size_t column, size_t pos);
static inline void nyx_print_0(int32_t value, int depth, const char *file, size_t line, size_t column, size_t pos);
static inline void nyx_print_4(nyx_string value, int depth, const char *file, size_t line, size_t column, size_t pos)
{
    (void)value; (void)file; (void)line; (void)column; (void)pos;
    if (depth > 16) {
        fputs("<...>", stdout);
        return;
    }
    const uint8_t *data = nyx_string_data_at(value, file, line, column, pos);
    if (value.length != 0) {
        fwrite(data, 1, value.length, stdout);
    }
}

static inline void nyx_print_0(int32_t value, int depth, const char *file, size_t line, size_t column, size_t pos)
{
    (void)value; (void)file; (void)line; (void)column; (void)pos;
    if (depth > 16) {
        fputs("<...>", stdout);
        return;
    }
    fprintf(stdout, "%d", (int)value);
}


#line 1 "test.nip"
int32_t nyx_u_f(int32_t nyx_u_n)
{
    int32_t nyx_match_1;
    int32_t nyx_match_0;
    (void)nyx_u_n;
#line 2 "test.nip"
#line 3 "test.nip"
    {
        nyx_range_6 nyx_range_55 = (nyx_match_0 = 0, (nyx_match_1 = nyx_u_n, ((nyx_range_6){ nyx_match_0, nyx_match_1, INT64_C(1) })));
        int32_t nyx_cur_55 = nyx_range_55.start;
        uint64_t nyx_idx_55 = 0;
        bool nyx_active_55 = nyx_range_55.step > 0 ? nyx_cur_55 < nyx_range_55.end : nyx_cur_55 > nyx_range_55.end;
        for (; (void)nyx_idx_55, nyx_active_55; nyx_idx_55++) {
            int32_t nyx_u_i = nyx_cur_55; (void)nyx_u_i;
            uint64_t nyx_dist_55 = nyx_range_55.step > 0 ? (uint64_t)nyx_range_55.end - (uint64_t)nyx_cur_55 : (uint64_t)nyx_cur_55 - (uint64_t)nyx_range_55.end;
            uint64_t nyx_mag_55 = nyx_range_55.step > 0 ? (uint64_t)nyx_range_55.step : UINT64_C(0) - (uint64_t)nyx_range_55.step;
            if (nyx_mag_55 >= nyx_dist_55) nyx_active_55 = false;
            else nyx_cur_55 = (int32_t)((int64_t)nyx_cur_55 + nyx_range_55.step);
#line 4 "test.nip"
#line 5 "test.nip"
            if ((nyx_u_i == ((int64_t)2ULL))) {
#line 5 "test.nip"
                {
#line 4 "test.nip"
                    {
                        nyx_string nyx_print_arg_92_0 = (nyx_string){ nyx_dyn_str_13, 0, 0, 4, 0 };
                        nyx_print_4(nyx_print_arg_92_0, 0, __FILE__, 4, 14, 92);
                        int32_t nyx_print_arg_92_1 = nyx_u_i;
                        fputc(' ', stdout);
                        nyx_print_0(nyx_print_arg_92_1, 0, __FILE__, 4, 14, 92);
                        fputc('\n', stdout);
                    }
                }
                break;
            }
#line 6 "test.nip"
            if ((nyx_u_i == ((int64_t)1ULL))) {
#line 6 "test.nip"
                {
#line 4 "test.nip"
                    {
                        nyx_string nyx_print_arg_92_0 = (nyx_string){ nyx_dyn_str_13, 0, 0, 4, 0 };
                        nyx_print_4(nyx_print_arg_92_0, 0, __FILE__, 4, 14, 92);
                        int32_t nyx_print_arg_92_1 = nyx_u_i;
                        fputc(' ', stdout);
                        nyx_print_0(nyx_print_arg_92_1, 0, __FILE__, 4, 14, 92);
                        fputc('\n', stdout);
                    }
                }
                continue;
            }
            {
#line 4 "test.nip"
                {
                    nyx_string nyx_print_arg_92_0 = (nyx_string){ nyx_dyn_str_13, 0, 0, 4, 0 };
                    nyx_print_4(nyx_print_arg_92_0, 0, __FILE__, 4, 14, 92);
                    int32_t nyx_print_arg_92_1 = nyx_u_i;
                    fputc(' ', stdout);
                    nyx_print_0(nyx_print_arg_92_1, 0, __FILE__, 4, 14, 92);
                    fputc('\n', stdout);
                }
            }
        }
    }
#line 8 "test.nip"
    if ((nyx_u_n > 5)) {
#line 9 "test.nip"
#line 10 "test.nip"
        {
            int32_t nyx_scope_result_42 = nyx_u_n;
            {
#line 9 "test.nip"
                {
                    nyx_string nyx_print_arg_209_0 = (nyx_string){ nyx_dyn_str_36, 0, 0, 3, 0 };
                    nyx_print_4(nyx_print_arg_209_0, 0, __FILE__, 9, 14, 209);
                    fputc('\n', stdout);
                }
            }
            {
#line 2 "test.nip"
                {
                    nyx_string nyx_print_arg_34_0 = (nyx_string){ nyx_dyn_str_1, 0, 0, 5, 0 };
                    nyx_print_4(nyx_print_arg_34_0, 0, __FILE__, 2, 12, 34);
                    fputc('\n', stdout);
                }
            }
            return nyx_scope_result_42;
        }
    }
#line 12 "test.nip"
    {
        int32_t nyx_scope_result_46 = 0;
        {
#line 2 "test.nip"
            {
                nyx_string nyx_print_arg_34_0 = (nyx_string){ nyx_dyn_str_1, 0, 0, 5, 0 };
                nyx_print_4(nyx_print_arg_34_0, 0, __FILE__, 2, 12, 34);
                fputc('\n', stdout);
            }
        }
        return nyx_scope_result_46;
    }
}

int main(void)
{
#line 14 "test.nip"
    {
        int32_t nyx_print_arg_260_0 = nyx_u_f(3);
        nyx_print_0(nyx_print_arg_260_0, 0, __FILE__, 14, 1, 260);
        fputc('\n', stdout);
    }
    return 0;
}
