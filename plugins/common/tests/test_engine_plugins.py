"""Build the engine plugins for the host and drive each through the SSP API with plugin_host.cpp.

Covers what the native engine tests cannot: EngineProcessor's block splitting and worker thread,
parameters and custom state through save and restore, and the SSP entry points. The first run
builds the plugins and their JUCE copies into build/plugins-host (a few minutes; ccache helps).
"""

import importlib.util
import json
import os
import re
import shutil
import socket
import struct
import subprocess
import sys
import time
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[3] / "tools"))
import ttyp_type  # noqa: E402

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
BUILD = ROOT / "build" / "plugins-host"
LOCAL_MANIFEST = ROOT / "tools" / "py2rack" / "modules-local.json"
DEPS = ROOT / "build" / "deps" / "host"


@pytest.fixture(scope="module")
def plugins():
    cxx = shutil.which("clang++") or shutil.which("c++")
    if cxx is None or shutil.which("cmake") is None:
        pytest.skip("needs cmake and a C++ compiler")
    if not all((DEPS / "lib" / lib).exists() for lib in ("libchuck.a", "libfaust.a", "libscsynth.a")):
        subprocess.run([ROOT / "scripts" / "build_deps.sh", "host"], check=True, capture_output=True)
    subprocess.run([ROOT / "scripts" / "fetch_teletype.sh"], check=True, capture_output=True)
    subprocess.run([ROOT / "scripts" / "fetch_alda.sh"], check=True, capture_output=True)
    subprocess.run([ROOT / "scripts" / "fetch_sapf.sh"], check=True, capture_output=True)
    subprocess.run([ROOT / "scripts" / "fetch_core.sh", "host"], check=True, capture_output=True)
    if not (DEPS / "lib" / "libfluidsynth.a").exists():
        subprocess.run([ROOT / "scripts" / "build_fluidsynth.sh", "host"], check=True, capture_output=True)
    subprocess.run(["cmake", "-S", ROOT, "-B", BUILD, "-DCMAKE_BUILD_TYPE=Release"], check=True, capture_output=True)
    targets = [f"{name.upper()}_VST3" for name in [*PRODUCTS, *EXAMPLES]]
    subprocess.run(["cmake", "--build", BUILD, "-j8", "--target", *targets], check=True, capture_output=True)
    host = BUILD / "plugin_host"
    subprocess.run([cxx, "-std=c++17", "-O1", "-pthread", f"-I{ROOT / 'ssp-sdk'}", HERE / "plugin_host.cpp",
                    "-ldl", "-o", host], check=True)
    return host


# source folder -> product name; Synthor lists only names of up to four characters
PRODUCTS = {"radio": "rdio", "csound": "csnd", "chuck": "chuk", "edrums": "edrm", "pstretch": "strc", "bard": "bard",
            "glitch": "gltc", "chorus": "chrs", "faust": "fstr", "scsynth": "scsy", "teletype": "ttyp",
            "dexed": "dexd", "alda": "alda", "fsyn": "fsyn", "breaks": "brks", "sfct": "sfct", "sapf": "sapf",
            "gseq": "gseq"}
# examples/: built and tested on the host, never shipped, so not in the manifest
EXAMPLES = {"svca": "svca", "tremolo": "trem"}


def so(name):
    product = PRODUCTS.get(name) or EXAMPLES[name]
    return BUILD / ("examples" if name in EXAMPLES else "plugins") / name / f"{name.upper()}_artefacts" / "Release" \
        / "VST3" / f"{product}.vst3" \
        / "Contents" / "x86_64-linux" / f"{product}.so"


def run(host, plugin, *cmds):
    r = subprocess.run([host, so(plugin), *map(str, cmds)], capture_output=True, text=True, timeout=120)
    assert r.returncode == 0, r.stderr[-4000:]
    levels = {}
    state = ""
    for line in r.stdout.splitlines():
        f = line.split(" ", 1)
        if f[0] == "level":
            ch, mean, last = line.split()[1:]
            levels[int(ch)] = (float(mean), float(last))
        elif f[0] == "state":
            state = f[1]
    return levels, state


def test_radio(plugins, tmp_path):
    bank = tmp_path / "radio" / "0"
    bank.mkdir(parents=True)
    (bank / "a.raw").write_bytes(struct.pack("<h", 8192) * 48000)  # 0.25
    (tmp_path / "radio" / "SETTINGS.TXT").write_text("crossfadeTime=25\nstartPotImmediate=1\n")
    # blocks of 300 against a prepared 128: EngineProcessor splits them
    levels, state = run(plugins, "radio", "prepare", 48000, 128, "set", "root", tmp_path / "radio",
                        "set", "a:level", 1, "run", 0.5, 300, "level", 0, "level", 2, "state")
    assert levels[0][0] > 0.1  # Out L
    assert levels[2][1] == pytest.approx(0.25, abs=1e-3)  # A Out: deck A's station at level 1
    assert f'root="{tmp_path / "radio"}"' in state
    # a preset's root keeps the preset's values; only a root chosen with Load applies SETTINGS.TXT
    assert 'id="fade" value="15.0"' in state and 'id="start_pot_imm" value="0.0"' in state


def test_csound(plugins, tmp_path):
    csd = tmp_path / "prog.csd"
    csd.write_text("<CsoundSynthesizer>\n<CsInstruments>\nksmps = 16\nnchnls = 8\nnchnls_i = 8\n0dbfs = 1\n"
                   'instr 1\n  outch 3, a(chnget:k("p1")), 4, inch(2)\nendin\nschedule 1, 0, -1\n'
                   "</CsInstruments>\n</CsoundSynthesizer>\n")
    levels, _ = run(plugins, "csound", "prepare", 48000, 128, "run", 0.2, 128, "level", 0)
    assert levels[0][0] > 0.01  # the built-in, unpatched
    # program set before prepare, as a preset restores it
    levels, state = run(plugins, "csound", "set", "program", csd, "set", "p1", 0.75, "in", 1, 0.3,
                        "prepare", 48000, 128, "run", 0.3, 300, "level", 2, "level", 3, "state")
    assert levels[2][1] == pytest.approx(0.75, abs=1e-6)
    assert levels[3][1] == pytest.approx(0.3, abs=1e-6)
    assert f'program="{csd}"' in state


def test_chuck(plugins, tmp_path):
    ck = tmp_path / "prog.ck"
    ck.write_text("global float p1;\nStep s => dac.chan(2);\nadc.chan(1) => dac.chan(3);\n"
                  "while (true) { p1 => s.next; 1::ms => now; }\n")
    levels, _ = run(plugins, "chuck", "prepare", 48000, 128, "run", 0.2, 128, "level", 0)
    assert levels[0][0] > 0.01  # the built-in, unpatched
    # program set after prepare, as Load does. Each set restores the state, so each queues a compile
    # on the worker; wait for the second, which restarts the program.
    levels, state = run(plugins, "chuck", "prepare", 48000, 128, "set", "program", ck, "set", "p1", 0.75,
                        "in", 1, 0.3, "wait", 1.0, "run", 0.3, 300, "level", 2, "level", 3, "state")
    assert levels[2][1] == pytest.approx(0.75, abs=1e-6)
    assert levels[3][1] == pytest.approx(0.3, abs=1e-6)
    assert f'program="{ck}"' in state


def test_alda(plugins, tmp_path):
    levels, _ = run(plugins, "alda", "prepare", 48000, 128, "run", 0.4, 128, "level", 0)
    assert levels[0][0] > 0.01  # the built-in's first part gates output 1
    score = tmp_path / "prog.alda"
    score.write_text("piano: o5 c1\nbass: o3 g1\n")
    levels, state = run(plugins, "alda", "prepare", 48000, 128, "set", "program", score, "wait", 1.0,
                        "run", 0.3, 300, "level", 0, "level", 1, "level", 4, "level", 5, "state")
    assert levels[0][1] == 1.0 and levels[1][1] == 1.0  # both gates open
    assert levels[4][1] == pytest.approx(0.2, abs=1e-6)  # C5: 1 V, 0.2 at 5 V per 1.0
    assert levels[5][1] == pytest.approx(-5 / 12 * 0.2, abs=1e-6)  # G3
    assert f'program="{score}"' in state


def test_alda_midi_port(plugins):
    """alda's own ALSA port, the MIDI in that other modules choose: it carries the score's notes."""
    if shutil.which("aseqdump") is None or not os.access("/dev/snd/seq", os.R_OK | os.W_OK):
        pytest.skip("needs aseqdump and the ALSA sequencer")
    cmds = ["prepare", 48000, 128, "wait", 2.0, "run", 1.0, 128, "wait", 2.0]
    host = subprocess.Popen([plugins, so("alda"), *map(str, cmds)],
                            stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True)
    try:
        port = None
        for _ in range(50):
            listing = subprocess.run(["aseqdump", "-l"], capture_output=True, text=True).stdout
            port = next((line.split()[0] for line in listing.splitlines() if line.split()[1:2] == ["alda"]), None)
            if port:
                break
            time.sleep(0.05)
        assert port, "no alda port in aseqdump -l"
        dump = subprocess.run(["timeout", "3", "aseqdump", "-p", port], capture_output=True, text=True).stdout
    finally:
        host.wait(timeout=30)
    assert "Program change" in dump
    assert re.search(r"Note on\s+0, note 60, velocity 69", dump), dump  # the built-in's first note, c on channel 1
    assert host.returncode == 0, host.stderr[-4000:]


def wav(path, frames, sample, channels=1):
    """16-bit 48 kHz, every sample `sample` (-1..1)"""
    data = struct.pack("<h", int(sample * 32767)) * frames * channels
    fmt = struct.pack("<IHHIIHH", 16, 1, channels, 48000, 96000 * channels, 2 * channels, 16)
    path.write_bytes(b"RIFF" + struct.pack("<I", 36 + len(data)) + b"WAVEfmt " + fmt + b"data"
                     + struct.pack("<I", len(data)) + data)


def test_sfct_buffer_channel(plugins, tmp_path):
    # sfct reads the first two channels of a file; a preset naming channel 2 of a quad file crashed
    quad = tmp_path / "quad.wav"
    wav(quad, 4800, 0.25, channels=4)
    for channel in (1, 2):
        _, state = run(plugins, "sfct", "prepare", 48000, 128, "set", "BUFFER_CHANNEL_0", channel,
                       "set", "BUFFER_FILE_0", quad, "run", 0.05, 128, "state")
        assert f'BUFFER_FILE_0="{quad}"' in state  # a failed load keeps the preset's reference


def test_edrums(plugins):
    # one rising edge on Clock plays step 0 of drum 1's default 4 of 16; blocks of 300 are split
    levels, state = run(plugins, "edrums", "prepare", 48000, 128, "in", 0, 1, "run", 0.2, 300, "level", 0, "level", 2,
                        "level", 3, "state")
    assert levels[0][0] > 0.01  # Out L
    assert levels[2][0] > 0.01  # 1 Out: the kick
    assert levels[3][0] == 0.0  # 2 Out: the tom has no hits
    assert 'id="1:hits" value="4.0"' in state and 'id="1:rate" value="7.0"' in state  # x1
    # voice off: the track only triggers
    levels, _ = run(plugins, "edrums", "set", "1:voice", 0, "prepare", 48000, 128, "in", 0, 1, "run", 0.2, 300,
                    "level", 2)
    assert levels[2][0] == 0.0
    levels, _ = run(plugins, "edrums", "set", "1:hits", 0, "prepare", 48000, 128, "in", 0, 1, "run", 0.2, 300,
                    "level", 0)
    assert levels[0][0] == 0.0


def test_pstretch(plugins, tmp_path):
    # mix 0 is the dry input
    levels, _ = run(plugins, "pstretch", "prepare", 48000, 128, "set", "a:mix", 0, "in", 0, 0.3, "run", 0.2, 300,
                    "level", 2)
    assert levels[2][1] == pytest.approx(0.3, abs=1e-6)
    # a clip from the folder set in the state, stretched at 1x
    wav(tmp_path / "1.wav", 48000, 0.25)
    levels, state = run(plugins, "pstretch", "prepare", 48000, 128, "set", "root", tmp_path, "set", "a:source", 2,
                        "set", "a:stretch", 0, "run", 1.5, 300, "level", 2, "state")
    assert levels[2][0] > 0.05  # A Out
    assert f'root="{tmp_path}"' in state


def test_bard(plugins, tmp_path):
    (tmp_path / "0").mkdir()
    wav(tmp_path / "0" / "a.wav", 48000 * 5, 0.25)
    (tmp_path / "bard.cfg").write_text("resume=off\n")
    # the worker opens the first book once it has scanned the root
    levels, state = run(plugins, "bard", "prepare", 48000, 128, "set", "root", tmp_path, "set", "b:volume", 0,
                        "wait", 0.3, "run", 0.5, 300, "level", 0, "level", 2, "level", 3, "state")
    assert levels[2][1] == pytest.approx(0.25, abs=1e-3)  # A Out: the book at volume 1
    assert levels[3][1] == 0.0  # B Out at volume 0
    assert levels[0][0] > 0.1
    assert f'root="{tmp_path}"' in state


def test_glitch(plugins):
    levels, _ = run(plugins, "glitch", "prepare", 48000, 128, "set", "a:algo", 3, "run", 0.2, 300, "level", 0, "level", 2)
    assert levels[0][0] > 1e-3 and levels[2][0] > 1e-3
    levels, _ = run(plugins, "glitch", "set", "a:level", 0, "set", "b:level", 0, "prepare", 48000, 128, "run", 0.2, 300,
                    "level", 0)
    assert levels[0][0] == 0.0


def test_chorus(plugins):
    # one block: mix 0 is dry; a mix CV of 1.0 opens the wet path, whose 5 ms delay is still silent
    levels, _ = run(plugins, "chorus", "set", "mix", 0, "prepare", 48000, 128, "in", 0, 0.3, "run", 0.003, 128,
                    "level", 0)
    assert levels[0][1] == pytest.approx(0.3, abs=1e-6)
    levels, state = run(plugins, "chorus", "set", "mix", 0, "prepare", 48000, 128, "in", 0, 0.3, "in", 4, 1.0,
                        "run", 0.003, 128, "level", 0, "state")
    assert levels[0][1] < 0.29
    assert 'id="mix" value="0"' in state and 'id="rate" value="0.3' in state


def test_faust(plugins, tmp_path):
    levels, _ = run(plugins, "faust", "prepare", 48000, 128, "run", 0.2, 128, "level", 0)
    assert levels[0][0] > 0.01  # the built-in, unpatched
    dsp = tmp_path / "prog.dsp"
    dsp.write_text('process = _ * hslider("gain", 0.5, 0, 2, 0.01), _;\n')
    # program set before prepare, as a preset restores it; p1 is gain, 0.75 of 0..2
    levels, state = run(plugins, "faust", "set", "program", dsp, "set", "p1", 0.75, "in", 0, 0.2, "in", 1, 0.3,
                        "prepare", 48000, 128, "run", 0.3, 300, "level", 0, "level", 1, "state")
    assert levels[0][1] == pytest.approx(0.3, abs=1e-6)
    assert levels[1][1] == pytest.approx(0.3, abs=1e-6)
    assert f'program="{dsp}"' in state


def test_svca(plugins):
    # the plugin guide's example: Level 0.5 is a gain of 0.5; a CV of 0.25 adds 0.25
    levels, _ = run(plugins, "svca", "set", "level", 0.5, "prepare", 48000, 128, "in", 0, 0.4, "in", 1, -0.2,
                        "run", 0.1, 128, "level", 0, "level", 1)
    assert levels[0][1] == pytest.approx(0.2, abs=1e-4)
    assert levels[1][1] == pytest.approx(-0.1, abs=1e-4)
    levels, _ = run(plugins, "svca", "set", "level", 0.5, "prepare", 48000, 128, "in", 0, 0.4, "in", 2, 0.25,
                    "run", 0.1, 128, "level", 0)
    assert levels[0][1] == pytest.approx(0.3, abs=1e-4)



def test_tremolo(plugins):
    # Faust's compiled example: depth 0 passes the input; depth 1 starts the LFO near half level
    levels, _ = run(plugins, "tremolo", "set", "depth", 0, "prepare", 48000, 128, "in", 0, 0.3, "run", 0.2, 128,
                    "level", 0)
    assert levels[0][1] == pytest.approx(0.3, abs=1e-4)
    # inputs: In L, In R, then a CV per control (rate, depth); a depth CV of 1.0 spans the range
    levels, _ = run(plugins, "tremolo", "set", "depth", 0, "set", "rate", 0.1, "prepare", 48000, 128, "in", 0, 0.3,
                    "in", 3, 1.0, "run", 0.2, 128, "level", 0)
    assert 0.1 < levels[0][1] < 0.16


def type_ttyp(text, after):
    """Type text with tools/ttyp_type.py, `after` seconds from now, once ttyp's key port accepts."""
    start = time.monotonic()
    while True:
        try:
            socket.create_connection(("127.0.0.1", ttyp_type.PORT), timeout=1).close()
            break
        except OSError:
            assert time.monotonic() - start < 10, "ttyp key port never opened"
            time.sleep(0.05)
    time.sleep(max(0.0, after - (time.monotonic() - start)))
    ttyp_type.send(text)
    time.sleep(0.2)


def colour_in(frame, rgb, x0, y0, x1, y1):
    """Pixels of colour rgb in a rectangle of a 1600-wide BGRA frame."""
    data, bgr = frame.read_bytes(), bytes(reversed(rgb))
    return sum(1 for y in range(y0, y1) for x in range(x0, x1) if data[(y * 1600 + x) * 4:(y * 1600 + x) * 4 + 3] == bgr)


def amber_in(frame, x0, y0, x1, y1):
    """Pixels of ttyp's amber, (255, 214, 140)."""
    return colour_in(frame, (255, 214, 140), x0, y0, x1, y1)


def run_ttyp(host, text, *cmds):
    """plugin_host with ttyp while text is typed into it, 2.5 s in (past the splash screen)."""
    proc = subprocess.Popen([host, so("teletype"), *map(str, cmds)], stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                            text=True)
    type_ttyp(text, 2.5)
    out, err = proc.communicate(timeout=120)
    assert proc.returncode == 0, err[-4000:]
    levels = {int(f[1]): float(f[3]) for f in (line.split() for line in out.splitlines()) if f[0] == "level"}
    state = next((line[6:] for line in out.splitlines() if line.startswith("state ")), "")
    return levels, state


CTRL = 0x01


def key_records(text, mod=0):
    """Tap records for text on a US keyboard, upper case shifted; with mod, each key is a chord."""
    recs = []
    for c in text:
        if c.isupper():
            recs.append(bytes([ttyp_type.TAP, ttyp_type.UNSHIFTED[c.lower()], ttyp_type.SHIFT | mod, 0]))
        else:
            recs.extend(r[:2] + bytes([r[2] | mod]) + r[3:] for r in ttyp_type.records(c))
    return recs


def run_typed(host, plugin, port, batches, *cmds):
    """plugin_host while key records reach the key port: batches is [(seconds after start, records)], or
    records sent once the port accepts. Returns each channel's last-sample levels in order, and the state."""
    if batches and isinstance(batches[0], bytes):
        batches = [(0, batches)]
    proc = subprocess.Popen([host, so(plugin), *map(str, cmds)], stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                            text=True)
    start = time.monotonic()
    for at, recs in batches:
        time.sleep(max(0.0, at - (time.monotonic() - start)))
        while True:
            try:
                with socket.create_connection(("127.0.0.1", port), timeout=1) as sock:
                    for r in recs:
                        sock.sendall(r)
                break
            except OSError:
                assert time.monotonic() - start < 10, f"key port {port} never opened"
                time.sleep(0.05)
    out, err = proc.communicate(timeout=120)
    assert proc.returncode == 0, err[-4000:]
    levels = {}
    for f in (line.split() for line in out.splitlines()):
        if f[0] == "level":
            levels.setdefault(int(f[1]), []).append(float(f[3]))
    state = next((line[6:] for line in out.splitlines() if line.startswith("state ")), "")
    return levels, state


def test_script_code_editor(plugins, tmp_path, monkeypatch):
    """chuk's code view: keys typed over TCP, run with Ctrl-Enter in place of the file, kept by the preset."""
    monkeypatch.setenv("CHUK_KEYBOARD", "0")  # never grab this machine's keyboard
    ck = tmp_path / "prog.ck"
    ck.write_text("// x\n")
    code = "Step s => dac.chan(2);\n0.5 => s.next;\nwhile (true) 1::second => now;\n"
    frame = tmp_path / "code.bgra"
    # the first render shows the editor and takes the keyboard; the second takes the keys typed meanwhile
    levels, state = run_typed(plugins, "chuck", 7774, key_records(code) + key_records("\n", CTRL),
                              "prepare", 48000, 128, "set", "program", ck, "wait", 0.5, "render", frame, "wait", 2,
                              "render", frame, "wait", 1, "run", 0.2, 128, "level", 2, "state", "render", frame)
    assert levels[2] == [pytest.approx(0.5, abs=1e-6)]
    assert ck.read_text() == "// x\n"  # Ctrl-Enter runs the code; only Ctrl-S writes the file
    assert f'program="{ck}"' in state and 'code="Step s' in state
    assert 'text=' not in state  # the text is the code run
    assert amber_in(frame, 0, 298, 1600, 324) > 0  # the code view's status line
    # coloured by the ChucK grammar: Step as a type, 0.5 as a number (CodeText.cpp's palette)
    assert colour_in(frame, (229, 192, 123), 0, 0, 1600, 290) > 0
    assert colour_in(frame, (209, 154, 102), 0, 0, 1600, 290) > 0

    # the preset runs the code, not the file; an edit not run is kept as text, and Ctrl-S writes it
    preset = tmp_path / "chuk.state"
    xml = state.encode() + b"\0"
    preset.write_bytes(struct.pack("<II", 0x21324356, len(xml)) + xml)
    levels, state = run_typed(plugins, "chuck", 7774, key_records("// y\n") + key_records("s", CTRL),
                              "prepare", 48000, 128, "load", preset, "wait", 0.5, "render", frame, "wait", 1,
                              "render", frame, "run", 0.2, 128, "level", 2, "state")
    assert levels[2] == [pytest.approx(0.5, abs=1e-6)]
    assert ck.read_text() == "// y\n" + code + "// x\n"
    assert 'code="Step s' in state and 'text="// y' in state


@pytest.mark.parametrize("keys, gate, attrs, absent", [
    ("l", 1.0, ['text=""'], []),           # Ctrl-L empties the text; the score plays on
    ("ll", 1.0, [], ["text="]),            # again on the empty text: it is back
    (".", 0.0, ['program="{score}"'], []),  # Ctrl-.: the score stops; the program stays
    ("r", 0.0, ['program=""'], ["text="]),  # Ctrl-R: stopped, cleared, no program
    ("rl", 0.0, ['program=""', "text="], []),  # Ctrl-L after the reset brings the text back
])
def test_alda_stop_clear_reset(plugins, tmp_path, monkeypatch, keys, gate, attrs, absent):
    monkeypatch.setenv("ALDA_KEYBOARD", "0")  # never grab this machine's keyboard
    score = tmp_path / "prog.alda"
    score.write_text("(quant 100)\npiano: o5 c1\n")
    frame = tmp_path / "code.bgra"
    # the first render shows the editor and takes the keyboard; the second takes the keys typed meanwhile
    levels, state = run_typed(plugins, "alda", 7775, [r for k in keys for r in key_records(k, CTRL)],
                              "prepare", 48000, 128, "set", "program", score, "wait", 0.5, "render", frame,
                              "wait", 1.5, "render", frame, "wait", 0.5, "run", 0.2, 128, "level", 0, "state")
    assert levels[0] == [gate]
    for a in attrs:
        assert a.format(score=score) in state, state
    for a in absent:
        assert a not in state, state


def tap(usage, mods=0):
    """One key record: a HID usage with modifiers (CTRL, ttyp_type.SHIFT)."""
    return bytes([ttyp_type.TAP, usage, mods, 0])


@pytest.mark.parametrize("name, enters, existing, saved", [
    ("copy", 1, None, True),        # a new file: one Enter
    ("other", 1, "x\n", False),     # an existing file: the first Enter only asks
    ("other", 2, "x\n", True),      # the second overwrites
    ("copy", 0, None, False),       # Esc cancels
])
def test_save_as(plugins, tmp_path, monkeypatch, name, enters, existing, saved):
    """Ctrl-Shift-S: the code view's text to a named file beside the program's, which becomes the program."""
    monkeypatch.setenv("ALDA_KEYBOARD", "0")
    score = tmp_path / "prog.alda"
    score.write_text("piano: o5 c1\n")
    target = tmp_path / f"{name}.alda"
    if existing:
        target.write_text(existing)
    keys = [tap(0x16, CTRL | ttyp_type.SHIFT)] + [tap(0x2A)] * len("prog.alda") + key_records(name)
    keys += [tap(0x28)] * enters if enters else [tap(0x29)]
    frame = tmp_path / "code.bgra"
    _, state = run_typed(plugins, "alda", 7775, keys, "prepare", 48000, 128, "set", "program", score, "wait", 0.5,
                         "render", frame, "wait", 1.5, "render", frame, "state")
    if saved:
        assert target.read_text() == "piano: o5 c1\n"
        assert f'program="{target}"' in state  # Ctrl-S now writes there
    else:
        assert target.read_text() == existing if existing else not target.exists()
        assert f'program="{score}"' in state
    assert score.read_text() == "piano: o5 c1\n"


def alda_preset(path, values, custom):
    params = "".join(f'<PARAM id="p{i}" value="{values.get(i, 0.0)}"/>' for i in range(1, 17))
    xml = (f'<?xml version="1.0" encoding="UTF-8"?> <VST><state>{params}</state>'
           f'<MIDI MIDI_IN="" MIDI_OUT="" MIDI_CH="0" MIDI_NOTE_IN="0"><Automation/></MIDI>'
           f'<CUSTOM program="" {custom}/></VST>').encode() + b"\0"
    path.write_bytes(struct.pack("<II", 0x21324356, len(xml)) + xml)


@pytest.mark.parametrize("custom, values, want", [
    ("", {}, {1: 0.5, 2: 0.5, 3: 0.5, 4: 0.0, 9: 1.0}),        # saved before the controls: neutral instead
    ('controls="1"', {3: 0.25}, {1: 0.0, 2: 0.0, 3: 0.25, 9: 1.0}),  # before Clock: Clock at score
    ('controls="2"', {3: 0.25, 9: 0.0}, {3: 0.25, 9: 0.0}),     # with Clock: kept as saved
])
def test_alda_old_patch_controls(plugins, tmp_path, custom, values, want):
    preset = tmp_path / "alda.state"
    alda_preset(preset, values, custom)
    _, state = run(plugins, "alda", "prepare", 48000, 128, "load", preset, "state")
    for i, v in want.items():
        assert f'<PARAM id="p{i}" value="{v}"/>' in state, state
    assert 'controls="2"' in state


def test_alda_status_line_fits(plugins, tmp_path, monkeypatch):
    """alda's Code view status line, the longest: its run state and key list end inside the screen."""
    monkeypatch.setenv("ALDA_KEYBOARD", "0")
    frame = tmp_path / "code.bgra"
    run_typed(plugins, "alda", 7775, [r for k in "ll" for r in key_records(k, CTRL)],
              "prepare", 48000, 128, "wait", 0.5, "render", frame, "wait", 1.5, "run", 0.5, 128, "render", frame)
    data = frame.read_bytes()
    xs = [(i // 4) % 1600 for i in range(1600 * 298 * 4, 1600 * 324 * 4, 4) if data[i:i + 3] == bytes([140, 214, 255])]
    assert xs, "no status line"
    assert max(xs) < 1515  # the view clips text at about x=1530, without an ellipsis


@pytest.mark.parametrize("plugin, port, env, colours", [
    # the built-in .csd: tags and instr as keywords, ; comments
    ("csound", 7773, "CSND_KEYBOARD", [(198, 120, 221), (130, 130, 130)]),
    # the built-in .dsp: declare as a keyword, "built-in" as a string, numbers
    ("faust", 7772, "FSTR_KEYBOARD", [(198, 120, 221), (152, 195, 121), (209, 154, 102)]),
])
def test_code_view_colours(plugins, tmp_path, monkeypatch, plugin, port, env, colours):
    """csnd's and fstr's code views, coloured by their grammars (CodeText.cpp's palette)."""
    monkeypatch.setenv(env, "0")
    frame = tmp_path / "code.bgra"
    run_typed(plugins, plugin, port, [bytes([2, 0x51, 0, 0])], "prepare", 48000, 128, "wait", 0.5, "render", frame,
              "wait", 1, "render", frame)
    assert amber_in(frame, 0, 298, 1600, 324) > 0  # the code view's status line
    for rgb in colours:
        assert colour_in(frame, rgb, 0, 0, 1600, 290) > 0, rgb


def test_script_code_parts(plugins, tmp_path, monkeypatch):
    """chuk's Ctrl-E blocks as shreds beside the program, Ctrl-<n> removing one, Ctrl-. clearing all."""
    monkeypatch.setenv("CHUK_KEYBOARD", "0")
    ck = tmp_path / "empty.ck"
    ck.write_text("")
    a = "Step a => dac.chan(2);\n0.25 => a.next;\nwhile (true) 1::second => now;\n"
    b = "Step b => dac.chan(3);\n0.5 => b.next;\nwhile (true) 1::second => now;\n"
    # each batch lands between renders, about 3.3 s apart; Ctrl-E on the blank line after a block runs it
    ctrl_e = key_records("e", CTRL)
    batches = [(0, key_records(a) + ctrl_e + key_records("\n" + b) + ctrl_e), (4.5, key_records("1", CTRL)),
               (7.8, key_records(".", CTRL))]
    measure = ["wait", 2, "render", tmp_path / "f.bgra", "wait", 1, "run", 0.2, 128, "level", 2, "level", 3]
    levels, _ = run_typed(plugins, "chuck", 7774, batches, "prepare", 48000, 128, "set", "program", ck,
                          "wait", 0.5, "render", tmp_path / "f.bgra", *measure, *measure, *measure)
    assert levels[2] == [pytest.approx(0.25, abs=1e-6), 0.0, 0.0]
    assert levels[3] == [pytest.approx(0.5, abs=1e-6), pytest.approx(0.5, abs=1e-6), 0.0]


@pytest.fixture
def sapf_env(tmp_path, monkeypatch):
    """sapf's files in tmp_path, the fork's prelude, and this machine's keyboard left alone."""
    monkeypatch.setenv("SAPF_KEYBOARD", "0")
    monkeypatch.setenv("SAPF_FILES", str(tmp_path))
    sapf = subprocess.run([ROOT / "scripts" / "fetch_sapf.sh"], check=True, capture_output=True, text=True)
    monkeypatch.setenv("SAPF_PRELUDE", str(Path(sapf.stdout.strip()) / "sapf-prelude.txt"))


SAPF_INPUTS = ["in", 0, 0.3, "in", 1, -0.2]


def test_sapf_pages(plugins, tmp_path, sapf_env):
    """sapf opens on its pages, running the built-in: inputs 1-2 to outputs 1-2, output log at the left."""
    frame = tmp_path / "pages.bgra"
    levels, state = run(plugins, "sapf", *SAPF_INPUTS, "prepare", 48000, 128, "wait", 2, "run", 0.5, 128,
                        "level", 0, "level", 1, "render", frame, "state")
    assert levels[0][1] == pytest.approx(0.3, abs=1e-6) and levels[1][1] == pytest.approx(-0.2, abs=1e-6)
    assert 'program=""' in state and "code=" not in state
    assert colour_in(frame, (150, 150, 150), 0, 200, 900, 370) > 0  # the output log
    assert amber_in(frame, 0, 298, 900, 324) == 0  # not the code view's status line


def test_sapf_code_view(plugins, tmp_path, sapf_env):
    """Ctrl-Enter stops the built-in and runs the text; Ctrl-E plays a block beside it; Ctrl-. stops all."""
    swap, block = "[1 adc 0 adc] play", "0 adc play"
    frame = tmp_path / "code.bgra"
    # each batch lands between renders, about 3.3 s apart; Ctrl-E on the blank line after a block runs it
    batches = [(0, key_records("l", CTRL) + key_records(swap) + key_records("\n", CTRL)),
               (4.5, key_records("\n\n" + block + "\n") + key_records("e", CTRL)),
               (7.8, key_records(".", CTRL))]
    measure = ["wait", 2, "render", frame, "wait", 1, "run", 0.3, 128, "level", 0, "level", 1]
    levels, state = run_typed(plugins, "sapf", 7771, batches, *SAPF_INPUTS, "prepare", 48000, 128, "wait", 0.5,
                              "render", frame, *measure, *measure, *measure, "state")
    assert levels[0] == [pytest.approx(-0.2, abs=1e-6), pytest.approx(0.1, abs=1e-6), 0.0]
    assert levels[1] == [pytest.approx(0.3, abs=1e-6), pytest.approx(0.3, abs=1e-6), 0.0]
    assert f'code="{swap}' in state and f'program="{tmp_path / "live.sapf"}"' in state
    assert amber_in(frame, 0, 298, 1600, 324) > 0  # the code view's status line
    assert colour_in(frame, (150, 150, 150), 0, 232, 1600, 298) > 0  # the output pane above it
    assert colour_in(frame, (86, 182, 194), 0, 0, 1600, 228) > 0  # built-in words, cyan (CodeText.cpp)

    # a preset runs its code once the prelude loads
    preset = tmp_path / "sapf.state"
    xml = state.encode() + b"\0"
    preset.write_bytes(struct.pack("<II", 0x21324356, len(xml)) + xml)
    levels, _ = run(plugins, "sapf", *SAPF_INPUTS, "prepare", 48000, 128, "load", preset, "wait", 2,
                    "run", 0.3, 128, "level", 0)
    assert levels[0][1] == pytest.approx(-0.2, abs=1e-6)


def test_sapf_mouse(plugins, tmp_path, sapf_env):
    """p1 and p2, Mouse X and Y, are the position sapf's mouse words read."""
    xml = ('<?xml version="1.0" encoding="UTF-8"?> <VST><state><PARAM id="p1" value="0.25"/>'
           '<PARAM id="p2" value="0.75"/></state><MIDI MIDI_IN="" MIDI_OUT="" MIDI_CH="0" MIDI_NOTE_IN="0">'
           '<Automation/></MIDI><CUSTOM program="" code="[0 2 mousex 0 2 mousey] play&#10;"/></VST>').encode() + b"\0"
    preset = tmp_path / "mouse.state"
    preset.write_bytes(struct.pack("<II", 0x21324356, len(xml)) + xml)
    levels, _ = run(plugins, "sapf", "prepare", 48000, 128, "load", preset, "wait", 2, "run", 0.3, 128,
                    "level", 0, "level", 1)
    assert levels[0][1] == pytest.approx(0.5, abs=1e-4) and levels[1][1] == pytest.approx(1.5, abs=1e-4)


def test_sapf_old_patch(plugins, tmp_path, sapf_env):
    """0.1.0's CODE and FILE: the text comes back unrun, saved to the old file."""
    xml = ('<?xml version="1.0" encoding="UTF-8"?> <VST><state/><MIDI MIDI_IN="" MIDI_OUT="" MIDI_CH="0" '
           'MIDI_NOTE_IN="0"><Automation/></MIDI><CUSTOM CODE="0 adc play" FILE="old.sapf"/></VST>').encode() + b"\0"
    preset = tmp_path / "old.state"
    preset.write_bytes(struct.pack("<II", 0x21324356, len(xml)) + xml)
    levels, state = run(plugins, "sapf", *SAPF_INPUTS, "prepare", 48000, 128, "load", preset, "wait", 2,
                        "run", 0.3, 128, "level", 0, "state")
    assert levels[0][1] == 0.0
    assert f'program="{tmp_path / "old.sapf"}"' in state and 'code=""' in state and 'text="0 adc play"' in state


def gseq_row(length=16, division=3, channel=0, voice=0, vel_cv=False, mute=False, steps=()):
    """A `rowN` attribute: the row's word, then 32 steps' (GseqEngine::Row and Step). steps: {index: (note,
    velocity, duration index)}, the rest off."""
    words = [(length - 1) | division << 5 | channel << 8 | voice << 13 | vel_cv << 16 | mute << 17]
    for s in range(32):
        note, vel, dur = dict(steps).get(s, (60, 100, 3))
        words.append((s in dict(steps)) | note << 1 | vel << 8 | dur << 15)
    return " ".join(f"{w:x}" for w in words)


def gseq_preset(path, rows, params=None):
    custom = " ".join(f'row{r}="{w}"' for r, w in rows.items())
    state = "".join(f'<PARAM id="{k}" value="{v}"/>' for k, v in (params or {}).items())
    xml = (f'<?xml version="1.0" encoding="UTF-8"?> <VST><state>{state}</state><MIDI MIDI_IN="" MIDI_OUT="" '
           f'MIDI_CH="0" MIDI_NOTE_IN="0"><Automation/></MIDI><CUSTOM pattern="1" {custom}/></VST>').encode() + b"\0"
    path.write_bytes(struct.pack("<II", 0x21324356, len(xml)) + xml)
    return path


def test_gseq(plugins, tmp_path):
    """A preset's pattern on the CV jacks: gate, pitch and velocity; a row whose voice another row took."""
    row1 = gseq_row(length=4, voice=1, vel_cv=True, steps={0: (72, 127, 7), 2: (48, 64, 7)})
    preset = gseq_preset(tmp_path / "g.state", {1: row1, 2: gseq_row(voice=1, steps={0: (60, 100, 7)})})
    frame = tmp_path / "g.bgra"
    # 120 bpm 16ths: a step is 0.125 s; step 1 sounds at 0.05 s, step 2 (off) at 0.2 s
    levels, state = run(plugins, "gseq", "prepare", 48000, 128, "load", preset, "run", 0.05, 128,
                        "level", 0, "level", 4, "level", 8, "level", 1, "render", frame, "state")
    assert levels[0][1] == 1.0
    assert levels[4][1] == pytest.approx(0.2, abs=1e-6)  # C5: 1 V
    assert levels[8][1] == pytest.approx(1.0, abs=1e-6)  # velocity 127: 5 V
    assert levels[1][1] == 0.0  # row 2 asked for voice 1 too: refused, so silent on CV
    assert f'row1="{row1}"' in state and 'row2="6f ' in state  # row 2 kept, without a voice
    assert amber_in(frame, 150, 36, 1526, 370) > 0  # the grid's lit cells
    levels, _ = run(plugins, "gseq", "prepare", 48000, 128, "load", preset, "run", 0.2, 128, "level", 0)
    assert levels[0][1] == 0.0


def test_gseq_default_rows(plugins, tmp_path):
    """Rows 1-4 play MIDI and CV voices 1-4 with velocity CV, rows 5-8 MIDI only; a preset that names some
    rows leaves the others as they were."""
    preset = gseq_preset(tmp_path / "g.state", {2: gseq_row(channel=2, steps={0: (72, 127, 7)})})
    levels, state = run(plugins, "gseq", "prepare", 48000, 128, "load", preset, "run", 0.05, 128,
                        "level", 1, "state")
    assert levels[1][1] == 0.0  # row 2 dropped its voice: MIDI only
    assert 'row1="1216f ' in state and 'row4="1846f ' in state  # channel k, voice k, velocity CV
    assert 'row3="1636f ' in state  # unnamed rows keep their defaults, voices included
    assert 'row5="56f ' in state and 'row8="86f ' in state  # MIDI only


def test_gseq_roll(plugins, tmp_path):
    """Mode roll (docs/dev/gseq-roll.md): every row takes row 1's channel, length, division and velocity CV,
    rows 1-4 voices 1-4; the state keeps the mode and the roll draws its notes."""
    row1 = gseq_row(length=8, division=2, channel=3, voice=1, vel_cv=True, steps={0: (60, 127, 7)})
    preset = gseq_preset(tmp_path / "g.state", {1: row1, 6: gseq_row(channel=9, voice=0)}, {"mode": 1})
    frame = tmp_path / "g.bgra"
    levels, state = run(plugins, "gseq", "prepare", 48000, 128, "load", preset, "run", 0.05, 128, "level", 0,
                        "render", frame, "state")
    assert 'id="mode" value="1' in state
    assert f'row1="{row1}"' in state
    assert 'row2="14347 ' in state and 'row4="18347 ' in state  # voices 2 and 4
    assert 'row6="10347 ' in state  # channel 3, length 8, division 2, no voice
    assert levels[0][1] == 1.0  # the note plays on voice 1
    assert amber_in(frame, 150, 36, 1526, 370) > 0  # the note's cell in the roll


def test_teletype(plugins, tmp_path, monkeypatch):
    monkeypatch.setenv("TTYP_KEYBOARD", "0")  # never grab this machine's keyboard
    r = subprocess.run([plugins, so("teletype"), "channels"], capture_output=True, text=True, timeout=60)
    assert r.returncode == 0, r.stderr[-2000:]
    inputs = [line[6:] for line in r.stdout.splitlines() if line.startswith("input ")]
    outputs = [line[7:] for line in r.stdout.splitlines() if line.startswith("output ")]
    assert inputs[:9] == [f"Trig {i}" for i in range(1, 9)] + ["In"]
    assert outputs[:8] == [f"TR {i}" for i in range(1, 5)] + [f"CV {i}" for i in range(1, 5)]

    # CV 1 is output 4 and TR 1 output 0; an SSP signal of 1.0 is 5 V. CV 2's range shifts it by -5 V.
    frame = tmp_path / "frame.bgra"
    levels, state = run_ttyp(plugins, "cv 1 v 3\ncv 2 v 3\ntr 1 1\nx 3\n",
                             "prepare", 48000, 128, "set", "cv2_range", 1, "run", 8, 128,
                             "level", 4, "level", 5, "level", 0, "state", "render", frame)
    assert levels[4] == pytest.approx(0.6, abs=0.005)
    assert levels[5] == pytest.approx(-0.4, abs=0.005)
    assert levels[0] == 1.0
    assert 'TELETYPE="' in state
    data = frame.read_bytes()
    amber = sum(1 for i in range(0, len(data), 4) if data[i:i + 3] == bytes([140, 214, 255]))
    assert amber > 100  # the screen's lit pixels
    assert amber_in(frame, 900, 164, 1580, 196) == 0  # no input high: no number lit

    # the preset restores the live scene: X is still 3 in a fresh instance
    preset = tmp_path / "ttyp.state"
    xml = state.encode() + b"\0"  # JUCE's copyXmlToBinary format, as rack stores a module's state
    preset.write_bytes(struct.pack("<II", 0x21324356, len(xml)) + xml)
    levels, _ = run_ttyp(plugins, "cv 1 v x\n", "prepare", 48000, 128, "load", preset, "run", 8, 128, "level", 4)
    assert levels[4] == pytest.approx(0.6, abs=0.005)

    # In and the trigger inputs, patched: In at 3 V reaches IN, and the status panel's inputs line lights Trig 1
    frame = tmp_path / "inputs.bgra"
    levels, _ = run_ttyp(plugins, "cv 1 in\n", "prepare", 48000, 128, "enable", 0, "enable", 8, "in", 0, 1.0,
                         "in", 8, 0.6, "run", 8, 128, "level", 4, "render", frame)
    assert levels[4] == pytest.approx(0.6, abs=0.005)
    assert amber_in(frame, 900, 164, 1580, 196) > 0  # a high trigger input lights its number

    # Disk (soft key 3) as teletype's USB stick: every scene out to ttNNs.txt, tt07.txt into scene 7
    scenes = tmp_path / "scenes"
    scenes.mkdir()
    (scenes / "tt07.txt").write_text("#1\nCV 4 V 2\n")
    monkeypatch.setenv("TTYP_SCENES", str(scenes))
    levels, _ = run_ttyp(plugins, "scene 7\n$ 1\n", "prepare", 48000, 128, "render", tmp_path / "f.bgra",
                         "button", 2, 1, "button", 2, 0, "run", 8, 128, "level", 7)
    assert sorted(p.name for p in scenes.glob("tt*s.txt")) == [f"tt{i:02d}s.txt" for i in range(32)]
    assert levels[7] == pytest.approx(0.4, abs=0.005)  # CV 4 = output 7


def cartridge(path, names):
    """a DX7 32-voice bulk dump: zero voices with these names"""
    data = b"".join(bytes(118) + n.ljust(10).encode() for n in names)
    path.write_bytes(bytes([0xF0, 0x43, 0, 9, 0x20, 0]) + data + bytes([(-sum(data)) & 0x7F, 0xF7]))


def test_dexed(plugins, tmp_path):
    bank = tmp_path / "bank.syx"
    cartridge(bank, [f"V{i}" for i in range(32)])
    r = subprocess.run([plugins, so("dexed"), "channels"], capture_output=True, text=True, timeout=60)
    assert r.stdout.split("\n")[:5] == ["io 2 2", "input Cutoff CV", "input Mod CV", "output Out L", "output Out R"]
    # no notes: silent; the preset keeps the cartridge and the chosen voice's 155 bytes
    levels, state = run(plugins, "dexed", "prepare", 48000, 128, "set", "voice", 3, "set", "cartridge", bank,
                        "run", 0.1, 300, "level", 0, "render", tmp_path / "f.bgra", "state")
    assert levels[0] == (0.0, 0.0)
    assert f'cartridge="{bank}"' in state
    assert re.search(r'voice_data="[0-9a-f]{310}"', state)
    # PG- (Left) from voice 1 wraps to 32; PG+ (Right) twice reaches 2
    press = lambda n: ("button", n, 1, "button", n, 0)
    _, pg = run(plugins, "dexed", "prepare", 48000, 128, "render", tmp_path / "f.bgra", *press(8), "state")
    assert 'id="voice" value="32.0"' in pg
    _, pg = run(plugins, "dexed", "prepare", 48000, 128, "render", tmp_path / "f.bgra", *press(8), *press(9),
                *press(9), "state")
    assert 'id="voice" value="2.0"' in pg
    # the file gone: the preset's voice, from the state, in every slot
    voice_data = re.search(r'voice_data="([0-9a-f]+)"', state).group(1)
    bank.rename(tmp_path / "gone.syx")
    preset = tmp_path / "preset.bin"
    xml = state.encode()
    preset.write_bytes(struct.pack("<II", 0x21324356, len(xml) + 1) + xml + b"\0")  # JUCE's copyXmlToBinary
    _, state = run(plugins, "dexed", "load", preset, "prepare", 48000, 128, "set", "voice", 7, "run", 0.1, 128, "state")
    assert f'cartridge="{bank}"' in state
    assert f'voice_data="{voice_data}"' in state


def test_fsyn(plugins, tmp_path):
    font = ROOT / "build" / "deps" / "src" / "TimGM6mb.sf2"
    r = subprocess.run([plugins, so("fsyn"), "channels"], capture_output=True, text=True, timeout=60)
    assert r.stdout.split("\n")[:4] == ["io 1 2", "input Gain CV", "output Out L", "output Out R"]
    # the font loads on the worker, and the preset keeps it; no notes: silent
    levels, state = run(plugins, "fsyn", "prepare", 48000, 128, "set", "soundfont", font, "wait", 1.0,
                        "run", 0.1, 300, "level", 0, "render", tmp_path / "f.bgra", "state")
    assert levels[0] == (0.0, 0.0)
    assert f'soundfont="{font}"' in state
    # a font that fails to load leaves the one playing, which the preset keeps
    _, state = run(plugins, "fsyn", "prepare", 48000, 128, "set", "soundfont", font, "wait", 1.0,
                   "set", "soundfont", tmp_path / "missing.sf2", "wait", 1.0, "run", 0.1, 128,
                   "render", tmp_path / "f.bgra", "state")
    assert f'soundfont="{font}"' in state


def test_breaks(plugins, tmp_path):
    r = subprocess.run([plugins, so("breaks"), "channels"], capture_output=True, text=True, timeout=60)
    assert r.stdout.split("\n")[:16] == ["io 11 4", "input Clock", "input Tap", "input Mode", "input Mult",
                                          "input Bank", "input Play", "input Unused 7", "input Unused 8",
                                          "input Amen CV", "input Break CV", "input Sample CV", "output Out L",
                                          "output Out R", "output Trig", "output Clock Out"]
    core = next((ROOT / "build" / "deps" / "src").glob("core-*"))
    spec = importlib.util.spec_from_file_location("core_fixtures", core / "test" / "rack" / "fixtures.py")
    fixtures = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(fixtures)
    card = tmp_path / "card"
    for bank in (1, 2):
        fixtures.sample(card, bank, 0, seconds=2)
    # Load picks a file in a bank: the card plays, and the preset keeps the card and bank 2
    # (the playing state is in the preset once a card has loaded)
    levels, state = run(plugins, "breaks", "prepare", 48000, 128, "set", "card", card / "bank2" / "0.0.wav",
                        "wait", 1.0, "run", 0.1, 128, "set", "playing_bank", 1, "wait", 1.0, "run", 1.0, 128,
                        "level", 0, "level", 1, "level", 3,
                        "render", tmp_path / "e.bgra", "state")
    assert levels[0][0] > 0.01 and levels[1][0] > 0.01
    assert f'card="{card}"' in state and 'playing_bank="1"' in state
    # a trigger at Play stops it, once the start is done (the firmware ignores it while a restart is pending)
    levels, _ = run(plugins, "breaks", "prepare", 48000, 128, "set", "card", card, "wait", 1.0, "run", 2.0, 128,
                    "in", 5, 1, "run", 0.01, 128, "in", 5, 0, "run", 1.0, 128, "level", 0)
    assert levels[0][0] < 1e-4
    # Tempo follows the card's tempo on the idle worker, with no editor; once turned, it sets the tempo
    for info in card.glob("bank*/*.info"):
        b = bytearray(info.read_bytes())
        b[4:8] = (int.from_bytes(b[4:8], "little") & ~0x1FF | 170).to_bytes(4, "little")
        info.write_bytes(bytes(b))
    tempo = lambda state: float(re.search(r'<PARAM id="tempo" value="([^"]+)"', state).group(1))
    _, state = run(plugins, "breaks", "prepare", 48000, 128, "set", "card", card, "wait", 1.0, "run", 0.5, 128,
                   "wait", 1.5, "state")
    assert tempo(state) == 170
    # encoder 1 on the clock page (Down twice): 16 detents of 5 BPM down; the firmware follows, and the
    # sync leaves it after its 1 s hold
    press = lambda n: ("button", n, 1, "button", n, 0)
    _, state = run(plugins, "breaks", "prepare", 48000, 128, "set", "card", card, "wait", 1.0, "run", 0.5, 128,
                   "render", tmp_path / "e.bgra", *press(11), *press(11), *("encoder", 0, -1) * 16, "run", 0.5, 128,
                   "wait", 1.5, "run", 0.1, 128, "wait", 0.2, "state")
    assert tempo(state) == 90 and 'tempo="90"' in state
    # Right (PG+) steps to bank 2 at the loop's end
    _, state = run(plugins, "breaks", "prepare", 48000, 128, "set", "card", card, "wait", 1.0, "run", 0.5, 128,
                   "render", tmp_path / "e.bgra", *press(9), "run", 2.5, 128, "wait", 1.0, "run", 2.5, 128, "state")
    assert 'playing_bank="1"' in state


def manifest_entry(host, plugin):
    """py2rack's manifest entry for a built plugin: channel names, and parameters by id"""
    r = subprocess.run([host, so(plugin), "channels", "state"], capture_output=True, text=True, timeout=60)
    assert r.returncode == 0, r.stderr[-2000:]
    entry = {"inputs": [], "outputs": [], "params": {}, "skipped": 0}
    for line in r.stdout.splitlines():
        kind, _, rest = line.partition(" ")
        if kind in ("input", "output"):
            entry[kind + "s"].append(rest)
        elif kind == "state":
            entry["params"] = {pid: pid for pid in re.findall(r'<PARAM id="([^"]+)"', rest)}
    return entry


def test_local_manifest_matches_the_plugins(plugins):
    # py2rack checks presets that use this repo's modules against this file; UPDATE_MANIFEST=1 rewrites it
    manifest = {PRODUCTS[name]: manifest_entry(plugins, name) for name in PRODUCTS}
    text = json.dumps(manifest, indent=2, sort_keys=True) + "\n"
    if os.environ.get("UPDATE_MANIFEST"):
        LOCAL_MANIFEST.write_text(text)
    assert LOCAL_MANIFEST.exists() and LOCAL_MANIFEST.read_text() == text, "run UPDATE_MANIFEST=1 make test"


def orange_pixels(frame):
    """The x of each pixel of the CV marker, juce::Colours::orange, in a 1600-wide BGRA frame."""
    data = frame.read_bytes()
    return [(i // 4) % 1600 for i in range(0, len(data), 4) if data[i:i + 3] == b"\x00\xa5\xff"]


def test_scsynth(plugins, tmp_path, monkeypatch):
    monkeypatch.setenv("SCSY_UGENS", str(DEPS / "scsynth" / "plugins"))
    levels, _ = run(plugins, "scsynth", "prepare", 48000, 128, "run", 0.2, 128, "level", 0, "level", 1,
                    "render", tmp_path / "builtin.bgra")
    assert levels[0][0] > 1e-3 and levels[1][0] > 1e-3  # the built-in drone, at level 0: quiet
    assert not orange_pixels(tmp_path / "builtin.bgra")  # no control has CV
    # a def and its sidecar, set before prepare, as a preset restores them; pitch and index have CV
    fm = ROOT / "plugins" / "scsynth" / "examples" / "fm.scsyndef"
    levels, state = run(plugins, "scsynth", "set", "program", fm, "set", "p4", 0.5, "prepare", 48000, 128, "in", 1, 0.2,
                        "run", 0.3, 128, "level", 0, "render", tmp_path / "fm.bgra", "state")
    assert levels[0][0] > 1e-3
    assert f'program="{fm}"' in state
    at_1v = orange_pixels(tmp_path / "fm.bgra")  # the marker on index
    assert at_1v
    run(plugins, "scsynth", "set", "program", fm, "set", "p4", 0.5, "prepare", 48000, 128, "run", 0.3, 128,
        "render", tmp_path / "fm0.bgra")
    # index's mark, right of pitch's: 1 V on input 2 moved it right
    assert max(at_1v) > max(orange_pixels(tmp_path / "fm0.bgra"))

