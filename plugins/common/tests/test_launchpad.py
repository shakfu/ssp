"""ssp::launchpad alone (launchpad_test.cpp)."""

import shutil
import subprocess
from pathlib import Path

import pytest

HERE = Path(__file__).resolve().parent
COMMON = HERE.parent


def test_launchpad(tmp_path):
    cxx = shutil.which("clang++") or shutil.which("c++")
    if cxx is None:
        pytest.skip("no host C++ compiler")
    subprocess.run([cxx, "-std=c++17", "-O1", "-Wall", f"-I{COMMON}", HERE / "launchpad_test.cpp",
                    COMMON / "launchpad" / "Launchpad.cpp", "-o", tmp_path / "launchpad_test"], check=True)
    r = subprocess.run([tmp_path / "launchpad_test"], capture_output=True, text=True, timeout=60)
    assert r.returncode == 0, r.stdout + r.stderr
    assert "all passed" in r.stdout
