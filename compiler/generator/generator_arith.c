#include "generator_internal.h"


typedef struct {
    const char *suffix;
    const char *type;
    const char *utype;
    const char *minimum;
    const char *maximum;
    unsigned width;
    bool is_signed;
} ArithmeticType;


static const ArithmeticType arithmetic_types[] = {
    {"i8", "int8_t", "uint8_t", "INT8_MIN", "INT8_MAX", 8, true},
    {"i16", "int16_t", "uint16_t", "INT16_MIN", "INT16_MAX", 16, true},
    {"i32", "int32_t", "uint32_t", "INT32_MIN", "INT32_MAX", 32, true},
    {"i64", "int64_t", "uint64_t", "INT64_MIN", "INT64_MAX", 64, true},
    {"u8", "uint8_t", "uint8_t", "0", "UINT8_MAX", 8, false},
    {"u16", "uint16_t", "uint16_t", "0", "UINT16_MAX", 16, false},
    {"u32", "uint32_t", "uint32_t", "0", "UINT32_MAX", 32, false},
    {"u64", "uint64_t", "uint64_t", "0", "UINT64_MAX", 64, false}
};


bool emit_arithmetic_runtime(TextBuffer *source, ErrorState *errors)
{
    size_t i;

    if (!buffer_append(
            source,
            "static void nyx_arith_fail_at(const char *message, const char *file,\n"
            "                              size_t line, size_t column, size_t offset)\n"
            "{\n"
            "    fflush(stdout);\n"
            "    fprintf(stderr, \"" MESSAGE_RUNTIME_POSITION "\\n\",\n"
            "            file, line, column, offset, message + sizeof(\"" MESSAGE_RUNTIME_PREFIX "\") - 1);\n"
            "    exit(1);\n"
            "}\n\n")) {
        error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
        return false;
    }
    for (i = 0; i < sizeof(arithmetic_types) / sizeof(arithmetic_types[0]);
         i++) {
        const ArithmeticType *type = &arithmetic_types[i];

        if (!buffer_appendf(
                source,
                "static NIPPY_UNUSED %s nyx_div_%s(%s a, %s b, const char *file, size_t line, size_t column, size_t offset)\n"
                "{\n"
                "    if (b == 0) nyx_arith_fail_at(\"" MESSAGE_RUNTIME_DIVISION_ZERO "\", file, line, column, offset);\n",
                type->type, type->suffix, type->type, type->type)) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            return false;
        }
        if (type->is_signed &&
            !buffer_appendf(source,
                            "    if (a == %s && b == -1) "
                            "nyx_arith_fail_at(\"" MESSAGE_RUNTIME_INTEGER_OVERFLOW "\", file, line, column, offset);\n",
                            type->minimum)) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            return false;
        }
        if (!buffer_appendf(source,
                            "    return a / b;\n"
                            "}\n\n"
                            "static NIPPY_UNUSED %s nyx_mod_%s(%s a, %s b, const char *file, size_t line, size_t column, size_t offset)\n"
                            "{\n"
                            "    if (b == 0) nyx_arith_fail_at("
                            "\"" MESSAGE_RUNTIME_DIVISION_ZERO "\", file, line, column, offset);\n",
                            type->type, type->suffix, type->type, type->type)) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            return false;
        }
        if (type->is_signed &&
            !buffer_appendf(source,
                            "    if (a == %s && b == -1) return 0;\n",
                            type->minimum)) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            return false;
        }
        if (!buffer_appendf(
                source,
                "    return a %% b;\n"
                "}\n\n"
                "static NIPPY_UNUSED %s nyx_shl_%s(%s a, int32_t b)\n"
                "{\n"
                "    return (%s)((%s)a << "
                "((uint32_t)b & %uu));\n"
                "}\n\n"
                "static NIPPY_UNUSED %s nyx_shr_%s(%s a, int32_t b)\n"
                "{\n"
                "    return (%s)((%s)a >> "
                "((uint32_t)b & %uu));\n"
                "}\n\n",
                type->type, type->suffix, type->type, type->type, type->utype,
                type->width - 1, type->type, type->suffix, type->type,
                type->type, type->utype, type->width - 1)) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            return false;
        }
    }
    return true;
}

/* One checked integer conversion helper. `from_signed` selects the source
   domain: a signed source is widened to int64_t, an unsigned one to uint64_t,
   so every pair of integer types is covered by two helpers per target. Only the
   comparisons that can fail are emitted, which keeps the generated C free of
   always-true tests. */
static bool emit_integer_conversion(TextBuffer *source,
                                    const ArithmeticType *type,
                                    bool from_signed)
{
    const char *domain = from_signed ? "int64_t" : "uint64_t";
    const char *prefix = from_signed ? "nyx_cs_" : "nyx_cu_";
    bool narrow = type->width < 64;

    if (!buffer_appendf(source,
                        "static NIPPY_UNUSED %s %s%s(%s value, const char *file, size_t line, size_t column, size_t offset)\n{\n"
                        "    (void)file; (void)line; (void)column; (void)offset;\n",
                        type->type, prefix, type->suffix, domain)) {
        return false;
    }
    if (from_signed) {
        if (type->is_signed && narrow &&
            !buffer_appendf(source,
                            "    if (value < (int64_t)%s) "
                            "nyx_arith_fail_at(\"" MESSAGE_RUNTIME_CONVERSION
                            "\", file, line, column, offset);\n",
                            type->minimum)) {
            return false;
        }
        if (!type->is_signed &&
            !buffer_append(source,
                           "    if (value < 0) "
                           "nyx_arith_fail_at(\"" MESSAGE_RUNTIME_CONVERSION
                           "\", file, line, column, offset);\n")) {
            return false;
        }
        if (narrow &&
            !buffer_appendf(source,
                            "    if (value > (int64_t)%s) "
                            "nyx_arith_fail_at(\"" MESSAGE_RUNTIME_CONVERSION
                            "\", file, line, column, offset);\n",
                            type->maximum)) {
            return false;
        }
    } else if ((type->is_signed || narrow) &&
               !buffer_appendf(source,
                               "    if (value > (uint64_t)%s) "
                               "nyx_arith_fail_at(\"" MESSAGE_RUNTIME_CONVERSION
                               "\", file, line, column, offset);\n",
                               type->maximum)) {
        return false;
    }
    return buffer_appendf(source, "    return (%s)value;\n}\n\n", type->type);
}


bool emit_conversion_runtime(TextBuffer *source, ErrorState *errors)
{
    /* The float-to-integer window is open at both ends in the truncated
       value: -128.9 truncates to -128 and is valid, -129.0 is not. A 64-bit
       lower bound is closed because -2^63 - 1 is not a double. */
    static const struct {
        const char *suffix;
        const char *type;
        const char *lower;
        const char *upper;
    } conversions[] = {
        {"i8", "int8_t", "value > -129.0", "128.0"},
        {"i16", "int16_t", "value > -32769.0", "32768.0"},
        {"i32", "int32_t", "value > -2147483649.0", "2147483648.0"},
        {"i64", "int64_t", "value >= -9223372036854775808.0",
         "9223372036854775808.0"},
        {"u8", "uint8_t", "value > -1.0", "256.0"},
        {"u16", "uint16_t", "value > -1.0", "65536.0"},
        {"u32", "uint32_t", "value > -1.0", "4294967296.0"},
        {"u64", "uint64_t", "value > -1.0", "18446744073709551616.0"}
    };
    size_t i;

    for (i = 0; i < sizeof(arithmetic_types) / sizeof(arithmetic_types[0]);
         i++) {
        if (!emit_integer_conversion(source, &arithmetic_types[i], true) ||
            !emit_integer_conversion(source, &arithmetic_types[i], false)) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            return false;
        }
    }
    for (i = 0; i < sizeof(conversions) / sizeof(conversions[0]); i++) {
        if (!buffer_appendf(
                source,
                "static NIPPY_UNUSED %s nyx_f2i_%s(double value, const char *file, size_t line, size_t column, size_t offset)\n"
                "{\n"
                "    if (!(%s && value < %s)) {\n"
                "        nyx_arith_fail_at(\"" MESSAGE_RUNTIME_CONVERSION "\", file, line, column, offset);\n"
                "    }\n"
                "    return (%s)value;\n"
                "}\n\n",
                conversions[i].type, conversions[i].suffix,
                conversions[i].lower, conversions[i].upper,
                conversions[i].type)) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            return false;
        }
    }
    /* Narrowing a finite double beyond FLT_MAX is undefined in C. Infinities
       and NaN pass through; `value - value == 0.0` is true only for finite. */
    if (!buffer_append(
            source,
            "static NIPPY_UNUSED float nyx_f2f_f32(double value, const char *file, size_t line, size_t column, size_t offset)\n"
            "{\n"
            "    if ((value > FLT_MAX || value < -FLT_MAX) &&\n"
            "        value - value == 0.0) {\n"
            "        nyx_arith_fail_at(\"" MESSAGE_RUNTIME_CONVERSION "\", file, line, column, offset);\n"
            "    }\n"
            "    return (float)value;\n"
            "}\n\n")) {
        error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
        return false;
    }
    return true;
}
