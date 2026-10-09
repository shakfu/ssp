"""Build engine_test.cpp with the host compiler and run it."""

import shutil
import subprocess
from pathlib import Path

import pytest

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
SRC = HERE.parent / "Source"


def test_engine(tmp_path):
    cxx = shutil.which("clang++") or shutil.which("c++") or shutil.which("g++")
    if cxx is None:
        pytest.skip("no host C++ compiler")
    exe = tmp_path / "engine_test"
    subprocess.run([cxx, "-std=c++17", "-Wall", "-O2", f"-I{SRC}", f"-I{ROOT / 'plugins' / 'common'}",
                    str(HERE / "engine_test.cpp"), "-o", str(exe)], check=True)
    r = subprocess.run([exe], capture_output=True, text=True)
    assert r.returncode == 0, r.stderr
