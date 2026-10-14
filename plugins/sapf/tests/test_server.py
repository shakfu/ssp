"""Step 2: sapf-sc's interpreter playing through a ScWorld run in real time (server_test.cpp).

ola voices must sound where the >nrt score of the same program and seed puts them: the scheduler
follows the World's sample clock. Needs build/deps/host (scripts/build_deps.sh host) and sapf-sc, as
test_harness.py does.
"""

import os
import shutil
import subprocess

import pytest

from onsets import check
from test_harness import HERE, PRELUDE, ROOT, SAPF

DEPS = ROOT / "build" / "deps" / "host"
# single-impulse voices, 0.1 to 0.3 s apart
PROGRAM = r"\[0 0 impulse 64 N] .1 .3 randz 1 1 ola"
SECONDS = 3.0


@pytest.fixture(scope="module")
def server_test():
    if not (DEPS / "lib" / "libscsynth.a").exists():
        subprocess.run([ROOT / "scripts" / "build_deps.sh", "host"], check=True, capture_output=True)
    if not shutil.which("clang++"):
        pytest.skip("no clang++")
    b = ROOT / "build" / "sapf-server-test"
    subprocess.run(["cmake", "-S", HERE, "-B", b, "-DCMAKE_CXX_COMPILER=clang++", f"-DSAPF_SC_DIR={SAPF}",
                    f"-DSCSYNTH_DEPS={DEPS}"], check=True, capture_output=True)
    subprocess.run(["cmake", "--build", b, "--target", "sapf_server_test", "-j", str(os.cpu_count())], check=True,
                   capture_output=True)
    return b / "sapf_server_test"


@pytest.fixture(scope="module")
def core_ugens(tmp_path_factory):
    """SC's core UGens, without sc3-plugins."""
    d = tmp_path_factory.mktemp("ugens")
    for so in (DEPS / "scsynth" / "plugins").glob("*.so"):
        (d / so.name).symlink_to(so)
    return d


# 2: the World's clock runs ahead of the wall clock; a scheduler on the wall clock sends voices late
@pytest.mark.parametrize("speed", [1.0, 2.0])
def test_voices_follow_the_score(server_test, core_ugens, tmp_path, speed):
    r = subprocess.run([server_test, PRELUDE, core_ugens, tmp_path, PROGRAM, str(SECONDS), str(speed)],
                       capture_output=True, text=True, timeout=60)
    assert r.returncode == 0, r.stderr
    assert check(r.stdout, tmp_path / "sched.osc", SECONDS) == []


def test_post_reaches_the_handler(server_test, core_ugens, tmp_path):
    r = subprocess.run([server_test, PRELUDE, core_ugens, tmp_path, PROGRAM, "0.5", "1"], capture_output=True,
                       text=True, timeout=60)
    assert r.returncode == 0, r.stderr
    posted = r.stdout.split("== post\n")[1]
    assert "wrote '" in posted and "sched.osc'" in posted
    assert "wrote '" not in r.stdout.split("== post\n")[0]
