"""Build engine_test.cpp with the host compiler and run it."""

import os
import shutil
import subprocess
from pathlib import Path

import pytest

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
SRC = HERE.parent / "Source"


# clang first: GCC's ThreadSanitizer aborts on kernels with high mmap entropy
def build(tmp_path_factory, name, *flags):
    cxx = shutil.which("clang++") or shutil.which("c++") or shutil.which("g++")
    if cxx is None:
        pytest.skip("no host C++ compiler")
    exe = tmp_path_factory.mktemp("glitch") / name
    cmd = [
        cxx, "-std=c++17", "-Wall", "-pthread", *flags,
        f"-I{SRC}", f"-I{ROOT / 'plugins' / 'common'}",
        str(HERE / "engine_test.cpp"), str(SRC / "GlitchEngine.cpp"),
        "-o", str(exe),
    ]
    subprocess.run(cmd, check=True)
    return exe


def test_engine(tmp_path_factory):
    exe = build(tmp_path_factory, "engine_test", "-O2")
    r = subprocess.run([exe], capture_output=True, text=True)
    assert r.returncode == 0, r.stderr


def test_threads_have_no_data_races(tmp_path_factory):
    exe = build(tmp_path_factory, "engine_race_test", "-O1", "-g", "-fsanitize=thread")
    env = dict(os.environ, TSAN_OPTIONS="exitcode=66")
    r = subprocess.run([exe, "threads"], env=env, capture_output=True, text=True)
    assert r.returncode == 0, r.stderr[-4000:]
