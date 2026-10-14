"""Build plugins/common/tests/midi in the host build and run it."""

import shutil
import subprocess
from pathlib import Path

import pytest

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
BUILD = ROOT / "build" / "plugins-host"


def test_midi_dispatch():
    if shutil.which("cmake") is None:
        pytest.skip("needs cmake")
    subprocess.run(["cmake", "-S", ROOT, "-B", BUILD, "-DCMAKE_BUILD_TYPE=Release"], check=True, capture_output=True)
    subprocess.run(["cmake", "--build", BUILD, "-j8", "--target", "base_midi_test"], check=True, capture_output=True)
    exe = next(BUILD.glob("plugins/common/tests/midi/base_midi_test_artefacts/**/base_midi_test"))
    r = subprocess.run([exe], capture_output=True, text=True, timeout=60)
    assert r.returncode == 0, r.stderr[-4000:]
