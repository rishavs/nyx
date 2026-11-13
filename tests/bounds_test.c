#include "test_support.h"


static TestResult compiles(const char *source, const char *what)
{
    CompilerResult result = compiler_compile(source, &test_config);
    TestResult status = TEST_FAIL;

    if (result.succeeded) {
        status = TEST_PASS;
    } else {
        fprintf(stderr, "  %s did not compile: %s\n", what,
                result.error_message == NULL ? "(no diagnostic)"
                                             : result.error_message);
    }
    compiler_result_free(&result);
    return status;
}


TestResult test_literal_array_index_out_of_bounds(void)
{
    return compile_fails(
        "var xs: Array<Int32, 2> = [1, 2]\nprint(xs[2])\n",
        "array index past capacity", MESSAGE_CHECKER_INDEX_BOUNDS, 2,
        (size_t)2);
}


TestResult test_literal_negative_index(void)
{
    return compile_fails(
        "var xs: Array<Int32, 2> = [1, 2]\nprint(xs[-1])\n",
        "negative array index", MESSAGE_CHECKER_INDEX_BOUNDS, -1,
        (size_t)2);
}


TestResult test_literal_string_index_out_of_bounds(void)
{
    return compile_fails(
        "var s: String<4> = \"abc\"\nprint(s.byte_at(4))\n",
        "string byte index past capacity", MESSAGE_CHECKER_INDEX_BOUNDS, 4,
        (size_t)4);
}


TestResult test_literal_array_assignment_out_of_bounds(void)
{
    return compile_fails(
        "var xs: Array<Int32, 2> = [1, 2]\nxs[2] = 3\n",
        "array assignment past capacity", MESSAGE_CHECKER_INDEX_BOUNDS, 2,
        (size_t)2);
}


TestResult test_negative_list_capacity(void)
{
    return compile_fails(
        "global arena: Store = Store(64)\n"
        "var xs: List<Int32> = arena.list<Int32>(-1)\n",
        "negative list capacity", MESSAGE_CHECKER_NEGATIVE_CAPACITY);
}


TestResult test_negative_builder_capacity(void)
{
    return compile_fails(
        "global arena: Store = Store(64)\n"
        "var b: TextBuilder = arena.builder(-1)\n",
        "negative builder capacity", MESSAGE_CHECKER_NEGATIVE_CAPACITY);
}


TestResult test_negative_table_capacity(void)
{
    return compile_fails(
        "global arena: Store = Store(64)\n"
        "var t: Table<String<8>, Int32> = "
        "arena.table<String<8>, Int32>(-1)\n",
        "negative table capacity", MESSAGE_CHECKER_NEGATIVE_CAPACITY);
}


TestResult test_negative_store_capacity(void)
{
    return compile_fails("global arena: Store = Store(-1)\n",
                         "negative store capacity",
                         MESSAGE_CHECKER_NEGATIVE_CAPACITY);
}


TestResult test_for_iterable_mutation(void)
{
    return compile_fails(
        "global arena: Store = Store(64)\n"
        "var xs: List<Int32> = arena.list<Int32>(2)\n"
        "for x in xs do\n"
        "  xs.push(1)\n"
        "end\n",
        "mutation inside for", MESSAGE_CHECKER_FOR_MUTATION);
}


TestResult test_for_iterable_mutation_nested(void)
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

TestResult test_constant_expression_overflow(void)
{
    return compile_fails("print(2147483647 + 1)\n",
                         "constant expression overflow",
                         MESSAGE_CHECKER_CONST_OVERFLOW);
}


TestResult test_constant_shift_range(void)
{
    return compile_fails("print(1 << 40)\n", "constant shift range",
                         MESSAGE_CHECKER_SHIFT_RANGE);
}


TestResult test_constant_zero_divisor(void)
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


TestResult test_const_negative_capacity(void)
{
    return compile_fails(
        "global arena: Store = Store(64)\n"
        "const n: Int32 = -1\n"
        "var xs: List<Int32> = arena.list<Int32>(n)\n",
        "const negative capacity", MESSAGE_CHECKER_NEGATIVE_CAPACITY);
}


TestResult test_const_struct_field_mutation(void)
{
    return compile_fails(
        "type P\n  x: Int32\nend\n"
        "const p: P = P(1)\n"
        "p.x = 3\n",
        "const struct mutation", MESSAGE_CHECKER_CONST_ASSIGN, "p");
}


TestResult test_const_array_mutation(void)
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


TestResult test_const_record_mutation(void)
{
    return compile_fails(
        "const r: Record<String<8>, Int32, 2> = {\"a\": 1}\n"
        "r[\"a\"] = 2\n",
        "const record mutation", MESSAGE_CHECKER_CONST_ASSIGN, "r");
}


TestResult test_discarded_error(void)
{
    return compile_fails(
        "fun f(): Int32 | Error\n  return 1\nend\n"
        "f()\n",
        "discarded Error", MESSAGE_CHECKER_DISCARDED_ERROR);
}


TestResult test_bound_error_value(void)
{
    return compiles(
        "fun f(): Int32 | Error\n  return 1\nend\n"
        "var r: Int32 | Error = f()\n",
        "bound Error result");
}


TestResult test_unreachable_after_return(void)
{
    return compile_fails(
        "fun f(): Int32\n  return 1\n  return 2\nend\n",
        "unreachable return", MESSAGE_CHECKER_UNREACHABLE_CODE);
}


TestResult test_unreachable_after_break(void)
{
    return compile_fails(
        "fun f(): Nil\n"
        "  while true\n"
        "    break\n"
        "    print(1)\n"
        "  end\n"
        "  return nil\n"
        "end\n",
        "unreachable break", MESSAGE_CHECKER_UNREACHABLE_CODE);
}


TestResult test_while_true_falls_through(void)
{
    return compiles(
        "fun f(): Int32\n"
        "  while true\n"
        "    return 1\n"
        "  end\n"
        "end\n"
        "print(f())\n",
        "infinite while returns");
}


TestResult test_always_recursive_function(void)
{
    return compile_fails(
        "fun f(x: Int32): Int32\n  return f(x + 1)\nend\n"
        "var n: Int32 = f(0)\n",
        "always recursive function", MESSAGE_CHECKER_ALWAYS_RECURSIVE, "f");
}

TestResult test_by_value_size_cap(void)
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
        (size_t)82020, (size_t)NIPPY_MAX_VALUE_BYTES);
}

TestResult test_local_inference_contexts(void)
{
    TestResult result = compile_fails(
        "fun f(): Nil\n  var x = []\n  return nil\nend\n",
        "local list context", MESSAGE_CHECKER_LIST_INFER);

    if (result != TEST_PASS) {
        return result;
    }
    result = compile_fails(
        "fun f(): Nil\n  var r = {\"a\": 1}\n  return nil\nend\n",
        "local Record context", MESSAGE_CHECKER_RECORD_LITERAL_CONTEXT);
    if (result != TEST_PASS) {
        return result;
    }
    return compile_fails(
        "fun f(): Nil\n  var s = \"x\"\n  return nil\nend\n",
        "local string context", MESSAGE_CHECKER_STRING_LITERAL_CONTEXT);
}


TestResult test_parenthesized_type_rparen(void)
{
    return compile_fails("var x: (Int32 = 1\n", "missing type rparen",
                         MESSAGE_PARSER_EXPECTED_RPAREN);
}


TestResult test_byte_literal_comparison_range(void)
{
    return compile_fails("var b: Byte = 1\nprint(300 == b)\n",
                         "Byte literal comparison range",
                         MESSAGE_CHECKER_OPERATOR_TYPE, "==");
}

const TestCase bounds_test_tests[] = {
    {"safety: constant overflow", test_constant_expression_overflow},
    {"safety: constant shift", test_constant_shift_range},
    {"safety: constant zero divisor", test_constant_zero_divisor},
    {"safety: const negative capacity", test_const_negative_capacity},
    {"safety: const struct mutation", test_const_struct_field_mutation},
    {"safety: const array mutation", test_const_array_mutation},
    {"safety: const record mutation", test_const_record_mutation},
    {"safety: discarded Error", test_discarded_error},
    {"safety: bound Error", test_bound_error_value},
    {"safety: unreachable return", test_unreachable_after_return},
    {"safety: unreachable break", test_unreachable_after_break},
    {"safety: while true returns", test_while_true_falls_through},
    {"safety: always recursive", test_always_recursive_function},
    {"safety: by-value size cap", test_by_value_size_cap},
    {"safety: local inference contexts", test_local_inference_contexts},
    {"safety: parenthesized type rparen", test_parenthesized_type_rparen},
    {"safety: Byte comparison range", test_byte_literal_comparison_range},
    {"bounds: literal array index", test_literal_array_index_out_of_bounds},
    {"bounds: literal negative index", test_literal_negative_index},
    {"bounds: literal string index", test_literal_string_index_out_of_bounds},
    {"bounds: literal array assignment",
     test_literal_array_assignment_out_of_bounds},
    {"bounds: negative list capacity", test_negative_list_capacity},
    {"bounds: negative builder capacity", test_negative_builder_capacity},
    {"bounds: negative table capacity", test_negative_table_capacity},
    {"bounds: negative store capacity", test_negative_store_capacity},
    {"bounds: mutation in for", test_for_iterable_mutation},
    {"bounds: nested mutation in for", test_for_iterable_mutation_nested},
};
const size_t bounds_test_test_count =
    sizeof(bounds_test_tests) / sizeof(bounds_test_tests[0]);
