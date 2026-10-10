"""scsy's example SynthDefs. Compile with nanosynth (github.com/shakfu/nanosynth):

    nanosynth compile examples.py -o .

dfm1 uses DFM1 from sc3-plugins, so it needs nanosynth's sc3 wrappers and, on the SSP, the
ugens folder. Each .txt beside a def names and scales its controls for p1..p16.
"""

from nanosynth import synthdef
from nanosynth.ugens import In, NumOutputBuses, Out, RLPF, SinOsc
from nanosynth.ugens.sc3 import DFM1


@synthdef()
def filter(cutoff=1000.0, res=0.3, mix=1.0):
    """A resonant lowpass on inputs 1 and 2, to outputs 1 and 2."""
    dry = In.ar(bus=NumOutputBuses.ir(), channel_count=2)
    wet = RLPF.ar(source=dry, frequency=cutoff, reciprocal_of_q=1.0 - res * 0.95)
    Out.ar(bus=0, source=dry * (1.0 - mix) + wet * mix)


@synthdef()
def dfm1(cutoff=1000.0, res=0.5, gain=1.0, type=0.0):
    """DFM1, a digitally modelled analogue filter, on inputs 1 and 2. type below 0.5 is low-pass,
    else high-pass."""
    dry = In.ar(bus=NumOutputBuses.ir(), channel_count=2)
    Out.ar(bus=0, source=DFM1.ar(source=dry, freq=cutoff, res=res, inputgain=gain, type=type, noiselevel=0.0))


@synthdef("ar")
def fm(pitch=110.0, ratio=2.0, index=2.0, level=0.5):
    """Two-operator FM, to outputs 1 and 2. pitch is audio-rate, so fm.txt's `cv 1` moves it every
    sample: 1 V/oct, as its range is log."""
    modulator = SinOsc.ar(frequency=pitch * ratio) * (pitch * ratio * index)
    sig = SinOsc.ar(frequency=pitch + modulator) * level
    Out.ar(bus=0, source=[sig, sig])
