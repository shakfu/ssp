"""SynthDefs and a sound file for the @buf tests in scsy_test.cpp:

    uv run --with nanosynth python buffers.py

click.wav is 64 frames of 16-bit mono at 48 kHz: frame i is i / 128.
"""

import struct
import wave
from pathlib import Path

from nanosynth import synthdef
from nanosynth.ugens import In, NumOutputBuses, Out, PlayBuf, RecordBuf

HERE = Path(__file__).parent


@synthdef()
def bufplay():  # buffer 0, looped, to output 1
    Out.ar(bus=0, source=PlayBuf.ar(buffer_id=0, loop=1))


@synthdef()
def bufrec():  # input 1 into buffer 1, once
    RecordBuf.ar(source=In.ar(bus=NumOutputBuses.ir()), buffer_id=1, loop=0)


@synthdef()
def bufread():  # buffer 1, looped, to output 1
    Out.ar(bus=0, source=PlayBuf.ar(buffer_id=1, loop=1))


for d in (bufplay, bufrec, bufread):
    (HERE / f"{d.name}.scsyndef").write_bytes(d.compile())
with wave.open(str(HERE / "click.wav"), "wb") as w:
    w.setnchannels(1)
    w.setsampwidth(2)
    w.setframerate(48000)
    w.writeframes(b"".join(struct.pack("<h", i * 256) for i in range(64)))
