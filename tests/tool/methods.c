#include "tool_support.h"

TEST("Store parameters in associated functions")
{
    return run_generated(
        "type X value: Int32 end\n"
        "method X.save(s: Store): Handle<X> return s.alloc<X>(self) end\n"
        "static X::copy(s: Store, x: X): Handle<X> return s.alloc<X>(x) end\n"
        "global arena: Store = Store(1)\n"
        "var x = X(42)\n"
        "print(x.save(arena).read().value, X::copy(arena, x).read().value)\n",
        "42 42\n", "Store parameters in methods and static functions");
}

TEST("method arguments evaluate left to right")
{
    return run_generated(
        "type Point x: Int64 end\n"
        "fun side(x: Int64): Int64 print(x) return x end\n"
        "method Point.set(a: Int64, b: Int64): Int64\n"
        "  self.x = a + b return self.x end\n"
        "var p = Point(0)\n"
        "print(p.set(side(1), side(2)))\n",
        "1\n2\n3\n", "method argument order");
}
