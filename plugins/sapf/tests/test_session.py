"""Step 4 on the host: the plugin's TextBuffer, KeyQueue and Session (session_test.cpp)."""

import os
import subprocess

from test_harness import PRELUDE, ROOT
from test_server import core_ugens, server_test  # noqa: F401  (fixtures: the build and the UGens)


def test_session(server_test, core_ugens, tmp_path):  # noqa: F811
    b = server_test.parent
    subprocess.run(["cmake", "--build", b, "--target", "sapf_session_test", "-j", str(os.cpu_count())], check=True,
                   capture_output=True)
    r = subprocess.run([b / "sapf_session_test", PRELUDE, core_ugens, tmp_path], capture_output=True, text=True,
                       timeout=120)
    assert r.returncode == 0, r.stdout + r.stderr
    assert "all passed" in r.stdout
