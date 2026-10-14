"""The step 1 harness on the host: block splitting, determinism, runaway blocks, and compare.py.

Builds against SAPF_SC_DIR, a sapf-sc checkout, or else the commit scripts/fetch_sapf.sh pins (network on
the first run). The device run is run_on_ssp.sh.
"""

import os
import shutil
import struct
import subprocess
import sys
from pathlib import Path

import pytest

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]

def sapf_dir() -> Path:
    if "SAPF_SC_DIR" in os.environ:
        return Path(os.environ["SAPF_SC_DIR"])
    out = subprocess.run([ROOT / "scripts" / "fetch_sapf.sh"], check=True, capture_output=True, text=True)
    return Path(out.stdout.strip())


SAPF = sapf_dir()
PRELUDE = SAPF / "sapf-prelude.txt"

sys.path.insert(0, str(HERE))
import compare  # noqa: E402
import examples  # noqa: E402


@pytest.fixture(scope="module")
def harness():
    if not shutil.which("clang++"):
        pytest.skip("no clang++")
    b = ROOT / "build" / "sapf-harness-test"
    subprocess.run(["cmake", "-S", HERE, "-B", b, "-DCMAKE_CXX_COMPILER=clang++", f"-DSAPF_SC_DIR={SAPF}"],
                   check=True, capture_output=True)
    subprocess.run(["cmake", "--build", b, "-j", str(os.cpu_count())], check=True, capture_output=True)
    return b / "sapf_harness"


def run(harness, tmp_path, code, **env):
    src = tmp_path / "in.sapf"
    src.write_text(code)
    out = tmp_path / "out"
    out.mkdir(exist_ok=True)
    r = subprocess.run([harness, PRELUDE, src, out], capture_output=True, text=True, timeout=120,
                       env={**os.environ, **env})
    assert r.returncode == 0, r.stderr
    return r.stdout.split("== block ")[1:], out


def test_blank_line_inside_brackets_stays_in_block(harness, tmp_path):
    blocks, out = run(harness, tmp_path, '[440\n\n550] 0 sinosc "a" >sd\n\n220 0 sinosc "b" >sd\n')
    assert len(blocks) == 2
    assert "error" not in "".join(blocks)
    assert (out / "a.scsyndef").exists() and (out / "b.scsyndef").exists()


def test_blank_line_in_string_or_comment_bracket(harness, tmp_path):
    # the bracket in the comment and the one in the string do not count
    blocks, _ = run(harness, tmp_path, '1 ; [\n\n"[" 2\n')
    assert len(blocks) == 2


def test_definitions_carry_over_and_stack_is_reported(harness, tmp_path):
    blocks, _ = run(harness, tmp_path, "3 = x\n\nx 4\n")
    assert "left on stack: 2" in blocks[1]


def test_seeded_blocks_are_deterministic(harness, tmp_path):
    # random voice times and frequencies (sapf-examples.txt)
    code = ('\\[ 4 = n (4 3 7 rand 6h xline 0 impulse 4 9c 9u xline * 1m decay) (n 4h 84h nrands) (n nurands) '
            '(n .01 .11 nrands) klank rpan2 3 1m fadeout] .6 1.2 randz 1 2 ola 2 "r" >nrt\n')
    outs = []
    for i in range(2):
        (tmp_path / str(i)).mkdir()
        outs.append(run(harness, tmp_path / str(i), code)[1] / "r.osc")
    assert outs[0].read_bytes() == outs[1].read_bytes()


def test_runaway_block_is_skipped(harness, tmp_path):
    # 10^12 terms: the child runs out of time
    blocks, _ = run(harness, tmp_path, "ord 1e12 N +/\n\n1 2 +\n", SAPF_BLOCK_SECONDS="1")
    assert blocks[0] == "1\nskipped: timeout\n"
    assert "left on stack: 1" in blocks[1]


def test_examples_rewrite_play_outside_comments():
    text = examples.rewrite("1 play ; play\nsplay play;tweet\n")
    assert text == '1 2 "ex001" >nrt ; play\nsplay 2 "ex002" >nrt;tweet\n'


def test_compare_allows_float32_rounding(tmp_path):
    for side, x in (("h", 1.0), ("s", 1.0000001)):
        d = tmp_path / side
        d.mkdir()
        (d / "f").write_bytes(b"#bundle\0" + struct.pack(">f", x))
        (tmp_path / f"{side}.log").write_text("loading file 'x'\nwrote '/a/f'\n")
    assert compare.main(*(str(tmp_path / p) for p in ("h.log", "s.log", "h", "s"))) == 0
    (tmp_path / "s" / "f").write_bytes(b"#bundle\0" + struct.pack(">f", 2.0))
    assert compare.main(*(str(tmp_path / p) for p in ("h.log", "s.log", "h", "s"))) == 1
