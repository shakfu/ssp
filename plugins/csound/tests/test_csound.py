"""Build csound_test.cpp against the host build of Csound and run it.

The first run builds Csound for the host with scripts/build_deps.sh (network, about a minute).
"""

import os
import shutil
import subprocess
from pathlib import Path

import pytest

BUILT = {}  # binaries shared by the example tests

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
SRC = HERE.parent / "Source"
DEPS = ROOT / "build" / "deps" / "host"


@pytest.fixture(scope="module")
def deps():
    if not (DEPS / "lib" / "libcsound.a").exists():
        subprocess.run([ROOT / "scripts" / "build_csound.sh", "host"], check=True, capture_output=True)
    return DEPS


# clang first: GCC's ThreadSanitizer aborts on kernels with high mmap entropy
def build(deps, out, name, *flags):
    cxx = shutil.which("clang++") or shutil.which("c++") or shutil.which("g++")
    if cxx is None:
        pytest.skip("no host C++ compiler")
    exe = out / name
    cmd = [
        cxx, "-std=c++17", "-Wall", "-pthread", *flags,
        f"-I{SRC}", f"-I{ROOT / 'plugins' / 'common'}", f"-I{ROOT / 'external' / 'readerwriterqueue'}",
        f"-I{deps / 'include' / 'csound'}",
        str(HERE / "csound_test.cpp"), str(SRC / "CsoundEngine.cpp"),
        str(ROOT / "plugins" / "common" / "engine" / "ScriptEngine.cpp"),
        str(deps / "lib" / "libcsound.a"), str(deps / "lib" / "libsndfile.a"), "-ldl", "-lm",
        "-o", str(exe),
    ]
    subprocess.run(cmd, check=True)
    return exe


def test_engine(deps, tmp_path_factory, tmp_path):
    exe = build(deps, tmp_path_factory.mktemp("csound"), "csound_test", "-O2")
    r = subprocess.run([exe, tmp_path], capture_output=True, text=True)
    assert r.returncode == 0, r.stderr[-4000:]


def test_threads_have_no_data_races(deps, tmp_path_factory, tmp_path):
    exe = build(deps, tmp_path_factory.mktemp("csound"), "csound_race_test", "-O1", "-g", "-fsanitize=thread")
    env = dict(os.environ, TSAN_OPTIONS="exitcode=66")
    r = subprocess.run([exe, tmp_path, "threads"], env=env, capture_output=True, text=True)
    assert r.returncode == 0, r.stderr[-4000:]


EXAMPLES = sorted((HERE.parent / "examples").iterdir())


@pytest.mark.parametrize("program", EXAMPLES, ids=[p.name for p in EXAMPLES])
def test_example(deps, tmp_path_factory, program):
    exe = BUILT.get("example") or BUILT.setdefault("example", build(deps, tmp_path_factory.mktemp("csound"), "csound_example", "-O2"))
    r = subprocess.run([exe, "example", program], capture_output=True, text=True, timeout=60)
    assert r.returncode == 0, r.stderr[-2000:]
    peaks = [float(line.split()[2]) for line in r.stdout.splitlines() if line.startswith("peak")]
    if "midi" not in program.name or "csound" == "csound":  # chuck's midi.ck needs a MIDI device
        assert max(peaks) > 1e-3, r.stdout
