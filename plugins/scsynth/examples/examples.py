"""scsy's example SynthDefs. Compile with nanosynth (github.com/shakfu/nanosynth):

    nanosynth compile examples.py -o .

dfm1 uses DFM1 from sc3-plugins, so it needs nanosynth's sc3 wrappers and, on the SSP, the
ugens folder. Each .txt beside a def names and scales its controls for p1..p16.
"""

from nanosynth import DoneAction, synthdef
from nanosynth.envelopes import EnvGen, Envelope
from nanosynth.ugens import In, LFPulse, LFSaw, LFTri, NumOutputBuses, Out, RLPF, Saw, SinOsc
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


@synthdef()
def lfo(rate=1.0, depth=2.5):
    """Four LFOs at one rate, as CV on outputs 1-4: sine, triangle, saw, square, +-depth volts.
    lfo.txt puts rate CV on input 1 and depth CV on input 2."""
    amp = depth * 0.2  # volts to the SSP's 0.2 per volt
    sqr = LFPulse.ar(frequency=rate) * 2 - 1
    waves = [SinOsc.ar(frequency=rate), LFTri.ar(frequency=rate), LFSaw.ar(frequency=rate), sqr]
    Out.ar(bus=0, source=[w * amp for w in waves])


@synthdef()
def midi_saw(freq=440.0, velocity=0.5, gate=1.0, cutoff=2000.0, res=0.3, attack=0.005, release=0.3, level=0.5):
    """A MIDI voice, to outputs 1 and 2: a saw through a resonant lowpass. gate makes it a voice:
    each note starts one, with freq and velocity, and it frees itself after its release."""
    env = EnvGen.kr(
        envelope=Envelope.adsr(attack_time=attack, decay_time=0.2, sustain=0.7, release_time=release),
        gate=gate,
        done_action=DoneAction.FREE_SYNTH,
    )
    sig = RLPF.ar(source=Saw.ar(frequency=freq), frequency=cutoff, reciprocal_of_q=1.0 - res * 0.95)
    sig = sig * env * velocity * level
    Out.ar(bus=0, source=[sig, sig])

