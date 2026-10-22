"""Step 3 on the host: the plugin's TextBuffer, KeyQueue and Session (session_test.cpp)."""

import os
import subprocess

from test_sapf_engine import PRELUDE, harness  # noqa: F401  (fixture: the build)


def test_session(harness, tmp_path):  # noqa: F811
    b = harness.parent
    subprocess.run(["cmake", "--build", b, "--target", "sapf_session_test", "-j", str(os.cpu_count())], check=True,
                   capture_output=True)
    r = subprocess.run([b / "sapf_session_test", PRELUDE, tmp_path], capture_output=True, text=True, timeout=180)
    assert r.returncode == 0, r.stdout + r.stderr
    assert "all passed" in r.stdout
