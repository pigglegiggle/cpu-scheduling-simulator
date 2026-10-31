import argparse
import json
from pathlib import Path
import sys
from .io import compare, load, prepare, run

parser = argparse.ArgumentParser(description="CPU Scheduling Simulator (virtual single CPU)")
parser.add_argument("command", choices=["run", "compare", "validate"])
parser.add_argument("workload", type=Path)
parser.add_argument("--policy", choices=["edf", "fixed", "fifo"], default="edf")
parser.add_argument("--output", type=Path)
parser.add_argument(
    "--no-trace", action="store_true", help="omit event trace; timeline and metrics remain"
)
args = parser.parse_args()
try:
    document = load(args.workload)
    if args.command == "compare":
        result = compare(document, trace=not args.no_trace)
    elif args.command == "run":
        result = run(document, args.policy, trace=not args.no_trace)
    else:
        # Native execution validates all core numeric constraints too.
        verified = run(document, "edf", trace=False)
        result = {
            "valid": True,
            "jobs": len(verified["jobs"]),
            "actions": len(prepare(document)[1]),
        }
    text = json.dumps(result, indent=2, allow_nan=False) + "\n"
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(text)
        print(args.output)
    else:
        print(text, end="")
except (ValueError, TypeError, KeyError, OverflowError, RuntimeError, OSError) as error:
    print(f"error: {error}", file=sys.stderr)
    sys.exit(2)
