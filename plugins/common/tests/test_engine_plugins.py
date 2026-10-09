"""Build the engine plugins for the host and drive each through the SSP API with plugin_host.cpp.

Covers what the native engine tests cannot: EngineProcessor's block splitting and worker thread,
parameters and custom state through save and restore, and the SSP entry points. The first run
builds the plugins and their JUCE copies into build/plugins-host (a few minutes; ccache helps).
"""

import json
import os
import re
import shutil
import struct
import subprocess
from pathlib import Path

import pytest

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
    if not all((DEPS / "lib" / lib).exists() for lib in ("libchuck.a", "libfaust.a")):
        subprocess.run([ROOT / "scripts" / "build_deps.sh", "host"], check=True, capture_output=True)
    subprocess.run(["cmake", "-S", ROOT, "-B", BUILD, "-DCMAKE_BUILD_TYPE=Release"], check=True, capture_output=True)
    targets = [f"{name.upper()}_VST3" for name in PRODUCTS]
    subprocess.run(["cmake", "--build", BUILD, "-j8", "--target", *targets], check=True, capture_output=True)
    host = BUILD / "plugin_host"
    subprocess.run([cxx, "-std=c++17", "-O1", "-pthread", f"-I{ROOT / 'ssp-sdk'}", HERE / "plugin_host.cpp",
                    "-ldl", "-o", host], check=True)
    return host


# source folder -> product name; Synthor lists only names of up to four characters
PRODUCTS = {"radio": "rdio", "csound": "csnd", "chuck": "chuk", "edrums": "edrm", "pstretch": "strc", "bard": "bard",
            "glitch": "gltc", "chorus": "chrs", "faust": "fstr"}


def so(name):
    product = PRODUCTS[name]
    return BUILD / "plugins" / name / f"{name.upper()}_artefacts" / "Release" / "VST3" / f"{product}.vst3" \
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
