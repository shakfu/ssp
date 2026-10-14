"""score.osc for scsynth_test.cpp: an NRT command file of the kind sapf-sc's >nrt writes.

    uv run --with nanosynth python score.py

The main synth sends bus 512 to output 1. At 0 s a voice writes 0.5 to bus 512 for 10 ms; at 0.05 s
another writes buffer 600's value, 0.25, which the score allocates and fills. Buffers 601-620 are
there for their /done replies, more than ScWorld's reply ring holds. The score is 0.1 s long.
"""

import struct
from pathlib import Path

from nanosynth import DoneAction, synthdef
from nanosynth.osc import OscBundle, OscMessage
from nanosynth.ugens import DC, In, Index, Line, Out


@synthdef()
def score_main(level=1.0):
    Out.ar(bus=0, source=In.ar(bus=512) * level)


@synthdef()
def score_click():
    Out.ar(bus=512, source=Line.ar(start=0.5, stop=0.5, duration=0.01, done_action=DoneAction.FREE_SYNTH))


@synthdef()
def score_table():
    gate = Line.ar(start=1, stop=1, duration=0.01, done_action=DoneAction.FREE_SYNTH)
    Out.ar(bus=512, source=Index.ar(buffer_id=600, source=DC.ar(source=0)) * gate)


def start(name, action):
    return OscMessage("/s_new", name, -1, action, 0)


bundles = [
    (0.0, [OscMessage("/b_alloc", 600, 4, 1, OscMessage("/b_setn", 600, 0, 1, 0.25)),
           *(OscMessage("/b_alloc", 601 + i, 4, 1) for i in range(20)),
           *(OscMessage("/d_recv", d.compile()) for d in (score_main, score_click, score_table))]),
    (0.0, [start("score_main", 1)]),
    (0.0, [start("score_click", 0)]),
    (0.05, [start("score_table", 0)]),
    (0.1, [OscMessage("/c_set", 0, 0)]),
]
out = b""
for time, messages in bundles:
    data = OscBundle(timestamp=time, contents=messages).to_datagram(realtime=False)
    out += struct.pack(">i", len(data)) + data
(Path(__file__).parent / "score.osc").write_bytes(out)
