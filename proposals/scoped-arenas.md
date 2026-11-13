# Scoped Arenas

**Status:** accepted direction, not implemented; block-scoped locals are implemented
**Goal:** maximize compile-time memory safety for arena-backed handles without a general ownership or borrow checker.

## Decision record

Accepted direction:

- One `Store` type. A store's lifetime comes from its binding scope.
- Program-level `var root: Store = Store(...)` is the program-lifetime store
  (`'static`). It has no `reset()`.
- `scope frame = Store(...) do ... end` creates a lexical arena.
- Handles from a program-level store can be passed and returned freely.
- Handles from a scoped store are region-bound and cannot escape.
- `reset()` is removed. Scope exit is the only reset. Keeping it sound would need
  epoch and effect tracking across calls, and removing it makes the per-value
  generation counters dead weight in safe code (see
  [Runtime checks after the static guarantee](#runtime-checks-after-the-static-guarantee)).
- A region is a **lexical depth integer**: program-level stores are depth 0 and
  every `scope` adds one. The whole checker is one judgment,
  `outlives(dest, src)`, applied uniformly to assignment, `return`, and `alloc`
  (see [Regions are depth integers](#regions-are-depth-integers)).
- Functions use **one elision rule instead of region annotations**: a call's
  result region is the deepest region among its region-carrying arguments (see
  [Function boundaries](#function-boundaries-the-elision-rule)).
- Functions that allocate into a scoped store take an ordinary `Store`
  parameter. There is **no `borrow` keyword**: `Store` is already non-copyable and
  cannot be returned or stored, so a `Store` parameter is borrow-only by
  construction.
- Region propagation through aggregates, `Store` parameters, and buffers/slices
  are part of the useful feature, not later polish.
- Arena slots cannot contain handles in the first version. This keeps regions
  flowing only through variables and structs, never through memory. A later
  extension lets a slot hold handles of its own store.
- A `scope` inside a loop compiles to a hoisted, recycled buffer, so per-iteration
  reuse costs what `reset()` did, without its hazard.
- There is no separate persistent store type, mode, or keyword.

Current compiler status: it has block-scoped locals and arena-backed `Text`,
`TextBuilder`, `List`, and `Slice`, but still has `reset()`, no `scope`, no
regions, and no `Store` parameters. Every stale-use check is a runtime check
today. This document is the target design, not a description of implemented
behavior.

## Summary

Arena allocation gives Nippy typed, non-null, bounds-safe handles. The remaining
dynamic lifetime event is `reset()`. This proposal makes that lifetime **lexical**
by introducing `scope` blocks:

```nippy
scope frame = Store(4096) do
  var p: Handle<Point> = frame.alloc<Point>(Point(1, 2))
  var q: Point = p.read()
end
```

A handle allocated from `frame` is valid only inside the block. At the end of the
block the arena is released. A scoped handle cannot escape the scope. There is no
`reset()` for a scoped store; scope exit is the reset.

The intended result:

- every safe Nippy handle is non-null;
- every safe Nippy handle points to a live slot of the correct type;
- every read/write is in bounds by construction;
- use-after-reset is a compile-time error, not a runtime panic;
- bounds checks remain, and the generation check becomes a defensive assertion
  for C interop and compiler bugs that safe code can drop (see
  [Runtime checks after the static guarantee](#runtime-checks-after-the-static-guarantee)).

This proposal does **not** require move semantics, owner backpointers, or
one-owner handles. Those are separate features for aliasing and exclusive
mutation.

## Motivation

Today a persistent global store and a handle look like this:

```nippy
var points: Store = Store(4096)

var p: Handle<Point> = points.alloc<Point>(Point(1, 2))
```

The handle carries a store id, offset, and generation. `read` and `write` check
all three. Safety is enforced at runtime.

The only way that handle becomes invalid is:

```nippy
points.reset()
```

At that point the store generation changes and every old handle panics on use.
That is safe, but not compile-time safe: the language permits a program that is
guaranteed to fail.

Scoped arenas make the invalidation point lexical. There is no point in the
program where a scoped handle can be used after its arena has died.

## Goals

1. Provide a lexical arena whose handles cannot outlive it.
2. Make escape detection a compile-time error.
3. Reclaim arena memory deterministically at scope exit.
4. Avoid moving, borrowing, or unique-owner analysis for ordinary handles.
5. Keep the existing `Handle<T>` surface type and `read`/`write` methods.
6. Keep runtime checks as defense in depth.

## Non-goals

- Per-object `free`.
- Unique ownership or one-owner handles.
- General-purpose borrow checking beyond store regions.
- Concurrent access to a shared arena.
- Extending the existing dynamic `Text` implementation. This proposal only
  makes its lifetime lexical; `Text` already exists and is runtime-checked.

## Programs this model does not serve

The model has two lifetimes: program lifetime and lexical scope. Programs whose
resources need a **dynamic lifetime between those two** are the gap.

### Long-lived resources with non-lexical lifetimes

Examples:

- WebSocket or long-polling sessions that live across many requests;
- game lobbies, matchmaking sessions, or streaming chunks;
- job queues and background tasks whose state outlives the call that started
  them;
- plugin instances that are loaded and unloaded at runtime;
- caches with eviction.

A `scope` ends at a lexical `end`. A program-level store ends at process exit.
There is no arena that can be created for a session and destroyed when that
session ends while other sessions continue. Keeping session state in the
program-level store avoids memory unsafety, but the memory is never reclaimed.

This is the most important limitation. A third mechanism — a dynamically scoped
arena with an explicit close and a check that no handles remain live — would be
needed. That is a separate ownership/lifetime feature, not part of this proposal.

### Per-object reclamation and bounded long-running memory

Global stores grow until exit. Scoped stores free everything at once. A program
with a bounded live set but unbounded allocation churn needs one of:

- dynamic arena lifetimes;
- per-object free;
- a custom slab/free-list allocator inside an arena buffer.

A slab allocator can be written at the Nippy level for fixed-size slots, but it
is low-level, type-specific, and still cannot return memory to the OS. It is not
a general replacement for per-object reclamation.

### Long-running servers and OOM

The program-level store is a monotonic bump arena: it grows and never reclaims
until process exit. A server that copies per-request data into it is not moving
memory between scopes; it is allocating a permanent copy. If that happens for
sessions, caches, queues, or logs on every request, the store grows without
bound and eventually hits OOM.

Scoped arenas fix only the request/operation path. They do not bound the
program-level working set.

The practical options for a server are:

1. **Keep the program-level store bounded.** Put only startup configuration,
   fixed tables, and genuinely process-lifetime data there. Do not promote
   transient request data into it.
2. **Use a scope per request/event.** Write the response, stream the result, or
   copy only the small long-lived summary. The rest is freed at scope exit.
3. **Use a scope per connection** where the server handles a connection inside
   one lexical block (one thread/actor per connection). The session arena then
   ends with the connection.
4. **Add dynamically scoped arenas** for sessions, caches, jobs, and plugins.
   Create an arena for the session/cache generation, close it when the owner
   ends, and ensure no handles escape it.
5. **Use fixed-size pools/generations** and rebuild a cache by replacing an
   entire generation, retiring the old one when no request still references it.
6. **Use foreign memory** for long-lived state managed by C, accepting that the
   arena safety guarantee does not apply to it.

The current model has no graceful OOM: allocation failure is fatal. A server
that needs backpressure or a `503` response under memory pressure needs fallible
allocation, which is a separate feature.

Without one of the dynamic-lifetime mechanisms above, an unbounded server is
not a good fit for this model. It is safe, but it will eventually stop. That is
an important limitation to state honestly.

### Async, callbacks, and state across suspension

A scoped arena ends when its lexical block ends. An `async` task, coroutine, or
C callback that suspends while it still holds scoped values cannot use an
ordinary scope: the scope would need to remain alive across suspension, and the
callback may run after the creating function returns.

Without async/callback support this is not current syntax, but it is the same
dynamic-lifetime gap. A task/session arena with an explicit lifetime would be
required.

### Stable pointers and foreign retention

Arena growth uses `realloc` and moves the backing block. C code that retains a
pointer into an arena across a later allocation can see it dangle. A scoped
arena that is freed while C still holds a pointer is also unsafe.

FFI therefore needs one of:

- copy in/out at the boundary;
- pass arena buffers only for the duration of a call;
- a pinning/stable-buffer API;
- a C-owned allocation that Nippy does not manage.

The last option is often correct: foreign resources such as textures, sockets,
and decoder contexts should have foreign lifetimes, not arena lifetimes.

### Custom allocators and low-level memory

No raw pointers and no per-object free means Nippy cannot directly express
memory-mapped I/O, DMA buffers, kernel allocators, custom page management, or
algorithms that depend on stable addresses and pointer arithmetic. FFI can
delegate those to C, but the Nippy side cannot manage them with the arena model.

### Graceful out-of-memory and real-time constraints

Allocation failure is fatal. Programs that must recover from memory exhaustion,
degrade gracefully, or guarantee allocation latency are not served by this model.
A fallible `alloc` returning an error would be a separate change.

### Concurrency

Global and scoped stores are not thread-safe. Concurrent access to a shared arena
needs synchronization, ownership, or per-thread arenas. That is a separate
concurrency design.

### What is still fine

The model covers programs whose dynamic data has either operation-scoped or
process-scoped lifetime:

- CLI tools and batch processors;
- compilers and parsers for one document at a time;
- request-scoped HTTP servers;
- image/video frame or per-file processing;
- games with level/frame scopes and a game-lifetime world;
- simulations with phase-scoped scratch data;
- caches whose size is bounded by the program-level store and rarely evicted.

The limitation is not computability. It is **resource lifetime and reclamation**.
If the target includes long-lived sessions, plugin unload, per-object free, or
async state, the two-lifetime model needs a third lifetime mechanism.

## Syntax

```ebnf
scope-statement = "scope", identifier, "=", "Store", "(", expression, ")",
                  "do", block, "end" ;
```

Example:

```nippy
scope frame = Store(4096) do
  var p: Handle<Point> = frame.alloc<Point>(Point(1, 2))
  var n: Int32 = p.read().x    // once field access exists
end
```

A `scope` is a statement, so it may appear:

- at program level;
- inside a function body;
- inside an `if`, `while`, or another `scope`.

`frame` is a store capability, not an ordinary value:

- it is visible only inside the block;
- it cannot be assigned, passed, returned, or stored in a field;
- `frame.reset()` is not available;
- `frame.alloc<T>(...)` is the only allocation operation;
- handles produced by it are scoped handles.

The block is a new lexical scope. Bindings declared inside it are not visible
after `end`.

## Prerequisites

Nippy has program-level variables, function parameters, and block-scoped local
variables. A `scope` block can therefore use the same **block-scoped `var`
bindings** the grammar already provides:

```nippy
scope frame = Store(4096) do
  var p: Handle<Point> = frame.alloc<Point>(Point(1, 2))
  var q: Point = p.read()
  var n: Int32 = q.x
end
```

The scope binding is the prerequisite, not `scope` itself, so implementation
phase 1 is complete. Three other pieces move from “later” to practical
prerequisites after the worked examples below:

1. **`Store` parameters.** Parsers, formatters, and allocator helpers must
   be able to allocate into a caller’s arena. No new keyword is needed; see
   [Store parameters](#store-parameters).
2. **Region-carrying returns.** A helper that allocates a `Text` or AST in
   the caller’s arena must return a value tied to that arena’s region. The
   [elision rule](#function-boundaries-the-elision-rule) provides this without
   region annotations.
3. **Region propagation through aggregates.** `Context`, AST nodes, records, and
   game state naturally contain handles and store parameters.

A demo can avoid these by allocating only at the `scope` site. Real programs
cannot. Treat them as part of the useful feature, not as optional polish.

## Semantics

### Region

Each `scope` introduces a fresh region:

```text
Handle<'frame, T>
```

The surface type remains `Handle<T>`; the region is inferred and checked by the
compiler. Two handles are compatible only if their regions are compatible.

A handle from `frame` has the `'frame` region. It may be:

- used as a value inside the block;
- copied to another binding inside the block;
- read and written inside the block;
- stored in a local aggregate that does not escape the block.

It may not be:

- assigned to a binding declared outside the block;
- returned from the block or function;
- stored in a program-level variable;
- stored in a struct or union that outlives the block;
- returned or stored by a callee past the caller's region; the
  [elision rule](#function-boundaries-the-elision-rule) makes passing it by value
  to a function safe.

### Regions are depth integers

The `'frame` notation above is explanatory. The checker does not need named
region variables, subtyping between regions, or a region environment. A region is
an integer, the lexical nesting depth of the store that owns the slot:

```text
program-level store   -> depth 0   ('static)
scope at depth k      -> depth k+1 for the stores it declares
```

Three quantities are compared, and all of them are depths:

- the **region of a value** is the deepest depth among the handles, `Text`,
  `List`, `Slice`, `TextBuilder`, and table descriptors it contains (`0` if it
  contains none);
- the **region of a variable** is the depth of the block that binds it
  (program-level variables are `0`);
- the **region of a store** is the depth at which it was declared.

One judgment covers every escape rule:

```text
outlives(dest, src)  :=  src <= dest
```

It is the only check, applied at three sites:

| Site | `dest` | Rejected when |
|---|---|---|
| `x = v` or `var x = v` | depth of the block that binds `x` | `region(v)` is deeper than `x`'s depth |
| `return v` | the caller's region (`0` for a function with no region-carrying parameters; see [Function boundaries](#function-boundaries-the-elision-rule)) | `region(v)` is deeper than that |
| `xs.push(v)`, `xs[i] = v`, `t[k] = v`, and any other write that stores a region-carrying `v` into a container | depth of the container | `region(v)` is deeper than the container |

For example, `a = b` in the nested-scope example below fails because `b` has
depth 2 and `a` was bound at depth 1. The three rules listed under
[Escape rules](#escape-rules) are this one comparison.

This is a small local check: one integer per expression and per binding, no
fixpoint, no flow sensitivity. It is the same rule at every site, which is the
point of the Cohesive pillar, and it needs no inference beyond "take the deepest
depth of the operands". The computation is bottom-up over the typed program, so
it fits the forward-only pipeline: it belongs in the analyser, which already
receives the typed program, and it must not require re-walking the syntax tree.

The checker can compute `region(v)` from types alone only if the type says whether
a value may carry a region. A struct is region-carrying when any field is. A
union is region-carrying when any alternative is. `Int32`, `Bool`, `Byte`, `Nil`,
`String<N>`, `Error`, and function types are never region-carrying, so ordinary
data costs nothing.

### Scope exit

At `end`:

1. every handle with the scope's region becomes dead;
2. the arena's allocations are released;
3. the store capability is no longer visible.

Because no scoped handle can escape, no use-after-free is possible in safe code.

### Allocation and growth

`frame.alloc<T>(value)` has the same allocation and growth behaviour as today:

- alignment from `T`;
- doubling growth;
- OOM is fatal;
- handles store offsets, so growth does not invalidate them.

Inside the scope, the store can grow. A handle remains valid because it stores
an offset, not a pointer.

### No reset

There is no `frame.reset()`. Reuse is achieved by a new `scope`:

```nippy
while condition
  scope frame = Store(4096) do
    ...
  end
end
```

Each iteration gets a fresh arena and fresh handles.

#### Loop recycling

A `scope` whose body repeats compiles to a **hoisted, recycled buffer**: the
generator allocates the buffer once before the loop and rewinds it at each scope
exit, instead of calling `malloc`/`free` per iteration. This is sound only
because of the region rules: nothing with the scope's depth can survive the
iteration, so no value can observe the rewind. It gives per-iteration reuse the
cost `reset()` had, without the use-after-reset hazard.

Consequences:

- the recycling is a code-generation decision, not a language feature; the source
  still reads as "a fresh arena per iteration";
- the buffer keeps its grown capacity between iterations, so steady-state loops
  stop allocating after the largest iteration;
- a `scope` inside a function that is itself called from a loop is not hoisted;
  each call initializes and frees its own store.

### Copying

Scoped handles are copyable **inside** the scope. Two copies refer to the same
slot and both access it through checked operations. This is memory-safe. It is
not exclusive, and this proposal does not claim it is.

If exclusive access is desired later, it belongs to a separate `Unique<T>` or
borrow feature.

### Reassignment and regions

Reassignment remains the normal mutable-variable assignment:

- it is a statement;
- the right-hand side must be assignable to the declared type;
- union injection and contextual literals still apply;
- assigning to a narrowed variable still invalidates the narrowing;
- handles remain copyable, not move-only;
- assigning a handle does **not** free the old slot.

The new rule is region compatibility:

> The destination must not outlive the value's region.

A value's region is the shortest-lived region among the handles and store
parameters it contains, which in depth terms is the **deepest** depth. For direct
handles, that is the depth of the store that allocated it. See
[Regions are depth integers](#regions-are-depth-integers).

```nippy
var root: Store = Store(4096)
var durable: Handle<Int32> = root.alloc<Int32>(1)

scope outer = Store(1024) do
  var a: Handle<Int32> = durable   // static -> lexical: allowed

  scope inner = Store(1024) do
    var b: Handle<Int32> = inner.alloc<Int32>(2)
    var c: Handle<Int32> = b      // same region: allowed

    a = b                          // error: inner value into outer variable
    c = a                          // outer value into inner variable: allowed
  end
end
```

The direction is:

```text
'static  -> any scope     allowed
'outer   -> 'inner        allowed
'inner   -> 'outer        rejected
'scope   -> program-level rejected
```

Assignment never changes an allocation's lifetime. All slots in a scoped store
are released at scope exit, regardless of which variables still point at them.
Overwriting a handle variable simply drops that reference; it does not free the
slot.

A `Store` parameter is a borrow-only capability, not an owning binding.
Reassigning the parameter itself is rejected; allocating through it is a separate
operation. An `access` binding is fixed to the slot captured at block entry.

### Nested scopes

Yes. Scopes nest lexically, and the worked examples depend on it: file/line,
document/field, image/pixel, game/frame.

```nippy
scope outer = Store(64 * 1024) do
  var a: Handle<Int32> = outer.alloc<Int32>(1)

  scope inner = Store(4096) do
    var b: Handle<Int32> = inner.alloc<Int32>(2)
    var x: Int32 = a.read() + b.read()   // outer handle usable inside
    var c: Handle<Int32> = outer.alloc<Int32>(3) // c has the outer region
  end

  var y: Int32 = a.read()                // still valid
end
```

Rules:

1. Each `scope` introduces a fresh store and a fresh region.
2. An inner scope can see outer bindings and outer store capabilities.
3. An outer scope cannot see inner bindings or the inner store.
4. The outer region outlives the inner region:
   `'outer: 'inner`.
5. An inner handle cannot be assigned to an outer binding, returned, or stored
   in an outer aggregate.
6. An outer handle can be used inside an inner scope.
7. The **region of an allocation comes from the store used**, not from the
   lexical scope where `alloc` is written. Inside `inner`, allocating through
   `outer` produces an outer-region handle that may escape `inner`.
8. In the first version, scope and binding names are not shadowed; a name must be
   unique among active scopes.
9. Early `return`, `break`, and `continue` must unwind every exited scope.

Rejected:

```nippy
var escaped: Handle<Int32> | Nil = nil

scope outer = Store(1024) do
  scope inner = Store(1024) do
    var b: Handle<Int32> = inner.alloc<Int32>(2)
    escaped = b                      // error: escaped outlives inner
  end
end
```

The error is not that `b` is used inside `inner`; it is that the destination
`escaped` would outlive `inner`.

Early exit must release inner stores before outer stores:

```nippy
fun pick(): Int32
  scope outer = Store(4096) do
    scope inner = Store(1024) do
      return 1        // frees inner, then outer
    end
  end
end
```

Code generation therefore needs cleanup labels or an explicit cleanup stack.
A direct C `return` inside a nested block would otherwise skip the `free` calls.
`break` and `continue` from inside a scope need the same treatment.

## Future: store-typed handles

Current handles carry a runtime store id, but the type is `Handle<T>`. A future
stricter model would put the store or region in the type:

> Handles carry a store/region parameter. Storing a handle is allowed when the
> source arena outlives the destination storage. Exact same-arena is the safe
> subset if you prefer simplicity.

This is recorded for later consideration, not implemented. Enforcing it needs
at least:

- `Handle<S, T>`, where `S` is a compile-time store identity or region;
- provenance for aggregates, lists, slices, and unions that contain handles;
- a way to express store-generic functions or accept store-specific signatures;
- checks on allocation, list creation, builders, and any other operation that
  stores a value into an arena.

Until then, cross-store handles remain runtime-checked and the hard same-arena
rule is not enforced.

## Escape rules

The checker needs one judgement: **does a value contain a region that is deeper
than its destination?** That is `outlives(dest, src)` from
[Regions are depth integers](#regions-are-depth-integers), applied at each site
where a value is stored:

1. **Assignment and initialization.** A scoped handle may be assigned to a binding
   in the same scope or a nested scope. It may not be assigned to a binding whose
   depth is shallower.
2. **Return.** A value may be returned only if its depth does not exceed the
   function's incoming region (see
   [Function boundaries](#function-boundaries-the-elision-rule)). A value built
   from the function's own `scope` is always deeper, so it is always rejected.
3. **Aggregates.** A field of a value is stored into that value, so the
   aggregate's depth is the deepest of its fields. Rule 1 and rule 2 then apply to
   the aggregate as a whole.
4. **Container writes.** `push`, indexed assignment, and table insertion check
   `region(v)` against the depth of the container, so a `List<Text>` in an outer
   scope cannot receive a `Text` from an inner scope. `s.alloc<T>(v)` is a write
   of the same kind, but in the first version it is trivially satisfied, because
   arena slots cannot hold handles (see
   [Slots that hold handles](#slots-that-hold-handles)). `s.copy`, `s.concat`,
   and `s.text` copy bytes and yield a fresh value in `s`'s region, so they retain
   nothing from their arguments.
5. **Calls.** A scoped value may be passed by value. The callee cannot store it
   anywhere deeper than the caller's region, and the result's region is derived
   from the arguments by the elision rule.

A function with its own local scope cannot return a handle allocated in that
scope. A function such as `parse_json(arena: Store, ...)` can return a
handle in the caller's region.

Examples of rejected programs:

```nippy
fun leak(): Handle<Point>
  scope frame = Store(16) do
    return frame.alloc<Point>(Point(1, 2))   // depth 1 into a depth-0 return
  end
end
```

```nippy
type Holder
  p: Handle<Point>
end

var global_holder: Holder = ...
scope frame = Store(16) do
  global_holder = Holder(frame.alloc<Point>(Point(1, 2)))   // depth 1 into depth 0
end
```

Accepted, because the callee cannot let the value outlive the call:

```nippy
fun consume(p: Handle<Point>): Nil
  ...
end

scope frame = Store(16) do
  var p: Handle<Point> = frame.alloc<Point>(Point(1, 2))
  consume(p)          // allowed: consume cannot store p past the call
end
```

## Functions

Persistent/global handles can be passed and returned freely because their
region is `'static` (depth 0).

Scoped handles need a rule. The invariant is:

> A scoped handle cannot escape its region.

### Function boundaries: the elision rule

Region annotations on function signatures are not needed. One rule, in two
halves, plays the role that lifetime elision plays in Rust, but with a single
region instead of one per parameter.

**Inside the callee**, every region-carrying value that arrives through a
parameter has the same region, `ρ`, which outlives every `scope` the callee
opens. `ρ` is deeper than `'static` and shallower than any scope inside the body.
Consequently:

- the callee may return a value of depth `0` (static) or `ρ`, and nothing deeper;
- the callee may not store a parameter-derived value into a program-level
  variable, because `ρ` is deeper than depth 0;
- the callee may allocate into a `Store` parameter, whose region is also `ρ`.

**At the call site**, the region of the call's result is the **deepest region
among the call's region-carrying arguments**. A call with no region-carrying
arguments yields a depth-0 result.

So this needs no annotation:

```nippy
fun parse_json(arena: Store, src: Text): Handle<Json> | Error
```

If `arena` is the `doc` scope's store and `src` is a `doc`-region `Text`, the
result is in the `doc` region. If the caller also passes a shallower value, the
result is still the deepest argument's region: the rule is **conservative**. When
that is too coarse, the escape hatch is the one the language already has: copy the
value explicitly into the outer store (`outer.copy(t)`, or an `alloc` into the
outer store), which produces an outer-region value.

**Call-site side condition.** Treating every parameter as one region `ρ` hides
the relative depth of two arguments from the callee, so the callee could write a
deeper value into a shallower container (`push` an inner-scope `Text` into an
outer `List<Text>`) and the callee's own check would pass. The caller therefore
checks one more thing: **every region-carrying argument must outlive (be no
deeper than) every `List` or `Table` argument whose element type is
region-carrying.** `Store` arguments are exempt in the first version, because no
store operation retains a region-carrying argument: `copy`, `concat`, and `text`
copy bytes, and `alloc` rejects handles. This is what keeps the cross-region
copy in the worked examples (`bump_path(file_arena, stats, fields.path)`) legal.
Lifting the slot restriction re-opens the `Store` case; see
[Slots that hold handles](#slots-that-hold-handles).

Why a single region is enough: values reach the callee only through its
parameters, so a returned value is derived from some parameter or from a depth-0
global. Treating all parameters as one region over-approximates what the callee
may return, which can reject a safe program but cannot accept an unsafe one. The
side condition above covers the other direction, where the callee writes one
parameter into another.

The conservative direction has one cost: a function that returns a depth-0 value
unrelated to its arguments still reports the arguments' deepest region at the call
site. If this matters in practice, a function with no region-carrying parameters
already yields depth 0, and a split of the function is the workaround.

### Store parameters

A function cannot allocate into a caller’s scoped arena by accident. It needs a
`Store` parameter:

```nippy
fun parse_request(arena: Store, raw: Text): Request
  var path: Text = arena.copy(raw)
  return Request("GET", path, raw)
end
```

There is **no `borrow` keyword**. `Store` is already a non-copyable resource that
cannot be returned, stored in a field, or held in a union. Allowing it as a
parameter type is therefore borrow-only by construction: the callee can allocate
through it for the duration of the call and can do nothing else with it.

- it is valid only for the call;
- it cannot be stored, returned, or reassigned;
- allocations through it belong to the caller's region (`ρ` in the callee);
- helpers may return handles or strings tied to that region, by the elision
  rule.

This is also what removes the need for a separate borrowed-handle parameter form:
a scoped handle passed by value cannot be stored or returned past the caller's
region, so a plain `Handle<T>` parameter is already safe to receive.

Without `Store` parameters and region-carrying returns, every parser, formatter,
and allocator must be inlined at the `scope` site. That is workable for a demo,
not for real APIs.

## Aggregates and generics

A struct or union containing a scoped handle must carry the same region. This is
the main area where region inference can become expensive.

A minimal implementation can forbid scoped handles in aggregates, but the
worked examples show that this is too restrictive for real code:

- a server `Context` carries a request and handles into the request arena (the
  arena itself is passed as a `Store` parameter, because a `Store` cannot be a
  field);
- a JSON AST is a recursive graph of handles and arena-backed strings;
- a log analyzer’s `Stats` contains handles to path-count records;
- a game `World` contains entity handles.

Treat region propagation through structs, unions, aliases, and generics as part
of the useful feature. Infer regions at construction sites and reject any
aggregate whose region would outlive the scope.

The first implementation can still start with a narrower rule for direct
bindings, but it should be designed so aggregate propagation is a natural
extension rather than a rewrite. In depth terms the extension is small: a struct
or union is region-carrying when any field or alternative is, and its region is
the deepest of its fields (see
[Regions are depth integers](#regions-are-depth-integers)).

### Slots that hold handles

In the first version an arena slot cannot contain a handle: `alloc<T>` requires a
fixed `T`, and `Handle`, `Text`, `List`, and the other descriptors are not fixed.
Keep that rule. It is what makes the analysis cheap, because **regions flow only
through variables and structs, never through memory**: there is no way to launder
a deep handle through a slot into a shallower variable.

The cost is an applicability gap. An arena cannot hold a linked structure such as
a tree, a graph, or the JSON AST in the worked examples.

The restriction can be lifted later without a new mechanism: **a slot in store `s`
may hold handles of `s` itself** (or of a shallower store). This falls out of the
same `outlives` check on `alloc`: `s.alloc<Node>(Node(1, child))` is legal when
`region(child)` is no deeper than `s`. Because the check is at the write, every
handle reachable from a slot is no deeper than the slot, and nothing needs to be
tracked through memory.

This extension re-opens one hole, which the first version avoids by exemption: a
callee that sees a `Store` parameter and a handle parameter as the same region
`ρ` cannot tell whether the handle outlives the store. The call-site side
condition in [Function boundaries](#function-boundaries-the-elision-rule) then
has to apply to `Store` arguments that receive region-carrying values, without
breaking the byte-copying operations (`copy`, `concat`, `text`) that retain
nothing. Resolving that is part of the extension, not of the first version.

## Memory-safety argument

For a scoped handle `h` in region `'a`:

1. `h` is created only by `alloc<T>` in `'a`, so it points to a slot of type `T`.
2. The slot is alive for the whole of `'a`; there is no reset or free inside the
   scope.
3. `h` cannot escape `'a`, so every use of `h` occurs while the slot is alive.
4. `read`/`write` operate on the whole `T`; there is no pointer arithmetic or
   indexing, so no out-of-bounds offset can be formed.
5. Handles are not nullable and have no default value.
6. Growth preserves offsets, so growth cannot invalidate `h`.

Therefore every safe scoped handle use is valid. The generation check is an
assertion that should never fire in safe code, and bounds checks remain for
dynamic indexing.

### What is and is not guaranteed

The guarantee is about **safe Nippy**, meaning:

- no C/FFI/unsafe boundary;
- no concurrency;
- no dynamically scoped arenas outside this proposal;
- the runtime checks are present and correct.

Under those assumptions the model can guarantee:

- no null handle use;
- no use-after-free or use-after-scope for arena-backed handles;
- no double free, because there is no per-object free;
- no invalid store id from safe code;
- no handle out-of-bounds, because `read`/`write` operate on the whole slot;
- no type confusion, because slots are typed by `alloc<T>`;
- no uninitialized reads, because allocation copies an initialized value.

This is a memory-safe language subset in the Rust sense: accepted safe programs
do not have undefined behaviour. It is not “all checks happen at compile time.”
Dynamic indexing such as `Text.byte_at(i)` and future `Buffer`/slice indexing
still needs a runtime bounds check, which panics rather than corrupts memory.
Rust does the same for dynamic indexing.

It is **not** equivalent to Rust’s full ownership model:

- multiple handles may refer to one slot; there is no exclusivity or mutable
  borrow checking;
- there are no dynamically scoped arenas for sessions, caches, plugins, or async
  tasks;
- there is no compile-time data-race prevention, because there is no concurrency
  model yet;
- C/FFI can retain pointers into arena memory and break the guarantee unless the
  boundary copies, pins, or uses foreign-owned memory;
- OOM is fatal, not recoverable;
- logical aliasing and stale-data bugs are not prevented.

So the honest summary is:

> Scoped arenas can give a memory-safe language subset with compile-time handle
> lifetime checks, not a Rust-equivalent ownership system.

### Runtime checks after the static guarantee

Removing `reset()` changes what the runtime checks are for. A **generation** exists
only to detect that a store was reset after a descriptor was created. Once scope
exit is the only reset and regions guarantee no descriptor outlives its scope, the
generation field is dead weight in safe code:

- `Handle`, `Text`, `List`, `Slice`, and `TextBuilder` descriptors can drop their
  generation field, which makes them smaller and removes a compare from every
  access;
- bounds checks stay, because dynamic indexing is still a runtime property;
- a store-id check stays only for the C/FFI boundary and as a compiler-bug
  assertion, and can be compiled out of a release build once the analyser is
  trusted (see the open question on release builds).

This is a rule 13 payoff of the design, not a goal of it: the lower overhead is a
consequence of moving the invalidation event from a runtime counter to a
lexical scope.

## Runtime representation and code generation

A scoped store can be emitted as a C local:

```c
{
    nyx_store frame;
    nyx_store_init(&frame, 4096);
    ...
    nyx_store_free(&frame);
}
```

The handle representation needs to identify the scoped store. Two options:

### Option A: handles carry a store reference internally

A scoped handle carries an internal reference to the stack `nyx_store`. This is
safe because the escape checker guarantees the handle cannot outlive the block.
Growth updates `frame.data`; the handle keeps the offset.

The surface still exposes no pointer.

### Option B: register scoped stores in a runtime store table

A scoped store is registered in a runtime table and handles use a table id. On
scope exit the entry is cleared. This keeps one handle representation for global
and scoped stores but needs a bounded table and registration on every scope.

Option A is simpler for scoped stores; global stores already have their table.
The implementation can keep two internal handle shapes behind one surface type.

Runtime checks remain:

- store identity;
- bounds;
- store identity and generation, only as defense against C/FFI and compiler bugs;
  safe code can drop them (see
  [Runtime checks after the static guarantee](#runtime-checks-after-the-static-guarantee)).

None of them is part of the safe-language safety argument once the region rules
hold.

## Relationship to current global stores

The current `Store` is a global arena with an explicit `reset()`. There are three
possible configurations.

### A. Current global stores with reset + scoped stores

Compile-time safety for global handles requires modeling reset. A handle is valid
only until the next reset of its store on any control-flow path. Functions can
reset global stores, so the checker needs:

- reset effects on function signatures or whole-program effect inference;
- epoch/region tags on global handles;
- invalidation of handles held in aggregates and by callers;
- handling of conditional resets and loops.

This is significantly more complex than scoped arenas, and it largely
reconstructs a region/effect system. Scoped stores can be fully checked, but
global handles remain runtime-checked unless this analysis is built.

### B. Persistent global stores without reset + scoped stores

Removing mid-program `reset()` makes every global handle valid for the whole
program. Global handles get a `'static` region. Scoped handles get lexical
regions. The analysis is simple:

- `'static` outlives everything;
- a scoped region outlives only its block;
- no reset effects.

This is the lowest-complexity way to get broad compile-time safety while keeping
the existing “allocate in a global store and return a handle” functions.

Persistent stores have one real use: **program-lifetime storage**. Any handle
that must outlive a lexical scope needs a region that does. Examples:

- a handle returned from a function and stored at program level;
- a handle used across function calls;
- long-lived configuration, tables, caches, or trees;
- the current API shape where `make_point()` allocates from a global store.

If none of those are needed, configuration C is viable.

### C. Scoped stores only, with a root scope

Remove global stores entirely. Every arena is a lexical scope. A top-level
`scope` that lasts until program exit is the root lifetime; it is a persistent
arena in behaviour, even if it is spelled as a scope.

This gives one lifetime rule — lexical regions — with no reset. The cost is at
function boundaries:

- a function cannot return a handle allocated in its own scope;
- a function cannot allocate into a caller’s scope without a store capability;
- functions that produce handles must either return values or receive a
  caller-owned region/capability.

That is a substantial API restriction. Without pointer-like store capabilities
or the elision rule, “scoped only” pushes allocation to the caller.

A root scope is the honest way to have program-lifetime data without a separate
persistent concept. If the language truly needs no long-lived dynamic data,
configuration C is the cleanest.

## Is the persistent arena useful?

The persistent arena is not about “keeping memory forever” as a goal. It is about
providing a region that outlives every function and every lexical scope.

Concrete uses:

- returning a handle from a function;
- storing a handle in a program-level variable;
- sharing long-lived data between functions;
- allocating a value whose lifetime is the process, not a block.

If Nippy programs are short-lived and all dynamic data is frame-scoped, those
uses disappear. The log, JSON, and image examples need no persistent arena. A
game world or long-lived cache needs a program-lifetime region; a program-level
`var root: Store` is the simpler spelling than a root scope.

In that sense, the persistent arena is optional as a separate tier, but a
program lifetime is not optional once state outlives an operation. A
program-level store is the recommended spelling of that lifetime.

The persistent arena is also the easiest way to preserve the current API shape,
where functions can allocate from a globally visible store and return handles.
Removing it changes the language’s function and allocation surface more than it
changes the safety analysis.

### Root scope as the persistent arena

Semantically, yes: a root-level `scope` that encloses the whole program is the
persistent arena. Its region is the program's lifetime; it is released at
program exit, not by `reset()`.

There is no need for a second allocation mechanism. The root scope is simply the
outermost region.

The subtlety is visibility across function boundaries:

- If a function is lexically outside the root scope, it cannot name the root
  store directly.
- Root-region handles can still be passed to it if the root region is treated as
  `'static` (it outlives every call), or passed by value under the elision rule.
- A function that must allocate into the root arena needs a `Store` parameter
  or a globally visible root capability.

So there are two spellings:

1. **Root scope with a `'static` region.** Handles can be passed and returned
   freely. Functions that allocate receive a `Store` parameter or a root capability.
   This is behaviourally a persistent arena.
2. **Root scope as an ordinary lexical block.** Its name and handles do not
   escape the block; functions need explicit region parameters. This is not a
   persistent arena in the same sense.

A program-level `var root: Store = Store(...)` is the simpler Nippy spelling of
the same idea: program lifetime with the existing visibility rule, no special
root-scope capability. If a program has no state that outlives an operation, it
does not need a program-level store at all.

### Recommendation: one Store type, lifetime from binding

The recommended model keeps **one `Store` type** and derives its lifetime from
where it is bound:

```nippy
var root: Store = Store(16 * 1024 * 1024)   // program lifetime ('static)

scope frame = Store(256 * 1024) do          // lexical region
  ...
end
```

- A program-level `var ...: Store` is the persistent/root arena.
- A `scope ... = Store(...)` is a lexical arena.
- Both use the same `Store` type, `alloc`, `read`, `write`, and `Handle<T>`.
- There is no `persistent` keyword, no separate store type, and no store mode.
- There is no `reset()`. A program-level store is released at program exit; a
  scoped store at scope exit.

Lifetime follows binding scope, the same rule as ordinary variables:

```text
program-level binding -> 'static
block-level binding   -> the block's region
```

This is the cohesive choice: one allocation mechanism, with lifetime made
explicit by the declaration's position.

### Visibility

Program-level stores are visible inside functions, as they are today. That is
what makes a program-level store useful for data that functions allocate and
return.

Scoped stores are visible only inside their block. A function that must allocate
into a scoped store receives a `Store` parameter:

```nippy
fun parse_request(arena: Store, raw: Text): Request
  ...
end
```

Scoped handles follow the region/escape rules. Program-level handles are
`'static` and can be passed and returned freely.

### Why this over a root scope

A root scope can express program lifetime, but it creates an avoidable visibility
problem: functions declared outside the root scope cannot name the root store, so
every allocating function needs a `Store` parameter or a special root
capability. A program-level store is the same lifetime with the visibility rule
Nippy already has, and no extra concept.

### Why this over reset

A program-level store with `reset()` reintroduces the only dynamic handle
lifetime event that scoped arenas eliminate. Compile-time safety then needs
epoch/reset-effect analysis across functions and callers. Removing `reset()`
keeps the static story simple:

- program-level handles: `'static`, always valid;
- scoped handles: lexical, escape-checked;
- no reset effects;
- runtime checks remain defensive assertions.

If a long-running program needs reuse, it should use scoped arenas per request,
file, document, image, or frame. A program-level store holds only state that
really is process-lifetime.

### Concept count

```text
Store declaration  -> program lifetime at top level, block region in scope
Store parameter    -> borrow-only capability to allocate in a caller's store
                      (no new keyword; Store is already non-copyable)
region             -> an integer depth, compared by one outlives judgment
```

“Persistent arena” survives only as an adjective for a program-level store. It
is not a separate tier.

## Worked examples and findings

These examples are target code under the proposal, not current compiler syntax.
They assume block-scoped locals, arena-backed `Text`, field access, `Store`
parameters, and helpers whose result region follows the elision rule.

### 1. CLI log analyzer

A file gets one arena for aggregates; each line gets a short-lived arena for
parsing scratch.

```nippy
fun analyze(path: Text): Nil
  scope file_arena = Store(1024 * 1024) do
    var stats: Handle<Stats> = file_arena.alloc<Stats>(Stats(0, 0, nil))
    var file: File = open_file(path)

    while has_line(file)
      scope line_arena = Store(4096) do
        var line: Text = read_line(file, line_arena)
        var fields: LineFields = parse_log_line(line_arena, line)

        var s: Stats = stats.read()
        s.total = s.total + 1
        if fields.status >= 500 then
          s.errors = s.errors + 1
        end
        stats.write(s)

        bump_path(file_arena, stats, fields.path)
      end
    end

    close_file(file)
    print_stats(stats)
  end
  return nil
end
```

`bump_path` copies `fields.path` into `file_arena` with `file_arena.copy`; storing the
line-scoped string directly in `Stats` is a compile-time escape error.

Finding: nested file/record scopes are natural, and the explicit copy between
regions is the right escape hatch. No persistent arena is needed.

### 2. JSON document parser

A document arena owns the AST, strings, and parser state. The parser is a
helper that allocates into the caller’s arena and returns a handle tied to it.

```nippy
fun load_config(path: Text): Nil
  scope doc = Store(256 * 1024) do
    var source: Text = read_text_file(doc, path)
    var parsed: Handle<Json> | Error = parse_json(doc, source)

    if parsed.is<Error>() then
      print_error(parsed)
    elseif parsed.is<Handle<Json>>() then
      var cfg: Config = decode_config(doc, parsed)
      run(cfg)
    end
  end
  return nil
end
```

`parse_json` returns `Handle<Json> | Error`; the handle’s region is the caller’s
`doc` region. Recursive `Json` nodes contain `Handle<Json>` children and
arena-backed strings.

Rejected:

```nippy
fun load_config(path: Text): Config
  scope doc = Store(256 * 1024) do
    return decode_config(...) // error: Config contains doc-region strings
  end
end
```

To return a document, the caller must supply the arena:

```nippy
fun load_document(arena: Store, path: Text): Handle<Json> | Error
```

Finding: recursive ASTs make region-carrying returns and aggregate
propagation unavoidable, and they need slots that hold handles of their own
store (see [Slots that hold handles](#slots-that-hold-handles)). Errors remain inline `Error` values, so failure paths
do not allocate.

### 3. Batch image processor

Each image gets an arena for its encoded bytes, decoded pixels, and resized
output. C decoding/encoding functions operate on arena-backed buffers.

```nippy
fun process_image(input: Text, output: Text): Nil
  scope image_arena = Store(8 * 1024 * 1024) do
    var encoded: Buffer = read_file(image_arena, input)
    var img: Image = decode_image(image_arena, encoded)
    var thumb: Image = resize_image(image_arena, img, 320, 320)
    write_png(output, thumb)
  end
  return nil
end
```

A batch loop calls this for each path. Thumbnails, pixel buffers, and decoded
data are freed per image; if a thumbnail cache must survive, it needs a
root/persistent region or an explicit copy.

Finding: real workloads need a `Buffer`/`Slice<Byte>` abstraction with arena
lifetime. `alloc<T>` for one value is not enough. C resources (file descriptors,
decoder contexts) still have their own lifetimes; the arena only owns Nippy’s
buffers.

### Findings that changed this proposal

1. **Store parameters are required.** Parsers, formatters, and allocators must
   allocate into a caller’s arena and return values tied to it. No `borrow`
   keyword is needed.
2. **Region-carrying returns are required, but annotations are not.** Helpers
   returning `Text`, `Handle<Json>`, or a record in the caller’s region are the
   normal shape, and the single-region elision rule covers them.
3. **Aggregate propagation is required.** `Context`, AST nodes, stats records,
   and game state contain scoped handles.
4. **Nested scopes are the normal usage.** file/line, document, request,
   image/frame are naturally two-level or more.
5. **Cross-region movement is explicit.** `outer.copy(...)` and
   allocating a new node in the outer arena are the safe escape hatches; moving
   a handle itself is rejected.
6. **Persistent arenas are optional for these workloads.** Logs, JSON documents,
   requests, and images fit scoped arenas plus caller-provided arenas.
7. **State that outlives the operation still needs a program lifetime.** A game
   `World`, a server cache, or a long-lived table needs a program-level `Store`
   (or an equivalent root scope). A program-level store is recommended because
   it keeps the existing function visibility rule.
8. **Dynamic buffers/slices are a prerequisite for real programs.** Without
   them, image/text workloads fall back to fixed inline values or many
   single-value allocations.

## Complexity comparison

| Model | Static analysis | Runtime checks | Main cost |
|---|---|---|---|
| Current global stores with reset | Reset/effect/epoch analysis for global handles | Needed for global handles unless full analysis | High complexity; easy to leave holes |
| Persistent global + scoped | Two region classes, no reset effects | Defensive only | Low-to-moderate; persistent memory grows |
| Scoped only + root scope | Region inference/escape for every handle | Defensive only | Moderate; functions need caller regions |
| Full unique ownership | Move/borrow/lifetime analysis | Owner token backstop | Very high; new language concepts |

The scoped-only model is not automatically simpler than persistent + scoped. It
removes reset analysis but adds region/capability rules at every function
boundary. Persistent + scoped has the simplest useful static story because
`'static` handles never need escape analysis.

## Alternatives

### Epoch-typed reset

Keep global `reset()` but give every store an epoch:

```text
reset(s)   // epoch 1 -> epoch 2
```

Handles from epoch 1 are statically invalid after the reset. Functions become
effect-polymorphic over epochs. This preserves reset but is a region/effect
system and is more complex than scoped stores.

### Borrow/access blocks

Scoped `access` blocks for zero-copy reads are complementary. They are not a
substitute for arena lifetime: an access block still needs the backing store to
outlive it. Scoped arenas make that guarantee lexical.

### Unique handles

Useful for exclusive mutation, in-place reclamation, or concurrency. Not needed
for memory safety while stores are reset-free and handles are checked. It is a
separate proposal.

## Implementation phases

1. Add block-scoped `var` bindings (prerequisite). **Implemented.**
2. Add `scope frame = Store(bytes) do ... end` parsing and AST/Typed nodes.
3. Compute a region depth for every region-carrying expression and binding in
   the analyser. Regions are integers, not part of the surface type.
4. Add the single `outlives(dest, src)` check at assignment, `return`, and
   container writes. Reject calls that pass a scoped value, until phase 6.
5. Propagate regions through structs, unions, aliases, and generics.
6. Allow `Store` parameters and implement the elision rule, including the
   call-site side condition for `List`/`Table` arguments.
7. Remove `reset()` and the per-value generation field; keep bounds checks.
8. Emit scoped stores as C locals with init/free.
9. Add cleanup labels/stack so `return`, `break`, and `continue` unwind nested
   scopes in reverse order.
10. Hoist and recycle the buffer of a `scope` inside a loop.
11. Add runtime store representation for scoped handles.
12. Add buffer/slice allocation for real workloads.
13. Lift the slot restriction so a slot may hold handles of its own store.
14. Add tests for legal nesting, escape rejection, early exits, growth, OOM,
    caller-provided arenas, the call-site side condition, and loop recycling.

## Open questions

1. Keyword and syntax: `scope`, `region`, or `arena`.
2. ~~Are block-scoped locals introduced generally, or only inside `scope`?~~
   Resolved: block-scoped locals are a general language feature.
3. ~~How is `borrow Store` written and inferred in function signatures?~~
   Resolved: a plain `Store` parameter; there is no `borrow` keyword.
4. ~~How are caller-provided regions expressed in generic functions and APIs?~~
   Resolved for the first version: they are not. The single-region elision rule
   derives the result region from the arguments. Revisit if the conservative
   result rejects real programs.
5. Confirm that program-level `var root: Store` is the only root-lifetime
   spelling, rather than also allowing a top-level `scope`.
6. What is the C/FFI boundary rule for arena-backed buffers and handles?
7. Do we keep runtime generation checks in release builds, or only in debug/FFI
   builds?
8. What is the first buffer/slice API: `Buffer`, `Slice<Byte>`, or both?
9. Are scope/binding names shadowed in nested scopes, or rejected as
   duplicates?
10. Is the elision rule too coarse for real APIs? A function with two scoped
    parameters returns the deeper of the two, so a result tied only to the
    shallower one must be copied explicitly.
11. How does the call-site side condition extend once slots can hold handles of
    their own store, without rejecting `copy`/`concat`/`text`?
