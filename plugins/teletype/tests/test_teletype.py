"""Build the teletype firmware and engine_test.cpp for the host and run it.

The first run fetches the teletype sources (scripts/fetch_teletype.sh) into build/deps/src/teletype.
"""

import shutil
import subprocess
from pathlib import Path

import pytest

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
SOURCE = HERE.parent / "Source"
FW_BUILD = ROOT / "build" / "ttyp-fw-host"


@pytest.fixture(scope="module")
def firmware():
    if shutil.which("cmake") is None or shutil.which("cc") is None:
        pytest.skip("needs cmake and a C compiler")
    subprocess.run([ROOT / "scripts" / "fetch_teletype.sh"], check=True, capture_output=True)
    subprocess.run(["cmake", "-S", HERE.parent / "firmware", "-B", FW_BUILD, "-DCMAKE_BUILD_TYPE=Release"],
                   check=True, capture_output=True)
    subprocess.run(["cmake", "--build", FW_BUILD, "-j8"], check=True, capture_output=True)
    return FW_BUILD / "ttyp_firmware.so"


@pytest.fixture(scope="module")
def engine_test(tmp_path_factory):
    cxx = shutil.which("c++") or shutil.which("clang++") or shutil.which("g++")
    if cxx is None:
        pytest.skip("no host C++ compiler")
    exe = tmp_path_factory.mktemp("ttyp") / "engine_test"
    cmd = [
        cxx, "-std=c++17", "-O2", "-Wall", "-pthread", f"-I{SOURCE}",
        str(HERE / "engine_test.cpp"),
        *(str(SOURCE / f) for f in ("Firmware.cpp", "Teletype.cpp", "KeyInput.cpp")),
        "-ldl", "-o", str(exe),
    ]
    subprocess.run(cmd, check=True)
    return exe


def test_engine(engine_test, firmware):
    r = subprocess.run([engine_test, firmware], capture_output=True, text=True, timeout=120)
    assert r.returncode == 0, r.stderr
    print(r.stdout.strip())
