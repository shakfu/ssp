"""SynthDefs for scsynth_test.cpp. Compiled with nanosynth (github.com/shakfu/nanosynth):

    nanosynth compile defs.py -o .

The .scsyndef files are committed, so the tests do not need nanosynth.
"""

from nanosynth import DoneAction, synthdef
from nanosynth.envelopes import EnvGen, Envelope
from nanosynth.ugens import DC, K2A, In, NumOutputBuses, Out, Saw, SinOsc
from nanosynth.ugens.sc3 import DFM1


@synthdef()
def sine(freq=440.0, amp=0.5):
    Out.ar(bus=0, source=SinOsc.ar(frequency=freq) * amp)


@synthdef()
def thru():
    Out.ar(bus=0, source=In.ar(bus=NumOutputBuses.ir(), channel_count=8))


@synthdef()
def dfm1(cutoff=800.0):
    Out.ar(bus=0, source=DFM1.ar(source=Saw.ar(frequency=110.0), freq=cutoff, noiselevel=0.0))


@synthdef()
def cr(x=0.55078125):  # float bits 0x3f0d0000: the file holds a 0x0d byte, which readText would drop
    Out.ar(bus=0, source=DC.ar(source=x))


@synthdef("ar")
def follow(x=0.0):  # an audio-rate control, sent out as it is
    Out.ar(bus=0, source=x)


@synthdef("ar")
def hold(x=0.55078125):  # an audio-rate control read only at the start
    Out.ar(bus=0, source=DC.ar(source=x))


@synthdef()
def voice(freq=440.0, velocity=0.0, gate=1.0, level=1.0):
    """A MIDI voice that shows itself: output 1 is velocity x level, output 2 freq / 1000, while
    held. Instant attack, 10 ms release, then it frees itself."""
    env = EnvGen.kr(
        envelope=Envelope.asr(attack_time=0.0, release_time=0.01), gate=gate, done_action=DoneAction.FREE_SYNTH
    )
    Out.ar(bus=0, source=[K2A.ar(source=velocity * level * env), K2A.ar(source=freq * 0.001 * env)])

