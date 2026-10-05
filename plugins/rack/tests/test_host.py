"""Build rack's Track and JSON code for the host under ThreadSanitizer, and run the tests.

The build goes to build/rack-host and is incremental; the first one compiles JUCE (about a minute).
"""

import os
import shutil
import subprocess
from pathlib import Path

import pytest

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
BUILD = ROOT / "build" / "rack-host"


@pytest.fixture(scope="module")
def build():
    cxx = shutil.which("clang++") or shutil.which("g++")
    if cxx is None or shutil.which("cmake") is None:
        pytest.skip("needs cmake and clang++ or g++")
    cmd = ["cmake", "-S", str(HERE / "host"), "-B", str(BUILD), f"-DCMAKE_CXX_COMPILER={cxx}"]
    if shutil.which("ninja") and not (BUILD / "CMakeCache.txt").exists():
        cmd += ["-G", "Ninja"]
    subprocess.run(cmd, check=True, capture_output=True)
    subprocess.run(["cmake", "--build", str(BUILD)], check=True, capture_output=True)
    return BUILD


def run(build, name):
    env = dict(os.environ, TSAN_OPTIONS="exitcode=66")
    # Module loads plugins/<name>.so relative to the working directory
    return subprocess.run([build / name], cwd=build, env=env, capture_output=True, text=True)


def test_track_has_no_data_races(build):
    r = run(build, "track_race_test")
    assert r.returncode == 0, r.stderr[-4000:]


def test_json_matrix(build):
    r = run(build, "json_matrix_test")
    assert r.returncode == 0, r.stderr[-4000:]
