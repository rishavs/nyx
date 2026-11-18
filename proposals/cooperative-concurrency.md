# Parallel blocks, atomics, and channels

- Feature names: `parallel`, `Atomic`, `Channel`, `when`, `defer`
- Start date: 2026-11-13
- Revised: 2026-11-17
- Status: implemented. `SEMANTICS.md` is the language reference; this document
  records why the design is what it is and what is still open.
- Replaces: two earlier designs, a fork/join `parallel_map` and a `spawn` /
  `join` / channels design with lent Stores. Neither shipped. Their history is in
  the repository.

## Summary

Nyx runs work on several threads with three small pieces that fit together:

- `parallel ... do ... end` runs function calls at the same time and waits for all
  of them. Its arms are threads that cannot outlive the block, so there are no
  thread handles, no `join`, and no detached threads.
- A block may own a **pool**, a fixed-capacity Store that every arm allocates in
  without waiting. Objects stay private to the arm that made them until the arms
  return, and a `then` section reads them all on one thread.
- Global **`Atomic`** cells and global **`Channel`s** are the only things arms
  share while they run. A channel carries a copy of a plain value.

The rule behind all of it: **threads never share mutable memory.** An arm
receives copies of plain values, cannot reach a global Store, and talks to other
arms only through a resource whose every operation is synchronised. Data races,
which are undefined behaviour in C11, are impossible by construction and checked
at compile time.

## Why this shape

**Scoped arenas set the theme.** A `scope` Store has a lexical lifetime and the
compiler rejects any value that could outlive it. A `parallel` block is the same
idea for threads: arms live exactly as long as the block, the pool dies at its
`end`, and a value from the pool cannot be stored where it outlives the block.
The escape analysis needed no new concept for this.

**Structured threads remove most of a thread API.** With no handles there is no
`Thread<R>`, no result-by-join, no thread that outlives the Store it was lent,
and no "did you join it" check. A server is one block whose arms never return; a
game is a block whose arms return when their channels close.

**Plain values cross, nothing else does.** A value is *plain* when it holds no
`Text`, `Handle`, `List`, `Slice`, `Table`, Store, or function. The rule is a
walk over the type graph, and it is exactly the set of values the escape analysis
already calls pointer-free. It applies to an arm's arguments, its assigned
result, and a channel's element.

## Decisions and the alternatives they beat

- **POSIX threads, not C11 threads.** glibc does not register `thrd_create`
  threads with ThreadSanitizer, so a race in generated code could not be checked.
  With `pthread` the race detector runs on the generated C, and it is clean on
  the programs in the tests.
- **A pool with an atomic cursor, not a growable Store.** Growth moves the buffer
  while another thread reads it. A fixed capacity never moves, and an allocation
  is one compare-and-swap. A full pool is a clear trap.
- **Objects private until the arms return, not shared mid-block.** An arm gets
  only plain values and the pool, so it holds no pointer to another arm's
  objects. That makes sharing the pool safe without locks, and it is why a
  `then` section is needed: results that point into the pool must be read while
  the pool is alive.
- **Atomics are global, scalar, and sequentially consistent.** Global means a
  static address and no lifetime or escape question; scalar means a cell cannot
  hide a data race; one memory order means nothing to get wrong. `add` returns
  the previous value so it hands out tickets.
- **Channels carry plain values, and live in static storage or a pool.** Same
  reasoning as atomics. The
  alternative that carries arenas (moving a Store between threads) needs a
  moved-from check on a Store variable and a Store whose lifetime is not lexical,
  which is ownership, and Nyx avoids that.
- **One process-wide lock for all channels.** It is what lets `when` wait on
  several channels and lets the deadlock report see every thread. The cost is that
  channel traffic from different threads serialises; at the scale of an engine or
  a server's job queue this is not the bottleneck. Per-channel locks would need a
  waiter list per channel.
- **`when` picks the first ready arm in source order.** Randomising or rotating
  would hide the priority in the runtime. With source order the programmer states
  it by writing the arms in that order.
- **`recv` returns `T | Nil`.** A distinct `Closed` type would allow `Nil`
  elements at the price of a built-in type, and a one-field wrapper struct covers
  the rare optional message.
- **A deadlock is a trap.** A hung server is the worst failure, and the one
  process-wide lock makes the accounting cheap. Counting the thread that waits for
  a block's arms only while an arm is unfinished, and clearing the blocked count on
  every state change, are what keep it from reporting a deadlock that is not one.
- **Why not Go's model.** Goroutines need a scheduler and a garbage collector,
  which Nyx rules out, and they share memory freely.
- **Why not locks or mutexes.** They let threads share mutable state, and locking
  bugs are what rule 14 asks Nyx to remove. A shared structure belongs in C
  interop.

## Added after the first cut

These were listed as gaps and are now built; each is a small extension of the
rules above, not a new mechanism.

- **Large messages.** The 64 KiB by-value cap became `CompilerConfig.max_value_bytes`
  (up to 16 MiB). A message, an arm argument, and an arm result are bounded by it,
  so a program that needs a 500 KB frame raises the cap and accepts the stack cost.
  Whether the default should move, and whether the cap should be tied to stack use,
  stays in `TODO.md`.
- **Rendezvous channels.** Capacity 0 on a global channel: a send waits until a
  receiver has taken the value, and `close` withdraws a parked value so the sender
  gets `false`. `try_send` and a `when` send arm are rejected on one, because a
  send arm cannot wait for the receiver after the lock is released.
- **Channels per block.** `pool.channel<T>(n)` makes a channel in a parallel pool
  and returns a `Channel<T>` value. It lives exactly as long as the block, so each
  run gets fresh state and a program can have one per lane. The pool is the only
  place allowed, because it never moves; a growable Store could reallocate under a
  thread waiting on a channel in it. A value is a descriptor (pool, offset,
  generation) checked on every use and covered by the existing escape analysis.
  To make channels before the arms start, a block may begin with `var`/`const`
  declarations.
- **Results from a repeated arm.** `with xs = f(lane) for lane in range(a, b)` fills
  an existing `Array<R,N>` in lane order. A pool-backed `List` would be the more
  general form and is still open.
- **Thread reuse.** Arms run on detached workers that wait between blocks. A job
  goes to an idle worker, or a new worker is started if none is idle, so an arm that
  blocks can never hold up another arm waiting in a queue. Measured on the
  development machine, a block of two arms costs about two thirds of what
  creating and joining a thread cost (most of both is kernel wake-up latency);
  nothing is created after warm-up.
- **Blocking outside channels.** There is nothing to fix yet: the language has no
  file or socket input. A thread blocked in such a call would be counted as
  running, so the report could never be wrong, only silent. It must learn about
  those waits when I/O arrives.

## What this does not do

- **Sharing a large object while threads run.** Arms cannot read existing Store
  data, so a big read-only world must be copied in or sent as messages. Options
  considered and not adopted: a frozen read-only Store that arms may read; sending
  ownership of a pool object through a channel; tracking which Store a value
  points into, which the analyser, tracking only a depth, cannot do.
- **Pool rendezvous channels, and a pool channel as a field or a result.** The first
  needs a capacity-0 path in `when`; the second needs the analyser to follow a
  descriptor through a struct.
- **Fairness.** Source order can starve a later arm behind a busy earlier one, and a
  closed channel is ready forever, so an arm on it starves everything after it.

## Implementation notes

- Generated C for a program that uses none of this is unchanged. Threaded
  programs make the scope generation counter atomic, the recursion depth counters
  `_Thread_local`, hold the stdout lock for each `print`, and route `exit` through
  a once-only guard so two trapping threads cannot both call it.
- The analyser audits every function an arm can reach through named calls and
  rejects a global Store or a call through a function value. Atomics, channels,
  `print`, `now`, `sleep`, scopes, pool channels, and nested `parallel` blocks are
  allowed.
- The poisoned-memory build (`make test-poison`) caught two use-after-realloc bugs
  in the checker while this was built: a pointer into the type or node list must
  not be held across a call that pushes onto it.
- Verification: unit and golden tests, the poisoned build, and ThreadSanitizer,
  AddressSanitizer, and UBSan runs of the threaded programs (see `BUILD.md`).

## Future possibilities

Collecting repeated-arm results into a pool-backed `List`; moving a pool object
through a channel; a frozen read-only Store shared by arms; pool rendezvous
channels; `swap` and `compare_exchange` on atomics; timers with a one-shot form;
and, once the language has I/O, deadlock reports that account for blocking reads.
