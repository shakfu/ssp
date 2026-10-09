"""Tests for publish.sh: each check refuses, and a ready release reaches gh with the right arguments.

Runs the script in a scratch git repo with a bare origin and a fake `gh` that logs its arguments.
"""

import os
import pathlib
import shutil
import subprocess

import pytest

SCRIPT = pathlib.Path(__file__).resolve().parent / "publish.sh"
TAG = "9.9.9"
NAME = "shakfu-ssp-plugins-9.9.9"


def git(repo, *args):
    subprocess.run(["git", "-C", str(repo), *args], check=True, capture_output=True)


@pytest.fixture
def repo(tmp_path):
    if shutil.which("git") is None:
        pytest.skip("needs git")
    origin, work, bin_ = tmp_path / "origin.git", tmp_path / "work", tmp_path / "bin"
    subprocess.run(["git", "init", "-q", "--bare", str(origin)], check=True)
    subprocess.run(["git", "init", "-q", str(work)], check=True)
    git(work, "config", "user.email", "t@t")
    git(work, "config", "user.name", "t")
    git(work, "remote", "add", "origin", str(origin))
    (work / "scripts").mkdir()
    shutil.copy2(SCRIPT, work / "scripts" / "publish.sh")
    (work / "VERSION").write_text("9.9.9\n")
    (work / ".gitignore").write_text("/releases/\n")
    git(work, "add", ".")
    git(work, "commit", "-q", "-m", "init")
    (work / "releases").mkdir()
    (work / "releases" / f"{NAME}.zip").write_bytes(b"zip")
    (work / "releases" / f"{NAME}-notes.md").write_text("## notes\n")
    # a fake gh: `release view` finds nothing unless EXISTS is set; every call is logged
    bin_.mkdir()
    (bin_ / "gh").write_text('#!/bin/sh\necho "$@" >> "$GH_LOG"\n'
                             '[ "$1 $2" = "release view" ] && [ -z "$EXISTS" ] && exit 1\nexit 0\n')
    (bin_ / "gh").chmod(0o755)
    return work


def publish(repo, **env):
    e = dict(os.environ, PATH=f"{repo.parent / 'bin'}:{os.environ['PATH']}", GH_LOG=str(repo.parent / "gh.log"), **env)
    return subprocess.run([str(repo / "scripts" / "publish.sh")], env=e, capture_output=True, text=True)


def tag_and_push(repo):
    git(repo, "tag", TAG)
    git(repo, "push", "-q", "origin", TAG)


def gh_calls(repo):
    log = repo.parent / "gh.log"
    return log.read_text().splitlines() if log.exists() else []


def test_publishes_a_ready_release(repo):
    tag_and_push(repo)
    r = publish(repo)
    assert r.returncode == 0, r.stderr
    assert gh_calls(repo)[-1] == (f"release create {TAG} releases/{NAME}.zip --title shakfu-ssp-plugins 9.9.9 "
                                  f"--notes-file releases/{NAME}-notes.md --verify-tag")


def test_refuses_without_a_tag(repo):
    r = publish(repo)
    assert r.returncode != 0 and "no tag" in r.stderr
    assert gh_calls(repo) == []


def test_refuses_an_unpushed_tag(repo):
    git(repo, "tag", TAG)
    r = publish(repo)
    assert r.returncode != 0 and "not on origin" in r.stderr


def test_refuses_a_dirty_tree(repo):
    tag_and_push(repo)
    (repo / "untracked").write_text("x")
    r = publish(repo)
    assert r.returncode != 0 and "uncommitted" in r.stderr


def test_refuses_a_tag_that_is_not_head(repo):
    tag_and_push(repo)
    (repo / "x").write_text("x")
    git(repo, "add", "x")
    git(repo, "commit", "-q", "-m", "later")
    os.utime(repo / "releases" / f"{NAME}.zip")  # newer than the new HEAD, so only the tag check fails
    r = publish(repo)
    assert r.returncode != 0 and "is not HEAD" in r.stderr


def test_refuses_a_zip_older_than_head(repo):
    tag_and_push(repo)
    os.utime(repo / "releases" / f"{NAME}.zip", (0, 0))
    r = publish(repo)
    assert r.returncode != 0 and "older than HEAD" in r.stderr


def test_refuses_an_existing_release(repo):
    tag_and_push(repo)
    r = publish(repo, EXISTS="1")
    assert r.returncode != 0 and "exists already" in r.stderr
    assert not any(c.startswith("release create") for c in gh_calls(repo))


def test_refuses_without_artifacts(repo):
    tag_and_push(repo)
    (repo / "releases" / f"{NAME}.zip").unlink()
    r = publish(repo)
    assert r.returncode != 0 and "make release" in r.stderr
