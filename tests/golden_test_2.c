#include "test_support.h"
#include "golden_data.h"


TestResult test_handle_field_codegen(void)
{
    const char *source =
        "type Holder\n"
        "  h: Handle<Int32>\n"
        "end\n"
        "\n"
        "var s: Store = Store(16)\n"
        "var p: Handle<Int32> = s.alloc<Int32>(1)\n"
        "var holder: Holder = Holder(p)\n";

    return compile_succeeds_parts(source, &golden_handle_field_c, golden_handle_field_h,
                            "a Handle struct field");
}


TestResult test_store_functions_codegen(void)
{
    const char *source =
        "var s: Store = Store(16)\n"
        "\n"
        "fun make(x: Int32): Handle<Int32>\n"
        "  return s.alloc<Int32>(x)\n"
        "end\n"
        "\n"
        "fun read_it(h: Handle<Int32>): Int32\n"
        "  return h.read()\n"
        "end\n"
        "\n"
        "var n: Int32 = read_it(make(42))\n";

    return compile_succeeds_parts(source, &golden_store_functions_c,
                            golden_store_functions_h,
                            "store use inside functions");
}


TestResult test_expression_statement_codegen(void)
{
    const char *source =
        "var s: Store = Store(16)\n"
        "\n"
        "fun clear(): Nil\n"
        "  s.reset()\n"
        "  return nil\n"
        "end\n";

    return compile_succeeds_parts(source, &golden_expression_statement_c,
                            golden_expression_statement_h,
                            "a call expression statement");
}


TestResult test_top_level_reset_codegen(void)
{
    const char *source =
        "var s: Store = Store(16)\n"
        "s.reset()\n";

    return compile_succeeds_parts(source, &golden_top_level_reset_c,
                            golden_top_level_reset_h,
                            "a top-level call statement");
}


TestResult test_byte_equality_codegen(void)
{
    const char *source =
        "fun is_seven(b: Byte): Bool\n"
        "  return b == 7\n"
        "end\n"
        "\n"
        "var ok: Bool = is_seven(7)\n";

    return compile_succeeds(source, golden_byte_equality_c, golden_byte_equality_h,
                            "Byte equality with a literal");
}


TestResult test_alloc_byte_codegen(void)
{
    const char *source =
        "var s: Store = Store(1)\n"
        "var b: Handle<Byte> = s.alloc<Byte>(255)\n";

    return compile_succeeds_parts(source, &golden_alloc_byte_c, golden_alloc_byte_h,
                            "allocating a Byte");
}



TestResult test_two_stores_codegen(void)
{
    const char *source =
        "var a: Store = Store(1)\n"
        "var b: Store = Store(1)\n"
        "var p: Handle<Int32> = a.alloc<Int32>(1)\n"
        "var q: Handle<Int32> = b.alloc<Int32>(2)\n"
        "var n: Int32 = p.read() + q.read()\n"
        "a.reset()\n";

    return compile_succeeds_parts(source, &golden_two_stores_c, golden_two_stores_h,
                            "two independent stores");
}


TestResult test_store_size_expr_codegen(void)
{
    const char *source =
        "var s: Store = Store(1 + 2)\n"
        "var h: Handle<Int32> = s.alloc<Int32>(1)\n";

    return compile_succeeds_parts(source, &golden_store_size_expr_c,
                            golden_store_size_expr_h,
                            "a constant store size expression");
}


TestResult test_generic_handle_codegen(void)
{
    const char *source =
        "var s: Store = Store(1)\n"
        "fun read_it<T>(h: Handle<T>): T\n"
        "  return h.read()\n"
        "end\n"
        "var n: Int32 = read_it(s.alloc<Int32>(42))\n";

    return compile_succeeds_parts(source, &golden_generic_handle_c,
                                  golden_generic_handle_h,
                                  "inference through Handle<T>");
}


TestResult test_generic_alloc_codegen(void)
{
    const char *source =
        "var s: Store = Store(1)\n"
        "fun alloc_one<T>(x: T): Handle<T>\n"
        "  return s.alloc<T>(x)\n"
        "end\n"
        "var n: Int32 = alloc_one(7).read()\n";

    return compile_succeeds_parts(source, &golden_generic_alloc_c,
                                  golden_generic_alloc_h,
                                  "allocating a generic value");
}


TestResult test_handle_union_codegen(void)
{
    const char *source =
        "type MaybeHandle = Handle<Int32> | Nil\n"
        "var s: Store = Store(1)\n"
        "var h: Handle<Int32> = s.alloc<Int32>(1)\n"
        "var m: MaybeHandle = h\n";

    return compile_succeeds_parts(source, &golden_handle_union_c, golden_handle_union_h,
                            "a Handle union alternative");
}


TestResult test_string_basic_codegen(void)
{
    const char *source =
        "var s: String<8> = \"hi\\n\"\n"
        "var n: Int32 = s.byte_len()\n"
        "var ok: Bool = s == \"hi\\n\"\n"
    ;

    return compile_succeeds(source, golden_string_basic_c,
                            golden_string_basic_h, "string basic");
}


TestResult test_string_union_codegen(void)
{
    const char *source =
        "var u: String<8> | Nil = \"hi\"\n"
        "var ok: Bool = typeof u is String<8>\n"
    ;

    return compile_succeeds(source, golden_string_union_c,
                            golden_string_union_h, "string union");
}


TestResult test_string_error_codegen(void)
{
    const char *source =
        "var e: Error = Error(\"E\", \"desc\", \"test.nip\", 1, 2, 3)\n"
        "var l: Int32 = e.line()\n"
        "var h: String<32> = e.header()\n"
    ;

    return compile_succeeds(source, golden_string_error_c,
                            golden_string_error_h, "string error");
}


TestResult test_string_store_codegen(void)
{
    const char *source =
        "var arena: Store = Store(8)\n"
        "var h: Handle<String<8>> = arena.alloc<String<8>>(\"hi\")\n"
        "var s: String<8> = h.read()\n"
    ;

    return compile_succeeds_parts(source, &golden_string_store_c,
                                  golden_string_store_h,
                                  "string store");
}


TestResult test_assignment_top_codegen(void)
{
    const char *source =
        "var x: Int32 = 1\n"
        "x = 2\n"
    ;

    return compile_succeeds(source, golden_assignment_top_c,
                            golden_assignment_top_h, "assignment top");
}


TestResult test_assignment_param_codegen(void)
{
    const char *source =
        "fun bump(x: Int32): Int32\n"
        "  x = x + 1\n"
        "  return x\n"
        "end\n"
        "\n"
        "var n: Int32 = bump(1)\n"
    ;

    return compile_succeeds(source, golden_assignment_param_c,
                            golden_assignment_param_h, "assignment param");
}


TestResult test_assignment_main_codegen(void)
{
    const char *source =
        "var a: Int32 = 1\n"
        "var b: Int32 = 2\n"
        "a = b\n"
    ;

    return compile_succeeds(source, golden_assignment_main_c,
                            golden_assignment_main_h, "main assignment");
}

const TestCase golden_test_2_tests[] = {
    {"inference through Handle<T>", test_generic_handle_codegen},
    {"allocating a generic value", test_generic_alloc_codegen},
    {"a Handle union alternative", test_handle_union_codegen},
    {"two independent stores", test_two_stores_codegen},
    {"a constant store size expression", test_store_size_expr_codegen},
    {"a Handle struct field", test_handle_field_codegen},
    {"store use inside functions", test_store_functions_codegen},
    {"a call expression statement", test_expression_statement_codegen},
    {"a top-level call statement", test_top_level_reset_codegen},
    {"Byte equality with a literal", test_byte_equality_codegen},
    {"allocating a Byte", test_alloc_byte_codegen},
    {"string: string basic codegen", test_string_basic_codegen},
    {"string: string union codegen", test_string_union_codegen},
    {"string: string error codegen", test_string_error_codegen},
    {"string: string store codegen", test_string_store_codegen},
    {"assignment: top-level print", test_assignment_top_codegen},
    {"assignment: parameter", test_assignment_param_codegen},
    {"assignment: main", test_assignment_main_codegen},
};

const size_t golden_test_2_test_count =
    sizeof(golden_test_2_tests) / sizeof(golden_test_2_tests[0]);
