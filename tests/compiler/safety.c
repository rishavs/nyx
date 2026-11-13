/* Compile-time safety checks: constant folding, const, bounds, and unreachable
   code. */

#include "compile_support.h"

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
    return compile_fails("print(2147483647 + 1)\n",
                         "constant expression overflow",
                         MESSAGE_CHECKER_CONST_OVERFLOW);
}

TEST("constant shift")
{
    return compile_fails("print(1 << 40)\n", "constant shift range",
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
        "fun f(): Nil\n"
        "  while true do\n"
        "    break\n"
        "    print(1)\n"
        "  end\n"
        "  return nil\n"
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
        (size_t)82020, (size_t)NIPPY_MAX_VALUE_BYTES);
}

TEST("local inference contexts")
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

TEST("parenthesized type rparen")
{
    return compile_fails("var x: (Int32 = 1\n", "missing type rparen",
                         MESSAGE_PARSER_EXPECTED_RPAREN);
}
