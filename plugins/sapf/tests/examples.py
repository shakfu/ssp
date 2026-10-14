"""Rewrite sapf-examples.txt for the harness: each `play` becomes `2 "exN" >nrt`, a 2 s score.

usage: python3 examples.py SAPF_EXAMPLES OUT
"""

import re
import sys


def rewrite(text: str) -> str:
    n = 0

    def score(_: re.Match) -> str:
        nonlocal n
        n += 1
        return f'2 "ex{n:03d}" >nrt'

    lines = []
    for line in text.splitlines():
        code, sep, comment = line.partition(";")  # sapf comments run from ; to the line's end
        lines.append(re.sub(r"(?<![\w>])play\b", score, code) + sep + comment)
    return "\n".join(lines) + "\n"


if __name__ == "__main__":
    with open(sys.argv[1]) as f:
        text = f.read()
    with open(sys.argv[2], "w") as f:
        f.write(rewrite(text))
