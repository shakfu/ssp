"""Tests for new_plugin.py, on a scratch copy of the examples, plugins/CMakeLists.txt and the Makefile."""

import pathlib
import re
import shutil
import sys

import pytest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import new_plugin  # noqa: E402

REPO = pathlib.Path(__file__).resolve().parents[1]


@pytest.fixture
def root(tmp_path):
    shutil.copytree(REPO / "examples", tmp_path / "examples", ignore=shutil.ignore_patterns("tests"))
    (tmp_path / "plugins" / "chorus").mkdir(parents=True)
    shutil.copy2(REPO / "plugins/chorus/CMakeLists.txt", tmp_path / "plugins/chorus/CMakeLists.txt")
    shutil.copy2(REPO / "plugins/CMakeLists.txt", tmp_path / "plugins/CMakeLists.txt")
    shutil.copy2(REPO / "Makefile", tmp_path / "Makefile")
    return tmp_path


def text(dest):
    return {p.relative_to(dest).as_posix(): p.read_text() for p in dest.rglob("*") if p.is_file()}


def test_cpp(root):
    assert new_plugin.main(["my_amp", "mamp", "--description", "mono amp"], root) == 0
    files = text(root / "plugins/my_amp")
    assert set(files) == {"CMakeLists.txt", "README.md", "CHANGELOG.md", "Source/MyAmpEngine.h",
                          "Source/PluginProcessor.h", "Source/PluginProcessor.cpp", "Source/SSPApi.cpp"}
    cmake = files["CMakeLists.txt"]
    assert "project(MY_AMP VERSION" in cmake and "juce_add_plugin(MY_AMP" in cmake and "target_sources(MY_AMP" in cmake
    assert "PLUGIN_CODE MAMP" in cmake and 'PRODUCT_NAME "mamp"' in cmake and 'DESCRIPTION "mono amp"' in cmake
    assert '"${PROJECT_SOURCE_DIR}/../common"' in cmake
    assert "namespace my_amp" in files["Source/MyAmpEngine.h"]
    assert "my_amp::MyAmpEngine" in files["Source/PluginProcessor.cpp"]
    # no template name left in code
    for f, t in files.items():
        assert not re.search(r"svca|Svca|SVCA", t), f
    assert "add_subdirectory(my_amp)\n" in (root / "plugins/CMakeLists.txt").read_text()
    assert "my_amp" not in (root / "Makefile").read_text()


def test_faust(root):
    assert new_plugin.main(["wobble", "wobl", "--faust", "--description", "wobbler"], root) == 0
    files = text(root / "plugins/wobble")
    assert "Source/wobble.dsp" in files and "Source/WobbleKernel.h" in files
    assert 'declare name "wobble";' in files["Source/wobble.dsp"]
    assert "namespace ssp::faust::wobble {" in files["Source/WobbleKernel.h"]
    assert "ssp::faust::wobble::mydsp" in files["Source/PluginProcessor.h"]
    assert "PLUGIN_CODE WOBL" in files["CMakeLists.txt"] and "project(WOBBLE VERSION" in files["CMakeLists.txt"]
    for f, t in files.items():
        # the program's description still says what it does, in the .dsp and in the kernel's metadata
        rest = "\n".join(line for line in t.splitlines() if "Stereo tremolo" not in line)
        assert not re.search(r"tremolo|Tremolo|TREM", rest), f
    mk = (root / "Makefile").read_text()
    rule = "\tscripts/faust_kernel.sh plugins/wobble/Source/wobble.dsp plugins/wobble/Source/WobbleKernel.h wobble\n"
    assert rule in mk
    # inside the faust-kernels recipe
    assert mk.index("faust-kernels:\n") < mk.index(rule) < mk.index("\n\n", mk.index("faust-kernels:\n"))


def test_added_after_the_last_plain_subdirectory(root):
    new_plugin.main(["my_amp", "mamp"], root)
    lists = (root / "plugins/CMakeLists.txt").read_text()
    assert lists.index("add_subdirectory(chorus)") < lists.index("add_subdirectory(my_amp)") < lists.index("foreach")


@pytest.mark.parametrize("args, message", [
    (["amp", "amp"], "four characters"),
    (["amp", "ampli"], "four characters"),
    (["amp", "Amps"], "four characters"),
    (["Amp", "amps"], "lower case"),
    (["amp", "chrs"], "already chrs"),
    (["chorus", "abcd"], "exists"),
])
def test_refuses(root, args, message):
    before = (root / "plugins/CMakeLists.txt").read_text()
    with pytest.raises(SystemExit, match=message):
        new_plugin.main(args, root)
    assert (root / "plugins/CMakeLists.txt").read_text() == before


def test_a_changed_template_fails_and_leaves_nothing(root):
    p = root / "examples/svca/Source/PluginProcessor.cpp"
    p.write_text(p.read_text().replace('"vca"', '"amp"'))
    with pytest.raises(SystemExit, match="update scripts/new_plugin.py"):
        new_plugin.main(["my_amp", "mamp"], root)
    assert not (root / "plugins/my_amp").exists()
