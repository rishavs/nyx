/* Stores, handles, and arena run-time behaviour. */

#include "tool_support.h"

TEST("store alloc/read")
{
    const char *source =
        "var s: Store = Store(1)\n"
        "var n: Int32 = s.alloc<Int32>(42).read()\n";

    return run_generated(source, "42\n", "store alloc/read");
}

TEST("store write")
{
    const char *source =
        "var s: Store = Store(1)\n"
        "fun roundtrip(h: Handle<Int32>): Int32\n"
        "  h.write(42)\n"
        "  return h.read()\n"
        "end\n"
        "var n: Int32 = roundtrip(s.alloc<Int32>(0))\n";

    return run_generated(source, "42\n", "store write");
}

TEST("store byte")
{
    const char *source =
        "var s: Store = Store(1)\n"
        "fun byte_ok(): Int32\n"
        "  if s.alloc<Byte>(255).read() == 255 then\n"
        "    return 1\n"
        "  end\n"
        "  return 0\n"
        "end\n"
        "var n: Int32 = byte_ok()\n";

    return run_generated(source, "1\n", "byte allocation");
}

TEST("store growth")
{
    const char *source =
        "var s: Store = Store(1)\n"
        "var a0: Handle<Int32> = s.alloc<Int32>(0)\n"
        "var a1: Handle<Int32> = s.alloc<Int32>(1)\n"
        "var a2: Handle<Int32> = s.alloc<Int32>(2)\n"
        "var a3: Handle<Int32> = s.alloc<Int32>(3)\n"
        "var a4: Handle<Int32> = s.alloc<Int32>(4)\n"
        "var a5: Handle<Int32> = s.alloc<Int32>(5)\n"
        "var a6: Handle<Int32> = s.alloc<Int32>(6)\n"
        "var a7: Handle<Int32> = s.alloc<Int32>(7)\n"
        "var a8: Handle<Int32> = s.alloc<Int32>(8)\n"
        "var a9: Handle<Int32> = s.alloc<Int32>(9)\n"
        "var a10: Handle<Int32> = s.alloc<Int32>(10)\n"
        "var a11: Handle<Int32> = s.alloc<Int32>(11)\n"
        "var a12: Handle<Int32> = s.alloc<Int32>(12)\n"
        "var a13: Handle<Int32> = s.alloc<Int32>(13)\n"
        "var a14: Handle<Int32> = s.alloc<Int32>(14)\n"
        "var a15: Handle<Int32> = s.alloc<Int32>(15)\n"
        "var a16: Handle<Int32> = s.alloc<Int32>(16)\n"
        "var a17: Handle<Int32> = s.alloc<Int32>(17)\n"
        "var a18: Handle<Int32> = s.alloc<Int32>(18)\n"
        "var a19: Handle<Int32> = s.alloc<Int32>(19)\n"
        "var n: Int32 = a19.read()\n";

    return run_generated(source, "", "store growth");
}

TEST("stale handle")
{
    const char *source =
        "var s: Store = Store(16)\n"
        "var h: Handle<Int32> = s.alloc<Int32>(1)\n"
        "s.reset()\n"
        "var n: Int32 = h.read()\n";

    TestResult result = run_generated_failure(source, MESSAGE_RUNTIME_STALE_HANDLE + 7,
                                              "stale handle");
    if (result != TEST_PASS) return result;
    return file_contains(test_path("runtime.log"), "nippy: test.nip:4:")
               ? TEST_PASS : TEST_FAIL;
}

TEST("stale handle write reports its source location")
{
    TestResult result = run_generated_failure(
        "var s: Store = Store(16)\n"
        "var h: Handle<Int32> = s.alloc<Int32>(1)\n"
        "s.reset()\nh.write(2)\n",
        MESSAGE_RUNTIME_STALE_HANDLE + 7, "stale handle write location");
    if (result != TEST_PASS) return result;
    return file_contains(test_path("runtime.log"), "nippy: test.nip:4:")
               ? TEST_PASS : TEST_FAIL;
}

TEST("two stores")
{
    const char *source =
        "var a: Store = Store(1)\n"
        "var b: Store = Store(1)\n"
        "var n: Int32 = a.alloc<Int32>(1).read() + "
        "b.alloc<Int32>(2).read()\n";

    return run_generated(source, "3\n", "two stores");
}

TEST("Error in store")
{
    const char *source =
        "fun line_of(h: Handle<Error>): Int32\n"
        "  return h.read().line()\n"
        "end\n"
        "var arena: Store = Store(4096)\n"
        "var n: Int32 = line_of(arena.alloc<Error>("
        "Error(\"E\", \"d\", \"f\", 9, 2, 3)))\n";

    return run_generated(source, "9\n", "Error in a store");
}

TEST("Text in store")
{
    const char *source =
        "fun len_of(h: Handle<String<16>>): Int32\n"
        "  return h.read().byte_len()\n"
        "end\n"
        "var arena: Store = Store(32)\n"
        "var n: Int32 = len_of(arena.alloc<String<16>>(\"hi\"))\n";

    return run_generated(source, "2\n", "Text in a store");
}

TEST("byte assignment")
{
    const char *source =
        "fun check(b: Byte): Int32\n"
        "  b = 200\n"
        "  if b == 200 then\n"
        "    return 1\n"
        "  end\n"
        "  return 0\n"
        "end\n"
        "var n: Int32 = check(1)\n";

    return run_generated(source, "1\n", "byte assignment");
}

TEST("handle assignment")
{
    const char *source =
        "var arena: Store = Store(16)\n"
        "fun swap(h: Handle<Int32>, other: Handle<Int32>): Int32\n"
        "  h = other\n"
        "  return h.read()\n"
        "end\n"
        "fun choose(): Int32\n"
        "  return swap(arena.alloc<Int32>(1), arena.alloc<Int32>(2))\n"
        "end\n"
        "var n: Int32 = choose()\n";

    return run_generated(source, "2\n", "handle assignment");
}

TEST("global store")
{
    const char *source =
        "global arena: Store = Store(16)\n"
        "fun read_global(): Int32\n"
        "  return arena.alloc<Int32>(7).read()\n"
        "end\n"
        "var n: Int32 = read_global()\n";

    return run_generated(source, "7\n", "global store");
}

TEST("handle union")
{
    const char *source =
        "fun classify(h: Handle<Int32 | Bool>): Int32\n"
        "  var x: Int32 | Bool = h.read()\n"
        "  if typeof x is Int32 then\n"
        "    return 1\n"
        "  end\n"
        "  return 0\n"
        "end\n"
        "global arena: Store = Store(64)\n"
        "var n: Int32 = classify(arena.alloc<Int32 | Bool>(1))\n";

    return run_generated(source, "1\n", "handle of a union");
}

TEST("handle function")
{
    const char *source =
        "fun inc(x: Int32): Int32\n"
        "  return x + 1\n"
        "end\n"
        "fun call(h: Handle<Fun<(Int32):Int32>>): Int32\n"
        "  var f: Fun<(Int32):Int32> = h.read()\n"
        "  return f(1)\n"
        "end\n"
        "global arena: Store = Store(64)\n"
        "var n: Int32 = call(arena.alloc<Fun<(Int32):Int32>>(inc))\n";

    return run_generated(source, "2\n", "handle of a function");
}

TEST("alloc array")
{
    const char *source =
        "global arena: Store = Store(64)\n"
        "fun first(): Int32\n"
        "  var a: Array<Int32, 2> = [7, 8]\n"
        "  var h: Handle<Array<Int32, 2>> = "
        "arena.alloc<Array<Int32, 2>>(a)\n"
        "  var copy: Array<Int32, 2> = h.read()\n"
        "  return copy[0]\n"
        "end\n"
        "var n: Int32 = first()\n";

    return run_generated(source, "7\n", "alloc of an Array");
}
