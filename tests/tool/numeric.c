/* Run-time behaviour of numeric types, literals, conversions, and base64. */

#include "tool_support.h"

TEST("runtime error follows prior output")
{
    char expected[256];
    TestResult result = run_generated_failure(
        "var zero: Int32 = 0\nprint(\"before\")\nprint(1 / zero)\n",
        "division by zero",
        "runtime error ordering and prefix");
    if (result != TEST_PASS) return result;
    snprintf(expected, sizeof(expected), "before\n" MESSAGE_RUNTIME_POSITION "\n",
             "test.nip", (size_t)3, (size_t)9, (size_t)44, "division by zero");
    if (!file_equals(test_path("runtime.log"), expected)) {
        fprintf(stderr, "  runtime output preceded earlier stdout\n");
        return TEST_FAIL;
    }
    return TEST_PASS;
}

TEST("strict equality")
{
    const char *source =
        "var a: Int32 = 3\n"
        "var b: Int64 = 3\n"
        "var c: Int32 = 3\n"
        "var s: String<4> = \"hi\"\n"
        "var t: String<8> = \"hi\"\n"
        "var text: Text = \"hi\"\n"
        "print(a == b, a === b, a === c, a === 3)\n"
        "print(3 === 3.0, true === 1, s === t, s === \"hi\")\n"
        "print(text === \"hi\", text === s, !(a === b))\n"
        "global arena: Store = Store(16)\n"
        "fun first(h: Handle<Int32>): Int32\n"
        "  h.write(h.read() + 1) return h.read() end\n"
        "fun second(h: Handle<Int32>): Int64\n"
        "  h.write(h.read() + 1) return Int64(h.read()) end\n"
        "var h = arena.alloc<Int32>(0)\n"
        "print(first(h) === second(h), h.read())\n";

    return run_generated(source,
                         "true false true true\n"
                         "false false false true\n"
                         "true false true\nfalse 2\n",
                         "strict equality");
}

TEST("exact mixed numeric equality")
{
    const char *source =
        "var s: Int64 = -1\n"
        "var u: Uint64 = 18446744073709551615\n"
        "var low: Int64 = -9223372036854775808\n"
        "var big: Int64 = 9007199254740993\n"
        "var exact: Int64 = 9007199254740992\n"
        "var f: Float64 = 9007199254740992.0\n"
        "var max_float: Float64 = 18446744073709551616.0\n"
        "var min_float: Float64 = -9223372036854775808.0\n"
        "var small: Float32 = 3.0\n"
        "var fraction: Float64 = 3.5\n"
        "var zero: Float64 = -0.0\n"
        "var huge: Float64 = 1e308\n"
        "var infinity: Float64 = huge * 2.0\n"
        "print(Uint16(3) == Int64(3), Int64(3) != Uint16(3))\n"
        "print(s == u, u == s, s != u)\n"
        "print(big == f, f == big, exact == f, f != exact)\n"
        "print(u == max_float, low == min_float, min_float == low)\n"
        "print(small == Int64(3), Int64(3) == small, fraction == 3)\n"
        "print(zero == 0, infinity == u, u != infinity)\n";

    return run_generated(source,
                         "true false\nfalse false true\n"
                         "false false true false\nfalse true true\n"
                         "true true false\ntrue false true\n",
                         "exact mixed numeric equality");
}

/* Runs one conversion that must trap and checks the shared diagnostic. */
static TestResult conversion_traps(const char *declaration,
                                   const char *expression, const char *what)
{
    char source[512];

    snprintf(source, sizeof(source), "%s\nprint(%s)\n", declaration,
             expression);
    return run_generated_failure(source, MESSAGE_RUNTIME_CONVERSION + 7, what);
}

TEST("numeric literals")
{
    const char *source =
        "var a: Int8 = 1\n"
        "var b: Int16 = 2\n"
        "var c: Int32 = 3\n"
        "var d: Int64 = 4\n"
        "var e: Uint8 = 5\n"
        "var f: Uint16 = 6\n"
        "var g: Uint32 = 7\n"
        "var h: Uint64 = 8\n"
        "var i: Float32 = 1.5\n"
        "var j: Float64 = 2.25\n"
        "print(a, b, c, d, e, f, g, h, i, j)\n";

    return run_generated(source, "1 2 3 4 5 6 7 8 1.5 2.25\n",
                         "numeric literals");
}

TEST("floating point display")
{
    const char *source =
        "print(1234567.0, 0.000001, 0.0000001, 10000000000000000.0, -0.0)\n"
        "var a: Float32 = 0.1\n"
        "var b: Float64 = 1.2345678901234567\n"
        "print(a, b)\n";

    return run_generated(source,
                         "1234567.0 0.000001 1e-7 1e+16 -0.0\n"
                         "0.1 1.2345678901234567\n",
                         "floating point display");
}

TEST("numeric operators")
{
    const char *source =
        "var a: Int8 = 7\n"
        "var b: Int8 = 2\n"
        "var c: Int64 = 9223372036854775807\n"
        "var d: Uint64 = 18446744073709551615\n"
        "var e: Uint8 = 200\n"
        "var f: Float64 = 5.0\n"
        "var g: Float64 = 2.0\n"
        "var h: Float32 = 1.5\n"
        "print(a / b, a % b, a << 2, a >> 1)\n"
        "print(c >> 1)\n"
        "print(d >> 1)\n"
        "print(e + 100)\n"
        "print(f / g, f > g, f == 5.0)\n"
        "print(h + 0.5)\n"
        "print(-h)\n"
        "print(~a)\n";

    return run_generated(
        source,
        "3 1 28 3\n"
        "4611686018427387903\n"
        "9223372036854775807\n"
        "44\n"
        "2.5 true true\n"
        "2.0\n"
        "-1.5\n"
        "-8\n",
        "numeric operators");
}

TEST("numeric conversions")
{
    const char *source =
        "var a: Int32 = 100\n"
        "var b: Int8 = Int8(a)\n"
        "var c: Uint8 = Uint8(a)\n"
        "var d: Int64 = Int64(9223372036854775807)\n"
        "var e: Float64 = Float64(3)\n"
        "var f: Float32 = Float32(1.5)\n"
        "var g: Int32 = Int32(2.9)\n"
        "var h: Int32 = Int32(-2.9)\n"
        "var wide: Int64 = Int64(a)\n"
        "var narrow: Int16 = Int16(wide)\n"
        "print(b, c, d, e, f, g, h, wide, narrow)\n";

    return run_generated(
        source, "100 100 9223372036854775807 3.0 1.5 2 -2 100 100\n",
        "numeric conversions");
}

TEST("numeric conversion trap")
{
    const char *source =
        "var f: Float64 = 128.5\n"
        "var x: Int8 = Int8(f)\n"
        "print(x)\n";

    TestResult result = run_generated_failure(source, MESSAGE_RUNTIME_CONVERSION + 7,
                                              "numeric conversion range");
    if (result != TEST_PASS) return result;
    return file_contains(test_path("runtime.log"),
                         "nippy: test.nip:2:15 (byte 37):")
               ? TEST_PASS : TEST_FAIL;
}

TEST("conversion trap, narrowing")
{
    return conversion_traps("var x: Int32 = 128", "Int8(x)",
                            "Int32 above Int8");
}

TEST("conversion trap, negative to unsigned")
{
    return conversion_traps("var x: Int32 = -1", "Uint32(x)",
                            "negative to Uint32");
}

TEST("conversion trap, negative to wider unsigned")
{
    return conversion_traps("var x: Int8 = -1", "Uint64(x)",
                            "negative Int8 to Uint64");
}

TEST("conversion trap, unsigned to signed")
{
    return conversion_traps("var x: Uint32 = 4294967295", "Int32(x)",
                            "Uint32 above Int32");
}

TEST("conversion trap, Uint64 to Int64")
{
    return conversion_traps("var x: Uint64 = 9223372036854775808",
                            "Int64(x)", "Uint64 above Int64");
}

TEST("conversion trap, Float32 overflow")
{
    return conversion_traps("var x: Float64 = 1e300", "Float32(x)",
                            "Float64 beyond Float32");
}

TEST("conversion trap, float below range")
{
    return conversion_traps("var x: Float64 = -129.0", "Int8(x)",
                            "float below Int8");
}

TEST("conversion trap, negative float to unsigned")
{
    return conversion_traps("var x: Float64 = -1.0", "Uint8(x)",
                            "negative float to Uint8");
}

TEST("conversion trap, NaN")
{
    return conversion_traps("var z: Float64 = 0.0\nvar x: Float64 = z / z",
                            "Int32(x)", "NaN to Int32");
}

TEST("conversion trap, float past Int64")
{
    /* 9223372036854775807.0 rounds up to 2^63, one past Int64's maximum. */
    return conversion_traps("var x: Float64 = 9223372036854775807.0",
                            "Int64(x)", "float rounded past Int64");
}

TEST("conversion boundaries")
{
    const char *source =
        "var a: Int32 = 127\n"
        "var b: Int64 = 4294967295\n"
        "var c: Uint64 = 9223372036854775807\n"
        "var d: Uint8 = 255\n"
        "var e: Float64 = -128.9\n"
        "var f: Float64 = -0.5\n"
        "var g: Float64 = 255.9\n"
        "var h: Float64 = 1.0e19\n"
        "var i: Int8 = -128\n"
        "print(Int8(a), Uint32(b), Int64(c), Int16(d))\n"
        "print(Int8(e), Uint8(f), Uint8(g), Uint64(h))\n"
        "print(Int64(i), Float32(0.5))\n";

    return run_generated(
        source,
        "127 4294967295 9223372036854775807 255\n"
        "-128 0 255 10000000000000000000\n"
        "-128 0.5\n",
        "conversion boundaries");
}

TEST("narrow arithmetic")
{
    /* C promotes narrow operands to int. These products and complements would
       overflow int or come out negative if computed there. */
    const char *source =
        "var a: Uint16 = 65535\n"
        "var b: Uint8 = 250\n"
        "var c: Int16 = 32767\n"
        "var d: Int16 = -32768\n"
        "var e: Int8 = -128\n"
        "var f: Uint8 = 255\n"
        "print(a * a, c * c, d * d, c + c, d - c)\n"
        "print(~b, ~b == 5, ~a, ~e, -e, -d)\n"
        "print(f * f, f + f, a << 15, a >> 15, e >> 7)\n";

    return run_generated(
        source,
        "1 1 0 -2 1\n"
        "5 true 0 127 -128 -32768\n"
        "1 254 32768 1 1\n",
        "narrow arithmetic");
}

TEST("wide arithmetic")
{
    const char *source =
        "var u: Uint32 = 4294967295\n"
        "var i: Int64 = 9223372036854775807\n"
        "var w: Uint64 = 18446744073709551615\n"
        "print(u * u, u + u, ~u)\n"
        "print(i + i, i * i, -i, w * w, w + 1)\n"
        "print(w / 3, i % 1000, w >> 63)\n";

    return run_generated(
        source,
        "1 4294967294 0\n"
        "-2 1 -9223372036854775807 1 0\n"
        "6148914691236517205 807 1\n",
        "wide arithmetic");
}

TEST("negated literal operands")
{
    const char *source =
        "var f: Float32 = -1.5\n"
        "var x: Int8 = 5\n"
        "var y: Int64 = -5\n"
        "print(f + -0.5, x + -1, x < -5, y == -5, y * -2, -1 + y)\n"
        "print(-3000000000 + 1, -1 + -2)\n";

    return run_generated(source,
                         "-2.0 4 false true 10 -6\n"
                         "-2999999999 -3\n",
                         "negated literal operands");
}

TEST("numeric literals in unions")
{
    const char *source =
        "var a: Int8 | Nil = -5\n"
        "var b: Float32 | Nil = 2.5\n"
        "var c: Int64 | Nil = 5000000000\n"
        "var d: Uint8 | Float64 = 200\n"
        "print(a, b, c, d)\n"
        "if typeof d is Uint8 then print(1) else print(0) end\n"
        "if typeof d is Float64 then print(1) else print(0) end\n";

    return run_generated(source,
                         "-5 2.5 5000000000 200\n"
                         "1\n"
                         "0\n",
                         "numeric literals in unions");
}

TEST("numeric collections")
{
    const char *source =
        "global arena: Store = Store(128)\n"
        "var xs: Array<Float32, 3> = [1.5, -2.5]\n"
        "var ys: List<Int64> = arena.list<Int64>(2)\n"
        "ys.push(1)\n"
        "ys.push(9000000000)\n"
        "var h: Handle<Uint16> = arena.alloc<Uint16>(65535)\n"
        "var t: Table<String<4>, Float64> = "
        "arena.table<String<4>, Float64>(2)\n"
        "t[\"a\"] = 0.25\n"
        "print(xs, ys, h.read(), t[\"a\"])\n";

    return run_generated(source, "[1.5, -2.5] [1, 9000000000] 65535 0.25\n",
                         "numeric collections");
}

TEST("radix literals")
{
    const char *source =
        "var a: Int32 = 0xFF\n"
        "var b: Int32 = 0b1010_1010\n"
        "var c: Int32 = 0o755\n"
        "var d: Uint64 = 0xFFFF_FFFF_FFFF_FFFF\n"
        "var e: Int64 = 0x7FFF_FFFF_FFFF_FFFF\n"
        "var f: Int8 = -0x80\n"
        "var g: Uint8 = 0xDe\n"
        "print(a, b, c, d, e, f, g)\n"
        "print(0xFFFFFFFF, 0b0, 0o0, 0x0, 007, 0xFF & 0b1111, 0x10 + 0o10)\n";

    return run_generated(
        source,
        "255 170 493 18446744073709551615 9223372036854775807 -128 222\n"
        "4294967295 0 0 0 7 15 24\n",
        "radix literals");
}

TEST("radix literals in types")
{
    /* A radix literal works wherever a decimal one does: capacities and sizes. */
    const char *source =
        "global arena: Store = Store(0x100)\n"
        "var xs: Array<Uint8, 0b100> = [1, 2]\n"
        "var s: String<0x10> = \"hi\"\n"
        "var l: List<Int32> = arena.list<Int32>(0o10)\n"
        "print(xs.len(), s.byte_len())\n";

    return run_generated(source, "2 2\n", "radix literals in types");
}

TEST("digit separators")
{
    const char *source =
        "var a: Int64 = 1_000_000_000_000\n"
        "var b: Float64 = 1_000.5\n"
        "var c: Float64 = 2.5e1_0\n"
        "var d: Int32 = 0b1111_0000 | 0x0F\n"
        "print(a, b, c, d)\n";

    return run_generated(source, "1000000000000 1000.5 25000000000.0 255\n",
                         "digit separators");
}

TEST("base64 text")
{
    const char *source =
        "var hello: Text = base64(\"SGVsbG8sIFdvcmxkIQ==\")\n"
        "print(hello, hello.byte_len())\n"
        "print(base64(\"TQ==\"), base64(\"TWE=\"), base64(\"TWFu\"))\n"
        "print(base64(\"\").byte_len())\n"
        "print(hello == \"Hello, World!\", hello.slice(0, 5))\n";

    return run_generated(source,
                         "Hello, World! 13\n"
                         "M Ma Man\n"
                         "0\n"
                         "true Hello\n",
                         "base64 text");
}

TEST("base64 binary bytes")
{
    /* Bytes that are not text: NUL, a high bit, and the all-ones byte. */
    const char *source =
        "var raw: Text = base64(\"AAECgP8=\")\n"
        "print(raw.byte_len(), raw.byte_at(0), raw.byte_at(1), "
        "raw.byte_at(2), raw.byte_at(3), raw.byte_at(4))\n"
        "for i, b in base64(\"+/+/\") do\n"
        "  print(i, b)\n"
        "end\n";

    return run_generated(source,
                         "5 0 1 2 128 255\n"
                         "0 251\n"
                         "1 255\n"
                         "2 191\n",
                         "base64 binary bytes");
}

TEST("base64 with a store")
{
    const char *source =
        "global arena: Store = Store(256)\n"
        "var copy: Text = arena.copy(base64(\"SGk=\"))\n"
        "var joined: Text = arena.concat(base64(\"SGk=\"), \" there\")\n"
        "var u: Text | Nil = base64(\"SGk=\")\n"
        "var xs: Array<Text, 2> = [base64(\"YQ==\"), base64(\"Yg==\")]\n"
        "print(copy, joined, u, xs[1])\n";

    return run_generated(source, "Hi Hi there Hi b\n", "base64 with a store");
}

TEST("numeric limits")
{
    const char *source =
        "var a: Int64 = -9223372036854775808\n"
        "var b: Uint64 = 18446744073709551615\n"
        "print(a)\n"
        "print(b)\n";

    return run_generated(source,
                         "-9223372036854775808\n18446744073709551615\n",
                         "numeric limits");
}
