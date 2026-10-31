# How to run

## First setup

From the repository root, using a C++17 compiler, make and Python 3.13:

```sh
make setup
make test
make demo
make ui
```

`make setup` creates `.venv`, installs `requirements.txt`, then builds a Python-ABI-specific native module in `scheduler/`. The C++ engine depends only on the standard library. Python is for the CLI, expansion, UI and reporting. Builds default to `clang++`; the Makefile's C++ targets can be invoked with `make CXX=g++`, while native builds read the `CXX` environment variable.

If Python 3.13 is installed as `python3.13`, use this equivalent setup:

```sh
python3.13 -m venv .venv
.venv/bin/python -m pip install -r requirements.txt
make native
```

When changing Python versions, recreate the virtual environment and native module for that interpreter. Native modules and virtual environments are not portable across OSes or Python ABIs.

## CLI

```sh
.venv/bin/python -m scheduler run examples/preemption.json --policy edf
.venv/bin/python -m scheduler compare examples/frame_pipeline.json --output build/comparison.json
.venv/bin/python -m scheduler validate examples/boundaries.json
```

Policies: `edf`, `fixed`, `fifo`. `--no-trace` removes event-log rows while keeping timeline and outcomes. Commands use no shell interpolation of job IDs. Validation errors print `error:` to stderr and exit with code 2. Workload input files are never overwritten by a run unless you explicitly choose that path as `--output`; choose a separate output path.

The Python module entry point must run from the repository root (or with that directory in `PYTHONPATH`). `make native` rebuilds only when sources or headers are newer than the compiled extension.

## UI

`make ui` starts the loopback Streamlit workbench at <http://127.0.0.1:8502>. Stop it with Ctrl+C. No API service or database is needed.

- Scheduling: example/upload → JSON → Run simulation → policy comparison → timeline → event replay → job table/download.
- Indexed heap: insert/update/cancel by numeric ID; inspect the actual C++ array/tree and map positions. Rank can be any allowed signed value in this lab; simulator job deadlines obey stricter workload rules.
- Measured benchmarks: reads retained JSON from `docs/benchmarks/latest.json`; it does not invent or rerun measurements on page load.

UI limits: 2 MiB input, 2,000 expanded jobs, first 300 timeline intervals displayed, up to 100 heap-lab nodes. Select a policy and move the event slider to inspect decision boundaries. If you edit a workload, the UI identifies results from the previous run until you run the new input. Uploaded files and session results are process-local and not persisted.

## Verification

```sh
make test       # native C++ checks plus Python/CLI/Streamlit tests
make sanitize   # AddressSanitizer and UndefinedBehaviorSanitizer on C++ tests
make benchmark  # compiled benchmark, measured report and retained raw JSON
```

`make test` needs the environment from setup. `make` alone builds the standalone C++ test/benchmark executables. `make benchmark` rewrites the public latest report with your measured environment and hashes, so retain or commit results intentionally. It does not need a GPU.

## Troubleshooting

| Symptom | Fix |
|---|---|
| Missing `clang++` on macOS | Install `xcode-select --install`. |
| Missing `_native` | Run `make setup` or `make native` with the correct environment. |
| Incompatible extension after changing Python | Recreate `.venv`, rebuild native and restart Streamlit. |
| Port 8502 in use | Stop the previous workbench; or invoke Streamlit manually with another `--server.port`. |
| Duplicate job ID | Check both explicit IDs and expanded `task#occurrence` IDs. |
| Too many expanded jobs | Reduce periodic counts or use the CLI limit instead of the smaller UI limit. |
| A task misses its deadline despite EDF | Inspect execution demand, contention, release times and overload; EDF is not a universal overload solution. |
| A previously missed job has zero tardiness | Deadline was extended after the miss; history is retained, final tardiness uses the final deadline. |
| Policy does not react to a priority update | Only Fixed Priority uses the priority field for ordering. |
| Graph looks unsorted | A heap requires parent ≤ children, not fully sorted array order. |
| No space left on device | Free disk space before installing/building; `.venv` and sanitizer binaries require additional storage. |

Private learning material lives under `study/` and is excluded by `.gitignore`. Generated executables, native extensions, caches and `.venv` are also ignored. Public measured reports and diagrams live in `docs/`.
