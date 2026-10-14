"""Compare the harness's host and SSP results: logs equal, files equal but for float32 rounding.

The SSP's libm rounds some wave-table values differently: about one float32 step. A word that
differs passes when both decode as floats within RTOL of each other.

usage: python3 compare.py HOST_LOG SSP_LOG HOST_DIR SSP_DIR
"""

import os
import re
import struct
import sys

RTOL = 1e-5


def log(path: str) -> list[str]:
    # paths differ between the machines: drop the prelude's line, keep written files' names
    with open(path) as f:
        return [re.sub(r"'[^']*/([^/']*)'", r"'\1'", line) for line in f if not line.startswith("loading file")]


def close(a: bytes, b: bytes) -> bool:
    if len(a) != len(b):
        return False
    for i in range(0, len(a) - len(a) % 4, 4):
        if a[i : i + 4] != b[i : i + 4]:
            (x,), (y,) = struct.unpack(">f", a[i : i + 4]), struct.unpack(">f", b[i : i + 4])
            if not abs(x - y) <= RTOL * max(abs(x), abs(y)):
                return False
    return a[len(a) - len(a) % 4 :] == b[len(b) - len(b) % 4 :]


def main(host_log: str, ssp_log: str, host_dir: str, ssp_dir: str) -> int:
    bad = []
    if log(host_log) != log(ssp_log):
        bad.append("logs differ")
    names = sorted(set(os.listdir(host_dir)) | set(os.listdir(ssp_dir)))
    rounded = 0
    for n in names:
        a, b = (os.path.join(d, n) for d in (host_dir, ssp_dir))
        if not (os.path.exists(a) and os.path.exists(b)):
            bad.append(f"{n}: only on one side")
            continue
        x, y = open(a, "rb").read(), open(b, "rb").read()
        if x == y:
            continue
        if close(x, y):
            rounded += 1
        else:
            bad.append(f"{n} differs")
    print(f"{len(names)} files, {rounded} within float32 rounding")
    for b in bad:
        print(b)
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(*sys.argv[1:]))
