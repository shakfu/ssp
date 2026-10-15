"""The step 1 harness on the host: block splitting, rendering, runaway blocks, and compare_render.py.

Builds against SAPF_DIR, a checkout of github.com/shakfu/sapf, else the commit scripts/fetch_sapf.sh pins
(network on the first run). The device runs are run_on_ssp.sh and measure_on_ssp.sh.
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
    if "SAPF_DIR" in os.environ:
        return Path(os.environ["SAPF_DIR"])
    out = subprocess.run([ROOT / "scripts" / "fetch_sapf.sh"], check=True, capture_output=True, text=True)
    return Path(out.stdout.strip())


SAPF = sapf_dir()
PRELUDE = SAPF / "sapf-prelude.txt"
DEPS = ROOT / "build" / "deps" / "host"

sys.path.insert(0, str(HERE))
import compare_render  # noqa: E402


@pytest.fixture(scope="module")
def harness():
    if not shutil.which("clang++"):
        pytest.skip("no clang++")
    if not (DEPS / "lib" / "libsndfile.a").exists():
        subprocess.run([ROOT / "scripts" / "build_deps.sh", "host"], check=True, capture_output=True)
    b = ROOT / "build" / "sapf-engine-harness-test"
    subprocess.run(["cmake", "-S", HERE, "-B", b, "-DCMAKE_CXX_COMPILER=clang++", "-DCMAKE_C_COMPILER=clang",
                    f"-DSAPF_DIR={SAPF}", f"-DDEPS={DEPS}"], check=True, capture_output=True)
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


def samples(path: Path) -> tuple[float, ...]:
    data = path.read_bytes()
    return struct.unpack(f"<{len(data) // 4}f", data)


def test_blank_line_inside_brackets_stays_in_block(harness, tmp_path):
    blocks, _ = run(harness, tmp_path, "[440\n\n550] 0 sinosc .1 * play\n\n1 2 +\n")
    assert len(blocks) == 2
    assert "error" not in "".join(blocks)


def test_blank_line_in_string_or_comment_bracket(harness, tmp_path):
    # the bracket in the comment and the one in the string do not count
    blocks, _ = run(harness, tmp_path, '1 ; [\n\n"[" 2\n')
    assert len(blocks) == 2


def test_definitions_carry_over_and_stack_is_reported(harness, tmp_path):
    blocks, _ = run(harness, tmp_path, "3 = x\n\nx 4\n")
    assert "left on stack: 2" in blocks[1]


def test_play_renders_two_seconds_of_stereo(harness, tmp_path):
    blocks, out = run(harness, tmp_path, "[440 0 sinosc .5 * 0] play\n\n1 2 +\n")
    assert "rendered 'block001.f32'" in blocks[0]
    x = samples(out / "block001.f32")
    assert len(x) == 2 * (96000 // 128 * 128)
    assert 0.49 < max(x[0::2]) <= 0.5
    assert max(map(abs, x[1::2])) == 0.0
    assert not (out / "block002.f32").exists()  # no play, no file


def test_seeded_blocks_are_deterministic(harness, tmp_path):
    code = "4 0 1 nrands 4h * 0 sinosc +/ .1 * play\n"
    outs = []
    for i in range(2):
        (tmp_path / str(i)).mkdir()
        outs.append(run(harness, tmp_path / str(i), code)[1] / "block001.f32")
    assert outs[0].read_bytes() == outs[1].read_bytes()


def test_runaway_block_is_skipped(harness, tmp_path):
    # 10^12 terms: the child runs out of time
    blocks, _ = run(harness, tmp_path, "ord 1e12 N +/\n\n1 2 +\n", SAPF_BLOCK_SECONDS="1")
    assert blocks[0] == "1\nskipped: timeout\n"
    assert "left on stack: 1" in blocks[1]


def write_run(d: Path, log: str, files: dict[str, bytes]):
    d.mkdir()
    (d.parent / f"{d.name}.log").write_text(log)
    for n, data in files.items():
        (d / n).write_bytes(data)


def compare(tmp_path) -> int:
    return compare_render.main(*(str(tmp_path / p) for p in ("pc.log", "pc2.log", "ssp.log", "pc", "pc2", "ssp")))


def test_compare_allows_rounding_and_varying_blocks(tmp_path):
    f = lambda *xs: struct.pack(f"<{len(xs)}f", *xs)  # noqa: E731
    log = "loading file '/a/p'\n== block 1\nrendered 'block001.f32'\n== block 2\nrendered 'block002.f32'\n"
    write_run(tmp_path / "pc", log, {"block001.f32": f(1.0), "block002.f32": f(0.5)})
    write_run(tmp_path / "pc2", log.replace("rendered 'block002.f32'\n", ""), {"block001.f32": f(1.0)})
    write_run(tmp_path / "ssp", log, {"block001.f32": f(1.00001), "block002.f32": f(0.7)})
    assert compare(tmp_path) == 0
    (tmp_path / "ssp" / "block001.f32").write_bytes(f(1.1))
    assert compare(tmp_path) == 1
    (tmp_path / "ssp" / "block001.f32").write_bytes(f(1.0))
    (tmp_path / "ssp" / "block002.f32").write_bytes(f(float("nan")))
    assert compare(tmp_path) == 1


def test_written_files_are_not_opened(harness, tmp_path):
    # sgram hands its image to `open`: a stub on PATH records any call
    stub = tmp_path / "bin"
    stub.mkdir()
    for name in ("open", "xdg-open"):
        (stub / name).write_text(f"#!/bin/sh\necho \"$1\" >> {tmp_path / 'opened'}\n")
        (stub / name).chmod(0o755)
    blocks, _ = run(harness, tmp_path, "440 0 sinosc .05 T 100 0 sgram\n",
                    PATH=f"{stub}:{os.environ['PATH']}", TMPDIR=str(tmp_path))
    assert "error" not in blocks[0]
    assert not (tmp_path / "opened").exists()
