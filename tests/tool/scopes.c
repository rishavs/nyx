#include "tool_support.h"

TEST("internal scoped Store constructor arguments")
{
    return run_generated(
        "type Pair first: Text second: Text end\n"
        "scope s = Store(1) do\n"
        " var p = Pair(second = s.copy(\"two\"), first = s.copy(\"one\"))\n"
        " print(p)\n"
        "end\n", "Pair { first: one, second: two }\n",
        "scoped Store constructor arguments");
}

TEST("internal scoped Store handles control-flow exits")
{
    return run_generated(
        "fun f(n: Int32): Int32\n"
        " scope s = Store(n) do\n"
        "  var h = s.alloc<Int32>(42)\n"
        "  if n == 1 then return h.read() end\n"
        "  while true do\n"
        "   scope t = Store(1) do\n"
        "    var v = t.alloc<Int32>(3)\n"
        "    if n == 2 then break end\n"
        "    if n == 3 then return v.read() end\n"
        "    return 0\n"
        "   end\n"
        "  end\n"
        " end\n"
        " return 7\n"
        "end\n"
        "print(f(1), f(2), f(3))\n",
        "42 7 3\n", "internal scoped Store exits");
}

TEST("loop exits preserve enclosing Stores")
{
    return run_generated(
        "scope outer = Store(16) do\n"
        " var h = outer.alloc<Int32>(42)\n"
        " for n in range(0, 3) do\n"
        "  scope inner = Store(1) do\n"
        "   if n < 2 then continue end\n"
        "   break\n"
        "  end\n"
        " end\n"
        " print(h.read())\n"
        "end\n",
        "42\n", "loop exits preserve enclosing Stores");
}

TEST("internal scoped Store evaluates capacity once")
{
    return run_generated(
        "fun capacity(): Int32 print(\"capacity\") return 1 end\n"
        "scope arena = Store(capacity()) do print(\"body\") end\n",
        "capacity\nbody\n", "scoped Store capacity evaluation");
}

TEST("internal scoped Store rejects a dynamic negative capacity")
{
    return run_generated_failure(
        "fun f(n: Int32) scope s = Store(n) do print(1) end end\n"
        "f(-1)\n", MESSAGE_RUNTIME_NEGATIVE_CAPACITY + sizeof(MESSAGE_RUNTIME_PREFIX) - 1,
        "negative scoped Store capacity");
}

/* Runs `source` with malloc and free counted, and requires its output to be
   `expected` followed by the "allocations releases" line. */
static TestResult run_counted(const char *source, const char *expected,
                              const char *what)
{
    const char *driver =
        "#include <stdlib.h>\n#include <stdio.h>\n"
        "static size_t allocations, releases;\n"
        "static void *tracked_malloc(size_t n) { void *p = malloc(n); if (p) allocations++; return p; }\n"
        "static void tracked_free(void *p) { if (p) releases++; free(p); }\n"
        "#define malloc tracked_malloc\n#define free tracked_free\n"
        "#define main generated_main\n#include \"runtime.c\"\n#undef main\n"
        "int main(void) { int r = generated_main(); printf(\"%zu %zu\\n\", allocations, releases); return r; }\n";
    CompilerConfig config = {.header_name = "runtime.h", .source_name = "test.nip"};
    CompilerResult compiled = compiler_compile(source, &config);
    char command[4096], executable[1024], log[1024];
    bool ok;

    if (!compiled.succeeded) {
        fprintf(stderr, "  %s: compile failed: %s\n", what,
                compiled.error_message == NULL ? "(none)" : compiled.error_message);
        compiler_result_free(&compiled);
        return TEST_ERROR;
    }
    snprintf(executable, sizeof(executable), "%s", test_path("scopes"));
    snprintf(log, sizeof(log), "%s", test_path("scopes.log"));
    ok = write_text(test_path("runtime.h"), compiled.h_source) &&
         write_text(test_path("runtime.c"), compiled.c_source) &&
         write_text(test_path("scope-driver.c"), driver);
    compiler_result_free(&compiled);
    if (!ok) return TEST_ERROR;
    snprintf(command, sizeof(command), "cc -std=c11 -Wall -Wextra -Wpedantic -Werror -o %s %s",
             executable, test_path("scope-driver.c"));
    if (run_command(command, log) != 0) {
        fprintf(stderr, "  %s: the counting driver did not build\n", what);
        return TEST_ERROR;
    }
    if (run_command(executable, log) != 0 || !file_equals(log, expected)) {
        fprintf(stderr, "  %s: output or allocation counts differ\n", what);
        return TEST_FAIL;
    }
    return TEST_PASS;
}

TEST("internal scoped Stores are reclaimed on control-flow exits")
{
    /* r opens two scopes; loop's scope is allocated once and kept until the
       loop ends; final opens one. */
    return run_counted(
        "fun r(): Int32 scope a = Store(16) do scope b = Store(16) do\n"
        " return 5 end end end\n"
        "fun loop() for x in range(0, 3) do scope s = Store(16) do\n"
        " if x < 2 then continue end break end end end\n"
        "print(r()) loop()\n"
        "scope final = Store(16) do end\n", "5\n4 4\n", "reclaimed exits");
}

TEST("a loop reuses one buffer per scope for its whole activation")
{
    return run_counted(
        "fun run(n: Int32): Int32\n"
        " var total: Int32 = 0\n"
        " var i: Int32 = 0\n"
        " while i < n do\n"
        "  scope s = Store(16) do\n"
        "   var h = s.alloc<Int32>(i)\n"
        "   total = total + h.read()\n"
        "  end\n"
        "  i = i + 1\n"
        " end\n"
        " return total\n"
        "end\n"
        "print(run(5), run(0), run(3))\n", "10 0 3\n2 2\n", "loop reuse");
}

TEST("a return inside a loop releases the loop's buffers")
{
    return run_counted(
        "fun find(n: Int32): Int32\n"
        " for i in range(0, 10) do\n"
        "  scope s = Store(16) do\n"
        "   var h = s.alloc<Int32>(Int32(i))\n"
        "   if h.read() == n then return h.read() end\n"
        "  end\n"
        " end\n"
        " return -1\n"
        "end\n"
        "print(find(3), find(20))\n", "3 -1\n2 2\n", "return releases cache");
}

TEST("nested loops keep a buffer per loop activation")
{
    return run_counted(
        "var total: Int32 = 0\n"
        "for a in range(0, 3) do\n"
        " scope outer = Store(16) do\n"
        "  var x = outer.alloc<Int32>(1)\n"
        "  for b in range(0, 4) do\n"
        "   scope inner = Store(16) do\n"
        "    var y = inner.alloc<Int32>(2)\n"
        "    total = total + x.read() + y.read()\n"
        "   end\n"
        "  end\n"
        " end\n"
        "end\n"
        "print(total)\n", "36\n4 4\n", "nested loops");
}

TEST("a recycled scope starts empty and its old handles are stale")
{
    return run_escaped_failure(
        "global keep: Store = Store(16)\n"
        "var old: Handle<Int32> = keep.alloc<Int32>(0)\n"
        "var n = 0\n"
        "while n < 2 do\n"
        " scope s = Store(16) do\n"
        "  if n == 1 then print(old.read()) end\n"
        "  old = s.alloc<Int32>(7)\n"
        " end\n"
        " n = n + 1\n"
        "end\n", MESSAGE_RUNTIME_STALE_HANDLE + 7, "recycled handle is stale");
}

TEST("the guide's caller-owned output example runs")
{
    return run_generated(
        "fun label(output: Store, input: Text): Text\n"
        " return output.concat(\"label: \", input)\n"
        "end\n"
        "scope output = Store(256) do\n"
        " var result: Text = \"\"\n"
        " scope scratch = Store(64) do\n"
        "  var input = scratch.copy(\"example\")\n"
        "  result = label(output, input)\n"
        " end\n"
        " print(result)\n"
        "end\n", "label: example\n", "guide output example");
}

TEST("the guide's linked data example runs")
{
    return run_generated(
        "type Node\n value: Int32\n next: Handle<Node> | Nil\nend\n"
        "scope chain = Store(256) do\n"
        " var tail = chain.alloc<Node>(Node(2, nil))\n"
        " var head = chain.alloc<Node>(Node(1, tail))\n"
        " print(head.read().value)\n"
        "end\n", "1\n", "guide linked example");
}

TEST("a recursive parser keeps its output and reclaims its scratch")
{
    return run_generated(
        "fun parse(out: Store, input: Text, depth: Int32, names: List<Text>)\n"
        " if depth > 0 then\n"
        "  scope scratch = Store(64) do\n"
        "   var piece = scratch.concat(\"n\", input)\n"
        "   names.push(out.copy(piece))\n"
        "  end\n"
        "  parse(out, input, depth - 1, names)\n"
        " end\n"
        "end\n"
        "scope out = Store(512) do\n"
        " var names: List<Text> = out.list<Text>(4)\n"
        " parse(out, \"x\", 3, names)\n"
        " print(names.len(), names[0], names[2])\n"
        "end\n", "3 nx nx\n", "recursive parser");
}

TEST("a scope inside a repeated handler starts empty each time")
{
    return run_generated(
        "fun handle(n: Int32): Int32\n"
        " scope s = Store(16) do\n"
        "  var h = s.alloc<Int32>(n)\n"
        "  return h.read() + 1\n"
        " end\n"
        "end\n"
        "var total: Int32 = 0\n"
        "for i in range(0, 4) do total = total + handle(Int32(i)) end\n"
        "print(total)\n", "10\n", "scope in a handler");
}
