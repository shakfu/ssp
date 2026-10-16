"""KeyInput's keyboard hand-off between plugin libraries (keys_test.cpp)."""

import shutil
import subprocess
from pathlib import Path

import pytest

HERE = Path(__file__).resolve().parent
COMMON = HERE.parent


def test_libraries_share_the_keyboard(tmp_path):
    cxx = shutil.which("clang++") or shutil.which("c++")
    if cxx is None:
        pytest.skip("no host C++ compiler")
    flags = ["-std=c++17", "-O2", "-Wall", "-pthread"]
    for lib in ("a", "b"):  # two copies, as two plugin libraries each have one
        subprocess.run([cxx, *flags, "-shared", "-fPIC", f"-I{COMMON}", HERE / "keys_shim.cpp",
                        COMMON / "keys" / "KeyInput.cpp", "-o", tmp_path / f"lib{lib}.so"], check=True)
    subprocess.run([cxx, *flags, HERE / "keys_test.cpp", "-ldl", "-o", tmp_path / "keys_test"], check=True)
    r = subprocess.run([tmp_path / "keys_test", tmp_path / "liba.so", tmp_path / "libb.so"], capture_output=True,
                       text=True, timeout=30)
    assert r.returncode == 0, r.stdout + r.stderr


def test_text_buffer_block(tmp_path):
    cxx = shutil.which("clang++") or shutil.which("c++")
    if cxx is None:
        pytest.skip("no host C++ compiler")
    subprocess.run([cxx, "-std=c++17", "-O1", "-Wall", f"-I{COMMON}", HERE / "text_test.cpp",
                    COMMON / "keys" / "TextBuffer.cpp", "-o", tmp_path / "text_test"], check=True)
    r = subprocess.run([tmp_path / "text_test"], capture_output=True, text=True, timeout=30)
    assert r.returncode == 0, r.stdout + r.stderr
