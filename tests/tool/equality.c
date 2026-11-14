#include "tool_support.h"

TEST("struct and array equality")
{
    const char *source =
        "type Point x: Int32 y: Int32 end\n"
        "type Segment points: Array<Point, 2> end\n"
        "type Label name: String<4> end\n"
        "var a: Array<Point, 2> = [Point(1,2), Point(3,4)]\n"
        "var b: Array<Point, 2> = [Point(1,2), Point(3,4)]\n"
        "var c: Array<Point, 2> = [Point(1,2)]\n"
        "var x = Segment(a) var y = Segment(b) var z = Segment(c)\n"
        "print(x == y, x != z, a == b, a == c, x === y)\n"
        "match x with y then print(\"matched\") else print(\"wrong\") end\n"
        "print(Label(\"hi\") == Label(\"hi\"))\n";
    return run_generated(source, "true true true false true\nmatched\ntrue\n", "struct and array equality");
}

TEST("list record and table equality")
{
    const char *source =
        "global arena: Store = Store(256)\n"
        "var a: List<Int32> = arena.list<Int32>(1)\n"
        "var b: List<Int32> = arena.list<Int32>(1)\n"
        "a.push(1) a.push(2) b.push(1) b.push(2)\n"
        "var r: Record<String<8>, Int32, 4> = {\"a\":1, \"b\":2}\n"
        "var s: Record<String<8>, Int32, 4> = {\"b\":2, \"a\":1}\n"
        "var x: Table<String<8>, Int32> = arena.table<String<8>, Int32>(1)\n"
        "var y: Table<String<8>, Int32> = arena.table<String<8>, Int32>(1)\n"
        "x[\"a\"] = 1 x[\"b\"] = 2 y[\"b\"] = 2 y[\"a\"] = 1\n"
        "print(a == b, r == s, x == y, a === b, r === s, x === y)\n"
        "r.remove(\"a\") r[\"a\"] = 1 x.remove(\"a\") x[\"a\"] = 1\n"
        "print(r == s, x == y)\n"
        "b[0] = 9 s[\"a\"] = 9 y[\"a\"] = 9\n"
        "print(a != b, r != s, x != y)\n";
    return run_generated(source, "true true true true true true\ntrue true\ntrue true true\n",
                         "list record and table equality");
}

TEST("stale table equality")
{
    TestResult result = run_generated_failure(
        "global s: Store = Store(32)\n"
        "var a: Table<String<4>, Int32> = s.table<String<4>, Int32>(1)\n"
        "var b: Table<String<4>, Int32> = s.table<String<4>, Int32>(1)\n"
        "s.reset()\nprint(a == b)\n",
        MESSAGE_RUNTIME_STALE_HANDLE + 7, "stale table equality");
    if (result != TEST_PASS) return result;
    return file_contains(test_path("runtime.log"), "nippy: test.nip:5:")
               ? TEST_PASS : TEST_FAIL;
}

TEST("string equality validates stale strings even when lengths differ")
{
    TestResult result = run_generated_failure(
        "global s: Store = Store(32)\n"
        "var text: Text = s.copy(\"long\")\n"
        "s.reset()\nprint(text == \"x\")\n",
        MESSAGE_RUNTIME_STALE_HANDLE + 7, "stale text equality");
    if (result != TEST_PASS) return result;
    return file_contains(test_path("runtime.log"), "nippy: test.nip:4:")
               ? TEST_PASS : TEST_FAIL;
}

TEST("cross type string equality")
{
    const char *source =
        "var a: String<4> = \"hi\" var b: String<8> = \"hi\"\n"
        "var c: Text = \"hi\" var d: Text = \"bye\"\n"
        "print(a == b, a == c, c == b, a != d, a === b, a === c)\n"
        "print(\"hi\" == \"hi\", a == \"longer\", a === \"longer\")\n";
    return run_generated(source, "true true true true false false\ntrue false false\n",
                         "cross type string equality");
}

TEST("stale list equality")
{
    return run_generated_failure(
        "global s: Store = Store(32)\n"
        "var a: List<Int32> = s.list<Int32>(1)\n"
        "var b: List<Int32> = s.list<Int32>(1)\n"
        "s.reset()\nprint(a == b)\n",
        MESSAGE_RUNTIME_STALE_HANDLE + 7, "stale list equality");
}

TEST("handle identity equality")
{
    return run_generated(
        "global a: Store = Store(1) global b: Store = Store(1)\n"
        "var h = a.alloc<Int32>(3) var alias = h\n"
        "var other = a.alloc<Int32>(3) var different = b.alloc<Int32>(3)\n"
        "var wide = a.alloc<Int64>(3)\n"
        "print(h == alias, h === alias, h == other, h != other, h == different)\n"
        "print(h === wide, h.read() == other.read())\n"
        "h.write(4) print(h == alias, h.read() == other.read())\n",
        "true true false true false\nfalse true\ntrue false\n",
        "handle identity equality");
}

TEST("handles nested in comparable composites")
{
    return run_generated(
        "type Box value: Handle<Int32> end\n"
        "global s: Store = Store(32)\n"
        "var h = s.alloc<Int32>(3) var k = s.alloc<Int32>(3)\n"
        "var a = Box(h) var b = Box(h) var c = Box(k)\n"
        "var x: Array<Handle<Int32>, 1> = [h]\n"
        "var y: Array<Handle<Int32>, 1> = [h]\n"
        "var xs: List<Handle<Int32>> = s.list<Handle<Int32>>(1)\n"
        "var ys: List<Handle<Int32>> = s.list<Handle<Int32>>(1)\n"
        "xs.push(h) ys.push(h)\n"
        "print(a == b, a != c, x == y, xs == ys)\n",
        "true true true true\n", "handles in composite equality");
}

TEST("stale handle equality")
{
    return run_generated_failure(
        "global s: Store = Store(32) var h = s.alloc<Int32>(1)\n"
        "s.reset() print(h == h)\n", MESSAGE_RUNTIME_STALE_HANDLE + 7,
        "stale handle equality");
}

TEST("stale handle strict equality across types")
{
    return run_generated_failure(
        "global s: Store = Store(32) var h = s.alloc<Int32>(1)\n"
        "s.reset() var w = s.alloc<Int64>(1) print(h === w)\n",
        MESSAGE_RUNTIME_STALE_HANDLE + 7, "stale mixed handle strict equality");
}

TEST("stale right handle strict equality")
{
    return run_generated_failure(
        "global s: Store = Store(32) var old = s.alloc<Int32>(1)\n"
        "s.reset() var current = s.alloc<Int64>(1) print(current === old)\n",
        MESSAGE_RUNTIME_STALE_HANDLE + 7, "stale right handle strict equality");
}

TEST("stale handle strict equality with value")
{
    return run_generated_failure(
        "global s: Store = Store(32) var old = s.alloc<Int32>(1)\n"
        "s.reset() print(3 === old)\n", MESSAGE_RUNTIME_STALE_HANDLE + 7,
        "stale handle strict equality with value");
}

TEST("handle equality without a store")
{
    return run_generated(
        "fun eq(a: Handle<Int32>, b: Handle<Int32>): Bool return a == b end\n"
        "fun strict(a: Handle<Int32>, b: Handle<Int64>): Bool return a === b end\n",
        "", "handle equality without store");
}
