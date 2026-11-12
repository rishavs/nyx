#include "tool_support.h"



TestResult test_runtime_fixed_list(void)
{
    const char *source =
        "fun score(xs: Array<Int32, 3>): Int32\n"
        "  return xs[0] + xs[1] + xs[2] + xs.len()\n"
        "end\n"
        "var n: Int32 = score([1, 2, 3])\n";

    return run_generated(source, "9\n", "fixed list");
}


TestResult test_runtime_dynamic_list(void)
{
    const char *source =
        "global arena: Store = Store(64)\n"
        "fun fill(xs: List<Int32>): Int32\n"
        "  xs.push(1)\n"
        "  xs.push(2)\n"
        "  xs[0] = 10\n"
        "  return xs[0] + xs[1] + xs.len()\n"
        "end\n"
        "var n: Int32 = fill(arena.list<Int32>(1))\n";

    return run_generated(source, "14\n", "dynamic list");
}


TestResult test_runtime_list_bounds(void)
{
    const char *source =
        "fun at(xs: Array<Int32, 1>): Int32\n"
        "  return xs[1]\n"
        "end\n"
        "var n: Int32 = at([1])\n";

    return run_generated_failure(source, MESSAGE_RUNTIME_LIST_BOUNDS,
                                 "fixed list bounds");
}


TestResult test_runtime_list_stale(void)
{
    const char *source =
        "global arena: Store = Store(64)\n"
        "var xs: List<Int32> = arena.list<Int32>(1)\n"
        "xs.push(1)\n"
        "arena.reset()\n"
        "xs.push(2)\n";

    return run_generated_failure(source, MESSAGE_RUNTIME_STALE_HANDLE,
                                 "stale dynamic list");
}



TestResult test_runtime_fixed_list_zero(void)
{
    const char *source =
        "fun count(xs: Array<Int32, 0>): Int32\n"
        "  return xs.len()\n"
        "end\n"
        "var n: Int32 = count([])\n";

    return run_generated(source, "0\n", "zero-length fixed list");
}



TestResult test_runtime_two_dynamic_lists(void)
{
    const char *source =
        "global arena: Store = Store(64)\n"
        "fun sizes(a: List<Int32>, b: List<Bool>): Int32\n"
        "  a.push(1)\n"
        "  b.push(true)\n"
        "  return a.len() + b.len()\n"
        "end\n"
        "var n: Int32 = sizes(arena.list<Int32>(1), arena.list<Bool>(1))\n";

    return run_generated(source, "2\n", "two dynamic list element types");
}



TestResult test_runtime_inferred_list(void)
{
    const char *source =
        "var n: Int32 = [1, 2, 3][0] + [1, 2, 3].len()\n";

    return run_generated(source, "4\n", "inferred list literal");
}



TestResult test_runtime_fixed_list_mutation(void)
{
    const char *source =
        "fun ops(xs: Array<Int32, 3>): Int32\n"
        "  xs.push(2)\n"
        "  xs.push(3)\n"
        "  xs[0] = 10\n"
        "  return xs.len() + xs[0]\n"
        "end\n"
        "var n: Int32 = ops([1])\n";

    return run_generated(source, "13\n", "fixed list push/index");
}


TestResult test_runtime_dynamic_list_mutation(void)
{
    const char *source =
        "global arena: Store = Store(64)\n"
        "fun ops(xs: List<Int32>): Int32\n"
        "  xs.push(1)\n"
        "  xs.push(2)\n"
        "  xs[0] = 10\n"
        "  return xs.len() + xs[0]\n"
        "end\n"
        "var n: Int32 = ops(arena.list<Int32>(1))\n";

    return run_generated(source, "12\n", "dynamic list push/index");
}



TestResult test_runtime_list_pop_value(void)
{
    const char *source =
        "fun unwrap(v: Int32 | Nil): Int32\n"
        "  if typeof v is Int32 then\n"
        "    return v\n"
        "  end\n"
        "  return 0\n"
        "end\n"
        "fun pop_value(xs: Array<Int32, 3>): Int32\n"
        "  return unwrap(xs.pop())\n"
        "end\n"
        "var n: Int32 = pop_value([1, 2, 3])\n";

    return run_generated(source, "3\n", "list pop value");
}


TestResult test_runtime_list_pop_empty(void)
{
    const char *source =
        "fun unwrap(v: Int32 | Nil): Int32\n"
        "  if typeof v is Int32 then\n"
        "    return v\n"
        "  end\n"
        "  return 0\n"
        "end\n"
        "fun empty(xs: Array<Int32, 1>): Int32\n"
        "  return unwrap(xs.pop())\n"
        "end\n"
        "var n: Int32 = empty([])\n";

    return run_generated(source, "0\n", "empty list pop");
}


TestResult test_runtime_fixed_list_full(void)
{
    const char *source =
        "fun full(xs: Array<Int32, 1>): Int32\n"
        "  xs.push(1)\n"
        "  xs.push(2)\n"
        "  return xs.len()\n"
        "end\n"
        "var n: Int32 = full([])\n";

    return run_generated_failure(source, MESSAGE_RUNTIME_LIST_BOUNDS,
                                 "fixed list capacity");
}



TestResult test_runtime_list_union_and_function(void)
{
    const char *source =
        "global arena: Store = Store(64)\n"
        "fun inc(x: Int32): Int32\n"
        "  return x + 1\n"
        "end\n"
        "fun count(xs: Array<Int32 | Bool, 2>,\n"
        "           fs: List<Fun<(Int32):Int32>>): Int32\n"
        "  fs.push(inc)\n"
        "  return xs.len() + fs.len()\n"
        "end\n"
        "var n: Int32 = count([1, true],\n"
        "  arena.list<Fun<(Int32):Int32>>(1))\n";

    return run_generated(source, "3\n", "list unions and functions");
}



TestResult test_runtime_function_list_pop(void)
{
    const char *source =
        "fun inc(x: Int32): Int32\n"
        "  return x + 1\n"
        "end\n"
        "fun is_fn(v: Fun<(Int32):Int32> | Nil): Int32\n"
        "  if typeof v is Fun<(Int32):Int32> then\n"
        "    return 1\n"
        "  end\n"
        "  return 0\n"
        "end\n"
        "fun pop_fn(xs: Array<Fun<(Int32):Int32>, 1>): Int32\n"
        "  return is_fn(xs.pop())\n"
        "end\n"
        "var n: Int32 = pop_fn([inc])\n";

    return run_generated(source, "1\n", "fixed function list pop");
}


TestResult test_runtime_union_list_pop(void)
{
    const char *source =
        "fun unwrap(v: Int32 | Bool | Nil): Int32\n"
        "  if typeof v is Bool then\n"
        "    return 2\n"
        "  end\n"
        "  return 0\n"
        "end\n"
        "fun pop_u(xs: Array<Int32 | Bool, 2>): Int32\n"
        "  return unwrap(xs.pop())\n"
        "end\n"
        "var n: Int32 = pop_u([1, true])\n";

    return run_generated(source, "2\n", "union list pop");
}



TestResult test_runtime_slice(void)
{
    const char *source =
        "global arena: Store = Store(64)\n"
        "fun first(s: Slice<Int32>): Int32\n"
        "  return s[0] + s.len()\n"
        "end\n"
        "fun make(xs: List<Int32>): Int32\n"
        "  xs.push(10)\n"
        "  xs.push(20)\n"
        "  xs.push(30)\n"
        "  return first(xs.slice(0, 3).slice(1, 2))\n"
        "end\n"
        "var n: Int32 = make(arena.list<Int32>(2))\n";

    return run_generated(source, "22\n", "list slice");
}


TestResult test_runtime_slice_stale(void)
{
    const char *source =
        "global arena: Store = Store(64)\n"
        "var xs: List<Int32> = arena.list<Int32>(1)\n"
        "xs.push(1)\n"
        "var s: Slice<Int32> = xs.slice(0, 1)\n"
        "arena.reset()\n"
        "var n: Int32 = s[0]\n";

    return run_generated_failure(source, MESSAGE_RUNTIME_STALE_HANDLE,
                                 "stale slice");
}



TestResult test_runtime_for_fixed_list(void)
{
    const char *source =
        "fun sum(): Int32\n"
        "  var xs: Array<Int32, 3> = [1, 2, 3]\n"
        "  var total: Int32 = 0\n"
        "  for x in xs do\n"
        "    total = total + x\n"
        "  end\n"
        "  return total\n"
        "end\n"
        "var n: Int32 = sum()\n";

    return run_generated(source, "6\n", "for over a fixed list");
}


TestResult test_runtime_for_dynamic_list(void)
{
    const char *source =
        "global arena: Store = Store(64)\n"
        "fun sum(): Int32\n"
        "  var xs: List<Int32> = arena.list<Int32>(4)\n"
        "  xs.push(1)\n"
        "  xs.push(2)\n"
        "  xs.push(3)\n"
        "  var total: Int32 = 0\n"
        "  for x in xs do\n"
        "    total = total + x\n"
        "  end\n"
        "  return total\n"
        "end\n"
        "var n: Int32 = sum()\n";

    return run_generated(source, "6\n", "for over an arena list");
}


TestResult test_runtime_for_slice(void)
{
    const char *source =
        "global arena: Store = Store(64)\n"
        "fun sum(): Int32\n"
        "  var xs: List<Int32> = arena.list<Int32>(4)\n"
        "  xs.push(10)\n"
        "  xs.push(20)\n"
        "  xs.push(30)\n"
        "  var part: Slice<Int32> = xs.slice(1, 2)\n"
        "  var total: Int32 = 0\n"
        "  for x in part do\n"
        "    total = total + x\n"
        "  end\n"
        "  return total\n"
        "end\n"
        "var n: Int32 = sum()\n";

    return run_generated(source, "50\n", "for over a slice");
}



TestResult test_runtime_record_ops(void)
{
    const char *source =
        "var x: Record<String<8>, Int32, 4> = {\"a\": 1, \"b\": 2}\n"
        "print(x[\"a\"], x.has(\"b\"), x.len())\n"
        "x[\"c\"] = 3\n"
        "print(x.remove(\"a\"), x.has(\"a\"), x[\"b\"], x.len())\n"
        "print(x)\n";
    const char *expected =
        "1 true 2\n"
        "true false 2 2\n"
        "{c: 3, b: 2}\n";

    return run_generated(source, expected, "Record operations");
}


TestResult test_runtime_table_ops(void)
{
    const char *source =
        "global arena: Store = Store(1024)\n"
        "fun run(): Nil\n"
        "  var t: Table<String<8>, Int32> = "
        "arena.table<String<8>, Int32>(1)\n"
        "  t[\"a\"] = 1\n"
        "  t[\"b\"] = 2\n"
        "  t[\"c\"] = 3\n"
        "  print(t[\"a\"], t[\"b\"], t[\"c\"], t.has(\"z\"), t.len())\n"
        "  print(t.remove(\"a\"), t.len())\n"
        "  return nil\n"
        "end\n"
        "run()\n";
    const char *expected =
        "1 2 3 false 3\n"
        "true 2\n";

    return run_generated(source, expected, "Table operations");
}


TestResult test_runtime_record_full(void)
{
    const char *source =
        "var x: Record<String<8>, Int32, 1> = {\"a\": 1}\n"
        "x[\"b\"] = 2\n";

    return run_generated_failure(source, MESSAGE_RUNTIME_RECORD_FULL,
                                 "Record full");
}


TestResult test_runtime_table_missing(void)
{
    const char *source =
        "global arena: Store = Store(64)\n"
        "var t: Table<String<8>, Int32> = "
        "arena.table<String<8>, Int32>(2)\n"
        "var n: Int32 = t[\"z\"]\n";

    return run_generated_failure(source, MESSAGE_RUNTIME_MAP_MISSING,
                                 "Table missing key");
}


TestResult test_runtime_table_stale(void)
{
    const char *source =
        "global arena: Store = Store(64)\n"
        "var t: Table<String<8>, Int32> = "
        "arena.table<String<8>, Int32>(2)\n"
        "arena.reset()\n"
        "var n: Int32 = t[\"a\"]\n";

    return run_generated_failure(source, MESSAGE_RUNTIME_STALE_HANDLE,
                                 "stale Table");
}



TestResult test_runtime_table_print(void)
{
    const char *source =
        "global arena: Store = Store(64)\n"
        "fun run(): Nil\n"
        "  var t: Table<String<8>, Int32> = "
        "arena.table<String<8>, Int32>(1)\n"
        "  t[\"a\"] = 1\n"
        "  print(t)\n"
        "  return nil\n"
        "end\n"
        "run()\n";
    return run_generated(source, "{a: 1}\n", "Table print");
}



TestResult test_runtime_record_for(void)
{
    const char *source =
        "var r: Record<String<8>, Int32, 4> = {\"a\": 1, \"b\": 2, \"c\": 3}\n"
        "var sum: Int32 = 0\n"
        "var count: Int32 = 0\n"
        "for k, v in r do\n"
        "  sum = sum + v\n"
        "  count = count + 1\n"
        "end\n"
        "for k in r do\n"
        "  count = count + 1\n"
        "end\n"
        "print(sum, count)\n";
    return run_generated(source, "6 6\n", "Record iteration");
}


TestResult test_runtime_table_for(void)
{
    const char *source =
        "global arena: Store = Store(256)\n"
        "fun run(): Nil\n"
        "  var t: Table<String<8>, Int32> = "
        "arena.table<String<8>, Int32>(1)\n"
        "  t[\"a\"] = 1\n"
        "  t[\"b\"] = 2\n"
        "  t[\"c\"] = 3\n"
        "  var sum: Int32 = 0\n"
        "  var count: Int32 = 0\n"
        "  for k, v in t do\n"
        "    sum = sum + v\n"
        "    count = count + 1\n"
        "  end\n"
        "  for k in t do\n"
        "    count = count + 1\n"
        "  end\n"
        "  print(sum, count)\n"
        "  return nil\n"
        "end\n"
        "run()\n";
    return run_generated(source, "6 6\n", "Table iteration");
}

const TestCase runtime_collection_test_tests[] = {
    {"runtime: fixed list", test_runtime_fixed_list},
    {"runtime: zero-length fixed list", test_runtime_fixed_list_zero},
    {"runtime: dynamic list", test_runtime_dynamic_list},
    {"runtime: two dynamic list types", test_runtime_two_dynamic_lists},
    {"runtime: inferred list literal", test_runtime_inferred_list},
    {"runtime: for fixed list", test_runtime_for_fixed_list},
    {"runtime: for dynamic list", test_runtime_for_dynamic_list},
    {"runtime: for slice", test_runtime_for_slice},
    {"runtime: Record operations", test_runtime_record_ops},
    {"runtime: Table operations", test_runtime_table_ops},
    {"runtime: Table print", test_runtime_table_print},
    {"runtime: Record iteration", test_runtime_record_for},
    {"runtime: Table iteration", test_runtime_table_for},
    {"runtime: Record full", test_runtime_record_full},
    {"runtime: Table missing", test_runtime_table_missing},
    {"runtime: Table stale", test_runtime_table_stale},
    {"runtime: fixed list mutation", test_runtime_fixed_list_mutation},
    {"runtime: dynamic list mutation", test_runtime_dynamic_list_mutation},
    {"runtime: list pop value", test_runtime_list_pop_value},
    {"runtime: list unions/functions", test_runtime_list_union_and_function},
    {"runtime: function list pop", test_runtime_function_list_pop},
    {"runtime: union list pop", test_runtime_union_list_pop},
    {"runtime: list slice", test_runtime_slice},
    {"runtime: stale slice", test_runtime_slice_stale},
    {"runtime: empty list pop", test_runtime_list_pop_empty},
    {"runtime: fixed list capacity", test_runtime_fixed_list_full},
    {"runtime: list bounds", test_runtime_list_bounds},
    {"runtime: stale list", test_runtime_list_stale},
};
const size_t runtime_collection_test_test_count =
    sizeof(runtime_collection_test_tests) / sizeof(runtime_collection_test_tests[0]);
