# Automatically sized parallel map

- Feature name: `parallel_map`
- Start date: 2026-11-13
- Status: proposed. Native-thread prototypes exercised.

## Summary

Add a blocking built-in, `parallel_map(input, worker)`, for independent CPU
work. The caller describes the jobs and Nippy chooses the number of
participants. The caller participates alongside native C11 threads. Results
preserve input order. This proposal adds no keyword.

## Motivation

A renderer or batch processor should be able to expose many independent jobs
without querying the machine or dividing work into one job per core. A
compiler-generated worker trampoline and shared chunk counter can run these
jobs on several cores with little runtime machinery.

## Guide-level explanation

```nippy
fun render_tile(job: TileJob): Tile
  return render(job)
end

var tiles = parallel_map(tile_jobs, render_tile)
```

Here `tile_jobs` is an `Array<TileJob,N>`. The call blocks until all tiles are
ready and returns an `Array<Tile,N>` with the same length and capacity as the
input. Tile types in this example are self-contained values. Tile count is a
property of the image and the chosen tile size. On a machine with fewer
available CPUs, each participant handles more tiles. Worker execution order
is unspecified.

## Reference-level explanation

### Input and callback

The first version takes an existing fixed-capacity `Array<T,N>` and a named
non-generic function of one argument with signature `T -> U`. The checker
derives the result type `Array<U,N>`. Both array types must satisfy existing
element, capacity, and by-value size limits. Empty input returns an empty
array without calling the worker. Evaluate the input once and materialize
its by-value snapshot before launching work.

Initially require self-contained element and result types. Accept inline
scalars, fixed strings, Error values, and eligible inline aggregates whose
members satisfy the same rule. Reject arena-backed descriptors and function
values transitively, including through aliases and generic aggregate
instantiations. Workers may use ordinary locals. When scoped Stores land,
allow worker-private Stores under the scoped-arena rules. Their descriptors
still cannot cross the map boundary.

Check the worker's entire reachable call graph. Allow named direct calls
and operations on private values. Reject mutable global Store access,
indirect calls, nested `parallel_map`, and external or externally visible
side effects, including printing and blocking I/O. Apply the same
restrictions even when the map runs inline. Use a visited set for recursive
calls. Resolve the audit over checked functions and concrete
instantiations, with diagnostics pointing to the worker and the offending
operation. This gives workers independent inputs and results without
sharing mutable backing storage.

### Automatic sizing and execution

Use the process's available logical CPU count as a starting budget. On Linux
count the CPUs in the process affinity mask. Fall back to one participant
when the count cannot be obtained. Cap participants by the number of jobs.
The budget includes the caller, so a one-participant run creates no threads.
An affinity count does not reflect every CPU quota or current system load.

For the initial implementation, start at most one helper per additional
participant for the duration of the call. If thread creation fails, continue
with the caller and the helpers already started. Reserve no jobs for threads
that failed to start. Workers claim disjoint ranges from a shared counter
using C11 atomics. Each writes its result to the matching output slot.
Join every started helper before returning or releasing the snapshot and
output storage. An unrecoverable join failure traps before reclaiming
storage. The generator emits the trampoline and helper code only when a map
is used.

Start with the caller alone for zero or one job. Benchmark a serial
cutoff and chunk sizes before tuning. A persistent pool can avoid thread
startup on repeated calls. Treat that as an optimization after measuring
the per-call implementation. It does not change the source API. Preserve
the normal C11 build path for programs without parallel work. Document
native thread support and required linker flags for parallel programs.

## Drawbacks

Automatic sizing provides opportunities to use the available CPUs when
enough jobs are runnable. Memory bandwidth, imbalance, thread startup, and
other runnable processes affect the best count. The built-in blocks its
caller and is intended for CPU work. The first version's array and
self-contained-value limits rule out large arena-backed results and
callbacks with captured state.

## Rationale and alternatives

A direct `Thread.start` API gives control over worker lifetime and blocking
work. It also makes callers choose thread counts, split jobs, and join the
workers. A map packages those decisions around one common workload.
Exposing a worker limit or a persistent pool adds API surface. Repeated
batches and main-thread latency can establish whether either is needed.

The earlier cooperative-task design addressed waiting for I/O. This proposal
uses blocking fork/join and has no suspension, event loop, or fiber runtime.

## Prior art and prototype findings

The scratch C11 probes under `tmp/` used `<threads.h>` and checked that each
worker contributed. On the test device, WSL exposed six cores and twelve
logical CPUs. The renderer's best median was around 76 ms with 24–32 spawned
workers, versus 483 ms on the main thread. Streaming over two arrays
totaling 256 MiB flattened around six workers. A separate synthetic blocking
workload benefited from more workers and was fastest at 48 in that sweep.
These measurements support automatic sizing as a starting policy and show
why it is workload-dependent. The probes measured explicit thread counts.
They did not exercise Nippy lowering or automatic sizing.

Reproduction sources are `tmp/native_threads.c` and
`tmp/native_thread_workloads.c`. Both build under Arch WSL with
`cc -std=c11 -O2 -Wall -Wextra -Wpedantic -Werror -pthread`.

## Unresolved questions

Choose a serial cutoff and chunk size after timing empty, small, uneven,
and repeated batches. Measure startup costs before adding a persistent
pool. A future platform policy may account for CPU quotas and offer a limit
for applications that reserve CPU time for other work.

## Implementation plan

1. Prototype the automatic participant count, caller participation, chunk
   claiming, and partial-launch fallback in C11. Compare per-call threads
   with reuse on repeated batches and measure launch cost and wall time.
2. Resolve the built-in through the checker, type the result using the
   existing Array machinery, and audit the reachable callback graph.
   Record worker identity and input and output types in the typed artifact.
   The analyser validates the links and the generator consumes the typed
   data.
3. Emit the C trampoline, disjoint writes, join, and availability query.
   Wire thread flags into the driver only for programs using the built-in.
   Keep the initial feature independent of scoped Store implementation.
4. Test empty and single-item inputs, partial arrays, output order,
   restricted callback effects, multiple workers, and thread-creation
   fallback. Verify the serial and parallel paths yield identical results.
   Update the affected language and build docs, then run `make test`
   and keep the strict C11 warning build clean.

The worker loop is small. The compiler work is larger because it must enforce
callback isolation, carry typed function information into C lowering, and
handle thread startup and cleanup through the normal compile and run paths.
Restricting the first release to arrays and named callbacks keeps that work
bounded.

## Future possibilities

Add slices or caller-provided output storage when real workloads need
larger batches. Consider reusable workers for repeated maps. Explicit native
thread handles and blocking I/O remain separate designs.
