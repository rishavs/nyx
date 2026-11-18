/* parallel blocks in generated programs: results, the shared pool, repeated
   arms, real concurrency, and what happens when an arm fails. */

#include "tool_support.h"

#define SQUARE "fun square(n: Int32): Int32 return n * n end\n"

TEST("arms assign their results to variables")
{
    return run_generated(
        SQUARE
        "var a: Int32 = 0\n"
        "var b: Int32 = 0\n"
        "parallel do\n"
        "  with a = square(6)\n"
        "  with b = square(7)\n"
        "end\n"
        "print(a, b)\n",
        "36 49\n", "assigned results");
}

TEST("arms see their arguments as they were when the block began")
{
    return run_generated(
        SQUARE
        "var n: Int32 = 3\n"
        "var a: Int32 = 0\n"
        "var b: Int32 = 0\n"
        "parallel do\n"
        "  with a = square(n)\n"
        "  with b = square(n + 1)\n"
        "end\n"
        "n = 100\n"
        "print(a, b, n)\n",
        "9 16 100\n", "arguments copied at the start");
}

TEST("every arm runs and results come back to the right variable")
{
    return run_generated(
        SQUARE
        "var r0: Int32 = 0 var r1: Int32 = 0 var r2: Int32 = 0\n"
        "var r3: Int32 = 0 var r4: Int32 = 0 var r5: Int32 = 0\n"
        "parallel do\n"
        "  with r0 = square(0) with r1 = square(1) with r2 = square(2)\n"
        "  with r3 = square(3) with r4 = square(4) with r5 = square(5)\n"
        "end\n"
        "print(r0, r1, r2, r3, r4, r5)\n",
        "0 1 4 9 16 25\n", "six arms");
}

TEST("arms allocate in the pool and the then section reads their results")
{
    return run_generated(
        "fun squares(out: Store, n: Int32): List<Int64>\n"
        "  var xs: List<Int64> = out.list<Int64>(n)\n"
        "  for i in range(0, n) do xs.push(Int64(i) * Int64(i)) end\n"
        "  return xs\n"
        "end\n"
        "parallel pool = Store(1 << 20) do\n"
        "  with var a = squares(pool, 1000)\n"
        "  with var b = squares(pool, 2000)\n"
        "  with var c = squares(pool, 3000)\n"
        "  with var d = squares(pool, 4000)\n"
        "then\n"
        "  print(a.len(), b.len(), c.len(), d.len(), a[999], d[3999])\n"
        "end\n",
        "1000 2000 3000 4000 998001 15992001\n", "pool results");
}

TEST("the then section can copy a pool result into a longer-lived Store")
{
    return run_generated(
        "fun note(out: Store): Text return out.copy(\"from an arm\") end\n"
        "global keep: Store = Store(256)\n"
        "var kept: Text = \"\"\n"
        "parallel pool = Store(256) do\n"
        "  with var t = note(pool)\n"
        "then\n"
        "  kept = keep.copy(t)\n"
        "end\n"
        "print(kept)\n",
        "from an arm\n", "copying out of the pool");
}

TEST("a repeated arm runs once for every lane, each seeing its own")
{
    return run_generated(
        "global total: Atomic<Int64> = Atomic(0)\n"
        "global ran: Atomic<Int32> = Atomic(0)\n"
        "fun add_lane(lane: Int32)\n"
        "  total.add(Int64(lane) * 10)\n"
        "  ran.add(1)\n"
        "end\n"
        "parallel do\n"
        "  with add_lane(lane) for lane in range(1, 6)\n"
        "end\n"
        "print(ran.load(), total.load())\n",
        "5 150\n", "repeated arm");
}

TEST("a repeated arm over an empty range runs nothing")
{
    return run_generated(
        "global ran: Atomic<Int32> = Atomic(0)\n"
        "fun tick() ran.add(1) end\n"
        "fun lane_tick(lane: Int32) ran.add(10) end\n"
        "parallel do\n"
        "  with lane_tick(lane) for lane in range(3, 3)\n"
        "  with tick()\n"
        "end\n"
        "print(ran.load())\n",
        "1\n", "empty repeated arm");
}

TEST("atomic tickets hand each job to exactly one lane")
{
    return run_generated(
        "global next: Atomic<Int32> = Atomic(0)\n"
        "global claimed: Atomic<Int64> = Atomic(0)\n"
        "global sum: Atomic<Int64> = Atomic(0)\n"
        "fun lane(total: Int32)\n"
        "  while true do\n"
        "    var i = next.add(1)\n"
        "    if i >= total then break end\n"
        "    claimed.add(1)\n"
        "    sum.add(Int64(i))\n"
        "  end\n"
        "end\n"
        "parallel do\n"
        "  with lane(20000) for k in range(0, 6)\n"
        "end\n"
        "print(claimed.load(), sum.load())\n",
        "20000 199990000\n", "ticket claiming");
}

TEST("lanes can allocate in one pool and the then section sees all of it")
{
    return run_generated(
        "global next: Atomic<Int32> = Atomic(0)\n"
        "fun lane(out: Store, total: Int32)\n"
        "  var mine: List<Int32> = out.list<Int32>(2)\n"
        "  while true do\n"
        "    var i = next.add(1)\n"
        "    if i >= total then break end\n"
        "    mine.push(i)\n"
        "  end\n"
        "end\n"
        "parallel pool = Store(1 << 20) do\n"
        "  with lane(pool, 5000) for k in range(0, 4)\n"
        "then\n"
        "  print(next.load() >= 5000)\n"
        "end\n",
        "true\n", "pool shared by lanes");
}

TEST("two arms genuinely run at the same time")
{
    return run_generated(
        "global a_ready: Atomic<Bool> = Atomic(false)\n"
        "global b_ready: Atomic<Bool> = Atomic(false)\n"
        "fun a_side(): Bool\n"
        "  a_ready.store(true)\n"
        "  var deadline: Int64 = now() + 5000\n"
        "  while !b_ready.load() && now() < deadline do sleep(1) end\n"
        "  return b_ready.load()\n"
        "end\n"
        "fun b_side(): Bool\n"
        "  b_ready.store(true)\n"
        "  var deadline: Int64 = now() + 5000\n"
        "  while !a_ready.load() && now() < deadline do sleep(1) end\n"
        "  return a_ready.load()\n"
        "end\n"
        "var a: Bool = false\n"
        "var b: Bool = false\n"
        "parallel do\n"
        "  with a = a_side()\n"
        "  with b = b_side()\n"
        "end\n"
        "print(a, b)\n",
        "true true\n", "arms overlap in time");
}

TEST("arms take turns printing without tearing a line")
{
    return run_generated(
        "global turn: Atomic<Int64> = Atomic(0)\n"
        "fun player(me: Int64, name: String<8>)\n"
        "  for round in range(0, 3) do\n"
        "    while turn.load() != round * 2 + me do sleep(1) end\n"
        "    print(name, round, \"is a whole line\")\n"
        "    turn.add(1)\n"
        "  end\n"
        "end\n"
        "parallel do\n"
        "  with player(0, \"ping\")\n"
        "  with player(1, \"pong\")\n"
        "end\n",
        "ping 0 is a whole line\npong 0 is a whole line\n"
        "ping 1 is a whole line\npong 1 is a whole line\n"
        "ping 2 is a whole line\npong 2 is a whole line\n",
        "printing from arms");
}

TEST("recursion in several arms at once has a depth guard per thread")
{
    return run_generated(
        "fun down(n: Int32): Int32\n"
        "  if n == 0 then return 0 end\n"
        "  return 1 + down(n - 1)\n"
        "end\n"
        "var a: Int32 = 0 var b: Int32 = 0 var c: Int32 = 0\n"
        "parallel do\n"
        "  with a = down(6000)\n"
        "  with b = down(6000)\n"
        "  with c = down(6000)\n"
        "end\n"
        "print(a, b, c)\n",
        "6000 6000 6000\n", "thread-local depth counters");
}

TEST("the first arm runs on the calling thread")
{
    return run_generated(
        "global first: Atomic<Int32> = Atomic(0)\n"
        "fun note(id: Int32) first.add(id) end\n"
        "parallel do\n"
        "  with note(1)\n"
        "end\n"
        "print(first.load())\n",
        "1\n", "one arm");
}

TEST("parallel blocks nest, and arms may start their own")
{
    return run_generated(
        "fun leaf(n: Int32): Int32 return n + 1 end\n"
        "fun mid(n: Int32): Int32\n"
        "  var r: Int32 = 0\n"
        "  parallel do with r = leaf(n) end\n"
        "  return r\n"
        "end\n"
        "var out: Int32 = 0\n"
        "var other: Int32 = 0\n"
        "parallel do\n"
        "  with out = mid(41)\n"
        "  with other = mid(9)\n"
        "end\n"
        "print(out, other)\n",
        "42 10\n", "nested parallel");
}

TEST("return, continue, and defer in a then section release the pool")
{
    return run_generated(
        "fun fill(out: Store, n: Int32): List<Int32>\n"
        "  var xs: List<Int32> = out.list<Int32>(n)\n"
        "  for i in range(0, n) do xs.push(i) end\n"
        "  return xs\n"
        "end\n"
        "fun run(early: Bool): Int32\n"
        "  defer do print(\"run done\") end\n"
        "  parallel pool = Store(1024) do\n"
        "    with var xs = fill(pool, 4)\n"
        "  then\n"
        "    defer do print(\"then defer\", xs.len()) end\n"
        "    if early then return xs.len() end\n"
        "    print(\"late\")\n"
        "  end\n"
        "  return 0\n"
        "end\n"
        "print(run(true))\n"
        "print(run(false))\n"
        "for i in range(0, 3) do\n"
        "  parallel pool = Store(256) do\n"
        "    with var xs = fill(pool, 2)\n"
        "  then\n"
        "    if i == 1 then continue end\n"
        "    print(\"loop\", i, xs.len())\n"
        "  end\n"
        "end\n",
        "then defer 4\nrun done\n4\nlate\nthen defer 4\nrun done\n0\n"
        "loop 0 2\nloop 2 2\n",
        "leaving a then section");
}

TEST("cores is at least one")
{
    return run_generated("print(cores() >= 1, cores() < 100000)\n",
                         "true true\n", "cores");
}

TEST("a trap in an arm ends the program with its message")
{
    return run_generated_failure(
        "global bad: Atomic<Int32> = Atomic(5)\n"
        "fun fail(xs: Array<Int32, 2>): Int32 return xs[bad.load()] end\n"
        "fun fine(): Int32 return 1 end\n"
        "var r: Int32 = 0\n"
        "var s: Int32 = 0\n"
        "parallel do\n"
        "  with r = fine()\n"
        "  with s = fail([1, 2])\n"
        "end\n",
        "index", "a trapping arm");
}

TEST("two arms that trap together report once and exit")
{
    return run_generated_failure(
        "global bad: Atomic<Int32> = Atomic(9)\n"
        "fun fail(xs: Array<Int32, 2>): Int32 return xs[bad.load()] end\n"
        "parallel do\n"
        "  with fail([1, 2]) for lane in range(0, 4)\n"
        "end\n",
        "index", "arms trapping together");
}

TEST("a pool that is too small traps with a clear message")
{
    return run_generated_failure(
        "fun fill(out: Store, n: Int32): List<Int32>\n"
        "  var xs: List<Int32> = out.list<Int32>(n)\n"
        "  for i in range(0, n) do xs.push(i) end\n"
        "  return xs\n"
        "end\n"
        "parallel pool = Store(64) do\n"
        "  with var xs = fill(pool, 1000)\n"
        "then\n"
        "  print(xs.len())\n"
        "end\n",
        MESSAGE_RUNTIME_POOL_FULL + sizeof(MESSAGE_RUNTIME_PREFIX) - 1,
        "a full pool");
}

TEST("a negative pool capacity traps")
{
    return run_generated_failure(
        SQUARE
        "var n: Int32 = -4\n"
        "parallel pool = Store(n) do\n"
        "  with square(1)\n"
        "end\n",
        MESSAGE_RUNTIME_NEGATIVE_CAPACITY + sizeof(MESSAGE_RUNTIME_PREFIX) - 1,
        "a negative capacity");
}

TEST("a repeated arm collects its results in lane order")
{
    return run_generated(
        "fun cube(n: Int32): Int32 return n * n * n end\n"
        "var cubes: Array<Int32, 8> = [99]\n"
        "parallel do\n"
        "  with cubes = cube(lane) for lane in range(2, 7)\n"
        "end\n"
        "print(cubes.len(), cubes[0], cubes[1], cubes[4])\n",
        "5 8 27 216\n", "collected lane results");
}

TEST("an empty repeated arm leaves its Array empty")
{
    return run_generated(
        "fun cube(n: Int32): Int32 return n * n * n end\n"
        "var cubes: Array<Int32, 4> = [1, 2]\n"
        "parallel do with cubes = cube(lane) for lane in range(3, 3) end\n"
        "print(cubes.len())\n",
        "0\n", "empty repeated arm");
}

TEST("a repeated arm that yields more results than the Array holds traps")
{
    return run_generated_failure(
        "fun cube(n: Int32): Int32 return n * n * n end\n"
        "var cubes: Array<Int32, 2> = []\n"
        "parallel do with cubes = cube(lane) for lane in range(0, 3) end\n",
        "a repeated arm produced more results than its Array holds",
        "too many lane results");
}

TEST("repeated-arm results of a struct type are collected in a function")
{
    return run_generated(
        "type Pair a: Int32 b: Int32 end\n"
        "fun make(n: Int32): Pair return Pair(n, n * 10) end\n"
        "fun gather(): Int32\n"
        "  var out: Array<Pair, 4> = []\n"
        "  parallel do with out = make(lane) for lane in range(1, 4) end\n"
        "  return out[2].b + out.len()\n"
        "end\n"
        "print(gather())\n",
        "33\n", "struct lane results");
}

TEST("thousands of back-to-back blocks reuse workers and keep every result")
{
    return run_generated(
        "global total: Atomic<Int64> = Atomic(0)\n"
        "fun add(n: Int32)\n"
        "  total.add(Int64(n))\n"
        "end\n"
        "for i in range(0, 3000) do\n"
        "  parallel do\n"
        "    with add(1)\n"
        "    with add(2)\n"
        "    with add(3) for lane in range(0, 2)\n"
        "  end\n"
        "end\n"
        "print(total.load())\n",
        "27000\n", "reused workers");
}

TEST("a block whose arms must all run at once still does after workers idle")
{
    return run_generated(
        "global arrived: Atomic<Int64> = Atomic(0)\n"
        "global passed: Atomic<Int64> = Atomic(0)\n"
        "fun nothing() end\n"
        "fun meet()\n"
        "  arrived.add(1)\n"
        "  while arrived.load() < 5 do end\n"
        "  passed.add(1)\n"
        "end\n"
        "parallel do with nothing() with nothing() end\n"
        "parallel do with meet() with meet() for lane in range(0, 4) end\n"
        "print(passed.load())\n",
        "5\n", "no queueing behind a blocked arm");
}
