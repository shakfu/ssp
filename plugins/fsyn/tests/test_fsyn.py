"""Build FluidSynth and engine_test.cpp with the host compiler, and run the tests with each soundfont.

The first run fetches and builds FluidSynth with scripts/build_fluidsynth.sh (network, about 160 MB). The
fetched TimGM6mb and FluidR3_GM always run; GeneralUser-GS runs when present in FSYN_SOUNDFONTS (default
~/.aldakit/soundfonts).
"""

import os
import shutil
import subprocess
from pathlib import Path

import pytest

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
SRC = HERE.parent / "Source"
DEPS = ROOT / "build" / "deps"
FONTS = Path(os.environ.get("FSYN_SOUNDFONTS", Path.home() / ".aldakit" / "soundfonts"))


@pytest.fixture(scope="module")
def fluidsynth():
    subprocess.run([ROOT / "scripts" / "build_fluidsynth.sh", "host"], check=True, capture_output=True)
    return DEPS / "host"


# clang first: GCC's ThreadSanitizer aborts on kernels with high mmap entropy
def build(prefix, out, name, *flags):
    cxx = shutil.which("clang++") or shutil.which("c++") or shutil.which("g++")
    if cxx is None:
        pytest.skip("no host C++ compiler")
    exe = out / name
    subprocess.run([cxx, "-std=c++17", "-Wall", "-pthread", *flags, f"-I{SRC}", f"-I{ROOT / 'plugins' / 'common'}",
                    f"-I{ROOT / 'external' / 'readerwriterqueue'}", f"-I{prefix / 'include'}",
                    HERE / "engine_test.cpp", SRC / "FluidEngine.cpp", prefix / "lib" / "libfluidsynth.a", "-lm",
                    "-o", exe], check=True)
    return exe


def fonts():
    found = [DEPS / "src" / "TimGM6mb.sf2", DEPS / "src" / "FluidR3_GM.sf2"]
    for name in ["GeneralUser-GS.sf2"]:
        found.append(pytest.param(FONTS / name, marks=pytest.mark.skipif(not (FONTS / name).exists(),
                                                                            reason=f"no {FONTS / name}")))
    return found


@pytest.mark.parametrize("font", fonts(), ids=lambda f: Path(f).stem)
def test_engine(fluidsynth, tmp_path, font):
    exe = build(fluidsynth, tmp_path, "engine_test", "-O2")
    r = subprocess.run([exe, font], capture_output=True, text=True, timeout=300)
    assert r.returncode == 0, r.stderr[-4000:]
    assert "all passed" in r.stdout


def test_threads_have_no_data_races(fluidsynth, tmp_path):
    exe = build(fluidsynth, tmp_path, "engine_race_test", "-O1", "-g", "-fsanitize=thread")
    env = dict(os.environ, TSAN_OPTIONS="exitcode=66")
    r = subprocess.run([exe, DEPS / "src" / "TimGM6mb.sf2", "threads"], env=env, capture_output=True, text=True,
                       timeout=300)
    assert r.returncode == 0, r.stderr[-4000:]
