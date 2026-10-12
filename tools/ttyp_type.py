#!/usr/bin/env python3
"""Type text into ttyp through its key port, as if typed on a US keyboard.

Each newline is Enter. The text goes to whichever mode ttyp is in: live mode runs each line, edit
mode adds it to the script. Forward the port first: ssh -L 7770:127.0.0.1:7770 root@<ssp>

usage: ttyp_type.py [--port N] [--delay S] [FILE]   (stdin without FILE)
"""

import argparse
import socket
import sys
import time

PORT = 7770
TAP = 2
SHIFT = 0x02

# HID usages (page 0x07) on a US layout; letters go unshifted, teletype upper-cases them
UNSHIFTED = {
    **{c: 0x04 + i for i, c in enumerate("abcdefghijklmnopqrstuvwxyz")},
    **{c: 0x1E + i for i, c in enumerate("1234567890")},
    "\n": 0x28, "\t": 0x2B, " ": 0x2C, "-": 0x2D, "=": 0x2E, "[": 0x2F, "]": 0x30, "\\": 0x31, ";": 0x33,
    "'": 0x34, "`": 0x35, ",": 0x36, ".": 0x37, "/": 0x38,
}
SHIFTED = {
    "!": 0x1E, "@": 0x1F, "#": 0x20, "$": 0x21, "%": 0x22, "^": 0x23, "&": 0x24, "*": 0x25, "(": 0x26,
    ")": 0x27, "_": 0x2D, "+": 0x2E, "{": 0x2F, "}": 0x30, "|": 0x31, ":": 0x33, '"': 0x34, "~": 0x35,
    "<": 0x36, ">": 0x37, "?": 0x38,
}


def records(text):
    """The 4-byte tap record for each character; ValueError for one with no key."""
    out = []
    for c in text.replace("\r\n", "\n"):
        low = c.lower()
        if low in UNSHIFTED:
            out.append(bytes([TAP, UNSHIFTED[low], 0, 0]))
        elif c in SHIFTED:
            out.append(bytes([TAP, SHIFTED[c], SHIFT, 0]))
        else:
            raise ValueError(f"no key for {c!r}")
    return out


def send(text, port=PORT, delay=0.01):
    recs = records(text)
    with socket.create_connection(("127.0.0.1", port), timeout=5) as sock:
        for r in recs:
            sock.sendall(r)
            time.sleep(delay)  # each tap runs a key handler; a line's Enter can run a long script


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("file", nargs="?", type=argparse.FileType("r"), default=sys.stdin)
    ap.add_argument("--port", type=int, default=PORT)
    ap.add_argument("--delay", type=float, default=0.01, help="seconds between keys")
    args = ap.parse_args()
    try:
        send(args.file.read(), args.port, args.delay)
    except (ValueError, OSError) as e:
        sys.exit(f"ttyp_type: {e}")


if __name__ == "__main__":
    main()
