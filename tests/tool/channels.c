/* Channels, timers, when, and the deadlock report in generated programs. */

#include "tool_support.h"

#define DEADLOCK (MESSAGE_RUNTIME_DEADLOCK + sizeof(MESSAGE_RUNTIME_PREFIX) - 1)

TEST("messages leave a channel in the order they entered")
{
    return run_generated(
        "global q: Channel<Int64> = Channel(4)\n"
        "q.send(1) q.send(2) q.send(3)\n"
        "var a = q.recv() var b = q.recv() var c = q.recv()\n"
        "if typeof a is Int64 && typeof b is Int64 && typeof c is Int64 then\n"
        "  print(a, b, c)\n"
        "end\n",
        "1 2 3\n", "fifo order");
}

TEST("a message is a copy of a plain value, and structs round trip")
{
    return run_generated(
        "type Msg id: Int32 name: String<8> pos: Array<Float64, 2> end\n"
        "global q: Channel<Msg> = Channel(2)\n"
        "var m: Msg = Msg(7, \"seven\", [1.5, 2.5])\n"
        "q.send(m)\n"
        "m = Msg(0, \"changed\", [0.0, 0.0])\n"
        "var got = q.recv()\n"
        "if typeof got is Msg then print(got.id, got.name, got.pos[1]) end\n",
        "7 seven 2.5\n", "struct messages");
}

TEST("try_send and try_recv never wait, and a full queue refuses a send")
{
    return run_generated(
        "global q: Channel<Bool> = Channel(2)\n"
        "print(q.try_send(true), q.try_send(false), q.try_send(true))\n"
        "var a = q.try_recv() var b = q.try_recv() var c = q.try_recv()\n"
        "print(typeof a is Bool, typeof b is Bool, typeof c is Nil)\n",
        "true true false\ntrue true true\n", "non-waiting operations");
}

TEST("closed is true only once the channel is closed and drained")
{
    return run_generated(
        "global q: Channel<Int64> = Channel(4)\n"
        "q.send(1)\n"
        "print(q.closed())\n"
        "q.close()\n"
        "print(q.closed())\n"
        "var a = q.recv()\n"
        "print(typeof a is Int64, q.closed())\n"
        "var b = q.recv()\n"
        "print(typeof b is Nil)\n",
        "false\nfalse\ntrue true\ntrue\n", "closed and drained");
}

TEST("a send on a closed channel returns false and keeps queued values")
{
    return run_generated(
        "global q: Channel<Int64> = Channel(4)\n"
        "q.send(5)\n"
        "q.close()\n"
        "q.close()\n"
        "print(q.send(6), q.try_send(7))\n"
        "var a = q.recv()\n"
        "if typeof a is Int64 then print(a) end\n",
        "false false\n5\n", "sending after close");
}

TEST("an optional message is wrapped in a struct")
{
    return run_generated(
        "type Reading value: Int32 | Nil end\n"
        "global q: Channel<Reading> = Channel(2)\n"
        "q.send(Reading(nil))\n"
        "q.send(Reading(4))\n"
        "var a = q.recv() var b = q.recv()\n"
        "if typeof a is Reading && typeof b is Reading then\n"
        "  print(typeof a.value is Nil, typeof b.value is Int32)\n"
        "end\n",
        "true true\n", "wrapper for an optional");
}

TEST("a timer ticks, carries the time it was due, and cannot be sent to")
{
    return run_generated(
        "global tick: Channel<Int64> = Every(20)\n"
        "var start: Int64 = now()\n"
        "var first = tick.recv()\n"
        "var second = tick.recv()\n"
        "if typeof first is Int64 && typeof second is Int64 then\n"
        "  print(second - first >= 20, now() - start >= 38)\n"
        "end\n"
        "print(tick.send(1), tick.try_send(1))\n",
        "true true\nfalse false\n", "timer ticks");
}

TEST("ticks nobody took are skipped, so a slow receiver sees no backlog")
{
    return run_generated(
        "global tick: Channel<Int64> = Every(60)\n"
        "var primed = tick.try_recv()\n"
        "sleep(290)\n"
        "var a = tick.try_recv()\n"
        "var b = tick.try_recv()\n"
        "print(typeof a is Int64, typeof b is Nil)\n",
        "true true\n", "timer coalescing");
}

TEST("a closed timer is closed and receives nil")
{
    return run_generated(
        "global tick: Channel<Int64> = Every(1000)\n"
        "print(tick.closed())\n"
        "tick.close()\n"
        "var a = tick.recv()\n"
        "print(tick.closed(), typeof a is Nil)\n",
        "false\ntrue true\n", "closing a timer");
}

TEST("when runs the first arm that is ready, in source order")
{
    return run_generated(
        "global a: Channel<Int64> = Channel(2)\n"
        "global b: Channel<Int64> = Channel(2)\n"
        "a.send(1) b.send(2)\n"
        "for i in range(0, 2) do\n"
        "  when\n"
        "    with x = b.recv() then print(\"b first\")\n"
        "    with y = a.recv() then print(\"a\")\n"
        "  end\n"
        "end\n"
        "a.send(1) b.send(2)\n"
        "when\n"
        "  with y = a.recv() then print(\"a first\")\n"
        "  with x = b.recv() then print(\"b\")\n"
        "end\n",
        "b first\na\na first\n", "arm priority");
}

TEST("when with an else never waits")
{
    return run_generated(
        "global a: Channel<Int64> = Channel(2)\n"
        "when\n"
        "  with x = a.recv() then print(\"recv\")\n"
        "else\n"
        "  print(\"nothing ready\")\n"
        "end\n"
        "a.send(9)\n"
        "when\n"
        "  with x = a.recv() then\n"
        "    if typeof x is Int64 then print(\"got\", x) end\n"
        "else\n"
        "  print(\"unexpected\")\n"
        "end\n",
        "nothing ready\ngot 9\n", "when with else");
}

TEST("a send arm sends once it can, and binds whether it was accepted")
{
    return run_generated(
        "global a: Channel<Int64> = Channel(1)\n"
        "global side: Atomic<Int64> = Atomic(0)\n"
        "fun value(): Int64 side.add(1) return 42 end\n"
        "when\n"
        "  with ok = a.send(value()) then print(\"sent\", ok)\n"
        "end\n"
        "when\n"
        "  with ok = a.send(value()) then print(\"sent again\")\n"
        "else\n"
        "  print(\"full\")\n"
        "end\n"
        "print(side.load())\n",
        "sent true\nfull\n2\n", "send arms evaluate their value once");
}

TEST("a send arm on a closed channel is ready and reports false")
{
    return run_generated(
        "global a: Channel<Int64> = Channel(1)\n"
        "a.close()\n"
        "when\n"
        "  with ok = a.send(1) then print(ok)\n"
        "end\n"
        "when\n"
        "  with x = a.recv() then print(typeof x is Nil)\n"
        "end\n",
        "false\ntrue\n", "closed channel in when");
}

TEST("break, continue, return, and defer work inside when arms")
{
    return run_generated(
        "global q: Channel<Int64> = Channel(8)\n"
        "fun scan(): Int64\n"
        "  defer do print(\"scan done\") end\n"
        "  var total: Int64 = 0\n"
        "  while true do\n"
        "    when\n"
        "      with v = q.recv() then\n"
        "        defer do print(\"arm\") end\n"
        "        if typeof v is Int64 then\n"
        "          if v == 2 then continue end\n"
        "          if v == 9 then return total end\n"
        "          total = total + v\n"
        "        else\n"
        "          break\n"
        "        end\n"
        "    end\n"
        "  end\n"
        "  return -1\n"
        "end\n"
        "q.send(1) q.send(2) q.send(3) q.send(9)\n"
        "print(scan())\n"
        "q.close()\n"
        "print(scan())\n",
        "arm\narm\narm\narm\nscan done\n4\narm\nscan done\n-1\n",
        "control flow in arms");
}

TEST("a timer arm lets a loop wait for a message or the next tick")
{
    return run_generated(
        "global tick: Channel<Int64> = Every(15)\n"
        "global never: Channel<Int64> = Channel(1)\n"
        "var n: Int32 = 0\n"
        "var start: Int64 = now()\n"
        "while n < 3 do\n"
        "  when\n"
        "    with x = never.recv() then print(\"never\")\n"
        "    with t = tick.recv() then n = n + 1\n"
        "  end\n"
        "end\n"
        "print(n, now() - start >= 40)\n",
        "3 true\n", "timer in when");
}

TEST("a pipeline of arms passes values through channels")
{
    return run_generated(
        "global input: Channel<Int64> = Channel(8)\n"
        "global output: Channel<Int64> = Channel(8)\n"
        "global total: Atomic<Int64> = Atomic(0)\n"
        "fun producer()\n"
        "  for i in range(1, 101) do input.send(i) end\n"
        "  input.close()\n"
        "end\n"
        "fun doubler()\n"
        "  while true do\n"
        "    var v = input.recv()\n"
        "    if typeof v is Int64 then output.send(v * 2) else break end\n"
        "  end\n"
        "  output.close()\n"
        "end\n"
        "fun consumer()\n"
        "  while true do\n"
        "    var v = output.recv()\n"
        "    if typeof v is Int64 then total.add(v) else break end\n"
        "  end\n"
        "end\n"
        "parallel do\n"
        "  with producer()\n"
        "  with doubler()\n"
        "  with consumer()\n"
        "end\n"
        "print(total.load())\n",
        "10100\n", "pipeline");
}

TEST("many producers and one consumer lose and duplicate nothing")
{
    return run_generated(
        "global jobs: Channel<Int64> = Channel(16)\n"
        "global done: Atomic<Int32> = Atomic(0)\n"
        "global sum: Atomic<Int64> = Atomic(0)\n"
        "global count: Atomic<Int64> = Atomic(0)\n"
        "fun produce(lane: Int32)\n"
        "  for i in range(0, 500) do jobs.send(Int64(lane) * 1000 + i) end\n"
        "  if done.add(1) == 3 then jobs.close() end\n"
        "end\n"
        "fun consume()\n"
        "  while true do\n"
        "    var v = jobs.recv()\n"
        "    if typeof v is Int64 then sum.add(v) count.add(1) else break end\n"
        "  end\n"
        "end\n"
        "parallel do\n"
        "  with produce(lane) for lane in range(0, 4)\n"
        "  with consume()\n"
        "  with consume()\n"
        "end\n"
        "print(count.load(), sum.load())\n",
        "2000 3499000\n", "many producers");
}

TEST("two arms can bounce a message many times without a false deadlock")
{
    return run_generated(
        "global ping: Channel<Int64> = Channel(1)\n"
        "global pong: Channel<Int64> = Channel(1)\n"
        "global last: Atomic<Int64> = Atomic(0)\n"
        "fun left()\n"
        "  ping.send(0)\n"
        "  while true do\n"
        "    var v = pong.recv()\n"
        "    if typeof v is Int64 then\n"
        "      if v >= 4000 then ping.close() break end\n"
        "      ping.send(v + 1)\n"
        "    else\n"
        "      break\n"
        "    end\n"
        "  end\n"
        "end\n"
        "fun right()\n"
        "  while true do\n"
        "    var v = ping.recv()\n"
        "    if typeof v is Int64 then last.store(v) pong.send(v + 1) else break end\n"
        "  end\n"
        "  pong.close()\n"
        "end\n"
        "parallel do with left() with right() end\n"
        "print(last.load() >= 3999)\n",
        "true\n", "ping pong");
}

TEST("three arms run a game loop and shut down by closing channels")
{
    return run_generated(
        "type Input key: Int32 end\n"
        "type Command push: Int32 end\n"
        "type World total: Int64 tick: Int64 end\n"
        "global input: Channel<Input> = Channel(16)\n"
        "global commands: Channel<Command> = Channel(16)\n"
        "global frames: Channel<World> = Channel(2)\n"
        "global tick: Channel<Int64> = Every(5)\n"
        "global drawn: Atomic<Int64> = Atomic(0)\n"
        "fun ui()\n"
        "  for i in range(1, 6) do\n"
        "    input.send(Input(Int32(i)))\n"
        "    sleep(3)\n"
        "  end\n"
        "  input.close()\n"
        "  while true do\n"
        "    var f = frames.recv()\n"
        "    if typeof f is World then drawn.add(1) else break end\n"
        "  end\n"
        "end\n"
        "fun logic()\n"
        "  while true do\n"
        "    var ev = input.recv()\n"
        "    if typeof ev is Input then commands.send(Command(ev.key * 10)) else break end\n"
        "  end\n"
        "  commands.close()\n"
        "end\n"
        "fun physics()\n"
        "  var world: World = World(0, 0)\n"
        "  while true do\n"
        "    when\n"
        "      with t = tick.recv() then\n"
        "        world = World(world.total, world.tick + 1)\n"
        "        frames.try_send(world)\n"
        "      with c = commands.recv() then\n"
        "        if typeof c is Command then\n"
        "          world = World(world.total + Int64(c.push), world.tick)\n"
        "        else\n"
        "          break\n"
        "        end\n"
        "    end\n"
        "  end\n"
        "  frames.close()\n"
        "  print(\"physics total\", world.total)\n"
        "end\n"
        "parallel do with ui() with logic() with physics() end\n"
        "print(\"frames drawn\", drawn.load() > 0)\n",
        "physics total 150\nframes drawn true\n", "game loop");
}

TEST("a receive that nothing can ever satisfy is reported as a deadlock")
{
    return run_generated_failure(
        "global q: Channel<Int64> = Channel(1)\n"
        "var v = q.recv()\n"
        "print(\"never\")\n",
        DEADLOCK, "deadlock in one thread");
}

TEST("a send to a full channel nobody reads is reported as a deadlock")
{
    return run_generated_failure(
        "global q: Channel<Int64> = Channel(1)\n"
        "q.send(1)\n"
        "q.send(2)\n",
        DEADLOCK, "deadlock on a full channel");
}

TEST("arms that wait for each other are reported as a deadlock")
{
    return run_generated_failure(
        "global a: Channel<Int64> = Channel(1)\n"
        "global b: Channel<Int64> = Channel(1)\n"
        "fun left() var x = a.recv() if typeof x is Int64 then b.send(1) end end\n"
        "fun right() var y = b.recv() if typeof y is Int64 then a.send(1) end end\n"
        "parallel do with left() with right() end\n"
        "print(\"never\")\n",
        DEADLOCK, "deadlock between arms");
}

TEST("an arm left waiting after the others finish is reported as a deadlock")
{
    return run_generated_failure(
        "global a: Channel<Int64> = Channel(1)\n"
        "fun quick() print(\"quick done\") end\n"
        "fun stuck() var x = a.recv() print(typeof x is Nil) end\n"
        "parallel do with quick() with stuck() end\n",
        DEADLOCK, "deadlock after the other arm ended");
}

TEST("an arm that finishes while another still waits is reported at its end")
{
    return run_generated_failure(
        "global a: Channel<Int64> = Channel(1)\n"
        "global b: Channel<Int64> = Channel(1)\n"
        "fun waiter() var x = a.recv() print(typeof x is Nil) end\n"
        "fun leaver() var y = b.recv() print(typeof y is Nil) end\n"
        "fun helper() sleep(30) b.send(1) end\n"
        "parallel do with waiter() with leaver() with helper() end\n",
        DEADLOCK, "deadlock noticed when an arm ends");
}

TEST("a when with nothing ready and no else is a deadlock when no one can send")
{
    return run_generated_failure(
        "global a: Channel<Int64> = Channel(1)\n"
        "global b: Channel<Int64> = Channel(1)\n"
        "when\n"
        "  with x = a.recv() then print(\"a\")\n"
        "  with y = b.recv() then print(\"b\")\n"
        "end\n",
        DEADLOCK, "deadlock in when");
}

TEST("a thread that sleeps or waits on a timer is not a deadlock")
{
    return run_generated(
        "global a: Channel<Int64> = Channel(1)\n"
        "global tick: Channel<Int64> = Every(10)\n"
        "fun late() sleep(40) a.send(5) end\n"
        "fun waiter(): Int64\n"
        "  var x = a.recv()\n"
        "  if typeof x is Int64 then return x end\n"
        "  return 0\n"
        "end\n"
        "var got: Int64 = 0\n"
        "parallel do with got = waiter() with late() end\n"
        "print(got)\n"
        "var t = tick.recv()\n"
        "print(typeof t is Int64)\n",
        "5\ntrue\n", "no false deadlock");
}

TEST("a raised value limit lets a large message cross a channel between threads")
{
    return run_generated_limit(
        "type Frame\n"
        "  id: Int32\n"
        "  pixels: Array<Byte, 500000>\n"
        "end\n"
        "global frames: Channel<Frame> = Channel(2)\n"
        "global seen: Atomic<Int64> = Atomic(0)\n"
        "fun render()\n"
        "  for i in range(1, 4) do\n"
        "    var px: Array<Byte, 500000> = []\n"
        "    px.push(Byte(7))\n"
        "    frames.send(Frame(Int32(i), px))\n"
        "  end\n"
        "  frames.close()\n"
        "end\n"
        "fun show()\n"
        "  while true do\n"
        "    var f = frames.recv()\n"
        "    if typeof f is Frame then seen.add(Int64(f.id) * 1000 + Int64(f.pixels.len())) else break end\n"
        "  end\n"
        "end\n"
        "parallel do with render() with show() end\n"
        "print(seen.load())\n",
        1024 * 1024, "6003\n", "large messages");
}

TEST("a rendezvous send waits until a receiver has taken the value")
{
    /* Both arms finish after the handoff, so their order is unspecified: only
       the main thread prints. */
    return run_generated(
        "global r: Channel<Int64> = Channel(0)\n"
        "global got: Atomic<Int64> = Atomic(0)\n"
        "global waited: Atomic<Int64> = Atomic(0)\n"
        "fun taker()\n"
        "  sleep(40)\n"
        "  var v = r.recv()\n"
        "  if typeof v is Int64 then got.store(v) end\n"
        "end\n"
        "fun giver()\n"
        "  var start: Int64 = now()\n"
        "  var ok: Bool = r.send(9)\n"
        "  if ok && now() - start >= 30 then waited.store(1) end\n"
        "end\n"
        "parallel do with giver() with taker() end\n"
        "print(got.load(), waited.load())\n",
        "9 1\n", "rendezvous handoff");
}

TEST("a when receive arm takes from a parked rendezvous sender and releases it")
{
    return run_generated(
        "global r: Channel<Int64> = Channel(0)\n"
        "global done: Atomic<Int64> = Atomic(0)\n"
        "fun giver()\n"
        "  var ok: Bool = r.send(5)\n"
        "  if ok then done.store(1) end\n"
        "end\n"
        "fun taker()\n"
        "  when\n"
        "    with v = r.recv() then\n"
        "      if typeof v is Int64 then print(\"got\", v) end\n"
        "  end\n"
        "end\n"
        "parallel do with giver() with taker() end\n"
        "print(done.load())\n",
        "got 5\n1\n", "when over rendezvous");
}

TEST("closing a rendezvous channel withdraws a parked send")
{
    return run_generated(
        "global r: Channel<Int64> = Channel(0)\n"
        "global res: Atomic<Int64> = Atomic(-1)\n"
        "fun giver()\n"
        "  var ok: Bool = r.send(5)\n"
        "  if ok then res.store(1) else res.store(0) end\n"
        "end\n"
        "fun closer()\n"
        "  sleep(40)\n"
        "  r.close()\n"
        "  var v = r.recv()\n"
        "  print(typeof v is Nil)\n"
        "end\n"
        "parallel do with giver() with closer() end\n"
        "print(res.load())\n",
        "true\n0\n", "close withdraws");
}

TEST("a rendezvous send with no receiver is reported as a deadlock")
{
    return run_generated_failure(
        "global r: Channel<Int64> = Channel(0)\n"
        "r.send(1)\n",
        DEADLOCK, "rendezvous deadlock");
}

TEST("a pipeline of pool channels passes values between three arms")
{
    return run_generated(
        "fun producer(jobs: Channel<Int64>)\n"
        "  for i in range(1, 6) do jobs.send(Int64(i)) end\n"
        "  jobs.close()\n"
        "end\n"
        "fun squarer(jobs: Channel<Int64>, results: Channel<Int64>)\n"
        "  while true do\n"
        "    var j = jobs.recv()\n"
        "    if typeof j is Int64 then results.send(j * j) else break end\n"
        "  end\n"
        "  results.close()\n"
        "end\n"
        "fun collect(results: Channel<Int64>)\n"
        "  var sum: Int64 = 0\n"
        "  while true do\n"
        "    var r = results.recv()\n"
        "    if typeof r is Int64 then sum = sum + r else break end\n"
        "  end\n"
        "  print(sum)\n"
        "end\n"
        "parallel pool = Store(4096) do\n"
        "  var jobs = pool.channel<Int64>(2)\n"
        "  var results = pool.channel<Int64>(2)\n"
        "  with producer(jobs)\n"
        "  with squarer(jobs, results)\n"
        "  with collect(results)\n"
        "end\n",
        "55\n", "pool pipeline");
}

TEST("every run of a block gets fresh channels, so a closed one never carries over")
{
    /* b is never closed and listed first, so its value is always taken before
       a's close ends the loop: the total is the same on every run. */
    return run_generated(
        "fun feed(a: Channel<Int64>, b: Channel<Int64>)\n"
        "  b.send(10)\n"
        "  a.send(1)\n"
        "  a.send(2)\n"
        "  a.close()\n"
        "end\n"
        "fun drain(a: Channel<Int64>, b: Channel<Int64>)\n"
        "  var total: Int64 = 0\n"
        "  var go = true\n"
        "  while go do\n"
        "    when\n"
        "      with y = b.recv() then\n"
        "        if typeof y is Int64 then total = total + y end\n"
        "      with x = a.recv() then\n"
        "        if typeof x is Int64 then total = total + x else go = false end\n"
        "    end\n"
        "  end\n"
        "  print(total)\n"
        "end\n"
        "for round in range(0, 3) do\n"
        "  parallel pool = Store(4096) do\n"
        "    var a = pool.channel<Int64>(4)\n"
        "    var b = pool.channel<Int64>(4)\n"
        "    with feed(a, b)\n"
        "    with drain(a, b)\n"
        "  end\n"
        "end\n",
        "13\n13\n13\n", "fresh channels per block");
}

TEST("one arm per lane can each own a channel from the pool")
{
    return run_generated(
        "global total: Atomic<Int64> = Atomic(0)\n"
        "fun lane_worker(lane: Int32, c: Channel<Int32>)\n"
        "  c.send(lane * 100)\n"
        "  var v = c.recv()\n"
        "  if typeof v is Int32 then total.add(Int64(v)) end\n"
        "end\n"
        "parallel pool = Store(8192) do\n"
        "  var shared = pool.channel<Int32>(8)\n"
        "  with lane_worker(lane, shared) for lane in range(1, 5)\n"
        "end\n"
        "print(total.load())\n",
        "1000\n", "lanes sharing a channel");
}

TEST("a pool channel works from the then section after the arms have returned")
{
    return run_generated(
        "fun fill(c: Channel<Int32>) c.send(7)\n c.send(8) end\n"
        "parallel pool = Store(1024) do\n"
        "  var c = pool.channel<Int32>(4)\n"
        "  with fill(c)\n"
        "then\n"
        "  var a = c.try_recv()\n"
        "  var b = c.try_recv()\n"
        "  var none = c.try_recv()\n"
        "  print(typeof a is Int32, typeof b is Int32, typeof none is Nil)\n"
        "end\n",
        "true true true\n", "then section receives");
}

TEST("a channel can be allocated only in a parallel pool")
{
    return run_generated_failure(
        "scope s = Store(1024) do\n"
        "  var c = s.channel<Int32>(2)\n"
        "  print(c.try_send(1))\n"
        "end\n",
        "a channel can be allocated only in a parallel block's pool",
        "channel in a scope Store");
}

TEST("a pool channel needs a capacity from 1 to the channel limit")
{
    TestResult zero = run_generated_failure(
        "fun nothing() end\n"
        "parallel pool = Store(1024) do\n"
        "  var c = pool.channel<Int32>(0)\n"
        "  with nothing()\n"
        "then\n"
        "  print(c.closed())\n"
        "end\n",
        "a channel capacity must be from 1 to 65536", "capacity zero");
    TestResult huge;

    if (zero != TEST_PASS) return zero;
    huge = run_generated_failure(
        "fun nothing() end\n"
        "parallel pool = Store(1024) do\n"
        "  var c = pool.channel<Int32>(70000)\n"
        "  with nothing()\n"
        "then\n"
        "  print(c.closed())\n"
        "end\n",
        "a channel capacity must be from 1 to 65536", "capacity too large");
    return huge;
}

TEST("a pool that cannot hold a channel traps as a full pool")
{
    return run_generated_failure(
        "fun nothing() end\n"
        "parallel pool = Store(16) do\n"
        "  var c = pool.channel<Int64>(100)\n"
        "  with nothing()\n"
        "then\n"
        "  print(c.closed())\n"
        "end\n",
        "parallel store is full", "pool too small for a channel");
}

TEST("a deadlock among arms waiting on pool channels is reported")
{
    return run_generated_failure(
        "fun wait_on(c: Channel<Int32>)\n"
        "  var v = c.recv()\n"
        "  print(typeof v is Int32)\n"
        "end\n"
        "parallel pool = Store(1024) do\n"
        "  var a = pool.channel<Int32>(1)\n"
        "  var b = pool.channel<Int32>(1)\n"
        "  with wait_on(a)\n"
        "  with wait_on(b)\n"
        "end\n",
        DEADLOCK, "pool channel deadlock");
}

TEST("a pool channel used after its block ended is a stale handle")
{
    return run_escaped_failure(
        "fun nothing() end\n"
        "parallel p0 = Store(1024) do\n"
        "  var keep = p0.channel<Int32>(2)\n"
        "  with nothing()\n"
        "then\n"
        "  parallel pool = Store(1024) do\n"
        "    var c = pool.channel<Int32>(2)\n"
        "    with nothing()\n"
        "  then\n"
        "    keep = c\n"
        "  end\n"
        "  print(keep.try_send(1))\n"
        "end\n",
        "stale", "a stale pool channel");
}
