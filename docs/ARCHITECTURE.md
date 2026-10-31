# Architecture and scheduling semantics

## Boundaries

The C++ library is the scheduling authority. Python validates JSON shape and expands periodic templates into finite jobs. pybind11 converts those values, releases the GIL while the C++ simulation runs, then converts results. The CLI and Streamlit import the same library; the UI does not implement a second scheduler.

This is a deterministic discrete-event, single-CPU simulation. A job's execution demand is supplied, not predicted. Simulation ticks do not map to host sleeps. No interrupt handler, OS task switch, hardware DMA, mutex blocking or multicore execution is modeled.

## Indexed heap invariant

For every index `i`, `positions[heap[i].id] == i`; both collections have equal cardinality and IDs are unique. The parent is never ranked after a child. `valid()` checks all these properties without modifying the heap or counters.

Keys are `(rank, release_sequence, input_id_index)`. Rank is absolute deadline for EDF, numeric priority for Fixed Priority, and zero for FIFO. Release sequence breaks ties permanently: preemption does not send a job to the back of its tie group. Simultaneous releases inherit input order.

- Insert appends and sifts up.
- Update looks up an arbitrary ID in the map and repairs upward or downward.
- Erase swaps the target with the last node, removes the node/map entry, then repairs the moved node. The root and last-node cases use the same implementation.
- Every swap updates the two map entries. No lazy deletion, duplicate copies or tombstones are used.
- The running job stays in the ready heap. Its remaining execution changes, but its rank does not, so the engine does not reinsert it on every event.

Heap repair is O(log R) worst case for R ready jobs, with expected O(1) hash lookup. Hash collisions can make lookup O(R) in the worst case; this implementation does not claim hard worst-case real-time bounds. Vector growth can reallocate on insert; the simulator reserves capacity from its known finite job count. Heap counters include initialization and all mutations, and a comparison counter counts only heap-order comparisons.

## Deadline heap

A second indexed min-heap contains released, unfinished, uncancelled jobs whose first miss has not been observed. Its rank is the current absolute deadline. It allows the event loop to jump to the next deadline, and permits updating/cancelling a deadline without scanning every active job.

When a deadline expires with execution remaining, its entry is removed and a `deadline_miss` event is recorded once. The job remains in the ready queue and can finish late. Extending a missed job's deadline does not erase history or reinsert it into the miss heap. Completed and cancelled jobs are removed from both heaps.

## Event ordering

Release and action vectors are independently stable-sorted once. Cursors consume each stream, so no cursor removal requires shifting a vector. At each timestamp `t`:

1. Finish the job that consumed its last execution unit at `t`.
2. Apply all controls at `t` in the original action-array order.
3. Release all noncancelled jobs at `t` in input order.
4. Mark unresolved active deadlines `<= t` missed.
5. Select the ready-heap root; count a preemption if the previously executing job remains runnable with work left and a different job is selected.
6. Advance to the earliest next completion, release, control or unresolved deadline; account the intervening CPU interval in one operation.

Completion exactly at a deadline succeeds. A cancel at the same time as release prevents execution. A cancel exactly at the unresolved deadline wins over the miss check. A control aimed at an already completed/cancelled job is an audited `ignored_action`. Controls can modify future jobs before release. Moving an active deadline into the past triggers a miss at the current event time. Updated deadlines must still be ≥ the original release time.

The simulation consumes the supplied release/control streams, even if later events concern cancelled or completed jobs. Accordingly, elapsed time can include trailing idle intervals before those events. Utilization uses this entire 0-to-final-event observation window.

## Complexity

Let N be jobs and A controls. Validation uses expected O(N+A) hash operations; release/control sorting costs O(N log N + A log A). Scheduling uses O((N+A) log N) heap work plus trace/timeline output. Each release, control and first miss adds only a bounded number of selection boundaries; completion also advances directly. A long gap of 10¹² ticks is one interval, not 10¹² iterations.

Memory is O(N+A+E), where E is emitted timeline/trace rows. Python result conversion duplicates structures. Hash-map operations have expected rather than adversarial worst-case bounds. Counter arithmetic uses integer virtual time; input bounds keep accumulated execution within signed 64-bit range.

## Metrics

- `elapsed`: final simulated event time measured from zero; `busy`: executed units, including work later cancelled; `idle = elapsed - busy`.
- `utilization = busy / elapsed` (zero when elapsed is zero).
- `completed`, `cancelled`: final job states; all supplied jobs reach one of these states.
- `deadline_misses`: number of jobs with an observed miss, including jobs later cancelled; `deadline_met`: completed jobs with no historical miss.
- `preemptions`: interruptions of still-runnable jobs by another selected job. Cancellation/completion is not a preemption. `dispatches` counts transitions to a different running job, including first dispatch and resume.
- `mean_response`: average first-start minus release among jobs that actually start, including subsequently cancelled jobs.
- `mean_wait`, `mean_turnaround`: completed jobs only. Wait = finish − release − original execution demand; turnaround = finish − release.
- `max_tardiness`: max(0, finish − final deadline) among completed jobs. It can be zero even when a historical miss remains after a deadline extension.
- `engine_wall_ms`: native validation/simulation and C++ result construction; excludes Python object conversion/serialization and UI rendering. It includes runtime allocation/deallocation on the simulation path, and is not a per-dispatch hardware latency.

Adjacent execution intervals for the same job are merged, so a deadline/control event that does not change the CPU owner need not create a new timeline bar. The full event log retains the boundary.

## Verification strategy

Heap mutations are checked after every operation against an independent ordered reference. The scheduling reference advances one tick at a time and scans all ready jobs; it shares neither production heaps nor event-loop code. Seeded scenarios compare completion/start/cancellation times, executed/remaining units, terminal states and historical misses across three policies. Independent boundary cases exercise the specification where both implementations might otherwise make the same assumption.

AddressSanitizer and UndefinedBehaviorSanitizer are separate host checks. They do not prove race freedom or hardware timing; there are no parallel scheduling worker threads in this single-CPU simulator.
