/* Compile-time safety checks: constant folding, const, bounds, and unreachable
   code. */

#include "compile_support.h"
#include "../../compiler/checker/checker_internal.h"

TEST("unknown layouts fail closed")
{
    TypedProgram program;
    ErrorState errors;
    CheckContext ctx = {0};
    TypedType unknown = {0};
    List instances, cache;
    size_t index;
    bool ok;

    typed_program_init(&program);
    error_init(&errors);
    list_init(&instances, sizeof(StructInstantiation));
    list_init(&cache, sizeof(ValueLayoutCache));
    unknown.kind = NYX_TYPE_UNKNOWN;
    if (!typed_program_add_type(&program, &unknown, &index)) return TEST_ERROR;
    ctx.program = &program;
    ctx.errors = &errors;
    ctx.struct_instantiations = &instances;
    ctx.layout_cache = &cache;
    ok = !check_type_value_size(&ctx, index, (SourcePos){0, 1, 1}) &&
         error_contains(&errors, MESSAGE_CHECKER_LAYOUT_UNAVAILABLE);
    list_free(&cache);
    list_free(&instances);
    typed_program_free(&program);
    return ok ? TEST_PASS : TEST_FAIL;
}

TEST("type size includes array metadata and alignment")
{
    char source[2048];
    size_t used = (size_t)snprintf(source, sizeof(source), "type Large\n");
    int i;
    for (i = 0; i < 16; i++)
        used += (size_t)snprintf(source + used, sizeof(source) - used,
                                 " field%d: Array<Byte,4096>\n", i);
    snprintf(source + used, sizeof(source) - used, "end\n");
    return compile_fails(source, "array metadata in layout",
                         MESSAGE_CHECKER_VALUE_TOO_LARGE, (size_t)65600,
                         (size_t)NYX_MAX_VALUE_BYTES);
}

TEST("an unused oversized type alias is rejected")
{
    CompilerConfig config = test_config;
    CompilerResult result;
    char expected[256];
    bool ok;
    config.max_list_capacity = 65536;
    result = compiler_compile("type Big = Array<Byte,65536>\n", &config);
    expect_message(expected, sizeof(expected), MESSAGE_CHECKER_VALUE_TOO_LARGE,
                   (size_t)65540, (size_t)NYX_MAX_VALUE_BYTES);
    ok = !result.succeeded && result.error_message != NULL &&
         strstr(result.error_message, expected) != NULL;
    compiler_result_free(&result);
    return ok ? TEST_PASS : TEST_FAIL;
}

TEST("type size is checked through deeply nested structs")
{
    char source[4096];
    size_t used = (size_t)snprintf(source, sizeof(source), "type Base\n");
    int i;
    for (i = 0; i < 16; i++)
        used += (size_t)snprintf(source + used, sizeof(source) - used,
                                 " field%d: String<4091>\n", i);
    used += (size_t)snprintf(source + used, sizeof(source) - used,
                             "end\ntype T0 value: Base end\n");
    for (i = 1; i <= 35; i++)
        used += (size_t)snprintf(source + used, sizeof(source) - used,
                                 "type T%d value: T%d end\n", i, i - 1);
    snprintf(source + used, sizeof(source) - used,
             "type TooBig value: T35 extra: Byte end\n");
    return compile_fails(source, "deep type layout", MESSAGE_CHECKER_VALUE_TOO_LARGE,
                         (size_t)65540, (size_t)NYX_MAX_VALUE_BYTES);
}

TEST("literal division by zero")
{
    return compile_fails(
        "var x: Int32 = 1 / 0\n",
        "literal division by zero",
        MESSAGE_CHECKER_ZERO_DIVISOR, "/");
}

TEST("literal modulo by zero")
{
    return compile_fails(
        "var x: Int32 = 1 % 0\n",
        "literal modulo by zero",
        MESSAGE_CHECKER_ZERO_DIVISOR, "%");
}

TEST("bounds: literal array index")
{
    return compile_fails(
        "var xs: Array<Int32, 2> = [1, 2]\nprint(xs[2])\n",
        "array index past capacity", MESSAGE_CHECKER_INDEX_BOUNDS, 2,
        (size_t)2);
}

TEST("bounds: literal negative index")
{
    return compile_fails(
        "var xs: Array<Int32, 2> = [1, 2]\nprint(xs[-1])\n",
        "negative array index", MESSAGE_CHECKER_INDEX_BOUNDS, -1,
        (size_t)2);
}

TEST("bounds: literal string index")
{
    return compile_fails(
        "var s: String<4> = \"abc\"\nprint(s.byte_at(4))\n",
        "string byte index past capacity", MESSAGE_CHECKER_INDEX_BOUNDS, 4,
        (size_t)4);
}

TEST("bounds: literal array assignment")
{
    return compile_fails(
        "var xs: Array<Int32, 2> = [1, 2]\nxs[2] = 3\n",
        "array assignment past capacity", MESSAGE_CHECKER_INDEX_BOUNDS, 2,
        (size_t)2);
}

TEST("bounds: negative list capacity")
{
    return compile_fails(
        "global arena: Store = Store(64)\n"
        "var xs: List<Int32> = arena.list<Int32>(-1)\n",
        "negative list capacity", MESSAGE_CHECKER_NEGATIVE_CAPACITY);
}

TEST("bounds: negative builder capacity")
{
    return compile_fails(
        "global arena: Store = Store(64)\n"
        "var b: TextBuilder = arena.builder(-1)\n",
        "negative builder capacity", MESSAGE_CHECKER_NEGATIVE_CAPACITY);
}

TEST("bounds: negative table capacity")
{
    return compile_fails(
        "global arena: Store = Store(64)\n"
        "var t: Table<String<8>, Int32> = "
        "arena.table<String<8>, Int32>(-1)\n",
        "negative table capacity", MESSAGE_CHECKER_NEGATIVE_CAPACITY);
}

TEST("bounds: negative store capacity")
{
    return compile_fails("global arena: Store = Store(-1)\n",
                         "negative store capacity",
                         MESSAGE_CHECKER_NEGATIVE_CAPACITY);
}

TEST("bounds: mutation in for")
{
    return compile_fails(
        "global arena: Store = Store(64)\n"
        "var xs: List<Int32> = arena.list<Int32>(2)\n"
        "for x in xs do\n"
        "  xs.push(1)\n"
        "end\n",
        "mutation inside for", MESSAGE_CHECKER_FOR_MUTATION);
}

TEST("bounds: nested mutation in for")
{
    return compile_fails(
        "global arena: Store = Store(64)\n"
        "var xs: List<Int32> = arena.list<Int32>(2)\n"
        "for x in xs do\n"
        "  if true then\n"
        "    xs.pop()\n"
        "  end\n"
        "end\n",
        "nested mutation inside for", MESSAGE_CHECKER_FOR_MUTATION);
}

TEST("constant overflow")
{
    return compile_fails("var x: Int32 = 2147483647 + 1\n",
                         "constant expression overflow",
                         MESSAGE_CHECKER_CONST_OVERFLOW);
}

TEST("constant shift")
{
    return compile_fails("var x: Int32 = 1 << 40\n", "constant shift range",
                         MESSAGE_CHECKER_SHIFT_RANGE);
}

TEST("constant zero divisor")
{
    TestResult result = compile_fails(
        "const z: Int32 = 0\nprint(1 / z)\n", "const zero divisor",
        MESSAGE_CHECKER_ZERO_DIVISOR, "/");

    if (result != TEST_PASS) {
        return result;
    }
    return compile_fails("const z: Int32 = 0\nprint(1 % z)\n",
                         "const zero modulus", MESSAGE_CHECKER_ZERO_DIVISOR,
                         "%");
}

TEST("const negative capacity")
{
    return compile_fails(
        "global arena: Store = Store(64)\n"
        "const n: Int32 = -1\n"
        "var xs: List<Int32> = arena.list<Int32>(n)\n",
        "const negative capacity", MESSAGE_CHECKER_NEGATIVE_CAPACITY);
}

TEST("const struct mutation")
{
    return compile_fails(
        "type P\n  x: Int32\nend\n"
        "const p: P = P(1)\n"
        "p.x = 3\n",
        "const struct mutation", MESSAGE_CHECKER_CONST_ASSIGN, "p");
}

TEST("const array mutation")
{
    TestResult result = compile_fails(
        "const xs: Array<Int32, 2> = [1, 2]\nxs.push(3)\n",
        "const array push", MESSAGE_CHECKER_CONST_ASSIGN, "xs");

    if (result != TEST_PASS) {
        return result;
    }
    return compile_fails("const xs: Array<Int32, 2> = [1, 2]\nxs[0] = 3\n",
                         "const array assignment",
                         MESSAGE_CHECKER_CONST_ASSIGN, "xs");
}

TEST("const record mutation")
{
    return compile_fails(
        "const r: Record<String<8>, Int32, 2> = {\"a\": 1}\n"
        "r[\"a\"] = 2\n",
        "const record mutation", MESSAGE_CHECKER_CONST_ASSIGN, "r");
}

TEST("discarded Error")
{
    return compile_fails(
        "fun f(): Int32 | Error\n  return 1\nend\n"
        "f()\n",
        "discarded Error", MESSAGE_CHECKER_DISCARDED_ERROR);
}

TEST("bound Error")
{
    return compile_accepts(
        "fun f(): Int32 | Error\n  return 1\nend\n"
        "var r: Int32 | Error = f()\n",
        "bound Error result");
}

TEST("unreachable return")
{
    return compile_fails(
        "fun f(): Int32\n  return 1\n  return 2\nend\n",
        "unreachable return", MESSAGE_CHECKER_UNREACHABLE_CODE);
}

TEST("unreachable break")
{
    return compile_fails(
        "fun f()\n"
        "  while true do\n"
        "    break\n"
        "    print(1)\n"
        "  end\n"
        "  return\n"
        "end\n",
        "unreachable break", MESSAGE_CHECKER_UNREACHABLE_CODE);
}

TEST("while true returns")
{
    return compile_accepts(
        "fun f(): Int32\n"
        "  while true do\n"
        "    return 1\n"
        "  end\n"
        "end\n"
        "print(f())\n",
        "infinite while returns");
}

TEST("always recursive")
{
    return compile_fails(
        "fun f(x: Int32): Int32\n  return f(x + 1)\nend\n"
        "var n: Int32 = f(0)\n",
        "always recursive function", MESSAGE_CHECKER_ALWAYS_RECURSIVE, "f");
}

TEST("by-value size cap")
{
    return compile_fails(
        "type Big\n"
        "  a0: String<4096>\n"
        "  a1: String<4096>\n"
        "  a2: String<4096>\n"
        "  a3: String<4096>\n"
        "  a4: String<4096>\n"
        "  a5: String<4096>\n"
        "  a6: String<4096>\n"
        "  a7: String<4096>\n"
        "  a8: String<4096>\n"
        "  a9: String<4096>\n"
        "  a10: String<4096>\n"
        "  a11: String<4096>\n"
        "  a12: String<4096>\n"
        "  a13: String<4096>\n"
        "  a14: String<4096>\n"
        "  a15: String<4096>\n"
        "  a16: String<4096>\n"
        "  a17: String<4096>\n"
        "  a18: String<4096>\n"
        "  a19: String<4096>\n"
        "end\n",
        "by-value size cap", MESSAGE_CHECKER_VALUE_TOO_LARGE,
        (size_t)82080, (size_t)NYX_MAX_VALUE_BYTES);
}

TEST("local inference contexts")
{
    TestResult result = compile_fails(
        "fun f()\n  var x = []\n  return\nend\n",
        "local list context", MESSAGE_CHECKER_LIST_INFER);

    if (result != TEST_PASS) {
        return result;
    }
    result = compile_fails(
        "fun f()\n  var r = {\"a\": 1}\n  return\nend\n",
        "local Record context", MESSAGE_CHECKER_RECORD_LITERAL_CONTEXT);
    if (result != TEST_PASS) {
        return result;
    }
    return compile_fails(
        "fun f()\n  var s = \"x\"\n  return\nend\n",
        "local string context", MESSAGE_CHECKER_STRING_LITERAL_CONTEXT);
}

TEST("parenthesized type rparen")
{
    return compile_fails("var x: (Int32 = 1\n", "missing type rparen",
                         MESSAGE_PARSER_EXPECTED_RPAREN);
}

/* "a + a + ... + a" with `terms` terms, after `prefix`. */
static char *sum_chain(const char *prefix, const char *term, size_t terms,
                       const char *suffix)
{
    size_t length = strlen(prefix) + strlen(suffix) + terms * (strlen(term) + 3) + 1;
    char *text = nyx_malloc(length);
    size_t i;

    if (text == NULL) return NULL;
    strcpy(text, prefix);
    for (i = 0; i < terms; i++) {
        if (i > 0) strcat(text, " + ");
        strcat(text, term);
    }
    strcat(text, suffix);
    return text;
}

TEST("a long chain of constants folds, and an overflow in it is found")
{
    char *fits = sum_chain("const k: Int32 = 1\nvar x: Int32 = ", "k", 600, "\n");
    char *overflows = sum_chain("var x: Int32 = 2147483647 + ", "1", 600, "\n");
    TestResult result;

    if (fits == NULL || overflows == NULL) {
        free(fits);
        free(overflows);
        return TEST_ERROR;
    }
    result = compile_accepts(fits, "a 600-term constant sum");
    if (result == TEST_PASS) {
        result = compile_fails(overflows, "an overflow at the start of a long sum",
                               MESSAGE_CHECKER_CONST_OVERFLOW);
    }
    free(fits);
    free(overflows);
    return result;
}

TEST("folding one expression does not remember what it found for the next")
{
    /* Both statements fold the same shape of tree under different constants. */
    return compile_fails(
        "const small: Int32 = 1\n"
        "const big: Int32 = 2147483647\n"
        "var a: Int32 = small + small + small + small\n"
        "var b: Int32 = big + big + big + big\n",
        "the second sum overflows", MESSAGE_CHECKER_CONST_OVERFLOW);
}

TEST("a divisor that is a folded constant is still checked")
{
    return compile_fails(
        "const two: Int32 = 2\n"
        "var a: Int32 = 10\n"
        "var b: Int32 = a / (two - two + (two - two))\n",
        "a divisor that folds to zero", MESSAGE_CHECKER_ZERO_DIVISOR, "/");
}

TEST("a constant is folded in the type of its expression")
{
    /* 4000000000 does not fit Int32 but is an ordinary Int64. */
    TestResult wide = compile_accepts(
        "var a: Int64 = 1\n"
        "var y: Int64 = a + (2000000000 + 2000000000)\n"
        "var z: Int64 = a * (3000000000 - 1) + (4000000000 * 2)\n",
        "an Int64 sum that overflows Int32");
    TestResult narrow;

    if (wide != TEST_PASS) return wide;
    narrow = compile_fails(
        "var a: Int32 = 1\n"
        "var y: Int32 = a + (2000000000 + 2000000000)\n",
        "the same sum in Int32", MESSAGE_CHECKER_CONST_OVERFLOW);
    if (narrow != TEST_PASS) return narrow;
    return compile_fails(
        "var a: Int64 = 1\n"
        "var y: Int64 = a * (9223372036854775807 + 1)\n"
        "var z: Int64 = a / (9223372036854775807 - 9223372036854775807)\n",
        "a divisor that folds to zero in Int64",
        MESSAGE_CHECKER_ZERO_DIVISOR, "/");
}

TEST("a constant shift count is checked against the width it shifts")
{
    TestResult fits = compile_accepts(
        "var a: Int64 = 1\nvar y: Int64 = a << (30 + 30)\n",
        "a shift count of 60 in Int64");

    if (fits != TEST_PASS) return fits;
    return compile_fails(
        "var a: Int64 = 1\nvar y: Int64 = a << (40 + 30)\n",
        "a shift count of 70 in Int64", MESSAGE_CHECKER_SHIFT_RANGE);
}
