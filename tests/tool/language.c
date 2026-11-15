/* Run-time behaviour of core language features in generated programs. */

#include "tool_support.h"

TEST("recursive generic nodes and forward references")
{
    return run_generated(
        "type Node<T> value: T next: Handle<Node<T>> | Nil end\n"
        "type Owner item: Handle<Item> | Nil end\n"
        "type Item owner: Owner value: Int64 end\n"
        "global arena: Store = Store(4096)\n"
        "var tail = arena.alloc<Node<Int64>>(Node(2, nil))\n"
        "var head = arena.alloc<Node<Int64>>(Node(1, tail))\n"
        "var first = head.read()\n"
        "if typeof first.next is Handle<Node<Int64>> then\n"
        " var link: Handle<Node<Int64>> = first.next\n"
        " print(first.value, link.read().value)\n"
        "end\n"
        "var item = arena.alloc<Item>(Item(Owner(nil), 42))\n"
        "var owner = Owner(item)\n"
        "if typeof owner.item is Handle<Item> then\n"
        " var link: Handle<Item> = owner.item print(link.read().value)\n"
        "end\n",
        "1 2\n42\n", "recursive nodes and forward types");
}

TEST("recursive node handles remain generation checked")
{
    return run_escaped_failure(
        "type Node value: Int64 next: Handle<Node> | Nil end\n"
        "global second: Store = Store(512)\n"
        "var head = second.alloc<Node>(Node(1, nil))\n"
        "scope first = Store(512) do\n"
        " var tail = first.alloc<Node>(Node(2, nil))\n"
        " head.write(Node(1, tail))\n"
        "end\n"
        "var item = head.read()\n"
        "if typeof item.next is Handle<Node> then\n"
        " var link: Handle<Node> = item.next print(link.read().value)\n"
        "end\n", "stale", "stale recursive node handle");
}

TEST("mutual handle types and recursive aliases")
{
    return run_generated(
        "type A other: Handle<B> | Nil = nil end\n"
        "type B other: Handle<A> | Nil = nil end\n"
        "type Link = Handle<Node> | Nil\n"
        "type Node value: Int64 next: Link = nil end\n"
        "global arena: Store = Store(1024)\n"
        "var a = arena.alloc<A>(A()) var b = arena.alloc<B>(B(a))\n"
        "a.write(A(b))\n"
        "var node = arena.alloc<Node>(Node(7))\n"
        "print(node.read().value)\n",
        "7\n", "mutual handles and aliases");
}
TEST("lazy ranges support values, steps and loop control")
{
    return run_generated(
        "fun show<T>(r: Range<T>) for n in r do print(n) end end\n"
        "var r: Range<Int64> = range(0, 4)\n"
        "show(r) show(range(5, 0, -2))\n"
        "for i, n in range(0, 5) do\n"
        " if n == 1 then continue end print(i, n) if n == 3 then break end\n"
        "end\n"
        "for n in range(0, 3, -1) do print(n) end\n"
        "for n in range(Uint64(18446744073709551615), "
        "Uint64(18446744073709551611), -2) do print(n) end\n",
        "0\n1\n2\n3\n5\n3\n1\n0 0\n2 2\n3 3\n"
        "18446744073709551615\n18446744073709551613\n", "lazy ranges");
}

TEST("ranges handle signed endpoints without overflow")
{
    return run_generated(
        "for n in range(Int64(9223372036854775807), "
        "Int64(-9223372036854775808), -9223372036854775808) do print(n) end\n",
        "9223372036854775807\n-1\n", "range signed endpoints");
}

TEST("range arguments are evaluated once in source order")
{
    return run_generated(
        "fun arg(n: Int64): Int64 print(n) return n end\n"
        "var r = range(arg(1), arg(4), arg(2))\n"
        "for n in r do print(n) end\n",
        "1\n4\n2\n1\n3\n", "range argument evaluation");
}

TEST("dynamic range zero step reports its source position")
{
    return run_generated_failure(
        "var step: Int64 = 0\nvar r = range(0, 5, step)\n",
        MESSAGE_RUNTIME_PREFIX "test.nip:2:9 (byte 28): "
        MESSAGE_RUNTIME_RANGE_ZERO_STEP,
        "dynamic zero range step");
}

TEST("struct field literal defaults")
{
    return run_generated(
        "type Rectangle width: Int64 height: Int64 = 10 end\n"
        "type Label text: String<8> = \"hello\" active: Bool = true end\n"
        "type Box<T> value: T count: Int64 = 2 end\n"
        "type DefaultBox<T> value: T = 12 end\n"
        "type Signed value: Int64 = -3 end\n"
        "var a = Rectangle(5) var b = Rectangle(width = 3)\n"
        "var c = Rectangle(height = 4, width = 1)\n"
        "var label = Label() var box = Box(7)\n"
        "var inferred = DefaultBox() var signed_value = Signed()\n"
        "print(a.width, a.height, b.height, c.height)\n"
        "print(label.text, label.active, box.value, box.count)\n"
        "print(inferred.value, signed_value.value)\n",
        "5 10 10 4\nhello true 7 2\n12 -3\n", "struct field defaults");
}

TEST("union subset narrowing retains values and tags")
{
    return run_generated(
        "fun show(x: Int64 | Bool)\n"
        "  match typeof x with Int64 then print(x) with Bool then print(x) end\n"
        "end\n"
        "fun run(x: Int64 | Bool | String<8>)\n"
        "  if !(typeof x is String<8>) then\n"
        "    show(x)\n"
        "    match typeof x with Int64 then print(x + 2) "
        "with Bool then print(x) end\n"
        "    if !(typeof x is Bool) then print(x + 1) end\n"
        "  end\n"
        "  if typeof x is Int64 || typeof x is Bool then show(x) end\n"
        "  if !(typeof x is String<8> || typeof x is Bool) then print(x + 3) end\n"
        "end\n"
        "run(42) run(true) run(\"abc\")\n",
        "42\n44\n43\n42\n45\ntrue\ntrue\ntrue\n", "union subset narrowing");
}

TEST("union subset narrowing on fields and array elements")
{
    return run_generated(
        "type Box value: Int64 | Bool | String<8> end\n"
        "fun show(x: Int64 | Bool)\n"
        "  match typeof x with Int64 then print(x) with Bool then print(x) end\n"
        "end\n"
        "var b = Box(true)\n"
        "var a: Array<Int64 | Bool | String<8>, 1> = [Int64(7)]\n"
        "if !(typeof b.value is String<8>) then\n"
        "  show(b.value)\n"
        "  if !(typeof b.value is Int64) then print(b.value) end\n"
        "end\n"
        "if !(typeof a[0] is String<8>) then show(a[0]) end\n",
        "true\ntrue\n7\n", "union subset field and element narrowing");
}

TEST("union subset narrowing retains nil")
{
    return run_generated(
        "fun show(x: Int64 | Nil)\n"
        "  match typeof x with Int64 then print(x) with Nil then print(\"nil\") end\n"
        "end\n"
        "fun run(x: Int64 | Bool | Nil)\n"
        "  if !(typeof x is Bool) then show(x) end\n"
        "end\n"
        "var x: Int64 | Bool | Nil = 9\n"
        "run(x) x = nil run(x)\n",
        "9\nnil\n", "union subset with nil");
}

TEST("compound alternatives preserve common narrowing facts")
{
    return run_generated(
        "fun show(x: Int64 | Bool)\n"
        "  match typeof x with Int64 then print(x) with Bool then print(x) end\n"
        "end\n"
        "fun run(a: Int64 | Bool | String<8>, b: Int64 | Bool | String<8>)\n"
        "  if (typeof a is Int64 && typeof b is Int64) || "
        "(typeof a is Int64 && typeof b is Bool) then\n"
        "    print(a + 1) show(b)\n"
        "  end\n"
        "end\n"
        "run(4, 8) run(4, true) run(false, 8)\n",
        "5\n8\n5\ntrue\n", "common narrowing facts");
}

TEST("control flow without user identifiers")
{
    return run_generated(
        "if true then print(1) end\n"
        "if false then print(0) else print(2) end\n"
        "while false do print(0) end\n",
        "1\n2\n", "control flow without user identifiers");
}

TEST("expression arguments and bitwise precedence")
{
    return run_generated(
        "fun id(x: Int64): Int64 return x end\n"
        "var x: Int32 | Nil = 4\n"
        "var bits: Int32 = 4\n"
        "print(typeof x is Int32, id(if true then 3 else 4 end))\n"
        "print(bits & 1 == 0, bits & 3 < 2, bits | 1 === 5)\n",
        "true 3\ntrue true true\n", "expression arguments and precedence");
}

TEST("match evaluates expression scrutinees once")
{
    return run_generated(
        "fun next(): Int64 print(\"called\") return 2 end\n"
        "fun choose(): Int64\n"
        "  return match next() with 1 then 10 with 2 then 20 else 30 end\n"
        "end\n"
        "print(choose())\n"
        "match next() with 1 then print(1) with 2 then print(2) else print(3) end\n",
        "called\n20\ncalled\n2\n", "match expression scrutinees");
}

TEST("match expression scrutinees in generic and recursive functions")
{
    return run_generated(
        "fun identity<T>(x: T): T return x end\n"
        "fun choose<T>(x: T): Int64\n"
        "  return match identity(x) with 1 then 11 else 22 end\n"
        "end\n"
        "fun recur(n: Int64): Int64\n"
        "  if n == 0 then return 0 end\n"
        "  return match recur(n - 1) with 0 then 1 else 2 end\n"
        "end\n"
        "print(choose(1), choose(2), recur(2))\n",
        "11 22 2\n", "match in generic and recursive functions");
}

TEST("match expression scrutinees in type tests and loops")
{
    return run_generated(
        "fun maybe(x: Bool): Int32 | Nil\n"
        "  if x then return Int32(3) else return nil end\nend\n"
        "fun result(x: Bool): Int64\n"
        "  match typeof maybe(x) with Int32 then return 1 with Nil then return 2 end\n"
        "end\n"
        "fun single(): Int64 match typeof 3 with Int64 then return 9 end end\n"
        "var n: Int64 = 1\n"
        "while true do match n + 0 with 1 then break else break end end\n"
        "print(result(true), result(false), n, single())\n",
        "1 2 1 9\n", "match type expressions and loop control");
}

TEST("match retains the original scrutinee across patterns")
{
    return run_generated(
        "type Flag value: Bool end\n"
        "method Flag.change(): Bool self.value = true return true end\n"
        "var flag = Flag(false)\n"
        "var result = match flag.value with flag.change() then 1 with false then 2 else 3 end\n"
        "print(result, flag.value)\n",
        "2 true\n", "match scrutinee snapshot");
}

TEST("match patterns use numeric and string value equality")
{
    return run_generated(
        "var fixed: String<4> = \"hi\"\n"
        "print(match \"hi\" with \"bye\" then 1 with \"hi\" then 2 else 3 end)\n"
        "print(match Int32(3) with Int64(3) then 4 else 5 end)\n"
        "print(match fixed with \"longer\" then 6 with \"hi\" then 7 else 8 end)\n",
        "2\n4\n7\n", "match value equality");
}

TEST("narrowing through and and negation")
{
    return run_generated(
        "fun positive(x: Int32 | Nil): Bool\n"
        "  if typeof x is Int32 && x > 0 then return true end\n"
        "  return false\nend\n"
        "fun negative(x: Int32 | Nil): Bool\n"
        "  if !(typeof x is Nil) then return x < 0 end\n"
        "  return false\nend\n"
        "print(positive(2), positive(nil), negative(-2), negative(nil))\n",
        "true false true false\n", "narrowing through and and negation");
}

TEST("narrowing through or and its false branch")
{
    return run_generated(
        "fun positive_or_nil(x: Int32 | Nil): Bool\n"
        "  return typeof x is Nil || x > 0\nend\n"
        "fun nonnegative(x: Int32 | Nil): Int32\n"
        "  if !(typeof x is Nil || x < 0) then return x + 1 end\n"
        "  return 0\nend\n"
        "fun negative(x: Int32 | Nil): Int32\n"
        "  if typeof x is Nil || x >= 0 then return 0 else return x end\n"
        "end\n"
        "print(positive_or_nil(nil), positive_or_nil(-1), nonnegative(4), negative(-2))\n",
        "true false 5 -2\n", "or narrowing");
}

TEST("narrowing after early return and in else")
{
    return run_generated(
        "fun value(x: Int32 | Nil): Int32\n"
        "  if typeof x is Nil then return 0 end\n"
        "  return x + 1\nend\n"
        "fun other(x: Int32 | Nil): Int32\n"
        "  if typeof x is Nil then return 0 else return x + 2 end\n"
        "end\n"
        "fun expr(x: Int32 | Nil): Int32\n"
        "  return if typeof x is Nil then Int32(0) else x + 1 end\nend\n"
        "print(value(nil), value(3), other(nil), other(3), expr(3))\n",
        "0 4 0 5 4\n", "narrowing after early return and in else");
}

TEST("generic optional inference")
{
    return run_generated(
        "fun pick<T>(x: T | Nil, fallback: T): T\n"
        "  if typeof x is T then return x end\n"
        "  return fallback\nend\n"
        "var value: Int32 | Nil = 5\n"
        "print(pick(Int32(3), Int32(4)), pick(value, Int32(7)))\n"
        "value = nil\nprint(pick(value, Int32(7)), pick(nil, Int32(8)))\n",
        "3 5\n7 8\n", "generic optional inference");
}

TEST("narrowing fixed array elements and struct fields")
{
    return run_generated(
        "type Box value: Int32 | Nil end\n"
        "var box = Box(4)\n"
        "var values: Array<Int32 | Nil, 2> = [3, 4]\n"
        "var i: Int32 = 1\n"
        "if typeof box.value is Int32 then print(box.value + 1) end\n"
        "if typeof values[0] is Int32 then print(values[0] + 2) end\n"
        "if typeof box.value is Int32 && box.value > 0 then print(box.value) end\n"
        "if typeof values[0] is Int32 && values[0] > 0 then print(values[0]) end\n"
        "if typeof values[i] is Int32 then print(values[i] + 1) end\n"
        "if typeof values[i - 1] is Int32 then print(values[i - 1]) end\n"
        "if typeof box.value is Int32 then box.value = nil end\n"
        "if typeof values[0] is Int32 then values[0] = nil end\n"
        "print(typeof box.value is Nil, typeof values[0] is Nil)\n",
        "5\n5\n4\n3\n5\n3\ntrue true\n", "narrowing fields and fixed array elements");
}

TEST("narrowing dynamic list elements")
{
    return run_generated(
        "global arena: Store = Store(64)\n"
        "var xs: List<Int32 | Nil> = arena.list<Int32 | Nil>(2)\n"
        "xs.push(Int32(3))\n"
        "if typeof xs[0] is Int32 && xs[0] > 0 then\n"
        "  print(xs[0]) print(xs[0] + 1)\n"
        "end\n"
        "match typeof xs[0] with Int32 then print(xs[0] + 2) with Nil then print(0) end\n",
        "3\n4\n5\n", "dynamic list narrowing");
}

TEST("functions without return annotations")
{
    const char *source =
        "fun show(x: Int32) print(x) end\n"
        "fun show_generic<T>(x: T) print(x) end\n"
        "fun apply(f: Fun<(Int32)>, x: Int32) f(x) end\n"
        "fun apply_generic<T>(f: Fun<(T)>, x: T) f(x) end\n"
        "type Point x: Int32 end\n"
        "method Point.show() print(self.x) end\n"
        "static Point::show_value(x: Int32) print(x) end\n"
        "var point = Point(3)\n"
        "show(1) show_generic(true) point.show() Point::show_value(4)\n"
        "apply(show, 5) apply_generic(show, Int32(6))\n";

    return run_generated(source, "1\ntrue\n3\n4\n5\n6\n",
                         "functions without return annotations");
}

TEST("nullable union initializer expression")
{
    const char *source =
        "global arena: Store = Store(32)\n"
        "var xs: List<Int32> = arena.list<Int32>(1)\n"
        "fun optional(): Int32 | Nil return nil end\n"
        "var value: Int32 | Nil = xs.pop()\n"
        "print(value)\n"
        "value = 3\nvalue = nil\nprint(value)\n"
        "print(optional())\n";

    return run_generated(source, "nil\nnil\nnil\n",
                         "nullable union initializer expression");
}

TEST("union narrowing")
{
    const char *source =
        "fun make(x: Int32): Int32 | Nil\n"
        "  return x\n"
        "end\n"
        "\n"
        "fun unwrap(v: Int32 | Nil): Int32\n"
        "  if typeof v is Int32 then\n"
        "    return v\n"
        "  end\n"
        "  return 0\n"
        "end\n"
        "\n"
        "fun compute(x: Int32): Int32\n"
        "  return unwrap(make(x))\n"
        "end\n";
    const char *driver =
        "#include <stdint.h>\n"
        "#include <stdio.h>\n"
        "\n"
        "int32_t nyx_u_compute(int32_t x);\n"
        "\n"
        "int main(void)\n"
        "{\n"
        "    printf(\"%d\\n\", (int)nyx_u_compute(3));\n"
        "    return 0;\n"
        "}\n";

    return run_generated_driver(source, driver, "3\n", "union narrowing");
}

TEST("generic instantiations")
{
    const char *source =
        "fun identity<T>(x: T): T\n"
        "  return x\n"
        "end\n"
        "\n"
        "fun flag(): Bool\n"
        "  return identity(true)\n"
        "end\n"
        "\n"
        "fun compute(x: Int32): Int32\n"
        "  if flag() then\n"
        "    return identity(x)\n"
        "  end\n"
        "  return 0\n"
        "end\n";
    const char *driver =
        "#include <stdint.h>\n"
        "#include <stdio.h>\n"
        "\n"
        "int32_t nyx_u_compute(int32_t x);\n"
        "\n"
        "int main(void)\n"
        "{\n"
        "    printf(\"%d\\n\", (int)nyx_u_compute(3));\n"
        "    return 0;\n"
        "}\n";

    return run_generated_driver(source, driver, "3\n",
                                "generic instantiations");
}

TEST("top-level assignment")
{
    const char *source =
        "var x: Int32 = 1\n"
        "x = 2\n";

    return run_generated(source, "2\n", "top-level assignment");
}

TEST("parameter assignment")
{
    const char *source =
        "fun bump(x: Int32): Int32\n"
        "  x = x + 1\n"
        "  return x\n"
        "end\n"
        "var n: Int32 = bump(1)\n";

    return run_generated(source, "2\n", "parameter assignment");
}

TEST("assignment narrowing")
{
    const char *source =
        "fun check(x: Int32 | Bool): Int32\n"
        "  if typeof x is Int32 then\n"
        "    x = true\n"
        "    if typeof x is Bool then\n"
        "      return 1\n"
        "    end\n"
        "  end\n"
        "  return 0\n"
        "end\n"
        "var n: Int32 = check(1)\n";

    return run_generated(source, "1\n", "assignment invalidates narrowing");
}

TEST("assignment in a loop")
{
    const char *source =
        "fun count(x: Int32): Int32\n"
        "  while x < 3 do\n"
        "    x = x + 1\n"
        "  end\n"
        "  return x\n"
        "end\n"
        "var n: Int32 = count(0)\n";

    return run_generated(source, "3\n", "assignment in a loop");
}

TEST("top-level while with break and continue")
{
    const char *source =
        "var n: Int32 = 0\n"
        "while n < 5 do\n"
        "  n = n + 1\n"
        "  if n == 2 then continue end\n"
        "  if n == 4 then break end\n"
        "end\n"
        "print(n)\n";

    return run_generated(source, "4\n", "top-level while control flow");
}

TEST("repeated assignment")
{
    const char *source =
        "var x: Int32 = 1\n"
        "x = 2\n"
        "x = 3\n";

    return run_generated(source, "3\n", "repeated assignment");
}

TEST("generic assignment")
{
    const char *source =
        "fun choose<T>(x: T, y: T): T\n"
        "  x = y\n"
        "  return x\n"
        "end\n"
        "var n: Int32 = choose(Int32(1), Int32(2))\n";

    return run_generated(source, "2\n", "generic assignment");
}

TEST("inferred assignment")
{
    const char *source =
        "var x = 1\n"
        "x = 2\n";

    return run_generated(source, "2\n", "inferred assignment");
}

TEST("nested narrowing invalidation")
{
    const char *source =
        "fun check(x: Int32 | Bool, y: Int32 | Bool): Int32\n"
        "  if typeof x is Int32 then\n"
        "    if typeof y is Int32 then\n"
        "      x = true\n"
        "      y = true\n"
        "      if typeof x is Bool && typeof y is Bool then\n"
        "        return 1\n"
        "      end\n"
        "    end\n"
        "  end\n"
        "  return 0\n"
        "end\n"
        "var n: Int32 = check(1, 2)\n";

    return run_generated(source, "1\n", "nested narrowing invalidation");
}

TEST("union assignment")
{
    const char *source =
        "fun set(u: Int32 | Bool): Int32\n"
        "  u = true\n"
        "  if typeof u is Bool then\n"
        "    return 1\n"
        "  end\n"
        "  return 0\n"
        "end\n"
        "var n: Int32 = set(1)\n";

    return run_generated(source, "1\n", "union assignment");
}

TEST("locals and fields")
{
    const char *source =
        "type Point\n"
        "  x: Int32\n"
        "  y: Int32\n"
        "end\n"
        "fun f(): Int32\n"
        "  var p: Point = Point(1, 2)\n"
        "  p.x = 3\n"
        "  var xs: Array<Int32, 2> = [10, 20]\n"
        "  xs[1] = 30\n"
        "  return p.x + p.y + xs[0] + xs[1]\n"
        "end\n"
        "var n: Int32 = f()\n";

    return run_generated(source, "45\n", "locals, fields, and indexing");
}

TEST("for index")
{
    const char *source =
        "fun sum(): Int32\n"
        "  var s: String<8> = \"hello\"\n"
        "  var total: Int32 = 0\n"
        "  for i, b in s do\n"
        "    total = total + i\n"
        "  end\n"
        "  return total\n"
        "end\n"
        "var n: Int32 = sum()\n";

    return run_generated(source, "10\n", "for with an index");
}

TEST("for mutation")
{
    const char *source =
        "global arena: Store = Store(64)\n"
        "fun extend(xs: List<Int32>)\n"
        "  xs.push(3)\n"
        "  return\n"
        "end\n"
        "fun sum(): Int32\n"
        "  var xs: List<Int32> = arena.list<Int32>(4)\n"
        "  xs.push(1)\n"
        "  xs.push(2)\n"
        "  var total: Int32 = 0\n"
        "  for x in xs do\n"
        "    extend(xs)\n"
        "    total = total + x\n"
        "  end\n"
        "  return total\n"
        "end\n"
        "var n: Int32 = sum()\n";

    TestResult result = run_generated_failure(source, MESSAGE_RUNTIME_FOR_MUTATED + 7,
                                              "for mutation");
    if (result != TEST_PASS) return result;
    return file_contains(test_path("runtime.log"), "nippy: test.nip:11:")
               ? TEST_PASS : TEST_FAIL;
}

TEST("nested structs")
{
    const char *source =
        "type Inner\n"
        "  x: Int32\n"
        "end\n"
        "type Middle\n"
        "  inner: Inner\n"
        "end\n"
        "type Outer\n"
        "  middle: Middle\n"
        "end\n"
        "print(Outer(Middle(Inner(7))))\n";
    const char *expected =
        "Outer { middle: Middle { inner: Inner { x: 7 } } }\n";

    return run_generated(source, expected, "nested struct print");
}

TEST("typeof if")
{
    const char *source =
        "type V = Int32 | Bool | Nil\n"
        "var x: V = 1\n"
        "if typeof x is Int32 then\n"
        "  print(\"int\", x)\n"
        "elseif typeof x is Bool then\n"
        "  print(\"bool\", x)\n"
        "else\n"
        "  print(\"nil\")\n"
        "end\n";
    return run_generated(source, "int 1\n", "typeof if");
}

TEST("typeof match")
{
    const char *source =
        "type V = Int32 | Bool | Nil\n"
        "var x: V = true\n"
        "match typeof x with Int32 then\n"
        "  print(\"int\", x)\n"
        "with Bool then\n"
        "  print(\"bool\", x)\n"
        "else\n"
        "  print(\"nil\")\n"
        "end\n";
    return run_generated(source, "bool true\n", "typeof match");
}

TEST("value match")
{
    const char *source =
        "var x: Bool = true\n"
        "match x with true then\n"
        "  print(\"yes\")\n"
        "with false then\n"
        "  print(\"no\")\n"
        "end\n";
    return run_generated(source, "yes\n", "value match");
}

TEST("if/match expressions")
{
    const char *source =
        "type V = Int32 | Bool\n"
        "var x: V = 1\n"
        "var y: Bool = true\n"
        "var a: Int32 = if typeof x is Int32 then 10 else 20 end\n"
        "var b: Int32 = if y == true then 1 else 0 end\n"
        "var c: Int32 = match y with true then 5 with false then 6 else 7 end\n"
        "var d: Int32 = match typeof x with Int32 then 8 with Bool then 9 else 0 end\n"
        "print(a, b, c, d)\n";
    return run_generated(source, "10 1 5 8\n", "if/match expressions");
}

TEST("const bindings")
{
    const char *source =
        "const x: Int32 = 3\n"
        "const y: Int32 = 4\n"
        "print(x + y)\n"
        "fun f(): Int32\n"
        "  const z: Int32 = 5\n"
        "  return z\n"
        "end\n"
        "print(f())\n"
        "global arena: Store = Store(64)\n"
        "const xs: List<Int32> = arena.list<Int32>(2)\n"
        "xs.push(1)\n"
        "print(xs.len())\n";
    return run_generated(source, "7\n5\n1\n", "const bindings");
}

TEST("operators")
{
    const char *source =
        "print(7 - 2, 3 * 4, 8 / 3, 8 % 3, -5, ~1)\n"
        "print(1 << 4, 16 >> 2, 6 & 3, 6 | 3, 6 ^ 3)\n"
        "print(!true, true && false, true || false)\n";
    return run_generated(source, "5 12 2 2 -5 -2\n16 4 2 7 5\nfalse false true\n",
                         "operators");
}

TEST("exhaustive match")
{
    const char *source =
        "type V = Int32 | Bool | Nil\n"
        "var x: V = true\n"
        "match typeof x with Int32 then\n"
        "  print(\"int\")\n"
        "with Bool then\n"
        "  print(\"bool\")\n"
        "with Nil then\n"
        "  print(\"nil\")\n"
        "end\n"
        "var b: Bool = false\n"
        "match b with true then\n"
        "  print(\"yes\")\n"
        "with false then\n"
        "  print(\"no\")\n"
        "end\n";
    return run_generated(source, "bool\nno\n", "exhaustive match");
}

TEST("divide by zero")
{
    const char *source =
        "var z: Int32 = 0\n"
        "print(1 / z)\n";
    return run_generated_failure(source, "division by zero",
                                 "runtime division by zero");
}

TEST("top-level index assignment")
{
    const char *source =
        "var xs: Array<Int32, 2> = [1, 2]\n"
        "xs[0] = 3\n"
        "print(xs[0])\n";

    return run_generated(source, "3\n", "top-level index assignment");
}

TEST("top-level field assignment")
{
    const char *source =
        "type Point\n"
        "  x: Int32\n"
        "  y: Int32\n"
        "end\n"
        "var p: Point = Point(1, 2)\n"
        "p.x = 3\n"
        "print(p.x)\n";

    return run_generated(source, "3\n", "top-level field assignment");
}

TEST("recursion guard")
{
    const char *source =
        "fun down(n: Int32): Int32\n"
        "  if n == -1 then\n"
        "    return 0\n"
        "  end\n"
        "  return down(n + 1)\n"
        "end\n"
        "var n: Int32 = down(1)\n";

    TestResult result = run_generated_failure(source, MESSAGE_RUNTIME_RECURSION + 7,
                                              "recursion depth guard");
    if (result != TEST_PASS) return result;
    return file_contains(test_path("runtime.log"), "nippy: test.nip:1:")
               ? TEST_PASS : TEST_FAIL;
}

TEST("handles can contain Text and other handles")
{
    return run_generated(
        "type Node value: Text next: Handle<Node> | Nil end\n"
        "global arena: Store = Store(8)\n"
        "var text: Text = arena.copy(\"hello\")\n"
        "var text_handle = arena.alloc<Text>(text)\n"
        "var node = arena.alloc<Node>(Node(text_handle.read(), nil))\n"
        "var pointer = arena.alloc<Handle<Node>>(node)\n"
        "print(pointer.read().read().value)\n",
        "hello\n", "handles with Text and recursive handle payloads");
}

TEST("Text loaded from a handle remains generation checked")
{
    return run_escaped_failure(
        "global storage: Store = Store(8)\n"
        "var value = storage.alloc<Text>(\"\")\n"
        "scope source = Store(8) do value.write(source.copy(\"hello\")) end\n"
        "print(value.read())\n",
        MESSAGE_RUNTIME_STALE_HANDLE + sizeof(MESSAGE_RUNTIME_PREFIX) - 1,
        "stale Text stored in a handle");
}

TEST("caller-owned Store descriptors work without globals")
{
    const char *source =
        "fun read(h: Handle<Int32>): Int32 return h.read() end\n"
        "fun size(t: Text): Int32 return t.byte_len() end\n";
    const char *driver =
        "#include \"runtime.h\"\n"
        "#include <stdio.h>\n"
        "#include <string.h>\n"
        "int32_t nyx_u_read(nyx_handle h);\n"
        "int32_t nyx_u_size(nyx_string t);\n"
        "int main(void) {\n"
        "  union { max_align_t align; unsigned char data[64]; } memory = {0};\n"
        "  nyx_store store = { memory.data, 64, 64, 1 };\n"
        "  int32_t value = 19;\n"
        "  nyx_handle h = { &store, 0, 1 };\n"
        "  nyx_string t = { NULL, &store, 16, 5, 1 };\n"
        "  memcpy(memory.data, &value, sizeof(value));\n"
        "  memcpy(memory.data + 16, \"hello\", 6);\n"
        "  printf(\"%d %d\\n\", (int)nyx_u_read(h), (int)nyx_u_size(t));\n"
        "  return 0;\n"
        "}\n";
    return run_generated_driver(source, driver, "19 5\n",
                                "caller-owned Store descriptors");
}

TEST("Store parameters forward through direct generic and indirect calls")
{
    return run_generated(
        "fun copy(s: Store, value: Text): Text return s.copy(value) end\n"
        "fun generic<T>(s: Store, value: T): Handle<T> return s.alloc<T>(value) end\n"
        "fun forward(s: Store): Text return copy(s, \"ok\") end\n"
        "fun indirect(f: Fun<(Store, Text):Text>, s: Store): Text\n"
        "  return f(s, \"go\") end\n"
        "global arena: Store = Store(1)\n"
        "print(forward(arena), generic(arena, 42).read(), indirect(copy, arena))\n",
        "ok 42 go\n", "Store parameters and function values");
}

TEST("function arguments evaluate left to right")
{
    return run_generated(
        "fun side(x: Int64): Int64 print(x) return x end\n"
        "fun pair(a: Int64, b: Int64): Int64 return a + b end\n"
        "fun generic<T>(a: T, b: T): T return a end\n"
        "fun indirect(f: Fun<(Int64, Int64):Int64>): Int64\n"
        "  return f(side(5), side(6)) end\n"
        "print(pair(side(1), side(2)))\n"
        "print(generic(side(3), side(4)))\n"
        "print(indirect(pair))\n",
        "1\n2\n3\n3\n4\n3\n5\n6\n11\n",
        "direct, generic, and indirect argument order");
}

TEST("constructor arguments evaluate in source order")
{
    return run_generated(
        "type P a: Int64 b: Int64 c: Int64 = 9 end\n"
        "fun side(x: Int64): Int64 print(x) return x end\n"
        "var first = P(side(1), side(2))\n"
        "var second = P(b = side(3), a = side(4))\n"
        "print(first, second)\n",
        "1\n2\n3\n4\nP { a: 1, b: 2, c: 9 } P { a: 4, b: 3, c: 9 }\n",
        "positional and named constructor argument order");
}

TEST("program names that are libc names")
{
    /* C has one global namespace, and the generated file includes headers that
       fill it. Names are emitted with a prefix, so none of these collide. */
    const char *source =
        "type P\n"
        "  x: Int32\n"
        "  EOF: Int32\n"
        "end\n"
        "fun div(a: Int32, b: Int32): Int32\n"
        "  return a / b\n"
        "end\n"
        "fun remove(EOF: Int32): Int32\n"
        "  return EOF + 1\n"
        "end\n"
        "fun puts(x: Int32): Int32\n"
        "  return x\n"
        "end\n"
        "var stdin: Int32 = div(10, 2)\n"
        "var p: P = P(1, 2)\n"
        "print(stdin, remove(p.EOF), puts(7), p)\n";

    return run_generated(source, "5 3 7 P { x: 1, EOF: 2 }\n",
                         "program names that are libc names");
}

TEST("a variable narrowed to Nil can be used")
{
    /* Nil carries no payload in the union, so reading it must not read a
       payload member. */
    const char *source =
        "fun describe(a: Int32 | Nil): Int32\n"
        "  if typeof a is Nil then\n"
        "    print(a)\n"
        "    return 0\n"
        "  end\n"
        "  return a\n"
        "end\n"
        "print(describe(nil), describe(4))\n";

    return run_generated(source, "nil\n0 4\n", "narrowed to Nil");
}
