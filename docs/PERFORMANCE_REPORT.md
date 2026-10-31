# Measured benchmark report

Recorded: 2026-11-01T01:19:45.714168+07:00

Platform: macOS-26.5.2-arm64-arm-64bit-Mach-O. Compiler: Apple clang version 21.0.0 (clang-2100.1.1.101). Optimization: `-O2`.

## Indexed heap vs linear minimum scan

1,000 mutations per batch; seven measured batches after warm-up. Values are batch-amortized microseconds per mutation + minimum lookup, **not per-request latency percentiles**.

| Active entries | Indexed heap µs/op | Scan µs/op | Scan / heap | Verified |
|---:|---:|---:|---:|---|
| 1,000 | 0.159 | 1.275 | 8.02× | Every minimum ID |
| 10,000 | 0.081 | 7.415 | 91.26× | Every minimum ID |
| 100,000 | 0.081 | 57.502 | 706.26× | Every minimum ID |

## Full EDF simulation

One observation per size; trace disabled, timeline enabled. This is host computation time for a virtual workload.

| Jobs | Host wall ms | Completed | Cancelled | Peak ready entries |
|---:|---:|---:|---:|---:|
| 1,000 | 1.06 | 905 | 95 | 806 |
| 10,000 | 4.89 | 9,010 | 990 | 8,911 |
| 100,000 | 64.71 | 90,050 | 9,950 | 89,951 |

Whole harness peak RSS: **60.36 MiB** (includes all sizes and both methods).

## Method and limits

- 1000 deterministic mutations per size; each mutation changes a rank or erases/reinserts an ID, then queries the minimum.
- Every fourth mutation targets the current minimum and increases its rank above the initial range; other IDs are random. A separate ordered-set oracle prepares the workload outside timing.
- Heap and scan initialization excluded. Scan updates a fixed-ID array in O(1), then scans all entries for its minimum.
- Seven batch repetitions, alternating execution order after one warm-up; every minimum ID verified against scan.
- Reported medians are medians of batch-amortized microseconds per mutation-plus-minimum, not per-request p50 latency.
- The scan baseline does not measure its erase/reinsert bookkeeping, favoring scan for replacement operations.
- Scheduler run includes validation, release/action sorting, two heaps, job results and timeline, but excludes event trace serialization.
- Each scheduler size has one wall-time observation; do not interpret these as scheduler latency percentiles.
- Virtual CPU time has zero dispatch/context-switch cost; host runtime is a separate metric, not an RTOS timing guarantee.
- RSS, where available, is the peak of the entire harness including baseline and scheduler, not index-only or per-size memory.

This compares against a straightforward linear scan, not an optimized operating-system scheduler. Workloads are synthetic; the simulation is single-core, fully preemptible and uses known execution demands. Reproduce with `make benchmark`. Results depend on hardware, compiler and background load.

[Raw observations and source/binary hashes](benchmarks/latest.json).
