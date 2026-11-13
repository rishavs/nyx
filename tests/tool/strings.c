/* Text, String<N>, TextBuilder, and Error at run time. */

#include "tool_support.h"

TEST("string ops")
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

TEST("string bounds")
{
    const char *source =
        "fun bad(s: String<4>, i: Int32): Byte\n"
        "  return s.byte_at(i)\n"
        "end\n"
        "var b: Byte = bad(\"abc\", 5)\n";

    return run_generated_failure(source, MESSAGE_RUNTIME_STRING_BOUNDS,
                                 "string bounds");
}

TEST("string union")
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

TEST("Error fields")
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

TEST("string assignment")
{
    const char *source =
        "fun replace(s: String<8>): String<8>\n"
        "  s = \"bye\"\n"
        "  return s\n"
        "end\n"
        "var n: Int32 = replace(\"hi\").byte_len()\n";

    return run_generated(source, "3\n", "string assignment");
}

TEST("Error assignment")
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

TEST("dynamic strings")
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

TEST("stale dynamic string")
{
    const char *source =
        "global arena: Store = Store(16)\n"
        "var s: Text = arena.copy(\"hi\")\n"
        "arena.reset()\n"
        "var b: Byte = s.byte_at(0)\n";

    return run_generated_failure(source, MESSAGE_RUNTIME_STALE_HANDLE,
                                 "stale dynamic string");
}

TEST("dynamic string bounds")
{
    const char *source =
        "fun at(s: Text): Byte\n"
        "  return s.byte_at(10)\n"
        "end\n"
        "var b: Byte = at(\"hi\")\n";

    return run_generated_failure(source, MESSAGE_RUNTIME_STRING_BOUNDS,
                                 "dynamic string bounds");
}

TEST("stale dynamic string length")
{
    const char *source =
        "global arena: Store = Store(16)\n"
        "var s: Text = arena.copy(\"hi\")\n"
        "arena.reset()\n"
        "var n: Int32 = s.byte_len()\n";

    return run_generated_failure(source, MESSAGE_RUNTIME_STALE_HANDLE,
                                 "stale dynamic string length");
}

TEST("dynamic Text union")
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

TEST("TextBuilder")
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

TEST("stale TextBuilder")
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

TEST("TextBuilder content")
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

TEST("TextBuilder reuse")
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

TEST("fixed list of strings")
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

/* Expected failure. SEMANTICS.md says `e.line` reads a field of an Error, but
   the checker only accepts the method form `e.line()`. This test was defined
   but never registered, so the gap went unnoticed. It reports TODO until the
   field form works, then passes. */
TEST("Error field access")
{
    const char *source =
        "fun line_of(e: Error): Int32\n"
        "  return e.line\n"
        "end\n"
        "var n: Int32 = line_of(Error(\"E\", \"d\", \"f\", 7, 2, 3))\n";

    return run_generated(source, "7\n", "Error field access") == TEST_PASS
               ? TEST_PASS
               : TEST_TODO;
}

TEST("for fixed string")
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

TEST("for fixed strings")
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

TEST("for arena strings")
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

TEST("Error.throw")
{
    const char *source =
        "var e: Error = Error.throw(\"E\", \"desc\")\n"
        "print(e)\n";
    const char *expected =
        "Error(E, desc, test.nip, 1, 22, 21)\n";

    return run_generated(source, expected, "Error.throw");
}
