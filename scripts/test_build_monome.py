"""build_monome.sh for the host, and libmonome's libftdi backend without a device: the failure paths a
missing or unknown grid takes. The success path needs a grid; docs/dev/monome.md has the device check.

The first run fetches libusb, libftdi1 and libmonome (network, about 1 MB) and builds them.
"""

import shutil
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[1]
DEPS = ROOT / "build" / "deps" / "host"

PROGRAM = r"""
#include <stdio.h>
#include <monome.h>

int main(void) {
    /* not an ftdi: name: no serial, so no protocol */
    if( monome_open("/dev/null") ) return 1;
    /* a monome serial with no such device: the mext protocol loads, then the open fails */
    if( monome_open("ftdi:m0000000") ) return 2;
    if( monome_open("ftdi:") ) return 3;
    puts("ok");
    return 0;
}
"""


@pytest.fixture(scope="module")
def deps():
    if shutil.which("cmake") is None or shutil.which("make") is None:
        pytest.skip("needs cmake and make")
    subprocess.run([ROOT / "scripts" / "build_monome.sh", "host"], check=True, capture_output=True)
    return DEPS


def test_open_without_a_device_fails_cleanly(deps, tmp_path):
    cc = shutil.which("clang") or shutil.which("cc")
    src = tmp_path / "open.c"
    src.write_text(PROGRAM)
    exe = tmp_path / "open"
    subprocess.run([cc, "-g", "-fsanitize=address,undefined", f"-I{deps / 'include'}", src,
                    deps / "lib" / "libmonome.a", deps / "lib" / "libftdi1.a", deps / "lib" / "libusb-1.0.a",
                    "-lpthread", "-o", exe], check=True)
    r = subprocess.run([exe], capture_output=True, text=True, timeout=60)
    assert r.returncode == 0, r.stdout + r.stderr[-4000:]
    assert r.stdout.strip() == "ok"
