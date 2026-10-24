"""ssp_card_image.py without a card: regular files stand in for the device."""

import hashlib
import os
import shutil
import subprocess
import sys

import pytest

sys.path.insert(0, os.path.dirname(__file__))
import ssp_card_image as sci  # noqa: E402


def disk(**over):
    d = {"name": "sdb", "path": "/dev/sdb", "size": 32_000_000_000, "type": "disk", "rm": True, "tran": "usb",
         "mountpoints": [None], "children": [
             {"name": "sdb6", "path": "/dev/sdb6", "size": 1, "type": "part", "fstype": "vfat", "label": "BOOT",
              "mountpoints": ["/media/sa/BOOT"]}]}
    d.update(over)
    return d


def test_check_disk_accepts_a_card():
    sci.check_disk(disk())
    sci.check_disk(disk(rm=False))  # a USB reader that does not report removable


@pytest.mark.parametrize("over", [{"type": "part"}, {"rm": False, "tran": "nvme"}])
def test_check_disk_refuses(over):
    with pytest.raises(SystemExit):
        sci.check_disk(disk(**over))


def test_check_disk_refuses_a_system_mount():
    d = disk()
    d["children"][0]["mountpoints"] = ["/"]
    with pytest.raises(SystemExit):
        sci.check_disk(d)


def test_image_length_is_the_last_partitions_end(tmp_path):
    for name, start, size in [("sdb1", 64, 8000), ("sdb7", 40992768, 18432000), ("sdb6", 32768, 40960000)]:
        (tmp_path / name).mkdir()
        (tmp_path / name / "start").write_text(f"{start}\n")
        (tmp_path / name / "size").write_text(f"{size}\n")
    (tmp_path / "queue").mkdir()  # not a partition
    assert sci.image_length(tmp_path) == (40992768 + 18432000) * 512


def test_zero_fill_writes_then_removes(tmp_path):
    (tmp_path / "keep").write_bytes(b"x")
    assert sci.zero_fill(tmp_path, limit=10 * sci.CHUNK + 5) == 10 * sci.CHUNK + 5
    assert [p.name for p in tmp_path.iterdir()] == ["keep"]


def test_zero_fill_stops_when_full(tmp_path, monkeypatch):
    real = open

    class Full:  # a file that takes 3 chunks, then reports a full disk
        def __init__(self, *a, **k):
            self.f, self.n = real(*a, **k), 0

        def write(self, b):
            if self.n == 3:
                raise OSError(sci.errno.ENOSPC, "full")
            self.n += 1
            return self.f.write(b)

        def fileno(self):
            return self.f.fileno()

        def __enter__(self):
            return self

        def __exit__(self, *a):
            self.f.close()

    monkeypatch.setattr("builtins.open", Full)
    assert sci.zero_fill(tmp_path) == 3 * sci.CHUNK
    assert list(tmp_path.iterdir()) == []


def card_parts(boot_mounted=True, rootfs_mounted=True):
    part = lambda name, fs, label, m: {"name": name, "path": f"/dev/{name}", "fstype": fs, "label": label,
                                       "mountpoints": [m]}
    return disk(children=[part("sdb3", "vfat", None, "/media/sa/3665-6331"),
                          part("sdb6", "vfat", "BOOT", "/media/sa/BOOT" if boot_mounted else None),
                          part("sdb7", "ext4", "rootfs", "/media/sa/rootfs" if rootfs_mounted else None)])


def test_zero_plan():
    # zerofree takes ext4 whether mounted or not (create unmounts first); vfat is filled; sdb3 never
    assert sci.zero_plan(card_parts(), "/usr/sbin/zerofree") == (["/media/sa/BOOT"], ["/dev/sdb7"], [])
    assert sci.zero_plan(card_parts(rootfs_mounted=False), "/usr/sbin/zerofree") == (["/media/sa/BOOT"], ["/dev/sdb7"], [])
    # without zerofree, ext4 is filled while mounted, else skipped
    assert sci.zero_plan(card_parts(), None) == (["/media/sa/BOOT", "/media/sa/rootfs"], [], [])
    assert sci.zero_plan(card_parts(boot_mounted=False, rootfs_mounted=False), None) == ([], [], ["/dev/sdb6", "/dev/sdb7"])


@pytest.mark.parametrize("suffix", [".zst", ".xz", ".gz"])
def test_round_trip(tmp_path, suffix):
    if not shutil.which(sci.COMPRESSORS[suffix][0][0]):
        pytest.skip(f"no {sci.COMPRESSORS[suffix][0][0]}")
    data = os.urandom(3 * sci.CHUNK + 1234) + bytes(2 * sci.CHUNK)
    card = tmp_path / "card"
    card.write_bytes(data + b"tail past the last partition")
    image = tmp_path / f"card.img{suffix}"
    sha = sci.copy_out(card, len(data), image)
    assert sha == hashlib.sha256(data).hexdigest()
    target = tmp_path / "target"
    target.write_bytes(bytes(len(data) + 100))  # a larger card: its tail stays
    assert sci.copy_in(image, target, len(data)) == sha
    assert sci.read_back(target, len(data)) == sha
    assert target.read_bytes()[len(data):] == bytes(100)


def test_copy_out_refuses_a_short_source(tmp_path):
    (tmp_path / "card").write_bytes(b"abc")
    with pytest.raises(SystemExit):
        sci.copy_out(tmp_path / "card", 10, tmp_path / "x.img.gz")


def test_copy_in_refuses_a_size_mismatch(tmp_path):
    (tmp_path / "card").write_bytes(b"abcdef")
    sci.copy_out(tmp_path / "card", 6, tmp_path / "x.img.gz")
    with pytest.raises(SystemExit):
        sci.copy_in(tmp_path / "x.img.gz", tmp_path / "t", 7)


def test_unknown_suffix():
    with pytest.raises(SystemExit):
        sci.compressor("card.img")


def test_needs_root():
    if os.geteuid() == 0:
        pytest.skip("running as root")
    r = subprocess.run([sys.executable, sci.__file__, "create", "/dev/null", "x.img.zst"], capture_output=True, text=True)
    assert r.returncode != 0 and "run as root" in r.stderr


def test_sidecar_name():
    assert str(sci.sidecar("a/b.img.zst")) == "a/b.img.zst.json"
