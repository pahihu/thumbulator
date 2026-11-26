#!/usr/bin/env python3
"""Time the same Cortex-M ComsTime image in each emulator."""
import argparse
import json
import re
from pathlib import Path
from resource import RUSAGE_CHILDREN, getrusage
from statistics import median
from subprocess import DEVNULL, run
from time import perf_counter

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--build", action="append", required=True, metavar="LABEL=EXECUTABLE")
parser.add_argument("--firmware", type=Path, default=Path("ComsTime.bin"))
parser.add_argument("--symbols", type=Path, default=Path("ComsTime.sym"))
parser.add_argument("--runs", type=int, default=9)
parser.add_argument("--output", type=Path, default=Path("measurements.json"))
args = parser.parse_args()
builds = {label: Path(path).resolve() for label, path in
          (item.split("=", 1) for item in args.build)}
names = tuple(builds)
samples = {name: [] for name in names}
expected = None

for repeat in range(args.runs):
    start = repeat % len(names)
    # Rotate starting positions and reverse every other group to balance order.
    order = names[start:] + names[:start]
    if (repeat // len(names)) % 2:
        order = order[::-1]
    for name in order:
        command = [str(builds[name]), "-f", "12000000",
                   "-m", str(args.symbols), str(args.firmware)]
        before_cpu = getrusage(RUSAGE_CHILDREN)
        before_wall = perf_counter()
        proc = run(command, stdin=DEVNULL, capture_output=True, check=True, timeout=60)
        wall = perf_counter() - before_wall
        after_cpu = getrusage(RUSAGE_CHILDREN)
        cpu = (after_cpu.ru_utime - before_cpu.ru_utime
               + after_cpu.ru_stime - before_cpu.ru_stime)
        stdout = proc.stdout.decode()
        assert "Communication Time benchmark" in stdout and not proc.stderr, (name, proc)
        assert re.search(r"^instructions\s+\d+", stdout, re.M), name
        if expected is None:
            expected = stdout
        assert stdout == expected, (name, stdout, expected)

        samples[name].append({"round": repeat + 1, "cpu_seconds": cpu, "wall_seconds": wall})
        print(f"{repeat + 1:2} {name:6} wall {wall:.4f} s CPU {cpu:.4f} s", flush=True)

medians = {
    name: {field: median(item[field] for item in values)
           for field in ("cpu_seconds", "wall_seconds")}
    for name, values in samples.items()
}
args.output.write_text(json.dumps({"samples": samples, "medians": medians,
                                   "guest_output": expected}, indent=2) + "\n")
for name, values in medians.items():
    print(name, *(f"{field}={value:.6f}" for field, value in values.items()))
