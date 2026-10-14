"""Check sapf_server_test's onsets against the >nrt score of the same program and seed.

usage: python3 onsets.py STDOUT_FILE SCHED_OSC SECONDS   (exit 1 on a mismatch)
"""

import struct
import sys
from pathlib import Path

RATE = 48000
BLOCK = 64  # scsynth starts a synth on the block that holds its time
START = 0.2  # play starts voices this far after it is called


def score_times(path: Path) -> list[float]:
    """Times of the voices' /s_new bundles in an NRT score; the first /s_new is the main synth."""
    data = path.read_bytes()
    times, pos = [], 0
    while pos < len(data):
        (size,) = struct.unpack(">i", data[pos : pos + 4])
        bundle = data[pos + 4 : pos + 4 + size]
        pos += 4 + size
        if b"/s_new" in bundle:
            secs, frac = struct.unpack(">II", bundle[8:16])
            times.append(secs + frac / 2**32)
    return times[1:]


def check(stdout: str, sched: Path, seconds: float) -> list[str]:
    """Problems with the run: errors posted, voices missing or more than a block from the score."""
    onsets_text, posted = stdout.split("== post\n")
    onsets = [int(line.split()[1]) for line in onsets_text.splitlines() if line.startswith("onset ")]
    problems = [f"posted: {line}" for line in posted.splitlines() if "error" in line]
    # voices that start inside the recording, with a margin before its end
    expected = [t for t in score_times(sched) if t + START < seconds - 0.1]
    if len(expected) < 8:
        problems.append(f"only {len(expected)} voices in the score")
    if len(onsets) < len(expected):
        return problems + [f"{len(onsets)} onsets, {len(expected)} voices in the score"]
    for k, t in enumerate(expected):
        want, got = (t - expected[0]) * RATE, onsets[k] - onsets[0]
        if abs(got - want) > BLOCK:
            problems.append(f"voice {k}: frame {got}, score {want:.0f}")
    return problems


if __name__ == "__main__":
    problems = check(Path(sys.argv[1]).read_text(), Path(sys.argv[2]), float(sys.argv[3]))
    print("\n".join(problems) or "onsets match the score")
    sys.exit(1 if problems else 0)
