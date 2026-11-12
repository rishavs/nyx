#include "generator_internal.h"


bool emit_arithmetic_runtime(TextBuffer *source, ErrorState *errors)
{
    if (!buffer_append(
            source,
            "static void nyx_arith_fail(const char *message)\n"
            "{\n"
            "    fprintf(stderr, \"%s\\n\", message);\n"
            "    exit(1);\n"
            "}\n\n"
            "static NIPPY_UNUSED int32_t nyx_div_i32(int32_t a, int32_t b)\n"
            "{\n"
            "    if (b == 0) nyx_arith_fail(\"division by zero\");\n"
            "    if (a == INT32_MIN && b == -1) "
            "nyx_arith_fail(\"integer overflow\");\n"
            "    return a / b;\n"
            "}\n\n"
            "static NIPPY_UNUSED int32_t nyx_mod_i32(int32_t a, int32_t b)\n"
            "{\n"
            "    if (b == 0) nyx_arith_fail(\"division by zero\");\n"
            "    if (a == INT32_MIN && b == -1) return 0;\n"
            "    return a % b;\n"
            "}\n\n"
            "static NIPPY_UNUSED int32_t nyx_shl_i32(int32_t a, int32_t b)\n"
            "{\n"
            "    return (int32_t)((uint32_t)a << ((uint32_t)b & 31u));\n"
            "}\n\n"
            "static NIPPY_UNUSED int32_t nyx_shr_i32(int32_t a, int32_t b)\n"
            "{\n"
            "    return (int32_t)((uint32_t)a >> ((uint32_t)b & 31u));\n"
            "}\n\n")) {
        error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
        return false;
    }
    return true;
}
