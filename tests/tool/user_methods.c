/* Runtime behaviour of user-defined instance and static functions. */

#include "tool_support.h"

TEST("generic struct methods and static functions")
{
    const char *source =
        "type Box<T> value: T end\n"
        "method Box<T>.replace(value: T): self\n"
        "  self.value = value\n  return self\nend\n"
        "method Box<T>.choose<U>(fallback: U): U\n"
        "  return fallback\nend\n"
        "static Box<T>::make(value: T): Box<T>\n"
        "  return Box(value)\nend\n"
        "var b: Box<Int32> = Box(3)\n"
        "var copy = b.replace(8)\n"
        "print(b.value)\nprint(copy.value)\n"
        "print(b.choose(true))\n"
        "var other = Box<Int32>::make(42)\n"
        "print(other.value)\n";

    return run_generated(source, "8\n8\ntrue\n42\n",
                         "generic struct methods and static functions");
}

TEST("contextual string arguments in generic associated calls")
{
    const char *source =
        "type Box<T> value: T end\n"
        "method Box<T>.choose<U>(fallback: U): U\n"
        "  return fallback\nend\n"
        "method Box<T>.first<U>(a: U, b: U): U\n"
        "  return a\nend\n"
        "static Box<T>::identity<U>(value: U): U\n"
        "  return value\nend\n"
        "type Holder value: String<8> end\n"
        "fun second<T>(a: T, b: T): T\n  return b\nend\n"
        "fun length(value: String<8>): Int32\n"
        "  return value.byte_len()\nend\n"
        "fun result(b: Box<Int32>): String<8>\n"
        "  return b.choose(\"yes\")\nend\n"
        "var b: Box<Int32> = Box(3)\n"
        "var label: String<8> = b.choose(\"ok\")\n"
        "var other: String<8> = Box<Int32>::identity(\"hi\")\n"
        "const fixed: String<8> = \"value\"\n"
        "var first = b.first(\"x\", fixed)\n"
        "var holder = Holder(b.choose(\"holder\"))\n"
        "holder.value = b.choose(\"field\")\n"
        "var array: Array<String<8>, 1> = [\"x\"]\n"
        "array[0] = b.choose(\"array\")\n"
        "var nested = second(fixed, b.choose(\"nested\"))\n"
        "var text: Text = b.choose(\"text\")\n"
        "var items: Array<String<8>, 1> = [b.choose(\"inline\")]\n"
        "var map: Record<String<8>, String<8>, 1> = "
        "{\"key\": b.choose(\"map\")}\n"
        "print(label)\nprint(other)\nprint(result(b))\n"
        "print(length(b.choose(\"abc\")))\nprint(first)\n"
        "print(holder.value)\nprint(array[0])\nprint(nested)\nprint(text)\n"
        "print(items[0])\nprint(map[\"key\"])\n";

    return run_generated(source,
                         "ok\nhi\nyes\n3\nx\nfield\narray\nnested\ntext\n"
                         "inline\nmap\n",
                         "contextual string arguments in generic calls");
}

TEST("generic owner and function specializations")
{
    const char *source =
        "type Box<T> value: T end\n"
        "method Box<T>.replace(value: T): self\n"
        "  self.value = value\n  return self\nend\n"
        "method Box<T>.choose<U>(fallback: U): U\n"
        "  return fallback\nend\n"
        "method Box<T>.read(): T\n  return self.value\nend\n"
        "method Box<T>.read_with<U>(unused: U): T\n"
        "  return self.value\nend\n"
        "static Box<T>::make(value: T): Box<T>\n"
        "  return Box(value)\nend\n"
        "static Box<T>::pass<U>(value: U): U\n"
        "  return value\nend\n"
        "fun fetch<V>(box: Box<V>): V\n"
        "  return box.read()\nend\n"
        "fun wrap<T>(value: T): Box<T>\n"
        "  return Box<T>::make(value)\nend\n"
        "var a = wrap(12)\n"
        "var b = Box<Bool>::make(true)\n"
        "var c = Box<Int8>::make(1)\n"
        "c.replace(2)\n"
        "print(a.value)\nprint(b.value)\nprint(c.value)\n"
        "print(a.choose(false))\nprint(Box<Bool>::pass(7))\n"
        "print(a.read_with(true))\nprint(b.read_with(true))\n"
        "print(a.read())\nprint(b.read())\nprint(fetch(a))\n";

    return run_generated(source,
                         "12\ntrue\n2\nfalse\n7\n12\ntrue\n12\ntrue\n12\n",
                         "generic owner and function specializations");
}

TEST("recursive functions on generic owner")
{
    const char *source =
        "type Box<T> value: T end\n"
        "method Box<T>.touch(n: Int32): self\n"
        "  if n > 0 then self.touch(n - 1) end\n"
        "  return self\nend\n"
        "static Box<T>::repeat(n: Int32, value: T): T\n"
        "  if n > 0 then return Box<T>::repeat(n - 1, value) end\n"
        "  return value\nend\n"
        "var b: Box<Int32> = Box(5)\n"
        "b.touch(3)\n"
        "print(b.value)\n"
        "print(Box<Int32>::repeat(3, 9))\n";

    return run_generated(source, "5\n9\n",
                         "recursive functions on generic owner");
}

TEST("multiple and nested generic owner arguments")
{
    const char *source =
        "type Pair<A, B> first: A second: B end\n"
        "method Pair<A, B>.flip(): Pair<B, A>\n"
        "  return Pair(self.second, self.first)\nend\n"
        "static Pair<A, B>::make(first: A, second: B): Pair<A, B>\n"
        "  return Pair(first, second)\nend\n"
        "type Box<T> value: T end\n"
        "static Box<T>::make(value: T): Box<T>\n"
        "  return Box(value)\nend\n"
        "var p = Pair<Int32, Bool>::make(3, true)\n"
        "var q = p.flip()\n"
        "var nested = Box<Box<Int32>>::make(Box(7))\n"
        "var array = Box<Array<Int32, 2>>::make([4, 5])\n"
        "print(q.first)\nprint(q.second)\nprint(nested.value.value)\n"
        "print(array.value[0])\n";

    return run_generated(source, "true\n3\n7\n4\n",
                         "multiple and nested generic owner arguments");
}

TEST("generic owner with an instantiated generic field")
{
    const char *source =
        "type Box<T> value: T end\n"
        "type Wrap<T> box: Box<T> end\n"
        "method Wrap<T>.get(): T\n  return self.box.value\nend\n"
        "static Wrap<T>::make(value: T): Wrap<T>\n"
        "  return Wrap(Box(value))\nend\n"
        "var wrapped = Wrap<Int32>::make(3)\n"
        "print(wrapped.get())\n";

    return run_generated(source, "3\n",
                         "generic owner with an instantiated generic field");
}

TEST("generic instance method on concrete struct")
{
    const char *source =
        "type Point x: Int32 end\n"
        "method Point.choose<T>(value: T): T\n"
        "  self.x = self.x + 1\n"
        "  return value\n"
        "end\n"
        "var p = Point(0)\n"
        "print(p.choose(42))\n"
        "print(p.choose(true))\n"
        "print(p.x)\n";

    return run_generated(source, "42\ntrue\n2\n",
                         "generic instance method on concrete struct");
}

TEST("generic static function on concrete struct")
{
    const char *source =
        "type Point x: Int32 end\n"
        "static Point::identity<T>(value: T): T\n"
        "  return value\n"
        "end\n"
        "print(Point::identity(42))\n"
        "print(Point::identity(true))\n";

    return run_generated(source, "42\ntrue\n",
                         "generic static function on concrete struct");
}

TEST("user methods with built-in member names")
{
    const char *source =
        "type Box value: Int32 end\n"
        "method Box.read(): Int32\n  return self.value\nend\n"
        "method Box.reset()\n  self.value = 0\n  return\nend\n"
        "method Box.len(): Int32\n  return self.value\nend\n"
        "static Box::get(b: Box): Int32\n  return b.value\nend\n"
        "var b = Box(7)\n"
        "print(b.read())\n"
        "print(b.len())\n"
        "print(Box::get(b))\n"
        "b.reset()\n"
        "print(b.read())\n";

    return run_generated(source, "7\n7\n7\n0\n",
                         "user methods with built-in member names");
}

TEST("user-defined methods and static functions")
{
    const char *source =
        "type Point\n"
        "  x: Int32,\n"
        "  y: Int32\n"
        "end\n"
        "\n"
        "method Point.move(dx: Int32, dy: Int32): self\n"
        "  self.x = self.x + dx\n"
        "  self.y = self.y + dy\n"
        "  return self\n"
        "end\n"
        "\n"
        "static Point::get_mid(p1: Point, p2: Point): Array<Int32, 2>\n"
        "  return [(p1.x + p2.x) / 2, (p1.y + p2.y) / 2]\n"
        "end\n"
        "\n"
        "var p = Point(1, 2)\n"
        "var pn = p.move(3, 4)\n"
        "var mid = Point::get_mid(p, pn)\n"
        "print(p.x)\n"
        "print(p.y)\n"
        "print(pn.x)\n"
        "print(pn.y)\n"
        "print(mid[0])\n"
        "print(mid[1])\n";

    return run_generated(source, "4\n6\n4\n6\n4\n6\n",
                         "user-defined methods and static functions");
}

TEST("method receiver field mutation")
{
    const char *source =
        "type Counter value: Int32 end\n"
        "method Counter.increment(): self\n"
        "  self.value = self.value + 1\n"
        "  return self\n"
        "end\n"
        "var c = Counter(4)\n"
        "var copy = c.increment()\n"
        "copy.value = 10\n"
        "print(c.value)\n"
        "print(copy.value)\n";

    return run_generated(source, "5\n10\n", "method returns a value copy");
}

TEST("method on a mutable field receiver")
{
    const char *source =
        "type Inner value: Int32 end\n"
        "type Outer inner: Inner end\n"
        "method Inner.set(value: Int32): self\n"
        "  self.value = value\n"
        "  return self\n"
        "end\n"
        "var outer = Outer(Inner(1))\n"
        "outer.inner.set(9)\n"
        "print(outer.inner.value)\n";

    return run_generated(source, "9\n", "method on a mutable field receiver");
}

TEST("recursive instance method")
{
    const char *source =
        "type Counter value: Int32 end\n"
        "method Counter.descend(n: Int32): self\n"
        "  if n > 0 then\n"
        "    self.descend(n - 1)\n"
        "  end\n"
        "  return self\n"
        "end\n"
        "var counter = Counter(5)\n"
        "counter.descend(3)\n"
        "print(counter.value)\n";

    return run_generated(source, "5\n", "recursive instance method");
}

TEST("method call on a narrowed union receiver")
{
    const char *source =
        "type Point x: Int32 end\n"
        "method Point.move(dx: Int32): self\n"
        "  self.x = self.x + dx\n"
        "  return self\n"
        "end\n"
        "fun move_if_present(point: Point | Nil): Int32\n"
        "  if typeof point is Point then\n"
        "    point.move(4)\n"
        "    return point.x\n"
        "  end\n"
        "  return 0\n"
        "end\n"
        "print(move_if_present(Point(3)))\n";

    return run_generated(source, "7\n",
                         "method call on a narrowed union receiver");
}
