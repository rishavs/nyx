# Scoped Arenas

**Status:** accepted direction, not implemented
**Goal:** maximize compile-time memory safety for arena-backed handles without a general ownership or borrow checker.

## Decision record

Accepted direction:

- One `Store` type. A store's lifetime comes from its binding scope.
- Program-level `var root: Store = Store(...)` is the program-lifetime store
  (`'static`). It has no `reset()`.
- `scope frame = Store(...) do ... end` creates a lexical arena.
- Handles from a program-level store can be passed and returned freely.
- Handles from a scoped store are region-bound and cannot escape.
- Functions that allocate into a scoped store take `borrow Store`.
- Scoped helpers may return values in the caller's region.
- Region propagation through aggregates, `borrow Store`, and buffers/slices are
  part of the useful feature, not later polish.
- There is no separate persistent store type, mode, or keyword.

Current compiler status: it still has `reset()`, no block-scoped locals, no
`scope`, no regions, no `borrow Store`, and no arena-backed dynamic strings.
This document is the target design, not a description of implemented behavior.

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
- the existing generation and bounds checks remain as defensive assertions for
  C interop and compiler bugs.

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
- General-purpose local variables and nested blocks. This proposal needs a
  limited form of block-scoped bindings; see “Prerequisites”.
- A general borrow checker.
- Concurrent access to a shared arena.
- A dynamic `String` implementation. Strings are fixed/inline today; a future
  arena-backed string would carry the same scope region.

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

Nippy currently has program-level variables and function parameters, but no
block-scoped variables. A `scope` block needs names for handles and values.

This proposal assumes **block-scoped `var` bindings**:

```nippy
scope frame = Store(4096) do
  var p: Handle<Point> = frame.alloc<Point>(Point(1, 2))
  var q: Point = p.read()
  var n: Int32 = q.x
end
```

A minimal first implementation could allow block-scoped bindings only inside a
`scope`, and only after introducing them would the feature be generalized. The
scope binding is the prerequisite, not `scope` itself.

If block-scoped locals are out of scope for the next compiler step, scoped
arenas cannot be used meaningfully yet.

Three other pieces move from “later” to practical prerequisites after the
worked examples below:

1. **`borrow Store` parameters.** Parsers, formatters, and allocator helpers must
   be able to allocate into a caller’s arena.
2. **Region-polymorphic returns.** A helper that allocates a `String` or AST in
   the caller’s arena must return a value tied to that arena’s region.
3. **Region propagation through aggregates.** `Context`, AST nodes, records, and
   game state naturally contain handles and borrows.

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
- passed by value to a function that could store or return it.

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

A value's region is the shortest-lived region among the handles and borrows it
contains. For direct handles, that is the scope of the store that allocated it.

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

A variable holding `borrow Handle<T>` or a `borrow Store` is a borrow binding,
not an owning handle. Reassigning the binding itself is rejected; writing
through the borrowed value is a separate operation. An `access` binding is fixed
to the slot captured at block entry.

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

The checker needs one new judgement: **does a value contain a handle from a
region that does not outlive the destination?**

The general rule is that the destination's region must outlive the handle:

1. A scoped handle may be assigned to a binding in the same scope or a nested
   scope.
2. It may not be assigned to a binding whose region is shorter-lived.
3. It may be returned only when the caller supplies the region, through a
   caller-provided arena or an equivalent region relationship.
4. It may be a field of a value only when that value's region is no longer-lived
   than the handle's region.
5. It may be passed by value only when the parameter's region relationship
   permits it; otherwise the function must take a borrow.

A function with its own local scope cannot return a handle allocated in that
scope. A function such as `parse_json(arena: borrow Store, ...)` can return a
handle in the caller's region.

Examples of rejected programs:

```nippy
fun leak(): Handle<Point>
  scope frame = Store(16) do
    return frame.alloc<Point>(Point(1, 2))
  end
end
```

```nippy
type Holder
  p: Handle<Point>
end

var global_holder: Holder = ...
scope frame = Store(16) do
  global_holder = Holder(frame.alloc<Point>(Point(1, 2)))
end
```

```nippy
fun consume(p: Handle<Point>): Nil
  ...
end

scope frame = Store(16) do
  var p: Handle<Point> = frame.alloc<Point>(Point(1, 2))
  consume(p)          // rejected until borrow parameters exist
end
```

The first implementation may reject **all** function calls that take a scoped
handle, including calls that would be safe. That is acceptable: scoped handles
are primarily for direct use inside the scope.

## Functions

Persistent/global handles can be passed and returned freely because their
region is `'static` (the whole program).

Scoped handles need a rule. The invariant is:

> A scoped handle cannot escape its region.

Passing a scoped handle by value to a function with no region relationship is
rejected because the callee could store it or return it. A function whose
signature ties a value to the caller’s region — for example through a
`borrow Store` parameter — may accept and return values in that region.

A separate borrow parameter is useful for read/write access without transfer:

```nippy
fun inspect(p: borrow Handle<Point>): Int32
  ...
end
```

The callee cannot return or store the borrow, and the borrow lives no longer
than the caller's scope. That is a small borrow rule, not a general ownership
system.

### Store capabilities

A function cannot allocate into a caller’s scoped arena by accident. It needs a
capability parameter:

```nippy
fun parse_request(arena: borrow Store, raw: String): Request
  var path: String = string_copy(borrow arena, ...)
  return Request("GET", path, raw)
end
```

`borrow Store` is a scoped capability, not a first-class store value:

- it is valid only for the caller’s scope;
- it cannot be stored or returned;
- allocations through it belong to that scope;
- helpers may return handles or strings tied to the capability’s region.

This implies **region-polymorphic functions**: the return region is derived from
the arena parameter. Returning a scoped value from a helper is safe when the
caller’s scope outlives the use.

Without store capabilities and region-polymorphic returns, every parser,
formatter, and allocator must be inlined at the `scope` site. That is workable
for a demo, not for real APIs.

## Aggregates and generics

A struct or union containing a scoped handle must carry the same region. This is
the main area where region inference can become expensive.

A minimal implementation can forbid scoped handles in aggregates, but the
worked examples show that this is too restrictive for real code:

- a server `Context` carries a request and an arena capability;
- a JSON AST is a recursive graph of handles and arena-backed strings;
- a log analyzer’s `Stats` contains handles to path-count records;
- a game `World` contains entity handles.

Treat region propagation through structs, unions, aliases, and generics as part
of the useful feature. Infer regions at construction sites and reject any
aggregate whose region would outlive the scope.

The first implementation can still start with a narrower rule for direct
bindings, but it should be designed so aggregate propagation is a natural
extension rather than a rewrite.

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

Therefore every safe scoped handle use is valid. The existing generation and
bounds checks are assertions that should never fire in safe code.

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
Dynamic indexing such as `String.byte_at(i)` and future `Buffer`/slice indexing
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

## Runtime representation and code generation

A scoped store can be emitted as a C local:

```c
{
    nippy_store frame;
    nippy_store_init(&frame, 4096);
    ...
    nippy_store_free(&frame);
}
```

The handle representation needs to identify the scoped store. Two options:

### Option A: handles carry a store reference internally

A scoped handle carries an internal reference to the stack `nippy_store`. This is
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
- generation (defense against C/FFI and compiler bugs);
- bounds.

They are not part of the safe-language safety argument once the region rules
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
or region-polymorphic functions, “scoped only” pushes allocation to the caller.

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
  `'static` (it outlives every call), or passed as `borrow Handle`.
- A function that must allocate into the root arena needs a `borrow Store`
  capability or a globally visible root capability.

So there are two spellings:

1. **Root scope with a `'static` region.** Handles can be passed and returned
   freely. Functions that allocate receive `borrow Store` or a root capability.
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
into a scoped store receives `borrow Store`:

```nippy
fun parse_request(arena: borrow Store, raw: String): Request
  ...
end
```

Scoped handles follow the region/escape rules. Program-level handles are
`'static` and can be passed and returned freely.

### Why this over a root scope

A root scope can express program lifetime, but it creates an avoidable visibility
problem: functions declared outside the root scope cannot name the root store, so
every allocating function needs a `borrow Store` parameter or a special root
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
borrow Store       -> capability to allocate in a scoped store
borrow Handle      -> scoped view of a slot
```

“Persistent arena” survives only as an adjective for a program-level store. It
is not a separate tier.

## Worked examples and findings

These examples are target code under the proposal, not current compiler syntax.
They assume block-scoped locals, arena-backed `String`, field access, `borrow
Store`, and region-polymorphic helpers.

### 1. CLI log analyzer

A file gets one arena for aggregates; each line gets a short-lived arena for
parsing scratch.

```nippy
fun analyze(path: String): Nil
  scope file_arena = Store(1024 * 1024) do
    var stats: Handle<Stats> = file_arena.alloc<Stats>(Stats(0, 0, nil))
    var file: File = open_file(path)

    while has_line(file)
      scope line_arena = Store(4096) do
        var line: String = read_line(file, borrow line_arena)
        var fields: LineFields = parse_log_line(borrow line_arena, line)

        var s: Stats = stats.read()
        s.total = s.total + 1
        if fields.status >= 500 then
          s.errors = s.errors + 1
        end
        stats.write(s)

        bump_path(borrow file_arena, stats, fields.path)
      end
    end

    close_file(file)
    print_stats(stats)
  end
  return nil
end
```

`bump_path` copies `fields.path` into `file_arena` with `string_copy`; storing the
line-scoped string directly in `Stats` is a compile-time escape error.

Finding: nested file/record scopes are natural, and the explicit copy between
regions is the right escape hatch. No persistent arena is needed.

### 2. JSON document parser

A document arena owns the AST, strings, and parser state. The parser is a
helper that allocates into the caller’s arena and returns a handle tied to it.

```nippy
fun load_config(path: String): Nil
  scope doc = Store(256 * 1024) do
    var source: String = read_text_file(borrow doc, path)
    var parsed: Handle<Json> | Error = parse_json(borrow doc, source)

    if parsed.is<Error>() then
      print_error(parsed)
    elseif parsed.is<Handle<Json>>() then
      var cfg: Config = decode_config(borrow doc, parsed)
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
fun load_config(path: String): Config
  scope doc = Store(256 * 1024) do
    return decode_config(...) // error: Config contains doc-region strings
  end
end
```

To return a document, the caller must supply the arena:

```nippy
fun load_document(arena: borrow Store, path: String): Handle<Json> | Error
```

Finding: recursive ASTs make region-polymorphic returns and aggregate
propagation unavoidable. Errors remain inline `Error` values, so failure paths
do not allocate.

### 3. Batch image processor

Each image gets an arena for its encoded bytes, decoded pixels, and resized
output. C decoding/encoding functions operate on arena-backed buffers.

```nippy
fun process_image(input: String, output: String): Nil
  scope image_arena = Store(8 * 1024 * 1024) do
    var encoded: Buffer = read_file(borrow image_arena, input)
    var img: Image = decode_image(borrow image_arena, encoded)
    var thumb: Image = resize_image(borrow image_arena, img, 320, 320)
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

1. **Store capabilities are required.** Parsers, formatters, and allocators must
   allocate into a caller’s arena and return values tied to it.
2. **Region-polymorphic returns are required.** Helpers returning `String`,
   `Handle<Json>`, or a record in the caller’s region are the normal shape.
3. **Aggregate propagation is required.** `Context`, AST nodes, stats records,
   and game state contain scoped handles or arena borrows.
4. **Nested scopes are the normal usage.** file/line, document, request,
   image/frame are naturally two-level or more.
5. **Cross-region movement is explicit.** `string_copy(borrow outer, ...)` and
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

1. Add block-scoped `var` bindings (prerequisite).
2. Add `scope frame = Store(bytes) do ... end` parsing and AST/Typed nodes.
3. Add a region field to handle types, inferred from the allocating scope.
4. Add `borrow Store` capability parameters and region-polymorphic returns.
5. Propagate regions through structs, unions, aliases, and generics.
6. Add escape checking for scoped handles:
   - outer assignment;
   - return;
   - aggregate escape;
   - function argument by value.
7. Emit scoped stores as C locals with init/free.
8. Add cleanup labels/stack so `return`, `break`, and `continue` unwind nested
   scopes in reverse order.
9. Add runtime store representation for scoped handles.
10. Add buffer/slice allocation for real workloads.
11. Add tests for legal nesting, escape rejection, early exits, growth, OOM,
    and caller-provided arenas.

## Open questions

1. Keyword and syntax: `scope`, `region`, or `arena`.
2. Are block-scoped locals introduced generally, or only inside `scope`?
3. How is `borrow Store` written and inferred in function signatures?
4. How are caller-provided regions expressed in generic functions and APIs?
5. Confirm that program-level `var root: Store` is the only root-lifetime
   spelling, rather than also allowing a top-level `scope`.
6. What is the C/FFI boundary rule for arena-backed buffers and handles?
7. Do we keep runtime generation checks in release builds, or only in debug/FFI
   builds?
8. What is the first buffer/slice API: `Buffer`, `Slice<Byte>`, or both?
9. Are scope/binding names shadowed in nested scopes, or rejected as
   duplicates?
