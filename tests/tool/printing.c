/* The print builtin and how it formats each type. */

#include "tool_support.h"

TEST("program prints its value")
{
    return run_generated("var x : Int32 = 13\n", "13\n", "program 13");
}

TEST("printing stale values reports the print location")
{
    TestResult result = run_generated_failure(
        "global arena: Store = Store(64)\n"
        "var s: Text = arena.copy(\"\")\n"
        "arena.reset()\nprint(s)\n",
        MESSAGE_RUNTIME_STALE_HANDLE + 7, "printing stale empty text");
    if (result != TEST_PASS ||
        !file_contains(test_path("runtime.log"), "nippy: test.nip:4:"))
        return TEST_FAIL;
    result = run_generated_failure(
        "global arena: Store = Store(64)\n"
        "var b: TextBuilder = arena.builder(4)\n"
        "arena.reset()\nprint(b)\n",
        MESSAGE_RUNTIME_STALE_HANDLE + 7, "printing stale builder");
    if (result != TEST_PASS) return result;
    return file_contains(test_path("runtime.log"), "nippy: test.nip:4:")
               ? TEST_PASS : TEST_FAIL;
}

TEST("print all")
{
    const char *source =
        "type Point\n"
        "  x: Int32\n"
        "  y: Int32\n"
        "end\n"
        "fun inc(x: Int32): Int32\n"
        "  return x + 1\n"
        "end\n"
        "global arena: Store = Store(512)\n"
        "fun run()\n"
        "  var fixed: String<8> = \"ada\"\n"
        "  var t: Text = arena.text(fixed)\n"
        "  var arr: Array<Int32, 3> = [1, 2, 3]\n"
        "  var p: Point = Point(4, 5)\n"
        "  var u: Int32 | Bool = 1\n"
        "  var e: Error = Error(\"E\", \"desc\", \"file\", 3, 7, 42)\n"
        "  var b: TextBuilder = arena.builder(16)\n"
        "  b.append(\"hi\")\n"
        "  var h: Handle<Int32> = arena.alloc<Int32>(9)\n"
        "  var xs: List<Int32> = arena.list<Int32>(2)\n"
        "  xs.push(10)\n"
        "  xs.push(20)\n"
        "  var s: Slice<Int32> = xs.slice(0, 2)\n"
        "  print(\"scalar\", 1, true, nil, b)\n"
        "  print(fixed)\n"
        "  print(t)\n"
        "  print(arr)\n"
        "  print(p)\n"
        "  print(u)\n"
        "  print(e)\n"
        "  print(b)\n"
        "  print(h)\n"
        "  print(inc)\n"
        "  print(xs)\n"
        "  print(s)\n"
        "  print()\n"
        "  return\n"
        "end\n"
        "run()\n";
    const char *expected =
        "scalar 1 true nil hi\n"
        "ada\n"
        "ada\n"
        "[1, 2, 3]\n"
        "Point { x: 4, y: 5 }\n"
        "1\n"
        "Error(E, desc, file, 3, 7, 42)\n"
        "hi\n"
        "<handle store=0 offset=48 generation=1>\n"
        "<function>\n"
        "[10, 20]\n"
        "[10, 20]\n"
        "\n";

    return run_generated(source, expected, "print every value kind");
}

TEST("print nested")
{
    const char *source =
        "type Item\n"
        "  name: Text\n"
        "  n: Int32\n"
        "end\n"
        "global arena: Store = Store(256)\n"
        "fun run()\n"
        "  var a: String<8> = \"ada\"\n"
        "  var t: Text = arena.text(a)\n"
        "  var names: List<Text> = arena.list<Text>(2)\n"
        "  names.push(t)\n"
        "  names.push(t)\n"
        "  var item: Item = Item(t, 3)\n"
        "  var items: Array<Item, 1> = [item]\n"
        "  print(names)\n"
        "  print(item)\n"
        "  print(items)\n"
        "  return\n"
        "end\n"
        "run()\n";
    const char *expected =
        "[ada, ada]\n"
        "Item { name: ada, n: 3 }\n"
        "[Item { name: ada, n: 3 }]\n";

    return run_generated(source, expected, "nested print");
}
