"""Run the C++ harness and retain evidence plus a human-readable measured report."""
import datetime
import hashlib
import json
from pathlib import Path
import platform
import re
import subprocess
import time

root = Path(__file__).resolve().parents[1]
output = root / "docs/benchmarks"
output.mkdir(parents=True, exist_ok=True)
binary = root / "build/benchmark"
command = [str(binary)]
if platform.system() == "Darwin":
    command = ["/usr/bin/time", "-l", *command]
begin = time.perf_counter()
observed = subprocess.run(command, text=True, capture_output=True, check=True, timeout=300)
result = json.loads(observed.stdout)
result["harness_wall_seconds"] = time.perf_counter() - begin
result["recorded_at"] = datetime.datetime.now().astimezone().isoformat()
result["platform"] = platform.platform()
result["compiler"] = subprocess.check_output(["clang++", "--version"], text=True).splitlines()[0]
result["flags"] = "-std=c++17 -O2 -Wall -Wextra -Wpedantic"
result["sha256"] = {str(p.relative_to(root)): hashlib.sha256(p.read_bytes()).hexdigest() for p in
                    [binary, root/"tests/benchmark.cpp", root/"src/scheduler.cpp", root/"include/das/indexed_heap.hpp"]}
if platform.system() == "Darwin":
    result["cpu"] = subprocess.check_output(["sysctl", "-n", "machdep.cpu.brand_string"], text=True).strip()
    result["ram_bytes"] = int(subprocess.check_output(["sysctl", "-n", "hw.memsize"], text=True))
    match = re.search(r"(\d+)\s+maximum resident set size", observed.stderr)
    if match:
        result["whole_harness_peak_rss_bytes"] = int(match[1])
result["methodology"] = [
    "1000 deterministic mutations per size; each mutation changes a rank or erases/reinserts an ID, then queries the minimum.",
    "Every fourth mutation targets the current minimum and increases its rank above the initial range; other IDs are random. A separate ordered-set oracle prepares the workload outside timing.",
    "Heap and scan initialization excluded. Scan updates a fixed-ID array in O(1), then scans all entries for its minimum.",
    "Seven batch repetitions, alternating execution order after one warm-up; every minimum ID verified against scan.",
    "Reported medians are medians of batch-amortized microseconds per mutation-plus-minimum, not per-request p50 latency.",
    "The scan baseline does not measure its erase/reinsert bookkeeping, favoring scan for replacement operations.",
    "Scheduler run includes validation, release/action sorting, two heaps, job results and timeline, but excludes event trace serialization.",
    "Each scheduler size has one wall-time observation; do not interpret these as scheduler latency percentiles.",
    "Virtual CPU time has zero dispatch/context-switch cost; host runtime is a separate metric, not an RTOS timing guarantee.",
    "RSS, where available, is the peak of the entire harness including baseline and scheduler, not index-only or per-size memory.",
]
raw = output / "latest.json"
raw.write_text(json.dumps(result, indent=2) + "\n")
lines = ["# Measured benchmark report", "", f"Recorded: {result['recorded_at']}", "",
         f"Platform: {result['platform']}. Compiler: {result['compiler']}. Optimization: `-O2`.", "",
         "## Indexed heap vs linear minimum scan", "",
         "1,000 mutations per batch; seven measured batches after warm-up. Values are batch-amortized microseconds per mutation + minimum lookup, **not per-request latency percentiles**.", "",
         "| Active entries | Indexed heap µs/op | Scan µs/op | Scan / heap | Verified |",
         "|---:|---:|---:|---:|---|"]
for size in result["sizes"]:
    lines.append(f"| {size['jobs']:,} | {size['heap_median_us']:.3f} | {size['scan_median_us']:.3f} | {size['scan_median_us']/size['heap_median_us']:.2f}× | Every minimum ID |")
lines += ["", "## Full EDF simulation", "", "One observation per size; trace disabled, timeline enabled. This is host computation time for a virtual workload.", "",
          "| Jobs | Host wall ms | Completed | Cancelled | Peak ready entries |", "|---:|---:|---:|---:|---:|"]
for size in result["sizes"]:
    lines.append(f"| {size['jobs']:,} | {size['scheduler_wall_ms']:.2f} | {size['completed']:,} | {size['cancelled']:,} | {size['ready_peak']:,} |")
if "whole_harness_peak_rss_bytes" in result:
    lines += ["", f"Whole harness peak RSS: **{result['whole_harness_peak_rss_bytes']/1048576:.2f} MiB** (includes all sizes and both methods)."]
lines += ["", "## Method and limits", "", *["- " + item for item in result["methodology"]], "",
          "This compares against a straightforward linear scan, not an optimized operating-system scheduler. Workloads are synthetic; the simulation is single-core, fully preemptible and uses known execution demands. Reproduce with `make benchmark`. Results depend on hardware, compiler and background load.", "",
          "[Raw observations and source/binary hashes](benchmarks/latest.json).", ""]
(root/"docs/PERFORMANCE_REPORT.md").write_text("\n".join(lines))
print(raw)
print(root/"docs/PERFORMANCE_REPORT.md")
