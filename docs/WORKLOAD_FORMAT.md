# Workload and output reference

Version 1 input is one JSON object. Unknown fields, duplicate keys and non-finite JSON constants are rejected. There is no expression evaluation or executable code in workload files.

| Top-level field | Meaning |
|---|---|
| `version` | Optional integer 1 |
| `name` | Optional display string, up to 200 characters |
| `time_unit` | `ticks` (default), `ms` or `us`; display label only |
| `jobs` | Finite array of explicit jobs |
| `periodic` | Finite templates expanded into additional jobs |
| `actions` | Finite timed controls |

At least one expanded job is required. Explicit jobs come first in input order, followed by each periodic template's occurrences. At simultaneous releases this order is the final stable tie-breaker.

## Explicit job

```json
{"id":"inspect", "release":100, "execution":6, "deadline":120, "priority":0}
```

`id`, `release`, `execution`, `deadline` are required. Priority defaults to 0; lower numbers win under Fixed Priority. EDF ignores priority for ranking. A deadline is an **absolute** timestamp, never a duration after release. Execution demand is known to the simulator and is decremented only while this job owns the CPU.

All numeric inputs must be integers, not floats or booleans. IDs are unique 1–80-character printable ASCII strings without spaces. Release/deadline/control times are 0..10¹²; execution is 1..10⁹; deadline must be ≥ release; priority is 0..1,000,000.

## Periodic template

```json
{"id":"frame", "offset":0, "period":16, "execution":7, "relative_deadline":16, "count":3, "priority":1}
```

Required: id, period, execution, relative_deadline, count. Offset and priority default to zero. Period/count are positive; relative_deadline can be zero. Occurrence IDs are `frame#0`, `frame#1`, `frame#2`. Occurrence `i` has release `offset + i*period` and deadline `release + relative_deadline`. Each expanded job must satisfy core bounds. Overlapping occurrences are independent jobs; no job is dropped because the next occurrence is released.

The finite count avoids an unbounded real-time process. The CLI/core accept at most 100,000 expanded jobs, and the UI caps this at 2,000. JSON text is at most 2 MiB in both interfaces.

## Actions

```json
[
  {"time":2, "job":"frame#0", "kind":"deadline", "value":10},
  {"time":3, "job":"frame#1", "kind":"priority", "value":0},
  {"time":4, "job":"frame#2", "kind":"cancel"}
]
```

Update actions require `value`; cancel forbids it. Targets must be known input job IDs, including future occurrences. Actions are stable-sorted by time and applied in input order at ties. Completed/cancelled jobs ignore subsequent controls, with an audit event. A priority update affects Fixed Priority ordering but does not alter EDF/FIFO ordering. Deadline updates affect EDF ordering and the independent miss tracker under all policies.

Limits: at most 100,000 actions. Core validation runs after expansion and before scheduling. The `validate` CLI command uses a full untraced simulation, so validation of a large workload still performs work proportional to the input.

## Results

`run` returns one policy result; `compare` returns an object keyed `edf`, `fixed`, `fifo`.

- `policy`, `name`, `time_unit`, `virtual_time: true`, `engine_wall_ms` identify the run.
- `jobs` reports final deadline/priority, terminal state, execution consumed/remaining, first start, finish, cancellation time, historical miss, wait and final-deadline tardiness. Unavailable timing fields are JSON null.
- `timeline` contains `{start,end,job}` intervals; null job means idle. Intervals are half-open `[start,end)`, contiguous from zero and coalesced when the owner does not change.
- `trace` contains `{time,event,job,ready_count,next_job}` rows. Event types: release, complete, dispatch, preempt, cancel, deadline_update, priority_update, deadline_miss, ignored_action. It is omitted as an empty array with `--no-trace`.
- `ready_count` includes the running job; `next_job` is the ready-heap root immediately after the named event, not a promise that no further simultaneous event will change it.
- `metrics` and `heaps` report the defined run outcomes and actual heap operations; see [metric definitions](ARCHITECTURE.md#metrics).

The trace is bounded at 1,000,000 rows. Large results are serialized in memory, so counts and input bounds are not a guarantee of low RAM use. The UI truncates only the timeline preview to 300 intervals and says so; the downloadable JSON retains the complete result.
