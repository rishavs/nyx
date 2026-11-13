#include "tool_support.h"



TestResult test_runtime_string_ops(void)
{
    const char *source =
        "fun check(s: String<16>): Int32\n"
        "  if s == \"abc\" then\n"
        "    return s.byte_len()\n"
        "  end\n"
        "  return 0\n"
        "end\n"
        "var n: Int32 = check(\"abc\")\n";

    return run_generated(source, "3\n", "string length and equality");
}


TestResult test_runtime_string_bounds(void)
{
    const char *source =
        "fun bad(s: String<4>, i: Int32): Byte\n"
        "  return s.byte_at(i)\n"
        "end\n"
        "var b: Byte = bad(\"abc\", 5)\n";

    return run_generated_failure(source, MESSAGE_RUNTIME_STRING_BOUNDS,
                                 "string bounds");
}


TestResult test_runtime_string_union(void)
{
    const char *source =
        "fun len_or_zero(u: String<16> | Nil): Int32\n"
        "  if typeof u is String<16> then\n"
        "    return u.byte_len()\n"
        "  end\n"
        "  return 0\n"
        "end\n"
        "var n: Int32 = len_or_zero(\"hey\")\n";

    return run_generated(source, "3\n", "string union narrowing");
}


TestResult test_runtime_error_fields(void)
{
    const char *source =
        "fun describe(e: Error | Nil): Int32\n"
        "  if typeof e is Error then\n"
        "    return e.line()\n"
        "  end\n"
        "  return 0\n"
        "end\n"
        "var n: Int32 = describe("
        "Error(\"E\", \"d\", \"f\", 7, 2, 3))\n";

    return run_generated(source, "7\n", "Error fields");
}


TestResult test_runtime_assignment_string(void)
{
    const char *source =
        "fun replace(s: String<8>): String<8>\n"
        "  s = \"bye\"\n"
        "  return s\n"
        "end\n"
        "var n: Int32 = replace(\"hi\").byte_len()\n";

    return run_generated(source, "3\n", "string assignment");
}


TestResult test_runtime_assignment_error(void)
{
    const char *source =
        "fun line_after(e: Error): Int32\n"
        "  e = Error(\"New\", \"d\", \"f\", 42, 2, 3)\n"
        "  return e.line()\n"
        "end\n"
        "var n: Int32 = line_after("
        "Error(\"Old\", \"d\", \"f\", 1, 2, 3))\n";

    return run_generated(source, "42\n", "Error assignment");
}



TestResult test_runtime_dynamic_strings(void)
{
    const char *source =
        "global arena: Store = Store(256)\n"
        "fun len(s: Text): Int32\n"
        "  return s.byte_len()\n"
        "end\n"
        "fun fixed_len(f: String<8>): Int32\n"
        "  return arena.text(f).byte_len()\n"
        "end\n"
        "fun is_world(s: Text): Int32\n"
        "  if s.slice(6, 5) == \"world\" then\n"
        "    return 1\n"
        "  end\n"
        "  return 0\n"
        "end\n"
        "var n: Int32 = len(\"hello\") +\n"
        "  len(arena.concat(\"hello\", \" world\")) +\n"
        "  fixed_len(\"hi\") +\n"
        "  is_world(\"hello world\") +\n"
        "  len(arena.copy(\"hi\"))\n";

    return run_generated(source, "21\n", "dynamic strings");
}


TestResult test_runtime_dynamic_string_stale(void)
{
    const char *source =
        "global arena: Store = Store(16)\n"
        "var s: Text = arena.copy(\"hi\")\n"
        "arena.reset()\n"
        "var b: Byte = s.byte_at(0)\n";

    return run_generated_failure(source, MESSAGE_RUNTIME_STALE_HANDLE,
                                 "stale dynamic string");
}



TestResult test_runtime_dynamic_string_bounds(void)
{
    const char *source =
        "fun at(s: Text): Byte\n"
        "  return s.byte_at(10)\n"
        "end\n"
        "var b: Byte = at(\"hi\")\n";

    return run_generated_failure(source, MESSAGE_RUNTIME_STRING_BOUNDS,
                                 "dynamic string bounds");
}


TestResult test_runtime_dynamic_string_stale_len(void)
{
    const char *source =
        "global arena: Store = Store(16)\n"
        "var s: Text = arena.copy(\"hi\")\n"
        "arena.reset()\n"
        "var n: Int32 = s.byte_len()\n";

    return run_generated_failure(source, MESSAGE_RUNTIME_STALE_HANDLE,
                                 "stale dynamic string length");
}



TestResult test_runtime_dynamic_string_union(void)
{
    const char *source =
        "fun len_or_zero(u: Text | Nil): Int32\n"
        "  if typeof u is Text then\n"
        "    return u.byte_len()\n"
        "  end\n"
        "  return 0\n"
        "end\n"
        "var n: Int32 = len_or_zero(\"hi\")\n";

    return run_generated(source, "2\n", "dynamic Text union");
}



TestResult test_runtime_string_builder(void)
{
    const char *source =
        "global arena: Store = Store(64)\n"
        "fun fill(b: TextBuilder): Int32\n"
        "  b.append(\"hello\")\n"
        "  b.append_byte(44)\n"
        "  b.append(\" world\")\n"
        "  return b.byte_len() + b.finish().byte_len()\n"
        "end\n"
        "var n: Int32 = fill(arena.builder(2))\n";

    return run_generated(source, "24\n", "TextBuilder");
}


TestResult test_runtime_string_builder_stale(void)
{
    const char *source =
        "global arena: Store = Store(64)\n"
        "var b: TextBuilder = arena.builder(4)\n"
        "b.append(\"hi\")\n"
        "arena.reset()\n"
        "b.append(\"x\")\n";

    return run_generated_failure(source, MESSAGE_RUNTIME_STALE_HANDLE,
                                 "stale TextBuilder");
}



TestResult test_runtime_string_builder_content(void)
{
    const char *source =
        "global arena: Store = Store(64)\n"
        "fun content(b: TextBuilder): Int32\n"
        "  b.append(\"hello\")\n"
        "  b.append_byte(44)\n"
        "  b.append(\" world\")\n"
        "  if b.finish() == \"hello, world\" then\n"
        "    return 1\n"
        "  end\n"
        "  return 0\n"
        "end\n"
        "var n: Int32 = content(arena.builder(1))\n";

    return run_generated(source, "1\n", "TextBuilder content");
}



TestResult test_runtime_string_builder_reuse(void)
{
    const char *source =
        "global arena: Store = Store(64)\n"
        "fun reuse(b: TextBuilder): Int32\n"
        "  b.append(\"hi\")\n"
        "  if b.finish() == \"hi\" then\n"
        "    b.append(\"!\")\n"
        "    if b.finish() == \"hi!\" then\n"
        "      return 1\n"
        "    end\n"
        "  end\n"
        "  return 0\n"
        "end\n"
        "var n: Int32 = reuse(arena.builder(2))\n";

    return run_generated(source, "1\n", "TextBuilder reuse after finish");
}


TestResult test_runtime_fixed_list_strings(void)
{
    const char *source =
        "fun count(xs: Array<String<8>, 2>): Int32\n"
        "  if xs[0] == \"a\" then\n"
        "    return xs.len()\n"
        "  end\n"
        "  return 0\n"
        "end\n"
        "var n: Int32 = count([\"a\", \"b\"])\n";

    return run_generated(source, "2\n", "fixed list of strings");
}


TestResult test_runtime_error_field_access(void)
{
    const char *source =
        "fun line_of(e: Error): Int32\n"
        "  return e.line\n"
        "end\n"
        "var n: Int32 = line_of(Error(\"E\", \"d\", \"f\", 7, 2, 3))\n";

    return run_generated(source, "7\n", "Error field access");
}


TestResult test_runtime_for_fixed_string(void)
{
    const char *source =
        "fun count(): Int32\n"
        "  var s: String<8> = \"hello\"\n"
        "  var n: Int32 = 0\n"
        "  for b in s do\n"
        "    n = n + 1\n"
        "  end\n"
        "  return n\n"
        "end\n"
        "var n: Int32 = count()\n";

    return run_generated(source, "5\n", "for over a fixed string");
}


TestResult test_runtime_for_fixed_strings(void)
{
    const char *source =
        "fun sum(): Int32\n"
        "  var names: Array<String<8>, 3> = [\"ada\", \"grace\", \"linus\"]\n"
        "  var total: Int32 = 0\n"
        "  for name in names do\n"
        "    total = total + name.byte_len()\n"
        "  end\n"
        "  return total\n"
        "end\n"
        "var n: Int32 = sum()\n";

    return run_generated(source, "13\n", "for over fixed strings");
}


TestResult test_runtime_for_arena_strings(void)
{
    const char *source =
        "global arena: Store = Store(256)\n"
        "fun sum(): Int32\n"
        "  var a: String<8> = \"ada\"\n"
        "  var b: String<8> = \"grace\"\n"
        "  var c: String<8> = \"linus\"\n"
        "  var names: List<Text> = arena.list<Text>(3)\n"
        "  names.push(arena.text(a))\n"
        "  names.push(arena.text(b))\n"
        "  names.push(arena.text(c))\n"
        "  var total: Int32 = 0\n"
        "  for name in names do\n"
        "    total = total + name.byte_len()\n"
        "  end\n"
        "  return total\n"
        "end\n"
        "var n: Int32 = sum()\n";

    return run_generated(source, "13\n", "for over arena strings");
}



TestResult test_runtime_error_throw(void)
{
    const char *source =
        "var e: Error = Error.throw(\"E\", \"desc\")\n"
        "print(e)\n";
    const char *expected =
        "Error(E, desc, test.nip, 1, 22, 21)\n";

    return run_generated(source, expected, "Error.throw");
}

const TestCase runtime_string_test_tests[] = {
    {"runtime: string ops", test_runtime_string_ops},
    {"runtime: string bounds", test_runtime_string_bounds},
    {"runtime: string union", test_runtime_string_union},
    {"runtime: Error fields", test_runtime_error_fields},
    {"runtime: dynamic strings", test_runtime_dynamic_strings},
    {"runtime: stale dynamic string", test_runtime_dynamic_string_stale},
    {"runtime: dynamic string bounds", test_runtime_dynamic_string_bounds},
    {"runtime: dynamic Text union", test_runtime_dynamic_string_union},
    {"runtime: TextBuilder", test_runtime_string_builder},
    {"runtime: TextBuilder content", test_runtime_string_builder_content},
    {"runtime: TextBuilder reuse", test_runtime_string_builder_reuse},
    {"runtime: fixed list of strings", test_runtime_fixed_list_strings},
    {"runtime: for fixed string", test_runtime_for_fixed_string},
    {"runtime: for fixed strings", test_runtime_for_fixed_strings},
    {"runtime: for arena strings", test_runtime_for_arena_strings},
    {"runtime: Error.throw", test_runtime_error_throw},
    {"runtime: Error fields", test_runtime_error_fields},
    {"runtime: stale TextBuilder", test_runtime_string_builder_stale},
    {"runtime: stale dynamic string length", test_runtime_dynamic_string_stale_len},
    {"runtime: string assignment", test_runtime_assignment_string},
    {"runtime: Error assignment", test_runtime_assignment_error},
};
const size_t runtime_string_test_test_count =
    sizeof(runtime_string_test_tests) / sizeof(runtime_string_test_tests[0]);
