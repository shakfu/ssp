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


def test_highlighter(tmp_path):
    cc, cxx = shutil.which("cc"), shutil.which("clang++") or shutil.which("c++")
    if cc is None or cxx is None:
        pytest.skip("no host C/C++ compiler")
    ext = COMMON.parents[1] / "external"
    runtime = ext / "tree-sitter" / "lib"
    grammars = ["chuck", "faust", "csound", "sapf", "alda"]
    subprocess.run([cc, "-O1", "-std=gnu11", "-c", f"-I{runtime / 'include'}", f"-I{runtime / 'src'}",
                    runtime / "src" / "lib.c", "-o", tmp_path / "lib.o"], check=True)
    for g in grammars:
        subprocess.run([cc, "-O1", "-std=gnu11", "-c", ext / f"tree-sitter-{g}" / "src" / "parser.c",
                        "-o", tmp_path / f"{g}.o"], check=True)
    subprocess.run([cxx, "-std=c++17", "-O1", "-Wall", f"-I{COMMON}", f"-I{runtime / 'include'}",
                    HERE / "highlight_test.cpp", COMMON / "keys" / "Highlight.cpp", COMMON / "keys" / "TextBuffer.cpp",
                    tmp_path / "lib.o", *(tmp_path / f"{g}.o" for g in grammars), "-o", tmp_path / "highlight_test"],
                   check=True)
    r = subprocess.run([tmp_path / "highlight_test", *(ext / f"tree-sitter-{g}" / "queries" / "highlights.scm"
                                                       for g in grammars)], capture_output=True, text=True, timeout=30)
    assert r.returncode == 0, r.stdout + r.stderr
