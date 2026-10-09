"""Tests for release_notes.py: finding a CHANGELOG section and writing the notes."""

import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import release_notes  # noqa: E402

CHANGELOG = """# Changelog

## [Unreleased]

- next

## [0.2.0-rc1] - 2026-10-01

- candidate

## [0.2.0] - 2026-10-09

### Plugins

- two

## [0.1.0]

- one
"""


def test_dated_heading_up_to_the_next_section():
    section = release_notes.extract_section(CHANGELOG, "0.2.0")
    assert release_notes.strip_blank_edges(section) == "### Plugins\n\n- two"


def test_bare_heading():
    assert release_notes.strip_blank_edges(release_notes.extract_section(CHANGELOG, "0.1.0")) == "- one"


def test_a_prefix_is_not_a_match():
    assert release_notes.extract_section(CHANGELOG, "0.2") is None
    assert "candidate" not in release_notes.extract_section(CHANGELOG, "0.2.0")


def test_writes_notes_with_header(tmp_path):
    (tmp_path / "CHANGELOG.md").write_text(CHANGELOG)
    out = tmp_path / "notes.md"
    assert release_notes.main(["0.2.0", "--changelog", str(tmp_path / "CHANGELOG.md"), "-o", str(out)]) == 0
    assert out.read_text() == "## Changes since the last Release\n\n### Plugins\n\n- two\n"


def test_falls_back_to_unreleased(tmp_path):
    (tmp_path / "CHANGELOG.md").write_text(CHANGELOG)
    out = tmp_path / "notes.md"
    assert release_notes.main(["9.9.9", "--changelog", str(tmp_path / "CHANGELOG.md"), "-o", str(out)]) == 0
    assert out.read_text().endswith("\n- next\n")


def test_no_section_exits_2_and_writes_nothing(tmp_path):
    (tmp_path / "CHANGELOG.md").write_text("# Changelog\n\n## [0.1.0]\n\n- one\n")
    out = tmp_path / "notes.md"
    assert release_notes.main(["9.9.9", "--changelog", str(tmp_path / "CHANGELOG.md"), "-o", str(out)]) == 2
    assert not out.exists()


def test_repo_links_point_at_the_tag(tmp_path):
    root = pathlib.Path(__file__).resolve().parents[1]
    out = tmp_path / "notes.md"
    assert release_notes.main(["0.2.0", "--changelog", str(root / "CHANGELOG.md"), "-o", str(out)]) == 0
    notes = out.read_text()
    assert "](https://github.com/shakfu/ssp/blob/0.2.0/plugins/chorus/CHANGELOG.md)" in notes
    assert "](plugins/" not in notes and "](docs/" not in notes


def test_repo_changelog_has_the_current_version():
    root = pathlib.Path(__file__).resolve().parents[1]
    version = (root / "VERSION").read_text().strip()
    section = release_notes.extract_section((root / "CHANGELOG.md").read_text(), version)
    assert section and section.strip()
