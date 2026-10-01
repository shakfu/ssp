"""Build engine_test.cpp with the host compiler and run it."""

import shutil
import subprocess
from pathlib import Path

import pytest

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
SOFTCUT = ROOT / "external" / "softcut-lib"


@pytest.fixture(scope="module")
def engine_test(tmp_path_factory):
    cxx = shutil.which("c++") or shutil.which("clang++") or shutil.which("g++")
    if cxx is None:
        pytest.skip("no host C++ compiler")
    exe = tmp_path_factory.mktemp("sfct") / "engine_test"
    cmd = [
        cxx, "-std=c++17", "-O2", "-Wall",
        f"-I{SOFTCUT / 'include'}", f"-I{HERE.parent / 'Source'}",
        str(HERE / "engine_test.cpp"),
        *map(str, sorted((SOFTCUT / "src").glob("*.cpp"))),
        "-o", str(exe),
    ]
    subprocess.run(cmd, check=True)
    return exe


def test_engine(engine_test):
    r = subprocess.run([engine_test], capture_output=True, text=True)
    assert r.returncode == 0, r.stderr
    print(r.stdout.strip())
