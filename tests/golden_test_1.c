#include "test_support.h"
#include "golden_data.h"


/* ---- positive: each test owns its source and its expected generated C ---- */

TestResult test_13_codegen(void)
{
    return compile_succeeds("var x : Int32 = 13\n", golden_13_c, golden_13_h,
                            "program 13");
}


TestResult test_type_declaration_codegen(void)
{
    const char *source =
        "type Point\n"
        "  x: Int32\n"
        "  y: Int32\n"
        "end\n";

    return compile_succeeds(source, golden_struct_c,
                            golden_struct_h, "a type declaration");
}


TestResult test_struct_values_codegen(void)
{
    const char *source =
        "type Point\n"
        "  x: Int32\n"
        "  y: Int32\n"
        "end\n"
        "\n"
        "var p1: Point = Point(1, 2)\n"
        "var p2: Point = Point(x = 3, y = 4)\n"
        "var p3 = Point(6, 7)\n";

    return compile_succeeds(source, golden_values_c,
                            golden_values_h, "struct values and inference");
}


TestResult test_function_codegen(void)
{
    const char *source =
        "fun adder(x:Int32, y:Int32):Int32\n"
        "  return x + y\n"
        "end\n"
        "\n"
        "var sum : Int32 = adder(1, 2)\n";

    return compile_succeeds(source, golden_function_c,
                            golden_function_h, "a function declaration");
}


TestResult test_function_type_and_higher_order_codegen(void)
{
    const char *source =
        "fun inc(x:Int32):Int32\n"
        "  return x + 1\n"
        "end\n"
        "\n"
        "fun apply(f:Fun<(Int32):Int32>, x:Int32):Int32\n"
        "  return f(x)\n"
        "end\n"
        "\n"
        "var n : Int32 = apply(inc, 41)\n";

    return compile_succeeds(source, golden_higher_order_c,
                            golden_higher_order_h,
                            "a higher-order function");
}


TestResult test_function_value_codegen(void)
{
    const char *source =
        "fun add(x:Int32, y:Int32):Int32\n"
        "  return x + y\n"
        "end\n"
        "\n"
        "var f : Fun<(Int32, Int32):Int32> = add\n"
        "var n : Int32 = f(2, 3)\n";

    return compile_succeeds(source, golden_function_value_c,
                            golden_function_value_h,
                            "a function value and indirect call");
}


TestResult test_recursive_function_codegen(void)
{
    const char *source =
        "fun countdown(n:Int32):Int32\n"
        "  return countdown(n)\n"
        "end\n"
        "\n"
        "var zero : Int32 = 0\n";

    return compile_succeeds(source, golden_recursive_c,
                            golden_recursive_h, "a recursive function");
}


TestResult test_if_elseif_else_codegen(void)
{
    const char *source =
        "fun classify(x:Int32):Int32\n"
        "  if x > 0 then\n"
        "    return 1\n"
        "  elseif x == 0 then\n"
        "    return 2\n"
        "  else\n"
        "    return 3\n"
        "  end\n"
        "end\n"
        "\n"
        "var n : Int32 = classify(5)\n";

    return compile_succeeds(source, golden_if_elseif_else_c,
                            golden_if_elseif_else_h,
                            "if/elseif/else");
}


TestResult test_while_break_continue_codegen(void)
{
    const char *source =
        "fun loop(x:Int32):Int32\n"
        "  while true\n"
        "    if x > 0 then\n"
        "      break\n"
        "    end\n"
        "    continue\n"
        "  end\n"
        "  return 7\n"
        "end\n"
        "\n"
        "var n : Int32 = loop(1)\n";

    return compile_succeeds(source, golden_while_break_continue_c,
                            golden_while_break_continue_h,
                            "while with break and continue");
}


TestResult test_bool_logic_codegen(void)
{
    const char *source =
        "fun choose(flag:Bool):Int32\n"
        "  if flag then\n"
        "    return 1\n"
        "  else\n"
        "    return 0\n"
        "  end\n"
        "end\n"
        "\n"
        "var n : Int32 = choose(!(1 == 2) && (2 < 3) || false)\n";

    return compile_succeeds(source, golden_logic_c,
                            golden_logic_h, "Bool type and logical operators");
}


TestResult test_bool_struct_field_codegen(void)
{
    const char *source =
        "type Flags\n"
        "  on: Bool\n"
        "  count: Int32\n"
        "end\n"
        "\n"
        "var f : Flags = Flags(true, 3)\n";

    return compile_succeeds(source, golden_bool_field_c,
                            golden_bool_field_h, "a Bool struct field");
}


TestResult test_whitespace_agnostic_codegen(void)
{
    const char *single_line =
        "fun f(x:Int32):Int32 if x > 0 then return 1 else return 0 end end "
        "var n:Int32=f(1)\n";
    const char *scattered =
        "fun\tf(x:Int32):Int32\n\n"
        "if x>0\nthen\nreturn\t1\nelse\nreturn 0\nend\nend\n"
        "var\tn\t:\tInt32\t=\tf(\t1\t)\n";
    TestResult first = compile_succeeds(single_line, golden_oneline_c,
                                        golden_oneline_h,
                                        "a single-line program");
    TestResult second = compile_succeeds(scattered, golden_scattered_c,
                                         golden_scattered_h,
                                         "a whitespace-scattered program");

    return first == TEST_PASS && second == TEST_PASS ? TEST_PASS : TEST_FAIL;
}


TestResult test_nil_return_codegen(void)
{
    const char *source =
        "fun nothing(): Nil\n"
        "  return nil\n"
        "end\n";

    return compile_succeeds(source, golden_nil_return_c,
                            golden_nil_return_h, "a Nil return");
}


TestResult test_union_vars_codegen(void)
{
    const char *source =
        "type Something = Int32 | Bool | Nil\n"
        "\n"
        "var a: Something = 3\n"
        "var b: Something = true\n"
        "var c: Something = nil\n";

    return compile_succeeds(source, golden_union_vars_c,
                            golden_union_vars_h, "union injection");
}


TestResult test_union_canonical_codegen(void)
{
    const char *source =
        "var a: Bool | Int32 = 3\n"
        "var b: Int32 | Bool = false\n";

    return compile_succeeds(source, golden_union_canonical_c,
                            golden_union_canonical_h, "canonical union order");
}


TestResult test_is_narrow_codegen(void)
{
    const char *source =
        "fun classify(x: Int32 | Bool | Nil): Int32\n"
        "  if typeof x is Int32 then\n"
        "    return x\n"
        "  elseif typeof x is Bool then\n"
        "    if x then\n"
        "      return 1\n"
        "    else\n"
        "      return 0\n"
        "    end\n"
        "  else\n"
        "    return 0\n"
        "  end\n"
        "end\n"
        "\n"
        "var n: Int32 = classify(3)\n";

    return compile_succeeds(source, golden_is_narrow_c,
                            golden_is_narrow_h, "is-test and narrowing");
}


TestResult test_generic_alias_codegen(void)
{
    const char *source =
        "type Option<T> = T | Nil\n"
        "\n"
        "var a: Option<Int32> = 3\n"
        "var b: Option<Int32> = nil\n";

    return compile_succeeds(source, golden_generic_alias_c,
                            golden_generic_alias_h, "a generic alias");
}


TestResult test_generic_struct_codegen(void)
{
    const char *source =
        "type Pair<A, B>\n"
        "  first: A\n"
        "  second: B\n"
        "end\n"
        "\n"
        "var p = Pair(1, true)\n";

    return compile_succeeds(source, golden_generic_struct_c,
                            golden_generic_struct_h, "a generic struct");
}


TestResult test_generic_function_codegen(void)
{
    const char *source =
        "fun identity<T>(x: T): T\n"
        "  return x\n"
        "end\n"
        "\n"
        "var n: Int32 = identity(3)\n";

    return compile_succeeds(source, golden_generic_function_c,
                            golden_generic_function_h, "a generic function");
}


TestResult test_generic_two_types_codegen(void)
{
    const char *source =
        "fun identity<T>(x: T): T\n"
        "  return x\n"
        "end\n"
        "\n"
        "var a: Int32 = identity(1)\n"
        "var b: Bool = identity(true)\n";

    return compile_succeeds(source, golden_generic_two_types_c,
                            golden_generic_two_types_h, "two generic instantiations");
}


TestResult test_generic_hof_codegen(void)
{
    const char *source =
        "fun apply<T>(f: Fun<(T):T>, x: T): T\n"
        "  return f(x)\n"
        "end\n"
        "\n"
        "fun inc(x: Int32): Int32\n"
        "  return x + 1\n"
        "end\n"
        "\n"
        "var n: Int32 = apply(inc, 1)\n";

    return compile_succeeds(source, golden_generic_hof_c,
                            golden_generic_hof_h, "a generic higher-order function");
}


TestResult test_generic_nested_codegen(void)
{
    const char *source =
        "type Option<T> = T | Nil\n"
        "type Pair<A, B>\n"
        "  first: A\n"
        "  second: B\n"
        "end\n"
        "\n"
        "var o: Option<Int32> = 3\n"
        "var p = Pair(o, false)\n";

    return compile_succeeds(source, golden_generic_nested_c,
                            golden_generic_nested_h, "nested generic types");
}


TestResult test_generic_self_codegen(void)
{
    const char *source =
        "fun count<T>(x: T): T\n"
        "  return count(x)\n"
        "end\n"
        "\n"
        "var n: Int32 = count(1)\n";

    return compile_succeeds(source, golden_generic_self_c,
                            golden_generic_self_h, "a recursive generic function");
}


TestResult test_union_field_codegen(void)
{
    const char *source =
        "type Holder\n"
        "  value: Int32 | Nil\n"
        "end\n"
        "\n"
        "var h: Holder = Holder(nil)\n";

    return compile_succeeds(source, golden_union_field_c,
                            golden_union_field_h, "a union struct field");
}


TestResult test_is_static_codegen(void)
{
    const char *source =
        "fun yes(x: Int32): Bool\n"
        "  return typeof x is Int32\n"
        "end\n"
        "\n"
        "fun no(x: Int32): Bool\n"
        "  return typeof x is Bool\n"
        "end\n";

    return compile_succeeds(source, golden_is_static_c,
                            golden_is_static_h, "statically decided is-tests");
}


TestResult test_is_nil_codegen(void)
{
    const char *source =
        "fun is_nil(x: Int32 | Nil): Bool\n"
        "  if typeof x is Nil then\n"
        "    return true\n"
        "  end\n"
        "  return false\n"
        "end\n";

    return compile_succeeds(source, golden_is_nil_c,
                            golden_is_nil_h, "an is-Nil test");
}


TestResult test_union_struct_codegen(void)
{
    const char *source =
        "type Point\n"
        "  x: Int32\n"
        "  y: Int32\n"
        "end\n"
        "\n"
        "fun pick(p: Point | Nil): Point\n"
        "  if typeof p is Point then\n"
        "    return p\n"
        "  end\n"
        "  return Point(0, 0)\n"
        "end\n"
        "\n"
        "var q: Point = pick(Point(1, 2))\n";

    return compile_succeeds(source, golden_union_struct_c,
                            golden_union_struct_h, "a struct union alternative");
}


TestResult test_generic_struct_two_codegen(void)
{
    const char *source =
        "type Pair<A, B>\n"
        "  first: A\n"
        "  second: B\n"
        "end\n"
        "\n"
        "var a = Pair(1, true)\n"
        "var b = Pair(true, 1)\n";

    return compile_succeeds(source, golden_generic_struct_two_c,
                            golden_generic_struct_two_h,
                            "two generic struct instantiations");
}


TestResult test_generic_struct_arg_codegen(void)
{
    const char *source =
        "type Point\n"
        "  x: Int32\n"
        "  y: Int32\n"
        "end\n"
        "\n"
        "fun identity<T>(x: T): T\n"
        "  return x\n"
        "end\n"
        "\n"
        "var p = identity(Point(1, 2))\n";

    return compile_succeeds(source, golden_generic_struct_arg_c,
                            golden_generic_struct_arg_h,
                            "a generic function over a struct");
}


TestResult test_generic_two_params_codegen(void)
{
    const char *source =
        "fun first<A, B>(a: A, b: B): A\n"
        "  return a\n"
        "end\n"
        "\n"
        "var n: Int32 = first(1, true)\n";

    return compile_succeeds(source, golden_generic_two_params_c,
                            golden_generic_two_params_h,
                            "a two-parameter generic function");
}


TestResult test_generic_nested_call_codegen(void)
{
    const char *source =
        "fun identity<T>(x: T): T\n"
        "  return x\n"
        "end\n"
        "\n"
        "fun wrap<T>(x: T): T\n"
        "  return identity(x)\n"
        "end\n"
        "\n"
        "var n: Int32 = wrap(3)\n";

    return compile_succeeds(source, golden_generic_nested_call_c,
                            golden_generic_nested_call_h,
                            "a generic function calling a generic function");
}


TestResult test_inject_call_codegen(void)
{
    const char *source =
        "fun take(v: Int32 | Nil): Int32\n"
        "  if typeof v is Int32 then\n"
        "    return v\n"
        "  end\n"
        "  return 0\n"
        "end\n"
        "\n"
        "fun pass(x: Int32): Int32\n"
        "  return take(x)\n"
        "end\n"
        "\n"
        "var n: Int32 = pass(3)\n";

    return compile_succeeds(source, golden_inject_call_c,
                            golden_inject_call_h, "union injection at a call argument");
}


TestResult test_union_field_int_codegen(void)
{
    const char *source =
        "type Holder\n"
        "  value: Int32 | Nil\n"
        "end\n"
        "\n"
        "var h: Holder = Holder(3)\n";

    return compile_succeeds(source, golden_union_field_int_c,
                            golden_union_field_int_h,
                            "union injection into a struct field");
}


TestResult test_generic_struct_named_codegen(void)
{
    const char *source =
        "type Pair<A, B>\n"
        "  first: A\n"
        "  second: B\n"
        "end\n"
        "\n"
        "var p = Pair(first = 1, second = true)\n";

    return compile_succeeds(source, golden_generic_struct_named_c,
                            golden_generic_struct_named_h,
                            "a named-argument generic constructor");
}


TestResult test_generic_struct_point_codegen(void)
{
    const char *source =
        "type Point\n"
        "  x: Int32\n"
        "  y: Int32\n"
        "end\n"
        "\n"
        "type Pair<A, B>\n"
        "  first: A\n"
        "  second: B\n"
        "end\n"
        "\n"
        "var p = Pair(Point(1, 2), true)\n";

    return compile_succeeds(source, golden_generic_struct_point_c,
                            golden_generic_struct_point_h,
                            "a generic struct over a struct");
}


TestResult test_alias_chain_codegen(void)
{
    const char *source =
        "type MaybeInt = Int32 | Nil\n"
        "type Value = MaybeInt | Bool\n"
        "\n"
        "var v: Value = true\n"
        "var n: Value = 3\n";

    return compile_succeeds(source, golden_alias_chain_c, golden_alias_chain_h,
                            "a chain of union aliases");
}


TestResult test_generic_union_return_codegen(void)
{
    const char *source =
        "fun maybe<T>(x: T): T | Nil\n"
        "  return x\n"
        "end\n"
        "\n"
        "var m: Int32 | Nil = maybe(3)\n";

    return compile_succeeds(source, golden_generic_union_return_c,
                            golden_generic_union_return_h,
                            "a generic union return");
}


TestResult test_nil_field_param_codegen(void)
{
    const char *source =
        "type Marker\n"
        "  value: Nil\n"
        "end\n"
        "\n"
        "fun take(n: Nil): Int32\n"
        "  return 0\n"
        "end\n"
        "\n"
        "var m: Marker = Marker(nil)\n"
        "var n: Int32 = take(nil)\n";

    return compile_succeeds(source, golden_nil_field_param_c, golden_nil_field_param_h,
                            "Nil in a field and a parameter");
}


TestResult test_store_alloc_codegen(void)
{
    const char *source =
        "var s: Store = Store(16)\n"
        "var p: Handle<Int32> = s.alloc<Int32>(1)\n"
        "var n: Int32 = p.read()\n";

    return compile_succeeds_parts(source, &golden_store_alloc_c, golden_store_alloc_h,
                            "a store allocation and read");
}


TestResult test_store_write_reset_codegen(void)
{
    const char *source =
        "var s: Store = Store(16)\n"
        "var p: Handle<Int32> = s.alloc<Int32>(1)\n"
        "var w: Nil = p.write(2)\n"
        "var r: Nil = s.reset()\n";

    return compile_succeeds_parts(source, &golden_store_write_reset_c,
                            golden_store_write_reset_h,
                            "store write and reset");
}


TestResult test_byte_basics_codegen(void)
{
    const char *source =
        "var b: Byte = 255\n"
        "var c: Byte | Nil = 7\n"
        "var ok: Bool = b == 255\n";

    return compile_succeeds(source, golden_byte_basics_c, golden_byte_basics_h,
                            "Byte values and equality");
}


TestResult test_byte_struct_codegen(void)
{
    const char *source =
        "type B\n"
        "  value: Byte\n"
        "end\n"
        "\n"
        "var b: B = B(200)\n";

    return compile_succeeds(source, golden_byte_struct_c, golden_byte_struct_h,
                            "a Byte struct field");
}

const TestCase golden_test_1_tests[] = {
    {"var declaration of 13 generates the golden C", test_13_codegen},
    {"type declaration generates the golden C struct", test_type_declaration_codegen},
    {"struct values and inference generate the golden C", test_struct_values_codegen},
    {"function generates the golden C", test_function_codegen},
    {"function type and higher-order call generate the golden C",
     test_function_type_and_higher_order_codegen},
    {"function value and indirect call generate the golden C",
     test_function_value_codegen},
    {"recursive function generates the golden C", test_recursive_function_codegen},
    {"Bool and if/elseif/else generate the golden C", test_if_elseif_else_codegen},
    {"while with break and continue generate the golden C",
     test_while_break_continue_codegen},
    {"Bool type and logical operators generate the golden C", test_bool_logic_codegen},
    {"Bool struct field generates the golden C", test_bool_struct_field_codegen},
    {"newline and whitespace variants generate the golden C",
     test_whitespace_agnostic_codegen},
    {"a store allocation and read", test_store_alloc_codegen},
    {"store write and reset", test_store_write_reset_codegen},
    {"Byte values and equality", test_byte_basics_codegen},
    {"a Byte struct field", test_byte_struct_codegen},
    {"a generic union return", test_generic_union_return_codegen},
    {"Nil in a field and a parameter", test_nil_field_param_codegen},
    {"a named-argument generic constructor", test_generic_struct_named_codegen},
    {"a generic struct over a struct", test_generic_struct_point_codegen},
    {"a chain of union aliases", test_alias_chain_codegen},
    {"a two-parameter generic function", test_generic_two_params_codegen},
    {"a generic function calling a generic function", test_generic_nested_call_codegen},
    {"union injection at a call argument", test_inject_call_codegen},
    {"union injection into a struct field", test_union_field_int_codegen},
    {"statically decided is-tests", test_is_static_codegen},
    {"an is-Nil test", test_is_nil_codegen},
    {"a struct union alternative", test_union_struct_codegen},
    {"two generic struct instantiations", test_generic_struct_two_codegen},
    {"a generic function over a struct", test_generic_struct_arg_codegen},
    {"a Nil return", test_nil_return_codegen},
    {"union injection", test_union_vars_codegen},
    {"canonical union order", test_union_canonical_codegen},
    {"is-test and narrowing", test_is_narrow_codegen},
    {"a generic alias", test_generic_alias_codegen},
    {"a generic struct", test_generic_struct_codegen},
    {"a generic function", test_generic_function_codegen},
    {"two generic instantiations", test_generic_two_types_codegen},
    {"a generic higher-order function", test_generic_hof_codegen},
    {"nested generic types", test_generic_nested_codegen},
    {"a recursive generic function", test_generic_self_codegen},
    {"a union struct field", test_union_field_codegen},
};

const size_t golden_test_1_test_count =
    sizeof(golden_test_1_tests) / sizeof(golden_test_1_tests[0]);
