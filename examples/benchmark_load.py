#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2004-2026 Manvel Avetisian

"""Benchmark GLB / .edge model load times.

Usage examples:
  python benchmark_load.py assets/*.glb
  python benchmark_load.py scene.edge --repeats 5
  python benchmark_load.py *.glb *.edge --backend null
"""
from __future__ import annotations

import argparse
import statistics
import sys
import time
from pathlib import Path

try:
    import pyedge
except (ModuleNotFoundError, ImportError) as exc:
    raise RuntimeError(
        "pyedge package is not importable.\n"
        "Build native bindings first:\n"
        "  cmake --build build --target pyedge"
    ) from exc


def _load_file(path: Path, renderer, scheduler) -> None:
    model = pyedge.StaticModelPTBNT(renderer, scheduler)
    if path.suffix.lower() in (".gltf", ".glb"):
        model.load_from_gltf(str(path))
    else:
        model.load_from_file(str(path))


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Measure average load time for .glb / .edge model files."
    )
    parser.add_argument(
        "files",
        nargs="+",
        type=Path,
        metavar="FILE",
        help=".glb, .gltf, or .edge file(s) to benchmark.",
    )
    parser.add_argument(
        "--repeats",
        type=int,
        default=1,
        metavar="N",
        help="Number of times to load each file (default: 1).",
    )
    parser.add_argument(
        "--backend",
        type=str,
        default="auto",
        metavar="BACKEND",
        help="Renderer backend: auto|vulkan|null (default: auto).",
    )
    args = parser.parse_args()

    paths = [p.expanduser().resolve() for p in args.files]
    missing = [p for p in paths if not p.exists()]
    if missing:
        for p in missing:
            print(f"error: file not found: {p}", file=sys.stderr)
        return 1

    repeats = max(1, args.repeats)

    renderer = pyedge.create_renderer(1, 1, backend=args.backend, offscreen=True)
    print(f"Backend: {renderer.backend}")
    scheduler = renderer.create_scheduler()

    # Warm-up: one silent load of the first file so the first timed result
    # is not penalised by driver/shader initialisation.
    try:
        _load_file(paths[0], renderer, scheduler)
    except Exception:
        pass

    times: dict[Path, list[float]] = {p: [] for p in paths}
    errors: dict[Path, str] = {}

    for path in paths:
        for _ in range(repeats):
            t0 = time.perf_counter()
            try:
                _load_file(path, renderer, scheduler)
            except Exception as exc:
                errors[path] = str(exc)
                break
            times[path].append(time.perf_counter() - t0)

    # ---- report ----
    successful = [p for p in paths if times[p]]
    all_times = [t for p in successful for t in times[p]]

    col = max((len(p.name) for p in paths), default=4)

    print()
    for path in sorted(successful, key=lambda p: statistics.mean(times[p])):
        ts = times[path]
        mean = statistics.mean(ts)
        if repeats > 1:
            stdev = statistics.stdev(ts) if len(ts) > 1 else 0.0
            print(f"  {mean:.3f}s ±{stdev:.3f}s  {path.name:<{col}}  ({len(ts)} runs)")
        else:
            print(f"  {mean:.3f}s  {path.name}")

    for path, msg in errors.items():
        print(f"  FAILED  {path.name}: {msg}")

    if all_times:
        avg = statistics.mean(all_times)
        total = sum(all_times)
        n = len(successful)
        print()
        print(f"  {n} file(s)  {len(all_times)} load(s)  total {total:.3f}s  avg {avg:.3f}s/load")

    return 0 if not errors else 1


if __name__ == "__main__":
    raise SystemExit(main())
