"""Build psnd's Alda core with the plugin's stand-ins for libremidi, and test it and alda_test.cpp.

The first run fetches psnd's Alda with scripts/fetch_alda.sh (network, about 11 MB).
"""

import os
import shutil
import subprocess
from pathlib import Path

import pytest

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
SRC = HERE.parent / "Source"


@pytest.fixture(scope="module")
def alda():
    r = subprocess.run([ROOT / "scripts" / "fetch_alda.sh"], check=True, capture_output=True, text=True)
    return Path(r.stdout.strip())


# clang first: GCC's ThreadSanitizer aborts on kernels with high mmap entropy
def compilers():
    cc = shutil.which("clang") or shutil.which("cc") or shutil.which("gcc")
    cxx = shutil.which("clang++") or shutil.which("c++") or shutil.which("g++")
    if cc is None or cxx is None:
        pytest.skip("no host C and C++ compilers")
    return cc, cxx


def core(alda, out, *flags):
    """psnd's impl/ and scheduler.c, and the plugin's AldaCore.c, as objects in `out`."""
    cc, _ = compilers()
    inc = [f"-I{SRC / 'shim'}", f"-I{alda / 'include'}", f"-I{alda / 'include' / 'alda'}", f"-I{alda / 'impl'}"]
    objs = []
    for c in [*sorted((alda / "impl").glob("*.c")), alda / "scheduler.c", SRC / "AldaCore.c"]:
        o = out / (c.stem + ".o")
        subprocess.run([cc, "-std=gnu11", *flags, *inc, "-c", c, "-o", o], check=True)
        objs.append(o)
    return inc, objs


def build_engine(alda, out, name, *flags):
    _, cxx = compilers()
    inc, objs = core(alda, out, *flags)
    exe = out / name
    subprocess.run([cxx, "-std=c++17", "-Wall", "-pthread", *flags, f"-I{SRC}", f"-I{ROOT / 'plugins' / 'common'}",
                    f"-I{ROOT / 'external' / 'readerwriterqueue'}",
                    *inc, HERE / "alda_test.cpp", SRC / "AldaEngine.cpp",
                    ROOT / "plugins" / "common" / "engine" / "ScriptEngine.cpp", *objs, "-lm", "-o", exe], check=True)
    return exe


@pytest.mark.parametrize("scores,expected", [("tests/shared_suite", None), ("examples", "tests/alda_reference")])
def test_core_matches_psnd(alda, tmp_path, scores, expected):
    """psnd's shared suite and its conformance run against Alda's own output, without libremidi."""
    cc, _ = compilers()
    inc, objs = core(alda, tmp_path, "-O1")
    exe = tmp_path / "suite"
    subprocess.run([cc, "-std=gnu11", "-O1", *inc, alda / "tests" / "test_shared_suite.c", *objs, "-lm", "-o", exe],
                   check=True)
    args = [alda / scores] + ([alda / expected] if expected else [])
    r = subprocess.run([exe, *args], capture_output=True, text=True, timeout=120)
    assert r.returncode == 0, r.stdout[-4000:] + r.stderr[-4000:]
    assert " 0 failed" in r.stdout


def test_engine(alda, tmp_path):
    exe = build_engine(alda, tmp_path, "alda_test", "-O2")
    r = subprocess.run([exe], capture_output=True, text=True, timeout=120)
    assert r.returncode == 0, r.stderr[-4000:]
    assert "all passed" in r.stdout


def test_threads_have_no_data_races(alda, tmp_path):
    exe = build_engine(alda, tmp_path, "alda_race_test", "-O1", "-g", "-fsanitize=thread")
    env = dict(os.environ, TSAN_OPTIONS="exitcode=66")
    r = subprocess.run([exe, "threads"], env=env, capture_output=True, text=True, timeout=300)
    assert r.returncode == 0, r.stderr[-4000:]


def test_examples_build(alda, tmp_path):
    exe = build_engine(alda, tmp_path, "alda_test", "-O1")
    examples = sorted((HERE.parent / "examples").glob("*.alda"))
    assert examples
    r = subprocess.run([exe, "check", *examples], capture_output=True, text=True, timeout=60)
    assert r.returncode == 0, r.stderr[-4000:]
