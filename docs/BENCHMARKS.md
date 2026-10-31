# Benchmark methodology

Run `make benchmark` from the repository root after setup. The C++ harness uses seed 20261101 and 1K/10K/100K entries. Python captures its output and environment/source hashes, records whole-process peak RSS on macOS, and writes `docs/benchmarks/latest.json` plus `docs/PERFORMANCE_REPORT.md`.

## Data structure experiment

For each size the harness prepares 1,000 deterministic operations before timing. Every fourth targets the current minimum and raises its rank above the initial range, forcing meaningful root repair. Other targets are random; approximately one third of all operations erase/reinsert an ID, and the rest update its rank. Each operation ends with a minimum query.

The heap is the same indexed C++ implementation used by the simulator. The baseline updates a fixed-ID array and linearly scans for the minimum, with identical rank/order/ID tie-breaking. It omits erase/reinsert bookkeeping, which favors the baseline on replacement operations. This is an algorithmic baseline, not a comparison against another scheduler product.

The harness verifies every returned minimum ID after warm-up and every measured batch, and checks the heap/map invariant outside timing. Each size uses seven measured batches with alternating method order. Initialization, workload generation, oracle preparation and invariant checks are excluded. Results are consumed through a checksum.

The metric is **median batch-amortized microseconds per mutation + minimum lookup**. It includes the complete operation loop divided by 1,000, not independent operation samples; it must not be called per-request p50/p95/p99. Random-target repairs often travel only part of the heap height. Hash lookups have expected O(1), and the implementation is not an adversarial worst-case bound.

## Scheduler experiment

One EDF simulation per size includes native validation, sorting, both heaps, job state, timeline creation and metrics, with event trace disabled. Jobs have execution 1, releases `i % 100`, deadlines `release + 100 + i % 1000`, priority `i % 16`. Every tenth job gets a cancellation at virtual time 50; some have already completed then, so not every cancellation succeeds. Completed + cancelled must equal the supplied count.

This is one observed **host wall runtime** per size, not a latency percentile or real-time interrupt/dispatch latency. Virtual scheduling charges zero overhead regardless of host runtime. Independent differential tests validate correctness; this larger benchmark checks conservation, not a separate full scheduling oracle for all 100K jobs.

## Limits

The machine is not reserved or thermally controlled. OS caches are not flushed, CPU affinity is not fixed, and desktop background processes remain active. macOS RSS is the peak of the entire harness, covering all sizes, both methods and simulation; it cannot be assigned to one size or to the heap alone. Non-macOS runs omit RSS. There is no hardware/RTOS/energy/network/concurrent-client benchmark.

Do not extrapolate the measured scan/heap ratio to all workloads. Quote workload, operation definition, baseline and machine together. Raw samples and hashes are retained so the report can be independently checked and regenerated.
