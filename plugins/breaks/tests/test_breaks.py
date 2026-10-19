"""Build engine_test.cpp with the host compilers and run it against a card of _core's generated test samples.

The first run fetches _core (about 50 MB) and generates its engine with scripts/fetch_core.sh, which needs
clang and uv.
"""

import importlib.util
import os
import shutil
import subprocess
from pathlib import Path

import pytest

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
SRC = HERE.parent / "Source"


@pytest.fixture(scope="module")
def core():
    if shutil.which("clang") is None or shutil.which("clang++") is None:
        pytest.skip("needs clang")
    r = subprocess.run([ROOT / "scripts" / "fetch_core.sh", "host"], check=True, capture_output=True, text=True)
    return Path(r.stdout.strip().splitlines()[-1])


@pytest.fixture(scope="module")
def card(core, tmp_path_factory):
    # _core's own fixture writer: sine samples with the sample manager's padding and .info metadata
    spec = importlib.util.spec_from_file_location("core_fixtures", core / "test" / "rack" / "fixtures.py")
    fixtures = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(fixtures)
    root = tmp_path_factory.mktemp("cards") / "card"
    for bank in (1, 2, 3):
        for slot in range(3):
            fixtures.sample(root, bank, slot, seconds=2)
    # 170 BPM, not the writer's 120: Tempo's default of 120 must not replace the card's tempo
    for info in root.glob("bank*/*.info"):
        b = bytearray(info.read_bytes())
        flags = int.from_bytes(b[4:8], "little")
        b[4:8] = (flags & ~0x1FF | 170).to_bytes(4, "little")
        info.write_bytes(bytes(b))
    return root


def build(core, out, name, *flags):
    objs = []
    for c, inc in [(core / "gen" / "host" / "engine.c", []), (HERE / "golden.c", [core / "test" / "rack"]),
                   (core / "lib" / "pcg_basic.c", []), (core / "lib" / "core_engine" / "card_format.c", [])]:
        o = out / (c.stem + ".o")
        subprocess.run(["clang", "-std=c11", "-w", "-fwrapv", "-ffp-contract=off", *flags,
                        f"-I{core / 'lib' / 'core_engine'}", *[f"-I{i}" for i in inc], "-c", c, "-o", o], check=True)
        objs.append(o)
    exe = out / name
    subprocess.run(["clang++", "-std=c++17", "-Wall", "-pthread", *flags, f"-I{SRC}", f"-I{ROOT / 'plugins' / 'common'}",
                    f"-I{core / 'rack' / 'src'}", f"-I{core / 'lib' / 'core_engine'}", HERE / "engine_test.cpp",
                    SRC / "BreaksEngine.cpp", core / "rack" / "src" / "Storage.cpp", *objs, "-lm", "-o", exe],
                   check=True)
    return exe


def test_engine(core, card, tmp_path):
    exe = build(core, tmp_path, "engine_test", "-O2")
    r = subprocess.run([exe, card], capture_output=True, text=True, timeout=300)
    assert r.returncode == 0, r.stderr[-4000:]
    assert "all passed" in r.stdout


def test_threads_have_no_data_races(core, card, tmp_path):
    exe = build(core, tmp_path, "engine_race_test", "-O1", "-g", "-fsanitize=thread")
    env = dict(os.environ, TSAN_OPTIONS="exitcode=66")
    r = subprocess.run([exe, card], env=env, capture_output=True, text=True, timeout=600)
    assert r.returncode == 0, r.stderr[-4000:]
