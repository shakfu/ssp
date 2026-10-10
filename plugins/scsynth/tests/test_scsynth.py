"""Build scsynth_test.cpp against the host build of libscsynth and run it.

The first run builds the dependencies for the host with scripts/build_deps.sh (network, a few minutes).
"""

import os
import shutil
import subprocess
from pathlib import Path

import pytest

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
SRC = HERE.parent / "Source"
DEPS = ROOT / "build" / "deps" / "host"
# SC_AUDIO_API selects SC's headers: as in scripts/scsynth/CMakeLists.txt
SC_DEFS = ["-DSC_AUDIO_API=SC_AUDIO_API_SSP", "-DSC_AUDIO_API_SSP=100", "-DSC_MEMORY_ALIGNMENT=32"]


@pytest.fixture(scope="module")
def deps():
    if not (DEPS / "lib" / "libscsynth.a").exists():
        subprocess.run([ROOT / "scripts" / "build_deps.sh", "host"], check=True, capture_output=True)
    return DEPS


@pytest.fixture(scope="module")
def core_ugens(deps, tmp_path_factory):
    """SC's core UGens without sc3-plugins: links to the .so files beside the sc3-plugins directory."""
    d = tmp_path_factory.mktemp("core-ugens")
    for so in (deps / "scsynth" / "plugins").glob("*.so"):
        (d / so.name).symlink_to(so)
    return d


@pytest.fixture(scope="module")
def tsan_lib(deps):
    """libscsynth built with ThreadSanitizer: without it, TSan cannot see SC's own atomics."""
    if not shutil.which("clang++"):
        pytest.skip("no clang++")
    b = deps / "build" / "scsynth-tsan"
    flags = "-fsanitize=thread -O1 -g"
    subprocess.run(["cmake", "-S", ROOT / "scripts" / "scsynth", "-B", b, "-DCMAKE_C_COMPILER=clang",
                    "-DCMAKE_CXX_COMPILER=clang++", f"-DCMAKE_C_FLAGS={flags}", f"-DCMAKE_CXX_FLAGS={flags}",
                    f"-DSC_PATH={(deps / 'include' / 'supercollider').resolve()}",
                    f"-DSNDFILE_PREFIX={deps}"], check=True, capture_output=True)
    subprocess.run(["cmake", "--build", b, "--target", "scsynth", "-j", str(os.cpu_count())], check=True,
                   capture_output=True)
    return b / "libscsynth.a"


# clang first: GCC's ThreadSanitizer aborts on kernels with high mmap entropy
def build(deps, out, name, *flags, lib=None):
    cxx = shutil.which("clang++") or shutil.which("c++") or shutil.which("g++")
    if cxx is None:
        pytest.skip("no host C++ compiler")
    sc = deps / "include" / "supercollider"
    inc = ["include/common", "common", "include/server", "include/plugin_interface", "server/scsynth",
           "external_libraries/boost", "external_libraries/boost_sync/include",
           "external_libraries/TLSF-2.4.6/src"]
    exe = out / name
    cmd = [
        cxx, "-std=c++17", "-Wall", "-pthread", *flags, *SC_DEFS,
        f"-I{SRC}", *(f"-I{sc / i}" for i in inc), f"-I{deps / 'include'}",
        str(HERE / "scsynth_test.cpp"), str(SRC / "ScWorld.cpp"), str(SRC / "ScsyDef.cpp"),
        str(lib or deps / "lib" / "libscsynth.a"), str(deps / "lib" / "libsndfile.a"), "-ldl", "-lm",
        "-o", str(exe),
    ]
    subprocess.run(cmd, check=True)
    return exe


def build_module(deps, out, *flags, lib=None):
    """scsy_test.cpp with the engine the plugin runs: ScsynthEngine over ScriptEngine."""
    cxx = shutil.which("clang++") or shutil.which("c++") or shutil.which("g++")
    sc = deps / "include" / "supercollider"
    inc = ["include/common", "common", "include/server", "include/plugin_interface", "server/scsynth",
           "external_libraries/boost", "external_libraries/boost_sync/include",
           "external_libraries/TLSF-2.4.6/src"]
    exe = out / "scsy_test"
    subprocess.run([
        cxx, "-std=c++17", "-Wall", "-pthread", *(flags or ("-O2",)), *SC_DEFS,
        f"-I{SRC}", f"-I{ROOT / 'plugins' / 'common'}", *(f"-I{sc / i}" for i in inc), f"-I{deps / 'include'}",
        str(HERE / "scsy_test.cpp"), *(str(SRC / f) for f in ("ScsynthEngine.cpp", "ScWorld.cpp", "ScsyDef.cpp")),
        str(ROOT / "plugins" / "common" / "engine" / "ScriptEngine.cpp"),
        str(lib or deps / "lib" / "libscsynth.a"), str(deps / "lib" / "libsndfile.a"), "-ldl", "-lm",
        "-o", str(exe),
    ], check=True)
    return exe


def run(exe, *args, env=None):
    r = subprocess.run([exe, *map(str, args), HERE / "defs"], capture_output=True, text=True, env=env, timeout=120)
    assert r.returncode == 0, r.stderr[-4000:]


def test_engine(deps, core_ugens, tmp_path_factory):
    run(build(deps, tmp_path_factory.mktemp("scsynth"), "scsynth_test", "-O2"), "engine", core_ugens)


def test_sc3_plugins_load_from_the_ugen_directory(deps, tmp_path_factory):
    run(build(deps, tmp_path_factory.mktemp("scsynth"), "scsynth_test", "-O2"), "sc3", deps / "scsynth" / "plugins")


def test_two_worlds_have_no_data_races(deps, core_ugens, tsan_lib, tmp_path_factory):
    exe = build(deps, tmp_path_factory.mktemp("scsynth"), "scsynth_race_test", "-O1", "-g", "-fsanitize=thread",
                lib=tsan_lib)
    run(exe, "threads", core_ugens, env=dict(os.environ, TSAN_OPTIONS="exitcode=66"))


def test_module_engine(deps, core_ugens, tmp_path_factory, tmp_path):
    exe = build_module(deps, tmp_path_factory.mktemp("scsy"))
    r = subprocess.run([exe, core_ugens, HERE / "defs", tmp_path], capture_output=True, text=True, timeout=120)
    assert r.returncode == 0, r.stderr[-4000:]


def test_module_engine_has_no_data_races(deps, core_ugens, tsan_lib, tmp_path_factory, tmp_path):
    exe = build_module(deps, tmp_path_factory.mktemp("scsy"), "-O1", "-g", "-fsanitize=thread", lib=tsan_lib)
    env = dict(os.environ, TSAN_OPTIONS="exitcode=66")
    r = subprocess.run([exe, core_ugens, HERE / "defs", tmp_path], capture_output=True, text=True, timeout=300,
                       env=env)
    assert r.returncode == 0, r.stderr[-4000:]


BUILT = {}  # the binary shared by the example tests
EXAMPLES = sorted((HERE.parent / "examples").glob("*.scsyndef"))


@pytest.mark.parametrize("program", EXAMPLES, ids=[p.name for p in EXAMPLES])
def test_example(deps, tmp_path_factory, program):
    exe = BUILT.get("example") or BUILT.setdefault("example", build_module(deps, tmp_path_factory.mktemp("scsy")))
    r = subprocess.run([exe, "example", deps / "scsynth" / "plugins", program], capture_output=True, text=True,
                       timeout=120)
    assert r.returncode == 0, r.stderr[-2000:]
