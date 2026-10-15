"""Compare the harness's PC and SSP results.

The PC runs twice. sapf seeds each voice thread from the wall clock, so `ola` and similar blocks differ
between runs; a block or file that differs between the two PC runs is nondeterministic. Its SSP file
must only exist where the PC rendered one and hold finite samples. A deterministic file must match
within ATOL per sample: the SSP's libm rounds differently.

usage: python3 compare_render.py PC_LOG PC2_LOG SSP_LOG PC_DIR PC2_DIR SSP_DIR
"""

import math
import os
import re
import struct
import sys

ATOL = 1e-4


def sections(path: str) -> dict[str, list[str]]:
    """A log's lines by block, with paths cut to file names; the prelude's lines dropped."""
    out: dict[str, list[str]] = {}
    block = ""
    with open(path) as f:
        for line in f:
            if line.startswith("== block "):
                block = line.split()[2]
                out[block] = []
            elif block:
                out[block].append(re.sub(r"'[^']*/([^/']*)'", r"'\1'", line))
    return out


def floats(data: bytes) -> tuple[float, ...]:
    return struct.unpack(f"<{len(data) // 4}f", data[: len(data) - len(data) % 4])


def max_diff(a: bytes, b: bytes) -> float:
    if len(a) != len(b):
        return math.inf
    return max((abs(x - y) for x, y in zip(floats(a), floats(b))), default=0.0)


def read(d: str, n: str) -> bytes | None:
    p = os.path.join(d, n)
    return open(p, "rb").read() if os.path.exists(p) else None


def main(pc_log: str, pc2_log: str, ssp_log: str, pc_dir: str, pc2_dir: str, ssp_dir: str) -> int:
    bad = []
    pc, pc2, ssp = sections(pc_log), sections(pc2_log), sections(ssp_log)
    if pc.keys() != ssp.keys():
        bad.append("logs have different blocks")
    loose = 0
    for b, lines in pc.items():
        if lines != pc2.get(b):
            loose += 1  # nondeterministic: what it renders varies
            keep = [x for x in lines if not x.startswith("rendered")]
            if keep != [x for x in ssp.get(b, []) if not x.startswith("rendered")]:
                bad.append(f"block {b}: log differs")
        elif lines != ssp.get(b):
            bad.append(f"block {b}: log differs")

    names = sorted(set(os.listdir(pc_dir)) | set(os.listdir(pc2_dir)) | set(os.listdir(ssp_dir)))
    exact = rounded = nondet = 0
    worst = 0.0
    for n in names:
        a, a2, s = read(pc_dir, n), read(pc2_dir, n), read(ssp_dir, n)
        if s is None and a is None and a2 is None:
            continue
        if a != a2:
            nondet += 1
            if s is None:
                continue  # silent this time
            if a is None and a2 is None:
                bad.append(f"{n}: only on the SSP")
            elif n.endswith(".f32") and not all(map(math.isfinite, floats(s))):
                bad.append(f"{n}: not finite on the SSP")
            continue
        if s is None or a is None:
            bad.append(f"{n}: only on one side")
        elif a == s:
            exact += 1
        elif n.endswith(".f32") and (d := max_diff(a, s)) <= ATOL:
            rounded += 1
            worst = max(worst, d)
        else:
            bad.append(f"{n} differs" + (f" by {max_diff(a, s):.3g}" if n.endswith(".f32") else ""))
    print(f"{len(pc)} blocks ({loose} nondeterministic); {len(names)} files: {exact} equal, {rounded} within "
          f"{ATOL} (worst {worst:.3g}), {nondet} nondeterministic")
    for b in bad:
        print(b)
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(*sys.argv[1:]))
