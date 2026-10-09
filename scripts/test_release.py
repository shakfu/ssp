"""Tests for release.py: plugin discovery, link rewriting, and the package's layout."""

import pathlib
import sys
import zipfile

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import release  # noqa: E402

ROOT = release.ROOT


def test_finds_every_plugin_with_its_version():
    plugins = {p.product: p for p in release.find_plugins(ROOT)}
    assert set(plugins) == {"bard", "chrs", "chuk", "csnd", "edrm", "fstr", "gltc", "strc", "rack", "rdio", "sfct"}
    assert all(p.version and p.description for p in plugins.values())


def test_links_point_at_the_tag_unless_copied_alongside():
    src = ROOT / "plugins" / "faust"
    text = "a [doc](../../docs/dev/faust.md#plan), [log](CHANGELOG.md), [web](https://x.y/z), [dir](examples)"
    out = release.rewrite_links(text, src, {"CHANGELOG.md"}, "9.9.9", ROOT)
    assert f"[doc]({release.REPO_URL}/blob/9.9.9/docs/dev/faust.md#plan)" in out
    assert "[log](CHANGELOG.md)" in out
    assert "[web](https://x.y/z)" in out
    assert f"[dir]({release.REPO_URL}/tree/9.9.9/plugins/faust/examples)" in out


def fake_build(tmp: pathlib.Path) -> tuple[pathlib.Path, pathlib.Path]:
    build, deps = tmp / "build", tmp / "deps"
    for p in release.find_plugins(ROOT):
        so = build / "plugins" / p.dir.name / f"{p.dir.name.upper()}_artefacts" / "Release" / "VST3" / \
            f"{p.product}.vst3" / "Contents" / "armv7l-linux" / f"{p.product}.so"
        so.parent.mkdir(parents=True)
        so.write_bytes(b"so")
    (deps / "share" / "faust").mkdir(parents=True)
    (deps / "share" / "faust" / "stdfaust.lib").write_text("")
    (deps / "share" / "faust" / "README.md").write_text("the Faust libraries' own readme")
    return build, deps


def test_package_mirrors_the_card(tmp_path):
    build, deps = fake_build(tmp_path)
    archive = release.package(ROOT, build, deps, tmp_path / "out", "9.9.9", None)
    top = tmp_path / "out" / "shakfu-ssp-plugins-9.9.9"
    products = {p.product for p in release.find_plugins(ROOT)}

    assert {f.stem for f in (top / "BOOT" / "plugins").iterdir()} == products
    assert (top / "BOOT" / "faust" / "libraries" / "stdfaust.lib").is_file()
    assert (top / "BOOT" / "faust" / "chorus.dsp").is_file()
    assert (top / "BOOT" / "rack_presets" / "empty.json").is_file()
    assert (top / "BOOT" / "csound" / "filter.csd").is_file()
    assert (top / "BOOT" / "chuck" / "filter.ck").is_file()
    for product in products:
        assert (top / "docs" / product / "README.md").is_file()
    assert (top / "docs" / "gltc" / "LICENSE").is_file()
    assert (top / "docs" / "rack" / "README-json-presets.md").is_file()
    assert not list((top / "docs").rglob("TODO.md"))
    assert (top / "docs" / "sfct" / "softcut-LICENSE.txt").is_file()

    readme = (top / "README.md").read_text()
    assert all(f"`{p}`" in readme for p in products)
    for m in ("bard", "chrs", "chuk", "csnd", "edrm", "gltc", "rdio", "strc"):  # ported from sk-engines
        row = next(line for line in readme.splitlines() if line.startswith(f"| `{m}` |"))
        assert "[sk-engines](https://github.com/shakfu/sk-engines)" in row
    fstr = (top / "docs" / "fstr" / "README.md").read_text()
    assert "../" not in fstr  # no link left pointing into the repo
    assert f"]({release.REPO_URL}/blob/9.9.9/docs/dev/faust.md)" in fstr

    with zipfile.ZipFile(archive) as z:
        assert all(n.startswith("shakfu-ssp-plugins-9.9.9/") for n in z.namelist())
        assert "shakfu-ssp-plugins-9.9.9/BOOT/plugins/fstr.so" in z.namelist()


def test_missing_build_stops(tmp_path):
    build, deps = fake_build(tmp_path)
    next(build.rglob("csnd.so")).unlink()
    try:
        release.package(ROOT, build, deps, tmp_path / "out", "9.9.9", None)
    except SystemExit as e:
        assert "csnd" in str(e)
    else:
        raise AssertionError("packaged without csnd.so")


def test_md_links_to_pdf():
    text = "[a](docs/rack/README.md) [b](README-json-presets.md#format) [c](https://x.y/z.md) [d](LICENSE)"
    assert release.md_links_to_pdf(text) == \
        "[a](docs/rack/README.pdf) [b](README-json-presets.pdf) [c](https://x.y/z.md) [d](LICENSE)"


def test_pdf_package_keeps_only_pdfs(tmp_path):
    build, deps = fake_build(tmp_path)

    def fake_render(md):  # the PDF holds the text quarto would have rendered
        md.with_suffix(".pdf").write_text(md.read_text())

    release.package(ROOT, build, deps, tmp_path / "out", "9.9.9", None, fake_render)
    top = tmp_path / "out" / "shakfu-ssp-plugins-9.9.9"
    libraries = top / "BOOT" / "faust" / "libraries"
    assert [md for md in top.rglob("*.md") if not md.is_relative_to(libraries)] == []
    assert (libraries / "README.md").is_file()  # third-party Markdown stays as it is
    assert not (libraries / "README.pdf").exists()

    assert "](docs/rack/README.pdf)" in (top / "README.pdf").read_text()
    assert "](README-json-presets.pdf)" in (top / "docs" / "rack" / "README.pdf").read_text()
    assert (top / "docs" / "rack" / "README-json-presets.pdf").is_file()
    assert (top / "BOOT" / "rack_presets" / "README.pdf").is_file()
    assert "/blob/9.9.9/docs/dev/faust.md)" in (top / "docs" / "fstr" / "README.pdf").read_text()
