#include "golden_data.h"

const char *const golden_string_store_c_parts[] = {
        "#include \"test.h\"\n",
        "\n",
        "#include <stdio.h>\n",
        "#include <stdlib.h>\n",
        "#include <string.h>\n",
        "\n",
        "#define NIPPY_STORE_COUNT 1\n",
        "\n",
        "static nyx_store nyx_stores[NIPPY_STORE_COUNT];\n",
        "\n",
        "static void nyx_panic(const char *message)\n",
        "{\n",
        "    fprintf(stderr, \"%s\\n\", message);\n",
        "    exit(1);\n",
        "}\n",
        "\n",
        "static size_t nyx_store_align(size_t offset, size_t alignment)\n",
        "{\n",
        "    size_t remainder;\n",
        "\n",
        "    if (alignment <= 1) {\n",
        "        return offset;\n",
        "    }\n",
        "    remainder = offset % alignment;\n",
        "    if (remainder == 0) {\n",
        "        return offset;\n",
        "    }\n",
        "    if (offset > (size_t)-1 - (alignment - remainder)) {\n",
        "        nyx_panic(\"nippy: out of memory\");\n",
        "    }\n",
        "    return offset + (alignment - remainder);\n",
        "}\n",
        "\n",
        "static void nyx_store_init(nyx_store *store, size_t initial_bytes)\n",
        "{\n",
        "    if (initial_bytes == 0) {\n",
        "        initial_bytes = 1;\n",
        "    }\n",
        "    store->data = malloc(initial_bytes);\n",
        "    if (store->data == NULL) {\n",
        "        nyx_panic(\"nippy: out of memory\");\n",
        "    }\n",
        "    store->used = 0;\n",
        "    store->capacity = initial_bytes;\n",
        "    store->generation = 1;\n",
        "}\n",
        "\n",
        "static void nyx_store_reserve(nyx_store *store, size_t required)\n",
        "{\n",
        "    size_t capacity;\n",
        "    unsigned char *grown;\n",
        "\n",
        "    if (required <= store->capacity) {\n",
        "        return;\n",
        "    }\n",
        "    capacity = store->capacity == 0 ? 1 : store->capacity;\n",
        "    while (capacity < required) {\n",
        "        if (capacity > (size_t)-1 / 2) {\n",
        "            nyx_panic(\"nippy: out of memory\");\n",
        "        }\n",
        "        capacity *= 2;\n",
        "    }\n",
        "    grown = realloc(store->data, capacity);\n",
        "    if (grown == NULL) {\n",
        "        nyx_panic(\"nippy: out of memory\");\n",
        "    }\n",
        "    store->data = grown;\n",
        "    store->capacity = capacity;\n",
        "}\n",
        "\n",
        "static nyx_handle nyx_store_alloc(nyx_store *store, size_t id,\n",
        "                                      const void *value, size_t size,\n",
        "                                      size_t align)\n",
        "{\n",
        "    nyx_handle handle;\n",
        "    size_t offset;\n",
        "    size_t end;\n",
        "\n",
        "    if (id >= (size_t)NIPPY_STORE_COUNT) {\n",
        "        nyx_panic(\"nippy: invalid store id\");\n",
        "    }\n",
        "    if (size == 0) {\n",
        "        nyx_panic(\"nippy: handle is out of range\");\n",
        "    }\n",
        "    offset = nyx_store_align(store->used, align);\n",
        "    if (offset > (size_t)-1 - size) {\n",
        "        nyx_panic(\"nippy: handle is out of range\");\n",
        "    }\n",
        "    end = offset + size;\n",
        "    nyx_store_reserve(store, end);\n",
        "    memcpy(store->data + offset, value, size);\n",
        "    store->used = end;\n",
        "    handle.store = (uint32_t)id;\n",
        "    handle.offset = offset;\n",
        "    handle.generation = store->generation;\n",
        "    return handle;\n",
        "}\n",
        "\n",
        "static unsigned char *nyx_handle_pointer(nyx_handle handle,\n",
        "                                           size_t size)\n",
        "{\n",
        "    nyx_store *store;\n",
        "\n",
        "    if ((size_t)handle.store >= (size_t)NIPPY_STORE_COUNT) {\n",
        "        nyx_panic(\"nippy: invalid store id\");\n",
        "    }\n",
        "    store = &nyx_stores[handle.store];\n",
        "    if (handle.generation != store->generation) {\n",
        "        nyx_panic(\"nippy: stale handle\");\n",
        "    }\n",
        "    if (handle.offset > store->used ||\n",
        "        size > store->used - handle.offset) {\n",
        "        nyx_panic(\"nippy: handle is out of range\");\n",
        "    }\n",
        "    return store->data + handle.offset;\n",
        "}\n",
        "\n",
        "static void nyx_handle_write(nyx_handle handle, const void *value,\n",
        "                               size_t size)\n",
        "{\n",
        "    memcpy(nyx_handle_pointer(handle, size), value, size);\n",
        "}\n",
        "\n",
        "static void nyx_store_reset(nyx_store *store)\n",
        "{\n",
        "    store->used = 0;\n",
        "    if (store->generation == UINT64_MAX) {\n",
        "        nyx_panic(\"nippy: stale handle\");\n",
        "    }\n",
        "    store->generation++;\n",
        "}\n",
        "\n",
        "static void nyx_store_free(nyx_store *store)\n",
        "{\n",
        "    free(store->data);\n",
        "    store->data = NULL;\n",
        "    store->used = 0;\n",
        "    store->capacity = 0;\n",
        "    store->generation = 0;\n",
        "    (void)nyx_store_reserve;\n",
        "    (void)nyx_store_alloc;\n",
        "    (void)nyx_handle_pointer;\n",
        "    (void)nyx_handle_write;\n",
        "    (void)nyx_store_reset;\n",
        "}\n",
        "\n",
        "static void nyx_string_bounds(void)\n",
        "{\n",
        "    fprintf(stderr, \"nippy: string index out of bounds\\n\");\n",
        "    exit(1);\n",
        "}\n",
        "\n",
        "static uint8_t nyx_string_8_at(nyx_string_8 s, int32_t i)\n",
        "{\n",
        "    if (i < 0 || (uint32_t)i >= s.length) {\n",
        "        nyx_string_bounds();\n",
        "    }\n",
        "    return s.data[i];\n",
        "}\n",
        "\n",
        "static bool nyx_string_8_equal(nyx_string_8 a, nyx_string_8 b)\n",
        "{\n",
        "    return a.length == b.length &&\n",
        "           memcmp(a.data, b.data, a.length) == 0;\n",
        "}\n",
        "\n",
        "int main(void)\n",
        "{\n",
        "    nyx_store_init(&nyx_stores[0], 8);\n",
        "#line 2 \"test.nip\"\n",
        "    nyx_handle h = nyx_store_alloc(&nyx_stores[0], 0, ",
        "&(union { nyx_string_8 nyx_value; }){ .nyx_value = ",
        "(nyx_string_8){ { 104, 105, 0 }, 2 } }, sizeof(nyx_string_8), ",
        "_Alignof(nyx_string_8));\n",
        "    (void)h;\n",
        "#line 3 \"test.nip\"\n",
        "    nyx_string_8 s = (*(nyx_string_8 *)nyx_handle_pointer(h, ",
        "sizeof(nyx_string_8)));\n",
        "    (void)s;\n",
        "    nyx_store_free(&nyx_stores[0]);\n",
        "    return 0;\n",
        "}\n",
};

const GoldenText golden_string_store_c = { golden_string_store_c_parts, 182 };

const char golden_string_store_h[] =
        "#ifndef NIPPY_GENERATED_H\n"
        "#define NIPPY_GENERATED_H\n"
        "\n"
        "#include <stdbool.h>\n"
        "#include <stdint.h>\n"
        "#include <stddef.h>\n"
        "\n"
        "typedef struct {\n"
        "    uint32_t store;\n"
        "    size_t offset;\n"
        "    uint64_t generation;\n"
        "} nyx_handle;\n"
        "\n"
        "typedef struct {\n"
        "    unsigned char *data;\n"
        "    size_t used;\n"
        "    size_t capacity;\n"
        "    uint64_t generation;\n"
        "} nyx_store;\n"
        "\n"
        "typedef struct {\n"
        "    uint8_t data[9];\n"
        "    uint32_t length;\n"
        "} nyx_string_8;\n"
        "\n"
        "static uint8_t nyx_string_8_at(nyx_string_8 s, int32_t i);\n"
        "\n"
        "#endif\n";

const char golden_assignment_top_c[] =
        "#include \"test.h\"\n"
        "\n"
        "#include <stdio.h>\n"
        "\n"
        "int32_t nyx_program_value(void)\n"
        "{\n"
        "#line 1 \"test.nip\"\n"
        "    int32_t x = 1;\n"
        "#line 2 \"test.nip\"\n"
        "    x = 2;\n"
        "    return x;\n"
        "}\n"
        "\n"
        "int main(void)\n"
        "{\n"
        "    printf(\"%d\\n\", (int)nyx_program_value());\n"
        "    return 0;\n"
        "}\n";

const char golden_assignment_top_h[] =
        "#ifndef NIPPY_GENERATED_H\n"
        "#define NIPPY_GENERATED_H\n"
        "\n"
        "#include <stdbool.h>\n"
        "#include <stdint.h>\n"
        "\n"
        "int32_t nyx_program_value(void);\n"
        "\n"
        "#endif\n";

const char golden_assignment_param_c[] =
        "#include \"test.h\"\n"
        "\n"
        "#include <stdio.h>\n"
        "\n"
        "#line 1 \"test.nip\"\n"
        "int32_t bump(int32_t x)\n"
        "{\n"
        "    (void)x;\n"
        "#line 2 \"test.nip\"\n"
        "    x = (int32_t)((uint32_t)(x) + (uint32_t)(1));\n"
        "#line 3 \"test.nip\"\n"
        "    return x;\n"
        "}\n"
        "\n"
        "int32_t nyx_program_value(void)\n"
        "{\n"
        "#line 6 \"test.nip\"\n"
        "    int32_t n = bump(1);\n"
        "    return n;\n"
        "}\n"
        "\n"
        "int main(void)\n"
        "{\n"
        "    printf(\"%d\\n\", (int)nyx_program_value());\n"
        "    return 0;\n"
        "}\n";

const char golden_assignment_param_h[] =
        "#ifndef NIPPY_GENERATED_H\n"
        "#define NIPPY_GENERATED_H\n"
        "\n"
        "#include <stdbool.h>\n"
        "#include <stdint.h>\n"
        "\n"
        "typedef int32_t (*nyx_fun_0)(int32_t);\n"
        "\n"
        "int32_t nyx_program_value(void);\n"
        "\n"
        "#endif\n";

const char golden_assignment_main_c[] =
        "#include \"test.h\"\n"
        "\n"
        "int main(void)\n"
        "{\n"
        "#line 1 \"test.nip\"\n"
        "    int32_t a = 1;\n"
        "    (void)a;\n"
        "#line 2 \"test.nip\"\n"
        "    int32_t b = 2;\n"
        "    (void)b;\n"
        "#line 3 \"test.nip\"\n"
        "    a = b;\n"
        "    return 0;\n"
        "}\n";

const char golden_assignment_main_h[] =
        "#ifndef NIPPY_GENERATED_H\n"
        "#define NIPPY_GENERATED_H\n"
        "\n"
        "#include <stdbool.h>\n"
        "#include <stdint.h>\n"
        "\n"
        "#endif\n";

