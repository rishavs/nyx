#include "tool_support.h"


TestResult test_runtime_union_narrowing(void)
{
    const char *source =
        "fun make(x: Int32): Int32 | Nil\n"
        "  return x\n"
        "end\n"
        "\n"
        "fun unwrap(v: Int32 | Nil): Int32\n"
        "  if typeof v is Int32 then\n"
        "    return v\n"
        "  end\n"
        "  return 0\n"
        "end\n"
        "\n"
        "fun compute(x: Int32): Int32\n"
        "  return unwrap(make(x))\n"
        "end\n";
    const char *driver =
        "#include <stdint.h>\n"
        "#include <stdio.h>\n"
        "\n"
        "int32_t compute(int32_t x);\n"
        "\n"
        "int main(void)\n"
        "{\n"
        "    printf(\"%d\\n\", (int)compute(3));\n"
        "    return 0;\n"
        "}\n";

    return run_generated_driver(source, driver, "3\n", "union narrowing");
}


TestResult test_runtime_generic_instantiations(void)
{
    const char *source =
        "fun identity<T>(x: T): T\n"
        "  return x\n"
        "end\n"
        "\n"
        "fun flag(): Bool\n"
        "  return identity(true)\n"
        "end\n"
        "\n"
        "fun compute(x: Int32): Int32\n"
        "  if flag() then\n"
        "    return identity(x)\n"
        "  end\n"
        "  return 0\n"
        "end\n";
    const char *driver =
        "#include <stdint.h>\n"
        "#include <stdio.h>\n"
        "\n"
        "int32_t compute(int32_t x);\n"
        "\n"
        "int main(void)\n"
        "{\n"
        "    printf(\"%d\\n\", (int)compute(3));\n"
        "    return 0;\n"
        "}\n";

    return run_generated_driver(source, driver, "3\n",
                                "generic instantiations");
}



TestResult test_runtime_assignment_top(void)
{
    const char *source =
        "var x: Int32 = 1\n"
        "x = 2\n";

    return run_generated(source, "2\n", "top-level assignment");
}


TestResult test_runtime_assignment_parameter(void)
{
    const char *source =
        "fun bump(x: Int32): Int32\n"
        "  x = x + 1\n"
        "  return x\n"
        "end\n"
        "var n: Int32 = bump(1)\n";

    return run_generated(source, "2\n", "parameter assignment");
}


TestResult test_runtime_assignment_union_narrowing(void)
{
    const char *source =
        "fun check(x: Int32 | Bool): Int32\n"
        "  if typeof x is Int32 then\n"
        "    x = true\n"
        "    if typeof x is Bool then\n"
        "      return 1\n"
        "    end\n"
        "  end\n"
        "  return 0\n"
        "end\n"
        "var n: Int32 = check(1)\n";

    return run_generated(source, "1\n", "assignment invalidates narrowing");
}



TestResult test_runtime_assignment_loop(void)
{
    const char *source =
        "fun count(x: Int32): Int32\n"
        "  while x < 3\n"
        "    x = x + 1\n"
        "  end\n"
        "  return x\n"
        "end\n"
        "var n: Int32 = count(0)\n";

    return run_generated(source, "3\n", "assignment in a loop");
}



TestResult test_runtime_assignment_repeated(void)
{
    const char *source =
        "var x: Int32 = 1\n"
        "x = 2\n"
        "x = 3\n";

    return run_generated(source, "3\n", "repeated assignment");
}



TestResult test_runtime_assignment_generic(void)
{
    const char *source =
        "fun choose<T>(x: T, y: T): T\n"
        "  x = y\n"
        "  return x\n"
        "end\n"
        "var n: Int32 = choose(1, 2)\n";

    return run_generated(source, "2\n", "generic assignment");
}



TestResult test_runtime_assignment_inferred(void)
{
    const char *source =
        "var x = 1\n"
        "x = 2\n";

    return run_generated(source, "2\n", "inferred assignment");
}



TestResult test_runtime_assignment_nested_narrowing(void)
{
    const char *source =
        "fun check(x: Int32 | Bool, y: Int32 | Bool): Int32\n"
        "  if typeof x is Int32 then\n"
        "    if typeof y is Int32 then\n"
        "      x = true\n"
        "      y = true\n"
        "      if typeof x is Bool && typeof y is Bool then\n"
        "        return 1\n"
        "      end\n"
        "    end\n"
        "  end\n"
        "  return 0\n"
        "end\n"
        "var n: Int32 = check(1, 2)\n";

    return run_generated(source, "1\n", "nested narrowing invalidation");
}



TestResult test_runtime_assignment_union(void)
{
    const char *source =
        "fun set(u: Int32 | Bool): Int32\n"
        "  u = true\n"
        "  if typeof u is Bool then\n"
        "    return 1\n"
        "  end\n"
        "  return 0\n"
        "end\n"
        "var n: Int32 = set(1)\n";

    return run_generated(source, "1\n", "union assignment");
}



TestResult test_runtime_locals_and_fields(void)
{
    const char *source =
        "type Point\n"
        "  x: Int32\n"
        "  y: Int32\n"
        "end\n"
        "fun f(): Int32\n"
        "  var p: Point = Point(1, 2)\n"
        "  p.x = 3\n"
        "  var xs: Array<Int32, 2> = [10, 20]\n"
        "  xs[1] = 30\n"
        "  return p.x + p.y + xs[0] + xs[1]\n"
        "end\n"
        "var n: Int32 = f()\n";

    return run_generated(source, "45\n", "locals, fields, and indexing");
}


TestResult test_runtime_for_index(void)
{
    const char *source =
        "fun sum(): Int32\n"
        "  var s: String<8> = \"hello\"\n"
        "  var total: Int32 = 0\n"
        "  for i, b in s do\n"
        "    total = total + i\n"
        "  end\n"
        "  return total\n"
        "end\n"
        "var n: Int32 = sum()\n";

    return run_generated(source, "10\n", "for with an index");
}


TestResult test_runtime_for_mutation(void)
{
    const char *source =
        "global arena: Store = Store(64)\n"
        "fun extend(xs: List<Int32>): Nil\n"
        "  xs.push(3)\n"
        "  return nil\n"
        "end\n"
        "fun sum(): Int32\n"
        "  var xs: List<Int32> = arena.list<Int32>(4)\n"
        "  xs.push(1)\n"
        "  xs.push(2)\n"
        "  var total: Int32 = 0\n"
        "  for x in xs do\n"
        "    extend(xs)\n"
        "    total = total + x\n"
        "  end\n"
        "  return total\n"
        "end\n"
        "var n: Int32 = sum()\n";

    return run_generated_failure(source, MESSAGE_RUNTIME_FOR_MUTATED,
                                 "for mutation");
}


TestResult test_runtime_nested_structs(void)
{
    const char *source =
        "type Inner\n"
        "  x: Int32\n"
        "end\n"
        "type Middle\n"
        "  inner: Inner\n"
        "end\n"
        "type Outer\n"
        "  middle: Middle\n"
        "end\n"
        "print(Outer(Middle(Inner(7))))\n";
    const char *expected =
        "Outer { middle: Middle { inner: Inner { x: 7 } } }\n";

    return run_generated(source, expected, "nested struct print");
}



TestResult test_runtime_typeof_if(void)
{
    const char *source =
        "type V = Int32 | Bool | Nil\n"
        "var x: V = 1\n"
        "if typeof x is Int32 then\n"
        "  print(\"int\", x)\n"
        "elseif typeof x is Bool then\n"
        "  print(\"bool\", x)\n"
        "else\n"
        "  print(\"nil\")\n"
        "end\n";
    return run_generated(source, "int 1\n", "typeof if");
}


TestResult test_runtime_typeof_match(void)
{
    const char *source =
        "type V = Int32 | Bool | Nil\n"
        "var x: V = true\n"
        "match typeof x with Int32 then\n"
        "  print(\"int\", x)\n"
        "with Bool then\n"
        "  print(\"bool\", x)\n"
        "else\n"
        "  print(\"nil\")\n"
        "end\n";
    return run_generated(source, "bool true\n", "typeof match");
}


TestResult test_runtime_value_match(void)
{
    const char *source =
        "var x: Bool = true\n"
        "match x with true then\n"
        "  print(\"yes\")\n"
        "with false then\n"
        "  print(\"no\")\n"
        "end\n";
    return run_generated(source, "yes\n", "value match");
}


TestResult test_runtime_if_match_expressions(void)
{
    const char *source =
        "type V = Int32 | Bool\n"
        "var x: V = 1\n"
        "var y: Bool = true\n"
        "var a: Int32 = if typeof x is Int32 then 10 else 20 end\n"
        "var b: Int32 = if y == true then 1 else 0 end\n"
        "var c: Int32 = match y with true then 5 with false then 6 else 7 end\n"
        "var d: Int32 = match typeof x with Int32 then 8 with Bool then 9 else 0 end\n"
        "print(a, b, c, d)\n";
    return run_generated(source, "10 1 5 8\n", "if/match expressions");
}



TestResult test_runtime_const(void)
{
    const char *source =
        "const x: Int32 = 3\n"
        "const y = 4\n"
        "print(x + y)\n"
        "fun f(): Int32\n"
        "  const z: Int32 = 5\n"
        "  return z\n"
        "end\n"
        "print(f())\n"
        "global arena: Store = Store(64)\n"
        "const xs: List<Int32> = arena.list<Int32>(2)\n"
        "xs.push(1)\n"
        "print(xs.len())\n";
    return run_generated(source, "7\n5\n1\n", "const bindings");
}



TestResult test_runtime_operators(void)
{
    const char *source =
        "print(7 - 2, 3 * 4, 8 / 3, 8 % 3, -5, ~1)\n"
        "print(1 << 4, 16 >> 2, 6 & 3, 6 | 3, 6 ^ 3)\n"
        "print(!true, true && false, true || false)\n";
    return run_generated(source, "5 12 2 2 -5 -2\n16 4 2 7 5\nfalse false true\n",
                         "operators");
}


TestResult test_runtime_exhaustive_match(void)
{
    const char *source =
        "type V = Int32 | Bool | Nil\n"
        "var x: V = true\n"
        "match typeof x with Int32 then\n"
        "  print(\"int\")\n"
        "with Bool then\n"
        "  print(\"bool\")\n"
        "with Nil then\n"
        "  print(\"nil\")\n"
        "end\n"
        "var b: Bool = false\n"
        "match b with true then\n"
        "  print(\"yes\")\n"
        "with false then\n"
        "  print(\"no\")\n"
        "end\n";
    return run_generated(source, "bool\nno\n", "exhaustive match");
}



TestResult test_runtime_divide_by_zero(void)
{
    const char *source =
        "var z: Int32 = 0\n"
        "print(1 / z)\n";
    return run_generated_failure(source, "division by zero",
                                 "runtime division by zero");
}

TestResult test_runtime_top_level_index_assignment(void)
{
    const char *source =
        "var xs: Array<Int32, 2> = [1, 2]\n"
        "xs[0] = 3\n"
        "print(xs[0])\n";

    return run_generated(source, "3\n", "top-level index assignment");
}

TestResult test_runtime_top_level_field_assignment(void)
{
    const char *source =
        "type Point\n"
        "  x: Int32\n"
        "  y: Int32\n"
        "end\n"
        "var p: Point = Point(1, 2)\n"
        "p.x = 3\n"
        "print(p.x)\n";

    return run_generated(source, "3\n", "top-level field assignment");
}

TestResult test_runtime_recursion_guard(void)
{
    const char *source =
        "fun down(n: Int32): Int32\n"
        "  if n == -1 then\n"
        "    return 0\n"
        "  end\n"
        "  return down(n + 1)\n"
        "end\n"
        "var n: Int32 = down(1)\n";

    return run_generated_failure(source, MESSAGE_RUNTIME_RECURSION,
                                 "recursion depth guard");
}

const TestCase runtime_language_test_tests[] = {
    {"runtime: union narrowing", test_runtime_union_narrowing},
    {"runtime: generic instantiations", test_runtime_generic_instantiations},
    {"runtime: locals and fields", test_runtime_locals_and_fields},
    {"runtime: for index", test_runtime_for_index},
    {"runtime: for mutation", test_runtime_for_mutation},
    {"runtime: typeof if", test_runtime_typeof_if},
    {"runtime: typeof match", test_runtime_typeof_match},
    {"runtime: value match", test_runtime_value_match},
    {"runtime: if/match expressions", test_runtime_if_match_expressions},
    {"runtime: const bindings", test_runtime_const},
    {"runtime: operators", test_runtime_operators},
    {"runtime: recursion guard", test_runtime_recursion_guard},
    {"runtime: divide by zero", test_runtime_divide_by_zero},
    {"runtime: exhaustive match", test_runtime_exhaustive_match},
    {"runtime: nested structs", test_runtime_nested_structs},
    {"runtime: top-level assignment", test_runtime_assignment_top},
    {"runtime: top-level index assignment",
     test_runtime_top_level_index_assignment},
    {"runtime: top-level field assignment",
     test_runtime_top_level_field_assignment},
    {"runtime: parameter assignment", test_runtime_assignment_parameter},
    {"runtime: assignment narrowing", test_runtime_assignment_union_narrowing},
    {"runtime: assignment in a loop", test_runtime_assignment_loop},
    {"runtime: repeated assignment", test_runtime_assignment_repeated},
    {"runtime: generic assignment", test_runtime_assignment_generic},
    {"runtime: inferred assignment", test_runtime_assignment_inferred},
    {"runtime: nested narrowing invalidation",
     test_runtime_assignment_nested_narrowing},
    {"runtime: union assignment", test_runtime_assignment_union},
};
const size_t runtime_language_test_test_count =
    sizeof(runtime_language_test_tests) / sizeof(runtime_language_test_tests[0]);
