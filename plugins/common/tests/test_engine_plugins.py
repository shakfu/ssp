"""Build the engine plugins for the host and drive each through the SSP API with plugin_host.cpp.

Covers what the native engine tests cannot: EngineProcessor's block splitting and worker thread,
parameters and custom state through save and restore, and the SSP entry points. The first run
builds three plugins and their JUCE copies into build/plugins-host (a few minutes; ccache helps).
"""

import shutil
import struct
import subprocess
from pathlib import Path

import pytest

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
BUILD = ROOT / "build" / "plugins-host"
DEPS = ROOT / "build" / "deps" / "host"


@pytest.fixture(scope="module")
def plugins():
    cxx = shutil.which("clang++") or shutil.which("c++")
    if cxx is None or shutil.which("cmake") is None:
        pytest.skip("needs cmake and a C++ compiler")
    if not (DEPS / "lib" / "libchuck.a").exists():
        subprocess.run([ROOT / "scripts" / "build_deps.sh", "host"], check=True, capture_output=True)
    subprocess.run(["cmake", "-S", ROOT, "-B", BUILD, "-DCMAKE_BUILD_TYPE=Release"], check=True, capture_output=True)
    subprocess.run(["cmake", "--build", BUILD, "-j8", "--target", "RADIO_VST3", "CSOUND_VST3", "CHUCK_VST3"],
                   check=True, capture_output=True)
    host = BUILD / "plugin_host"
    subprocess.run([cxx, "-std=c++17", "-O1", "-pthread", f"-I{ROOT / 'ssp-sdk'}", HERE / "plugin_host.cpp",
                    "-ldl", "-o", host], check=True)
    return host


# source folder -> product name; Synthor lists only names of up to four characters
PRODUCTS = {"radio": "rdio", "csound": "csnd", "chuck": "chuk"}


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
    # program set after prepare, as Load does
    levels, state = run(plugins, "chuck", "prepare", 48000, 128, "set", "program", ck, "set", "p1", 0.75,
                        "in", 1, 0.3, "run", 0.3, 300, "level", 2, "level", 3, "state")
    assert levels[2][1] == pytest.approx(0.75, abs=1e-6)
    assert levels[3][1] == pytest.approx(0.3, abs=1e-6)
    assert f'program="{ck}"' in state
