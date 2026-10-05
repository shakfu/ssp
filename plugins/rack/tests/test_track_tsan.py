"""Build Track for the host under ThreadSanitizer and race it against its control calls.

The build goes to build/rack-tsan and is incremental; the first one compiles JUCE (about a minute).
"""

import os
import shutil
import subprocess
from pathlib import Path

import pytest

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
BUILD = ROOT / "build" / "rack-tsan"


@pytest.fixture(scope="module")
def track_race_test():
    cxx = shutil.which("clang++") or shutil.which("g++")
    if cxx is None or shutil.which("cmake") is None:
        pytest.skip("needs cmake and clang++ or g++")
    cmd = ["cmake", "-S", str(HERE / "tsan"), "-B", str(BUILD), f"-DCMAKE_CXX_COMPILER={cxx}"]
    if shutil.which("ninja") and not (BUILD / "CMakeCache.txt").exists():
        cmd += ["-G", "Ninja"]
    subprocess.run(cmd, check=True, capture_output=True)
    subprocess.run(["cmake", "--build", str(BUILD)], check=True, capture_output=True)
    return BUILD / "track_race_test"


def test_track_has_no_data_races(track_race_test):
    env = dict(os.environ, TSAN_OPTIONS="exitcode=66")
    # Module loads plugins/<name>.so relative to the working directory
    r = subprocess.run([track_race_test], cwd=BUILD, env=env, capture_output=True, text=True)
    assert r.returncode == 0, r.stderr[-4000:]
