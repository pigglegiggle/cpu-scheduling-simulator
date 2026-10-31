"""Compile the native backend for the active Python ABI, then replace atomically."""
import os
from pathlib import Path
import subprocess
import sys
import sysconfig
import tempfile
import pybind11

root = Path(__file__).resolve().parents[1]
output = root / "scheduler" / ("_native" + sysconfig.get_config_var("EXT_SUFFIX"))
sources = [root / p for p in ("bindings/python.cpp", "src/scheduler.cpp",
                            "include/das/scheduler.hpp", "include/das/indexed_heap.hpp", "scripts/build_native.py")]
if output.exists() and output.stat().st_mtime >= max(p.stat().st_mtime for p in sources):
    print("Native module is up to date.")
    sys.exit(0)
(root / "build").mkdir(exist_ok=True)
with tempfile.TemporaryDirectory(prefix="native-", dir=root / "build") as temporary:
    compiled = Path(temporary) / output.name
    command = [os.environ.get("CXX", "clang++"), "-std=c++17", "-O2", "-Wall", "-Wextra",
               "-Wpedantic", "-shared", "-fPIC", "-I" + str(root / "include"),
               "-I" + pybind11.get_include(), "-I" + sysconfig.get_path("include"),
               str(root / "bindings/python.cpp"), str(root / "src/scheduler.cpp"), "-o", str(compiled)]
    if sys.platform == "darwin":
        command += ["-undefined", "dynamic_lookup"]
    subprocess.run(command, check=True)
    os.replace(compiled, output)
print("Built", output.name)
