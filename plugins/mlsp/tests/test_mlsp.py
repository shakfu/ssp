"""Build uSEQ's signal engine, from the fork shakfu/useq, and test it and mlsp_test.cpp.

The first run fetches the fork with scripts/fetch_useq.sh (network).
"""

import os
import shutil
import subprocess
from pathlib import Path

import pytest

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
SRC = HERE.parent / "Source"
DEFS = ["-DUSE_OWN_ARDUINO_STR"]  # uSEQ's own Arduino String, as its desktop builds
SUITES = ["test_signal_engine", "test_signal_engine_golden", "test_live_edit", "test_signal_engine_robustness",
          "test_output_classification", "test_ugens", "test_state_identity", "test_signal_engine_phase4"]


@pytest.fixture(scope="module")
def useq():
    r = subprocess.run([ROOT / "scripts" / "fetch_useq.sh"], check=True, capture_output=True, text=True)
    return Path(r.stdout.strip())


# clang first: GCC's ThreadSanitizer aborts on kernels with high mmap entropy
def compiler():
    cxx = shutil.which("clang++") or shutil.which("c++") or shutil.which("g++")
    if cxx is None:
        pytest.skip("no host C++ compiler")
    return cxx


def engine(useq, out, *flags):
    """uSEQ's signal engine and the String it needs, as objects in `out`."""
    src = useq / "src"
    objs = []
    for c in [*sorted((src / "signal_engine").glob("*.cpp")), src / "utils" / "string.cpp", src / "utils" / "itoa.cpp"]:
        o = out / (c.stem + ".o")
        subprocess.run([compiler(), "-std=c++17", *flags, *DEFS, f"-I{src}", "-c", c, "-o", o], check=True)
        objs.append(o)
    return objs


def build_test(useq, out, name, *flags):
    objs = engine(useq, out, *flags)
    exe = out / name
    subprocess.run([compiler(), "-std=c++17", "-Wall", "-pthread", *flags, *DEFS, f"-I{SRC}", f"-I{ROOT / 'plugins' / 'common'}",
                    f"-I{useq / 'src'}", HERE / "mlsp_test.cpp", SRC / "UseqEngine.cpp",
                    ROOT / "plugins" / "common" / "engine" / "ScriptEngine.cpp",
                    ROOT / "plugins" / "common" / "engine" / "Clock.cpp", *objs, "-lm", "-o", exe], check=True)
    return exe


def test_useq_suite(useq, tmp_path):
    """uSEQ's own signal-engine suites pass on the fork."""
    src = useq / "src"
    objs = engine(useq, tmp_path, "-O1")
    for suite in SUITES:
        exe = tmp_path / suite
        subprocess.run([compiler(), "-std=c++17", "-O1", *DEFS, f"-I{useq}", f"-I{src}",
                        useq / "test" / "signal_engine" / f"{suite}.cpp", *objs, "-lm", "-o", exe], check=True)
        r = subprocess.run([exe], cwd=useq / "test", capture_output=True, text=True, timeout=300)
        assert r.returncode == 0, suite + "\n" + r.stdout[-4000:] + r.stderr[-4000:]
        assert "All tests passed" in r.stdout, suite


def test_engine(useq, tmp_path):
    exe = build_test(useq, tmp_path, "mlsp_test", "-O2")
    r = subprocess.run([exe], capture_output=True, text=True, timeout=120)
    assert r.returncode == 0, r.stderr[-4000:]
    assert "all passed" in r.stdout


def test_threads_have_no_data_races(useq, tmp_path):
    exe = build_test(useq, tmp_path, "mlsp_race_test", "-O1", "-g", "-fsanitize=thread")
    env = dict(os.environ, TSAN_OPTIONS="exitcode=66")
    r = subprocess.run([exe, "threads"], env=env, capture_output=True, text=True, timeout=300)
    assert r.returncode == 0, r.stderr[-4000:]


def test_examples_compile(useq, tmp_path):
    exe = build_test(useq, tmp_path, "mlsp_test", "-O1")
    examples = sorted((HERE.parent / "examples").glob("*.useq"))
    assert examples
    r = subprocess.run([exe, "check", *examples], capture_output=True, text=True, timeout=60)
    assert r.returncode == 0, r.stderr[-4000:]
