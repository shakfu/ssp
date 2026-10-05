"""Build graph_order_test.cpp with the host compiler and run it."""

import shutil
import subprocess
from pathlib import Path

import pytest

HERE = Path(__file__).resolve().parent


@pytest.fixture(scope="module")
def graph_order_test(tmp_path_factory):
    cxx = shutil.which("c++") or shutil.which("clang++") or shutil.which("g++")
    if cxx is None:
        pytest.skip("no host C++ compiler")
    exe = tmp_path_factory.mktemp("rack") / "graph_order_test"
    cmd = [
        cxx, "-std=c++17", "-O2", "-Wall", "-Wextra",
        f"-I{HERE.parent / 'Source'}",
        str(HERE / "graph_order_test.cpp"),
        "-o", str(exe),
    ]
    subprocess.run(cmd, check=True)
    return exe


def test_graph_order(graph_order_test):
    r = subprocess.run([graph_order_test], capture_output=True, text=True)
    assert r.returncode == 0, r.stderr
