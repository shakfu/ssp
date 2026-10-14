"""Build the engine plugins for the host and drive each through the SSP API with plugin_host.cpp.

Covers what the native engine tests cannot: EngineProcessor's block splitting and worker thread,
parameters and custom state through save and restore, and the SSP entry points. The first run
builds the plugins and their JUCE copies into build/plugins-host (a few minutes; ccache helps).
"""

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
            "dexed": "dexd"}
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


def wav(path, frames, sample):
    """16-bit mono 48 kHz, every sample `sample` (-1..1)"""
    data = struct.pack("<h", int(sample * 32767)) * frames
    path.write_bytes(b"RIFF" + struct.pack("<I", 36 + len(data)) + b"WAVEfmt "
                     + struct.pack("<IHHIIHH", 16, 1, 1, 48000, 96000, 2, 16) + b"data" + struct.pack("<I", len(data)) + data)


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


def amber_in(frame, x0, y0, x1, y1):
    """Pixels of ttyp's amber, (255, 214, 140), in a rectangle of a 1600-wide BGRA frame."""
    data = frame.read_bytes()
    return sum(1 for y in range(y0, y1) for x in range(x0, x1)
               if data[(y * 1600 + x) * 4:(y * 1600 + x) * 4 + 3] == bytes([140, 214, 255]))


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

