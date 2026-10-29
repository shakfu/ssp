"""Build prdt_test (tests/CMakeLists.txt) against the vendored libpd and run it.

Builds go to build/prdt-host/<variant>; the first run compiles libpd (about 20 s).
"""

import os
import re
import shutil
import subprocess
from pathlib import Path

import pytest

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
BUILD = ROOT / "build" / "prdt-host"

# clang first: GCC's ThreadSanitizer aborts on kernels with high mmap entropy
VARIANTS = {
    "release": ("Release", ""),
    "tsan": ("RelWithDebInfo", "-fsanitize=thread"),
    "asan": ("RelWithDebInfo", "-fsanitize=address -fno-omit-frame-pointer"),
}


def build(variant):
    if shutil.which("cmake") is None:
        pytest.skip("no cmake")
    cc, cxx = shutil.which("clang"), shutil.which("clang++")
    if cxx is None:
        pytest.skip("no clang")
    kind, flags = VARIANTS[variant]
    out = BUILD / variant
    subprocess.run(
        ["cmake", "-S", HERE, "-B", out, f"-DCMAKE_BUILD_TYPE={kind}", f"-DCMAKE_C_COMPILER={cc}",
         f"-DCMAKE_CXX_COMPILER={cxx}", f"-DCMAKE_C_FLAGS={flags}", f"-DCMAKE_CXX_FLAGS={flags}",
         f"-DCMAKE_EXE_LINKER_FLAGS={flags}"],
        check=True, capture_output=True)
    subprocess.run(["cmake", "--build", out, "-j", str(os.cpu_count() or 4)], check=True, capture_output=True)
    return out / "prdt_test"


def test_engine(tmp_path):
    r = subprocess.run([build("release"), tmp_path], capture_output=True, text=True)
    assert r.returncode == 0, r.stderr[-4000:]


def test_threads_have_no_data_races(tmp_path):
    env = dict(os.environ, TSAN_OPTIONS=f"exitcode=66 suppressions={HERE / 'tsan.supp'}")
    r = subprocess.run([build("tsan"), tmp_path, "threads"], env=env, capture_output=True, text=True)
    assert r.returncode == 0, r.stderr[-4000:]


def test_no_memory_errors(tmp_path):
    # each load frees its instance: what PdEngine::destroy frees beyond pdinstance_free stays freed
    env = dict(os.environ, ASAN_OPTIONS="detect_leaks=1", LSAN_OPTIONS=f"suppressions={HERE / 'lsan.supp'}")
    r = subprocess.run([build("asan"), tmp_path], env=env, capture_output=True, text=True)
    assert r.returncode == 0, r.stderr[-4000:]


def test_external_does_not_load(tmp_path):
    exe = build("release")
    src = tmp_path / "hello.c"
    src.write_text('#include "m_pd.h"\nstatic t_class *c;\nstatic void *hello_new(void) { return pd_new(c); }\n'
                   'void hello_setup(void) { c = class_new(gensym("hello"), hello_new, 0, sizeof(t_object), 0, 0); }\n')
    subprocess.run([shutil.which("clang"), "-shared", "-fPIC", f"-I{ROOT / 'external/libpd/pure-data/src'}",
                    "-DPDINSTANCE=1", "-DPDTHREADS=1", src, "-o", tmp_path / "hello.pd_linux"], check=True)
    r = subprocess.run([exe, tmp_path, "external"], capture_output=True, text=True)
    assert r.returncode == 0, r.stderr[-4000:]


def cyclone_objects():
    """Every cyclone object prdt registers: the lowercase class names in the vendored sources, less the ones
    pd/CMakeLists.txt leaves out, and proxies"""
    src = ROOT / "external" / "pd-cyclone" / "cyclone_objects" / "binaries"
    skip = {"loadmess", "mousestate", "mousefilter", "active"}
    names = set()
    for f in sorted([*(src / "control").glob("*.c"), *(src / "audio").glob("*.c")]):
        if f.stem in skip or f.stem == "scope_dialog":
            continue
        text = f.read_text(errors="replace")
        names.update(re.findall(r'class_new\(gensym\("([a-z!=<>%+*/.~-][^"]*)"', text))
        names.update(f"zl.{m}" for m in re.findall(r"^ZL_ALIAS_SETUP\((\w+)\)", text, re.M))
    # funbuffcom holds funbuff's shared data; "zl." is zl's alias macro, whose names the line above adds
    names -= {"funbuffcom", "zl."}
    return sorted(n for n in names if not n.startswith("_") and "proxy" not in n and "-inlet" not in n)


def test_every_cyclone_object_creates(tmp_path):
    # under AddressSanitizer, so each also frees without a leak
    names = cyclone_objects()
    assert len(names) > 180
    patch = "#N canvas 0 50 450 300 12;\n" + "".join(f"#X obj 10 {10 + 20 * i} {n};\n" for i, n in enumerate(names))
    (tmp_path / "all.pd").write_text(patch)
    env = dict(os.environ, ASAN_OPTIONS="detect_leaks=1", LSAN_OPTIONS=f"suppressions={HERE / 'lsan.supp'}")
    r = subprocess.run([build("asan"), tmp_path, "load", tmp_path / "all.pd"], env=env, capture_output=True, text=True)
    assert r.returncode == 0, r.stderr[-4000:]


ELSE = ROOT / "external" / "pd-else"
# the sources pd/CMakeLists.txt leaves out
ELSE_SKIP = {"play.file~", "streamin~", "streamout~", "sfload", "sfinfo", "pdlink", "pdlink~", "beat~", "mouse",
             "keycode", "properties", "else"}


def else_classes():
    """ELSE's class names in the vendored sources prdt compiles, less internal classes, proxies, and "del~ in" and
    "del~ out", which [del~ in] and [del~ out] create"""
    src = ELSE / "Source"
    names = set()
    for f in sorted([*(src / "Control").glob("*.c"), *(src / "Audio").glob("*.c"), *(src / "Extra/Aliases").glob("*.c")]):
        if f.stem in ELSE_SKIP:
            continue
        names.update(re.findall(r'class_new\(gensym\("([^"]+)"', f.read_text(errors="replace")))
    return sorted(n for n in names if not n.startswith("_") and "proxy" not in n and "-inlet" not in n and " " not in n)


def cyclone_owns():
    """else_prdt.c's CYCLONE_OWNS"""
    text = (HERE.parent / "pd" / "else_prdt.c").read_text()
    return sorted(re.findall(r'"([^"]+)"', re.search(r"CYCLONE_OWNS\[\] = \{(.*?)\};", text, re.S).group(1)))


def test_cyclone_owns_the_names_both_have():
    assert cyclone_owns() == sorted(set(else_classes()) & set(cyclone_objects()))


def run_asan(tmp_path, names, *path):
    patch = "#N canvas 0 50 450 300 12;\n" + "".join(f"#X obj 10 {10 + 20 * i} {n};\n" for i, n in enumerate(names))
    (tmp_path / "all.pd").write_text(patch)
    env = dict(os.environ, ASAN_OPTIONS="detect_leaks=1", LSAN_OPTIONS=f"suppressions={HERE / 'lsan.supp'}")
    return subprocess.run([build("asan"), tmp_path, "load", tmp_path / "all.pd", *path], env=env, capture_output=True,
                          text=True)


def test_every_else_object_creates(tmp_path):
    # by both names, under AddressSanitizer, so each also frees without a leak; cyclone's where it has the name
    names = else_classes()
    assert len(names) > 350
    r = run_asan(tmp_path, names + [f"else/{n}" for n in names])
    assert r.returncode == 0, r.stderr[-4000:]


# ELSE abstractions that need an object prdt does not have: pdlua's [display], ffmpeg's [sfload] and [sfinfo],
# [mouse], [keycode], [pd~], [plaits~] or [sfont~]
ELSE_UNSUPPORTED = {"about.MERDA", "abs.pd~", "brane.m~", "gran.player~", "keymap", "multi.vsl", "pattern", "phaser~",
                    "phaser.m~", "plaits.m~", "plaits.unit", "player~", "pvoc.player~", "range.hsl", "sample~",
                    "sequencer", "sfont.m~", "slider2d", "superosc~", "superosc.m~", "superosc.unit"}


def test_every_else_abstraction_creates(tmp_path):
    # flat in else/, as install-prdt.sh copies them
    flat = tmp_path / "else"
    flat.mkdir()
    for f in (ELSE / "Abstractions").rglob("*.pd"):
        shutil.copy(f, flat / f.name)
    names = sorted(f.stem for f in flat.glob("*.pd") if f.stem not in ELSE_UNSUPPORTED)
    assert len(names) > 280
    r = run_asan(tmp_path, names, tmp_path, flat)
    assert r.returncode == 0, r.stderr[-4000:]


def test_libraries_share_no_global_names():
    # a name two static libraries define links to one of them without an error
    if shutil.which("nm") is None:
        pytest.skip("no nm")
    out = build("release").parent / "pd"
    seen = {}
    for lib in [*out.glob("libpd_*.a"), *out.glob("libpd/libs/*.a")]:
        r = subprocess.run(["nm", "-g", "--defined-only", lib], check=True, capture_output=True, text=True)
        for name in {f[2] for f in (line.split() for line in r.stdout.splitlines()) if len(f) == 3}:
            seen.setdefault(name, []).append(lib.name)
    assert {n: libs for n, libs in seen.items() if len(libs) > 1} == {}
