"""GseqEngine (gseq_test.cpp) and LaunchpadView (launchpad_view_test.cpp) natively, and their threads under
ThreadSanitizer."""

import os
import shutil
import subprocess
from pathlib import Path

import pytest

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
SRC = HERE.parent / "Source"
COMMON = ROOT / "plugins" / "common"


def build(out, name, test, *flags):
    # clang first: GCC's ThreadSanitizer aborts on kernels with high mmap entropy
    cxx = shutil.which("clang++") or shutil.which("c++")
    if cxx is None:
        pytest.skip("no host C++ compiler")
    exe = out / name
    subprocess.run([cxx, "-std=c++17", "-Wall", "-pthread", *flags, f"-I{SRC}", f"-I{COMMON}",
                    f"-I{ROOT / 'external' / 'readerwriterqueue'}", HERE / test, SRC / "GseqEngine.cpp",
                    SRC / "LaunchpadView.cpp", COMMON / "engine" / "Clock.cpp", COMMON / "launchpad" / "Launchpad.cpp",
                    "-o", exe], check=True)
    return exe


TESTS = ["gseq_test.cpp", "launchpad_view_test.cpp"]


@pytest.mark.parametrize("test", TESTS)
def test_engine(tmp_path, test):
    r = subprocess.run([build(tmp_path, "t", test, "-O2")], capture_output=True, text=True, timeout=60)
    assert r.returncode == 0, r.stdout + r.stderr
    assert "all passed" in r.stdout


@pytest.mark.parametrize("test", TESTS)
def test_threads_have_no_data_races(tmp_path, test):
    exe = build(tmp_path, "race", test, "-O1", "-g", "-fsanitize=thread")
    env = dict(os.environ, TSAN_OPTIONS="exitcode=66")
    r = subprocess.run([exe, "threads"], env=env, capture_output=True, text=True, timeout=300)
    assert r.returncode == 0, r.stdout + r.stderr[-4000:]
