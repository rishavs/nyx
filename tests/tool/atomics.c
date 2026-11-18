/* Atomic cells in generated programs. */

#include "tool_support.h"

TEST("atomics load, store, and add; add returns the previous value")
{
    return run_generated(
        "global stop: Atomic<Bool> = Atomic(false)\n"
        "global served: Atomic<Int64> = Atomic(10)\n"
        "global small: Atomic<Int8> = Atomic(-128)\n"
        "fun bump(): Int64\n"
        "  return served.add(5)\n"
        "end\n"
        "print(stop.load(), served.load(), small.load())\n"
        "print(bump(), served.load())\n"
        "stop.store(true)\n"
        "print(stop.load())\n"
        "served.add(-3)\n"
        "print(served.load())\n",
        "false 10 -128\n10 15\ntrue\n12\n", "atomic operations");
}

TEST("atomic integers wrap on overflow")
{
    return run_generated(
        "global small: Atomic<Int8> = Atomic(127)\n"
        "global byte: Atomic<Uint8> = Atomic(250)\n"
        "print(small.add(1), small.load())\n"
        "print(byte.add(10), byte.load())\n",
        "127 -128\n250 4\n", "atomic wrap-around");
}

TEST("atomics are shared by every function")
{
    return run_generated(
        "global counter: Atomic<Int32> = Atomic(0)\n"
        "fun a() counter.add(1) end\n"
        "fun b() a() a() end\n"
        "b()\n"
        "a()\n"
        "print(counter.load())\n",
        "3\n", "atomics across functions");
}
