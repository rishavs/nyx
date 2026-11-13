# Scoped arenas

- Feature name: `scoped_arenas`
- Start date: 2026-11-13
- RFC PR: pending
- Tracking issue: pending
- Status: proposed, implementation specification

## Summary

Add lexical Stores and allow a function to receive a Store capability from its
caller. Check lifetime escapes through variables, containers, and calls.
Reclaim a scoped Store on every normal control-flow exit. Program-level
Stores live until program exit. Remove source-level `reset()` when the
feature ships. Retain runtime generation and bounds checks in this release.

## Motivation

Today a Store can be reset while descriptors into it remain reachable. Stale
access traps at runtime. A lexical Store allows the compiler to reject the
escape and the programmer to see when reclamation occurs. It fits scratch
data for a request, frame, file, or compiler pass. Caller-selected allocation
lets a parser keep its output while reclaiming its own scratch.

This design uses one allocation capability and one outlives relation. Aliases
remain legal. It keeps allocator selection visible in source and confines
the extra bookkeeping to compilation. Independently retired sessions,
entities, and cache entries need a separate reclamation design.

## Guide-level explanation

A Store declared with `global` lasts for the program. A Store introduced by
`scope` lasts through its body. Pass either to a function with a `Store`
parameter to let the function allocate into it.

```nippy
fun label(output: Store, input: Text): Text
  return output.concat("label: ", input)
end

scope output = Store(256) do
  var result: Text = ""
  scope scratch = Store(64) do
    var input = scratch.copy("example")
    result = label(output, input)
  end
  print(result)
end
```

This prints `label: example`. The result refers to `output`; concatenation
copies the input bytes. Replacing the assignment with `result = input` is
rejected. The diagnostic identifies `scratch` as the origin and `result`
as the destination.

Stores can also hold linked data. A handle breaks a type-layout cycle:

```nippy
type Node
  value: Int32
  next: Handle<Node> | Nil
end

scope chain = Store(256) do
  var tail = chain.alloc<Node>(Node(2, nil))
  var head = chain.alloc<Node>(Node(1, tail))
  print(head.read().value)
end
```

This prints `1`. Replacing `tail` with a handle allocated in a nested Store
is rejected. References to enclosing Stores are also allowed when they
outlive the receiving slot.

An opaque load cannot establish which Store backs a child handle. Use the
original handle when it is still available. For a structure that routinely
traverses and replaces links, use a `List` of nodes with `Int32` links and
update through the original list binding. Its backing Store is known and
index access stays bounds-checked. This RFC adds no separate link syntax.

For iteration scratch, put the Store inside the loop. Each entered scope
starts empty. A program-level Store can hold configuration or other
program-lifetime data. Replacing a name or popping an item does not reclaim
an arena allocation. A read-only helper can take a `Slice<Text>` when it
needs to inspect a list without retaining anything through the list itself.

## Reference-level explanation

### Syntax and Store capabilities

```ebnf
scope-statement = "scope", identifier, "=", "Store", "(", expression, ")",
                  "do", block, "end" ;
```

A scope is a statement wherever ordinary statements are allowed. Its name
follows the current no-shadowing rule for visible names. Its capacity is an
`Int32` expression evaluated once at entry, before the name becomes visible.
Zero is valid. A constant negative value is rejected by the checker. A dynamic
negative value traps before initialization. Capacity is an initial reservation
in bytes. Existing global Store capacity expressions remain constant.
`global name: Store = Store(...)` is canonical. Use `global` in place of
the legacy top-level `var` Store declaration. Rename identifiers named
`scope` when migrating. The scope statement already names its resource
constructor and needs no additional type annotation.

A Store capability supports the same allocation methods regardless of origin.
A bare capability name is legal as a method receiver or an actual argument
for a `Store` parameter. It can be forwarded and may name a global Store,
an active scoped Store, or another Store parameter. It cannot be assigned,
copied into an ordinary binding, returned, captured, placed inside an
aggregate, or supplied as a generic type argument. A `Fun` signature may
name `Store` as a direct parameter. Parameter names are immutable capabilities.
Every other existing type position that disallows Store still disallows it.

### Payloads and type predicates

- A **region-carrying** type is `Handle`, `Text`, `TextBuilder`, `List`,
  `Slice`, or `Table`, or an inline struct, union, Array, or Record containing
  one transitively. Resolve aliases and substitute generic parameters first.
  Primitive values, fixed strings, Error, and closure-free functions carry no
  region. Store capabilities are analyzed separately.
- A **retaining** descriptor is a List or Table with region-carrying elements
  or values, or a Handle with a region-carrying payload. These descriptors can
  write references into their backing storage. A Slice never writes into its
  own backing storage, but an element read may expose a retaining descriptor.
  Inline aggregates can expose retaining descriptors through their members.
  TextBuilder writes bytes and does not retain its Text argument.
- `alloc<T>` and `Handle<T>` accept existing fixed payloads plus Text and
  Handle descriptors, including transitively in otherwise eligible inline
  aggregates. Retain existing restrictions on collection element shapes,
  and on Records as handle payloads. List, Table, Slice, TextBuilder, and Store
  remain disallowed in handle payloads. Validate every payload type after
  substitution. Layout follows inline edges only; validation may follow handle
  edges and terminates with a visited set. A cycle made entirely of inline
  edges remains an error. A forward type name remains an error except for
  reference to the currently defined named struct through a Handle edge.

Reserve a placeholder for the struct before resolving its fields. A Handle
type can refer to this placeholder by index. Keep inline layout dependencies
acyclic and order layout and C definitions by those dependencies, since
insertion order alone no longer suffices. An opaque Handle layout has no C
definition dependency on its payload. A self-reference in a generic struct
must use its own type parameters in declaration order. Reject expanding
recursion such as `Node<T>` containing `Handle<Node<Pair<T>>>` before
instantiation. Validate that every placeholder is completed before checking
the final typed artifact.
- Determine whether an arena descriptor can expose a retaining descriptor by
  examining its element or payload type, through inline members and nested
  descriptors. These are reachability predicates on the resolved type graph.
  An already visited type adds no new edges to search.

### Lifetimes and value facts

Use `src <= dest` to mean the source outlives the destination. Static is
zero. Scope entry introduces a distinct lexical Store identity and increments
depth on that active chain. Ordinary blocks do not increment arena depth.
For a function, incoming values and capabilities outlive the activation, and
every Store it opens is shorter-lived than the activation. Store identity is
separate from ordering. Equal numeric depths from separate lexical chains
convey no relationship. The compiler uses lexical ancestry and symbolic
parameter bounds; neither is part of a Nippy surface type.

Keep the following facts for an expression of a region-carrying type:

| Fact | Conservative meaning |
| --- | --- |
| `upper` | The shortest lifetime the value can reference, as an upper bound on depth. |
| `backing` | A lower bound on the depth of the immediate backing Store for a descriptor. |
| `floor` | A lower bound on the depth of every retaining destination reachable through the value, including its own backing Store when it is retaining. |

The lower bounds matter because a write must be safe for the **longest-lived**
possible destination. An absent destination has no floor and places no
constraint. An unknown possible destination has floor zero. An exact Store
capability supplies its Store lifetime as both upper and lower bound.

Store binding lifetime and variable content are separate. The destination
for an inline write is the lifetime at the root binding's declaration. Use
the innermost enclosing Store scope, including in top-level code. Outside
all Store scopes use the function activation, or zero at program level.
The checker reads the current facts for
a variable's value. It never substitutes the lifetime of the binding for
the lower bound on mutable backing storage.

### Expression and write rules

| Operation | Facts or check |
| --- | --- |
| Scalar or other non-region-carrying result | Upper zero and no retaining destination. This includes a scalar read through a scoped handle. |
| Text literal | Upper zero and no retaining destination. |
| Store allocation, including copy, concat, text, builder, list, table, and alloc | Upper and immediate backing are the Store lifetime. The allocator copies bytes for copy, concat, and text. For alloc, check the payload's upper against the Store lifetime. |
| Builder finish | Upper is the builder's upper. It produces a fresh Text in the builder's Store. Append copies bytes. |
| Slice or subslice | Upper and backing come from the parent. The Slice has no immediate retaining destination. |
| Struct, union injection, Array or Record construction | Upper is the maximum of member uppers and floor is the minimum of member floors. Keep the field and alternative facts defined below. |
| Inline field, narrowed union, or inline element read | Use a tracked member when present. At the first indexed collection or opaque load, inherit the source upper and use the source floor for a reachable retaining descriptor's backing and floor, or zero if the source has no floor. Use zero for an untracked non-retaining backing. |
| Arena-backed index, get, pop, iteration element, table value, Slice read, or handle read | The result's upper is bounded by the receiver's upper. This follows from the write invariant. If the result can reach mutable backing storage, use zero as the conservative lower bound. A non-region-carrying result has upper zero. Keys and length follow their ordinary value types. |
| If or match expression | Join reachable results with maximum upper and minimum lower bounds. Narrowing drops impossible alternatives while keeping the surviving facts. |
| Inline assignment, including fields, Array push and index writes, and Record insertion | Check the right-hand side's upper against the root binding lifetime. Update the root's facts as described below. |
| Arena write, including List push/set, Table insertion, and handle write | Check the payload's upper against the receiver's immediate backing lower bound. The read/write methods retain their existing by-value semantics. |
| Return | Check that the result upper outlives the activation's exit. Preserve its dependence on incoming values for direct calls. |
| Call | Apply the call rules below. |

For any arena descriptor that can expose retaining destinations through its
payload or elements, set the reachable floor to zero. Otherwise its reachable
floor is its immediate backing if it is retaining, and absent if it is not.
This conservative rule covers aliases and memory loads without a heap
points-to analysis. A non-retaining descriptor may still need a backing fact
for slicing. At a merge, a missing alternative contributes no retaining floor.

Track every inline path consisting of named struct fields and union
alternatives, including paths through nested inline aggregates. Stop at
Array, Record, or an arena descriptor. Track whole-value bounds there.
For an arena load, populate its result with fallback facts throughout;
it does not recover the paths of the value originally written. Join facts
path by path. A statically absent union alternative contributes no facts
at a join. A write to an untracked indexed element widens the whole-value
bounds at that collection path and recomputes enclosing bounds.

Track initializer facts and replace them on whole-binding assignment. Update
known inline field paths on field assignment. For inline element writes or an
untracked path, conservatively join the incoming facts with the old facts at
the affected root, invalidating precise facts that could have changed. Use the
same upper/lower joins for control-flow merges. Analyze loop headers to a
fixed point using entry and backedge facts, including continue edges.
Project facts to surviving bindings at scope exit. Existing narrowing and
reachability rules continue to apply. Arena writes change storage contents,
while the conservative load rule keeps aliases valid without updating their
facts. No memory write, including a collection initializer that copies
descriptors, may bypass the retention check.

### Calls and summaries

Check each named function and concrete generic instantiation once. Each
incoming region-carrying parameter supplies symbolic upper, backing when
applicable, and floor when applicable. Track struct fields and union
alternatives as symbolic projections. Inline indexed collections use their
whole-value bounds. A Store parameter supplies one symbolic Store lifetime.
Use only relationships justified by the bounds, lexical ancestry, and the
fact that all incoming references outlive callee-local Stores.

A direct-call summary contains the return facts and any required inequalities
between incoming bounds. A retaining operation in a callee either proves its
inequality locally, emits a requirement on incoming bounds, or is rejected.
A requirement involving a callee-local Store can be discharged locally or
rejected. It cannot be deferred to callers. Reject a return depending on any
callee-local Store, even when the caller discards it. At a direct call,
substitute the argument facts, check or propagate the requirements, and
substitute the return facts. This works through helper chains. In particular,
a helper that copies into its Store argument and returns the copy has that
Store as its result dependency and has no retention requirement on the input.

Use the same projection rules at substitution as at an ordinary read. For
call summaries, give a projection from an absent union alternative the same
conservative fallback as an unknown projection. If the source has no floor,
use zero for a fallback that needs a retaining lower bound. Check all
summarized requirements, including those from branches the caller might
never take. The whole-argument upper bounds every projected upper. For any
possible retaining projection, the whole
argument's floor is no deeper than that projection's backing or floor.
These relationships also hold for symbolic parameters. Each immediate
backing lifetime is bounded above by its descriptor's upper.

Use maxima for upper joins and minima for lower joins. A symbolic bound is
a parameter index, a tracked inline path, and an upper, backing, or floor
selector. A Store parameter uses one bound at the empty path. Represent a
return as facts indexed by result paths and a requirement as an ordered
pair of contributing bounds. Normalize joins as sets of distinct bounds.
Expand an inequality between a maximum and a minimum into requirements for
every contributing pair. A direct call looks up each symbolic bound on the
matching actual argument path and substitutes that fact into the pair or
result. This vocabulary is finite per function: all paths come from its
resolved parameter and result types, and lexical identities come from its
body. Loop and recursive joins only add contributors and requirements to
these finite sets. With B possible contributors, one requirement set has
at most B squared pairs. Use checked allocation and the existing compiler
input limits. Record analysis time and peak memory on deeply nested inputs
before tuning or introducing a new configuration limit.
Unknown lower bounds are zero. Drop local facts that cannot occur outside
their scope. Diagnostics preserve a source position for each contributor and
the retaining operation that introduced an obligation. Bound analysis storage
with the compiler's checked allocation and input limits.

For indirect calls, use this conservative contract:

1. Every region-carrying argument and every Store argument must outlive every
   reachable retaining destination of the arguments. Include each Store
   argument as a destination, since the callee can allocate retaining storage
   in it. Use argument uppers and reachable floors for these checks.
2. The result upper is the maximum argument upper, including Stores. A
   region-free result has upper zero. Any result backing or retaining lower
   bound is zero.
3. A function used as a first-class value must satisfy this contract.
   Verify its inferred requirements under the
   contract, including projected requirements. A projected source upper is
   bounded by the whole argument's upper, and a projected destination floor
   is bounded below by the whole argument's floor. Requirements to retain
   into a named global Store are outside the generic contract unless they
   already follow from it. Reject function-value conversion when the contract
   cannot establish the requirements.

Named direct calls use their precise summaries even for a function that can
also be used as a value. Function values carry no runtime effect metadata.
This rule avoids dispatch-dependent checking. It can reject a mixed-lifetime
indirect call that a direct call accepts. Include Store arguments to enforce
the same invariant on fresh slots inside every callee. Dropping them would
require a different invariant for unpublished allocations and a rule for
when they become reachable. Named calls already distinguish byte-copying
helpers from allocations that retain descriptors.

Infer summaries for direct self-recursion by fixed point. Start with no
return alternatives or obligations. Analyze the body using the current
summary at self-calls, join new return alternatives and accumulate required
inequalities, and repeat until unchanged. Treat a recursive call with no
known return alternatives as having no returning edge for this analysis.
Defer rejection until convergence. This is separate from the existing
control-flow rules for required returns. A generic self-call must use the
same concrete instantiation. Existing declaration-order rules rule out
mutual recursion between named functions. A recursive parser that only
reads its input and copies into its output Store can therefore use the same
contract as a nonrecursive parser.

For example, given a struct with a `List<Text>` field, a function that pushes
its Text parameter into that field requires the Text upper to outlive the
field's backing. Passing an outer list wrapped in that struct and a Text from
an inner Store fails at the call. The same write performed directly on the
field fails by the same inequality, even when the struct also contains inner
Text. In either case, copying the Text into the list's Store first makes the
write valid.

For example, `add_name(batch: Batch, name: Text)` calling
`batch.names.push(name)` emits the pair
`(parameter 1, [], upper) <= (parameter 0, [names], backing)`, with indices
starting at zero. In the nested example, substitution gives
`inner <= outer`. The call fails even if another field of `batch` also refers
to `inner`.

### Scope exit and safety invariant

On each entered scope, initialize a fresh logical Store. On fallthrough,
return, break, or continue, release every Store exited by that edge in reverse
entry order. A scope has the same flow outcomes as its body. A return
evaluates and materializes its value before cleanup, then returns the
temporary. Break and continue target the same loop as they do without the
scope. A deterministic fatal runtime trap terminates the program and does
not run lexical cleanup.

Every reference stored inline outlives its binding. Every reference retained
in an arena slot outlives that slot's Store. Direct calls preserve these
invariants through their summaries, including direct recursion. Indirect
calls enforce the conservative contract. Whole-value reads and projections preserve the
reference bound. Dynamic indexing and other runtime validity checks are
still required. The guarantee covers single-threaded safe Nippy and assumes
correct C lowering.

### C11 representation

Use one descriptor ABI for scoped and global allocations. The Store component
is a pointer to a stable Store header. Global headers have static storage
duration; scoped headers are locals in the declaring activation. Store
parameters pass this pointer. Arena-backed descriptors use byte offsets into
the backing buffer, retaining current generation and bounds validation in
this release. Static Text keeps its distinct literal representation within the
same Text ABI. No descriptor exposes its pointer to Nippy or C callers.

Keep contiguous reallocating buffers for this release. Preserve checked
alignment, size arithmetic, offset bounds, and initialization. Resolve
backing pointers after evaluating operands and after operations that can
grow the Store. If a helper needs to grow a Store while copying from it,
carry offsets and resolve again, or stage the bytes safely. Use overlap-safe
copies where required. Generation checks do not make a pointer to a dead
header safe. The compiler's escape check establishes header lifetime.

Initial lowering frees at each exit. Optional recycling may reuse a buffer
within one loop activation after the first actual entry, retaining peak
capacity until loop exit. It must preserve size-expression evaluation and
validation, fresh logical contents, and avoid allocation on an untaken entry.
No shrink policy or inter-call cache is required. Do not enable recycling
until its allocation and OOM behavior is specified and tested. Preserve
left-to-right operand evaluation through temporaries.

## Drawbacks

- Region analysis adds compiler work. Control-flow joins and direct-call
  requirements replace the original one-depth rule. Bounds are conservative
  after opaque loads and indirect calls. A read-only helper can take a Slice
  when its elements cannot expose retaining storage. A named direct call can
  express a more precise contract. Explicit byte copies promote Text to an
  outer Store; linked values need reconstruction when references would
  otherwise escape.
- Named-call contracts depend on the body. Editing a retaining operation can
  break a caller even when its written signature stays the same. Propagate
  the call chain with each requirement so diagnostics can identify the write
  that caused it. Future module interfaces and incremental compilation would
  need to persist and invalidate these contracts.
- Store parameters must be threaded through helpers that choose where to
  allocate. This is intentional. The same API works with a global Store and
  caller scratch without an implicit allocator.
- Reallocation can copy the backing buffer. Replaced collection buffers may
  remain live for existing slices. Whole-value read and write may copy large
  values. Per-request handlers called from a loop still allocate independently
  under the initial lowering. No measured speedup is assumed.
- Removing reset requires migration. Move scratch allocation into the repeated
  lexical block or handler invocation. For state with independent retirement,
  existing reset-based designs may need restructuring. Fatal OOM remains the
  current allocation contract.

## Rationale and alternatives

A scalar depth loses the location of an outer list inside a record that also
holds inner Text. A single call-site floor for every call is sound with a
corresponding callee contract, but rejects useful mixed-lifetime helpers,
including Store helpers that only copy bytes. A Slice helps with read-only
list access, but cannot express the full Store case. Separate bounds and
direct-call summaries preserve those cases while keeping the conservative
contract at indirect calls. No surface lifetime syntax is needed. The
additional checker work earns its place by allowing a common caller-owned
output pattern, including recursive parsers, without a forced copy or a
public lifetime annotation. Heap provenance, more precise indirect effects,
and zero-copy access are outside that case and stay out of this release.

Using binding depth for all reads rejects ordinary returns of caller-owned
or global data held in locals and loses mutable backing provenance. Keeping
facts for the current value handles both. The zero lower bound for opaque
loads is a deliberate limit on analysis through memory.

An epoch-typed reset would need effects for invalidation across calls.
Unique ownership would address independently freed objects and exclusive
access, at a higher language and compiler cost. A library can provide a bump
allocator, while scope cleanup and statically checked escapes need compiler
support.

A stable Store header plus offsets lets all functions share one ABI and
reuses the current growth strategy. A non-moving chunked allocator merits a
separate comparison for large buffers and stable addresses. A stable address
alone does not protect a foreign pointer retained past Store exit. A free-block
cache could reduce churn across calls at the cost of retained memory; a stack
buffer trades heap traffic for stack usage. Neither is needed for correctness.

## Prior art

Nippy's existing Stores already exercise bump allocation, offset descriptors,
and reset-time generation checks. This RFC keeps that allocation model and
replaces user-controlled invalidation with lexical cleanup.

Rust's function lifetime elision gives inputs separate lifetimes and has
specific rules for choosing an output lifetime. This RFC uses a body-derived
contract for direct calls and a single conservative contract for function
values. [Rust Reference](https://doc.rust-lang.org/reference/lifetime-elision.html)

## Unresolved questions

The initial feature has a specified syntax, retention contract, ABI, cleanup
behavior, and migration. Follow-up decisions include the measured value of
recycling, chunked growth, and removing generation fields, and which programs
justify more precise indirect calls or reads through arena memory. Those
changes require their own specification and evidence.

## Implementation plan

1. **Descriptor ABI.** Switch every generated arena helper from Store IDs to
   stable header pointers. Keep current language behavior and runtime checks.
   Run the existing suite before adding syntax.
2. **Scope lowering and local checks.** Add the keyword, AST and typed nodes,
   capacity, body, lexical Store identity, and source positions. Add cleanup
   on all exits and validate typed links. Implement local value facts,
   projections, joins, and retaining checks. Keep the feature internal
   while cross-call behavior is incomplete.
3. **Function boundaries.** Add Store parameters and implement the indirect
   contract. Add direct-call summaries and the self-recursion fixed point.
   The checker computes contracts and diagnostics. The read-only analyser
   validates the typed links and scope ownership. The generator uses only
   the typed artifact.
4. **Linked payloads.** Add recursive struct placeholders, layout ordering,
   and payload validation. Check alloc and handle writes, including copies
   made in generated helpers. Keep opaque reads conservative.
5. **Release and migration.** Remove reset and the legacy top-level var
   Store syntax with their tests and lowering. Update README, ARCH, API,
   SEMANTICS, GRAMMAR, BUILD, and the AGENTS status table as affected.
   Record that arena handles supersede the proposed one-pointer-per-storage
   rule for these values and that the checker owns lifetime analysis.
6. **Verification.** Test each slice as it lands. Run the guide examples and
   reject both mixed-aggregate escapes. Exercise inline writes, aliases,
   joins, generic calls, indirect calls, recursive parsing with scratch input,
   linked payloads, expanding recursion, allocation failure, and nested
   return, break, and continue. Verify growth during argument evaluation,
   untaken scopes, and evaluation of a return before cleanup. Run
   make test and keep the C11 warning build clean. Measure allocation count,
   peak retained bytes, bytes copied, and checker time. Include a handler
   called from a loop, where lexical loop recycling alone cannot help.
   Compare a bounded free-block cache only if allocator churn is material.

Add lowercase, stage-prefixed diagnostics through include/messages.h and
assert each in tests. Cover illegal capabilities, invalid capacities and
payloads, escaping assignments and returns, retaining writes, unsatisfied
call obligations, incompatible function values, and expanding recursion.
For a call error, preserve the source and destination bounds and a witness
chain to the retaining operation. Deduplicate obligations by bound pair,
keeping the shortest witness chain and using source order to break ties.

## Future possibilities

Typed reusable pools with generation-checked IDs can support game entities,
queues, and event-loop connections whose lifetimes end independently. A
future task-owned scope could survive suspension, with cancellation waiting
for operations that still use its Store. Shared Stores require a concurrency
and data-race contract. Stable or foreign-owned buffers, recoverable
allocation, and zero-copy access need separate designs. A bounded WebSocket
server and a game with sustained entity churn are workload gates for those
extensions, rather than for lexical reclamation itself.
