/* defer in generated programs: when the deferred blocks run, in what order,
   and how they interact with return values, loops, and scoped Stores. */

#include "tool_support.h"

TEST("defers run in reverse order and a return value is computed first")
{
    return run_generated(
        "fun f(n: Int32): Int32\n"
        "  defer do print(\"a\") end\n"
        "  defer do print(\"b\") end\n"
        "  if n > 0 then\n"
        "    defer do print(\"c\") end\n"
        "    return n * 2\n"
        "  end\n"
        "  print(\"body\")\n"
        "  return 0\n"
        "end\n"
        "print(f(1))\n"
        "print(f(0))\n",
        "c\nb\na\n2\nbody\nb\na\n0\n", "defer order");
}

TEST("a deferred block sees the variables as they are when it runs")
{
    return run_generated(
        "fun f(): Int32\n"
        "  var count: Int32 = 1\n"
        "  defer do print(\"count\", count) end\n"
        "  count = 5\n"
        "  return count\n"
        "end\n"
        "print(f())\n",
        "count 5\n5\n", "deferred variable reads");
}

TEST("a defer that was never reached does not run")
{
    return run_generated(
        "fun f(flag: Bool)\n"
        "  if flag then\n"
        "    defer do print(\"flagged\") end\n"
        "  end\n"
        "  print(\"done\", flag)\n"
        "end\n"
        "f(true)\n"
        "f(false)\n"
        "fun early(flag: Bool)\n"
        "  if flag then return end\n"
        "  defer do print(\"late\") end\n"
        "  print(\"end\")\n"
        "end\n"
        "early(true)\n"
        "early(false)\n",
        "flagged\ndone true\ndone false\nend\nlate\n", "unreached defers");
}

TEST("a loop body runs its defers on every iteration and on break and continue")
{
    return run_generated(
        "for i in range(0, 4) do\n"
        "  defer do print(\"end\", i) end\n"
        "  if i == 1 then continue end\n"
        "  if i == 3 then\n"
        "    defer do print(\"inner\", i) end\n"
        "    break\n"
        "  end\n"
        "  print(\"body\", i)\n"
        "end\n"
        "print(\"after\")\n",
        "body 0\nend 0\nend 1\nbody 2\nend 2\ninner 3\nend 3\nafter\n",
        "defer in loops");
}

TEST("break unwinds only the loop it leaves")
{
    return run_generated(
        "fun f()\n"
        "  defer do print(\"function\") end\n"
        "  for i in range(0, 2) do\n"
        "    defer do print(\"outer\", i) end\n"
        "    for j in range(0, 2) do\n"
        "      defer do print(\"inner\", i, j) end\n"
        "      if j == 0 then break end\n"
        "    end\n"
        "  end\n"
        "end\n"
        "f()\n",
        "inner 0 0\nouter 0\ninner 1 0\nouter 1\nfunction\n",
        "defer and nested loops");
}

TEST("a deferred block may change variables a loop condition reads")
{
    return run_generated(
        "fun f(): Int32\n"
        "  var count: Int32 = 0\n"
        "  while true do\n"
        "    defer do count = count + 10 end\n"
        "    count = count + 1\n"
        "    if count > 20 then break end\n"
        "  end\n"
        "  return count\n"
        "end\n"
        "print(f())\n",
        "33\n", "defer in a while loop");
}

TEST("a defer inside a defer runs when the outer defer ends")
{
    return run_generated(
        "fun f()\n"
        "  defer do\n"
        "    defer do print(\"inner\") end\n"
        "    print(\"outer\")\n"
        "  end\n"
        "  print(\"work\")\n"
        "end\n"
        "f()\n",
        "work\nouter\ninner\n", "nested defers");
}

TEST("a defer block can loop with its own break and continue")
{
    return run_generated(
        "fun f(): Int32\n"
        "  defer do\n"
        "    for i in range(0, 4) do\n"
        "      if i == 1 then continue end\n"
        "      if i == 3 then break end\n"
        "      print(\"d\", i)\n"
        "    end\n"
        "  end\n"
        "  return 1\n"
        "end\n"
        "print(f())\n",
        "d 0\nd 2\n1\n", "loops inside a defer");
}

TEST("a defer runs before the scope Store of its block is released")
{
    return run_generated(
        "fun f(early: Bool): Int32\n"
        "  var log: Int32 = 0\n"
        "  defer do print(\"function\", log) end\n"
        "  scope s = Store(256) do\n"
        "    var text = s.copy(\"hello\")\n"
        "    defer do\n"
        "      var again = s.copy(\"deferred\")\n"
        "      print(text, again)\n"
        "      log = log + 1\n"
        "    end\n"
        "    if early then return 7 end\n"
        "    print(\"scope body\")\n"
        "  end\n"
        "  print(\"after scope\")\n"
        "  return 1\n"
        "end\n"
        "print(f(true))\n"
        "print(f(false))\n",
        "hello deferred\nfunction 1\n7\nscope body\nhello deferred\n"
        "after scope\nfunction 1\n1\n",
        "defer and scoped Stores");
}

TEST("a scope inside a loop releases its Store after the defer on continue")
{
    return run_generated(
        "fun f(): Int32\n"
        "  var total: Int32 = 0\n"
        "  for i in range(0, 3) do\n"
        "    scope s = Store(64) do\n"
        "      defer do total = total + 1 end\n"
        "      var t = s.copy(\"x\")\n"
        "      if i == 1 then continue end\n"
        "      print(t, i)\n"
        "    end\n"
        "  end\n"
        "  return total\n"
        "end\n"
        "print(f())\n",
        "x 0\nx 2\n3\n", "defer in a scope in a loop");
}

TEST("defers work in recursion, methods, generics, and match arms")
{
    return run_generated(
        "fun fact(n: Int32): Int32\n"
        "  defer do print(\"leave\", n) end\n"
        "  if n <= 1 then return 1 end\n"
        "  return n * fact(n - 1)\n"
        "end\n"
        "type Counter hits: Int32 end\n"
        "method Counter.touch(): Int32\n"
        "  defer do print(\"touched\") end\n"
        "  return self.hits + 1\n"
        "end\n"
        "fun pick(x: Int32 | Bool): Int32\n"
        "  match typeof x with Int32 then\n"
        "    defer do print(\"int arm\") end\n"
        "    return x\n"
        "  with Bool then\n"
        "    defer do print(\"bool arm\") end\n"
        "    return 0\n"
        "  end\n"
        "end\n"
        "fun generic<T>(v: T): T\n"
        "  defer do print(\"generic\") end\n"
        "  return v\n"
        "end\n"
        "print(fact(3))\n"
        "var c: Counter = Counter(4)\n"
        "print(c.touch())\n"
        "var a: Int32 = pick(5)\n"
        "var b: Int32 = pick(true)\n"
        "print(a, b)\n"
        "print(generic(9))\n",
        "leave 1\nleave 2\nleave 3\n6\ntouched\n5\nint arm\nbool arm\n5 0\n"
        "generic\n9\n",
        "defer in recursion, methods, generics, and match");
}

TEST("program-level blocks can hold defers")
{
    return run_generated(
        "var n: Int32 = 0\n"
        "while n < 3 do\n"
        "  defer do print(\"tick\", n) end\n"
        "  n = n + 1\n"
        "  if n == 2 then continue end\n"
        "end\n"
        "if n == 3 then\n"
        "  defer do print(\"if\") end\n"
        "  print(\"inside\")\n"
        "end\n"
        "print(n)\n",
        "tick 1\ntick 2\ntick 3\ninside\nif\n3\n", "program-level defers");
}

TEST("a bare return in a function without a result runs its defers")
{
    return run_generated(
        "fun early(flag: Bool)\n"
        "  defer do print(\"cleanup\") end\n"
        "  if flag then return end\n"
        "  print(\"full\")\n"
        "end\n"
        "early(true)\n"
        "early(false)\n",
        "cleanup\nfull\ncleanup\n", "bare return with defers");
}

TEST("a trap ends the program without running defers")
{
    return run_generated_failure_lacking(
        "global arena: Store = Store(64)\n"
        "fun crash()\n"
        "  var xs: List<Int32> = arena.list<Int32>(1)\n"
        "  defer do print(\"deferred\") end\n"
        "  print(xs[5])\n"
        "end\n"
        "crash()\n",
        MESSAGE_RUNTIME_LIST_BOUNDS + sizeof(MESSAGE_RUNTIME_PREFIX) - 1,
        "deferred",
        "trap with a pending defer");
}
