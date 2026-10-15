"""Summarize the harness's stderr: render load, host blocks over their time, and simulated FIFO depths.

usage: python3 loads.py HARNESS_STDERR
"""

import re
import sys

LINE = re.compile(r"block (\d+) eval [\d.]+ s, render load ([\d.]+), worst block ([\d.]+), over (\d+), fifo ([\d ]+)")
DEPTHS = (1, 2, 4, 8)  # harness.cpp, kFifoDepths


def quantile(xs: list[float], p: float) -> float:
    xs = sorted(xs)
    return xs[min(len(xs) - 1, int(len(xs) * p))]


def main(path: str) -> int:
    rows = []
    with open(path) as f:
        for line in f:
            if m := LINE.match(line):
                rows.append((int(m[1]), float(m[2]), float(m[3]), int(m[4]), [int(x) for x in m[5].split()]))
    if not rows:
        print("no renders")
        return 1
    loads = [r[1] for r in rows]
    worst = [r[2] for r in rows]
    print(f"{len(rows)} renders. load median {quantile(loads, .5):.3f}, p95 {quantile(loads, .95):.3f}, "
          f"max {max(loads):.3f}; worst block median {quantile(worst, .5):.2f}, max {max(worst):.2f}")
    print(f"in the callback: {sum(r[3] > 0 for r in rows)} renders with a block over its time "
          f"({sum(r[3] for r in rows)} blocks)")
    for i, d in enumerate(DEPTHS):
        late = [r for r in rows if r[4][i] > 0]
        print(f"FIFO {d} blocks ({d * 128 / 48:.1f} ms): {len(late)} renders late ({sum(r[4][i] for r in rows)} blocks)"
              + (f"; blocks {', '.join(str(r[0]) for r in late[:12])}" if late else ""))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1]))
