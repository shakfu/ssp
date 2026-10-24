#!/usr/bin/env python3
"""Image an SSP SD card to a compressed file, and write such an image to another card. Run as root:

    sudo python3 scripts/ssp_card_image.py create /dev/sdX ssp-card.img.zst [--no-zero]
    sudo python3 scripts/ssp_card_image.py write ssp-card.img.zst /dev/sdX

The image is the whole device, from sector 0 to the end of the last partition: the rk3288 boots from a
Rockchip loader in raw sectors before the partitions, so a per-partition copy would not boot. Stopping at
the last partition lets the image fit a card a little smaller than the source; `write` then rebuilds the
backup GPT at the new card's end (sgdisk -e).

`create` first zeroes the free space of the filesystems labelled BOOT and rootfs, so deleted data
compresses away: zerofree on ext4 once unmounted, as it writes only free blocks that are not already zero;
zero files that fill a mounted vfat, which has no such tool. Other filesystems are left alone (the SSP's
4 MB vfat partition claims 15.9 GB).
The suffix picks the compressor: .zst (zstd), .xz or .gz. A sidecar IMAGE.json holds the raw size and
SHA-256, which `write` checks the card against after writing.
"""

import argparse
import errno
import hashlib
import json
import os
import pathlib
import shutil
import subprocess
import sys
import time

CHUNK = 4 << 20
ZERO_LABELS = ("BOOT", "rootfs")
ZERO_FSTYPES = ("vfat", "ext4")
ZERO_FILE = ".ssp-zero"  # ZERO_FILE0, ZERO_FILE1, ...: FAT32 caps a file at 4 GiB
COMPRESSORS = {  # suffix: (compress, decompress)
    ".zst": (["zstd", "-T0", "-q", "-c"], ["zstd", "-dcq"]),
    ".xz": (["xz", "-T0", "-c"], ["xz", "-dc"]),
    ".gz": (["gzip", "-c"], ["gzip", "-dc"]),
}


def fail(msg):
    sys.exit(f"ssp_card_image: {msg}")


def lsblk(device):
    out = subprocess.run(["lsblk", "-J", "-b", "-o", "NAME,PATH,SIZE,TYPE,FSTYPE,LABEL,MOUNTPOINTS,RM,TRAN,MODEL",
                          device], capture_output=True, text=True, check=True).stdout
    return json.loads(out)["blockdevices"][0]


def check_disk(disk):
    """Refuses anything but a whole removable or USB disk with no system mount."""
    if disk["type"] != "disk":
        fail(f"{disk['path']} is a {disk['type']}, not a whole disk")
    if not (disk.get("rm") or disk.get("tran") == "usb"):
        fail(f"{disk['path']} is neither removable nor on USB")
    for part in [disk] + disk.get("children", []):
        for m in part.get("mountpoints") or []:
            if m and not m.startswith(("/media/", "/run/media/", "/mnt")):
                fail(f"{part['path']} is mounted at {m}: not an SD card")


def partitions(disk):
    return disk.get("children", [])


def image_length(sysfs_disk):
    """The byte just past the last partition, from /sys/block/<disk>/<part>/{start,size} (512-byte sectors)."""
    ends = [int((p / "start").read_text()) + int((p / "size").read_text())
            for p in pathlib.Path(sysfs_disk).iterdir() if (p / "start").exists()]
    if not ends:
        fail(f"no partitions under {sysfs_disk}")
    return max(ends) * 512


def zero_fill(directory, limit=None):
    """Fills a filesystem's free space with zero files, then deletes them; returns the bytes written."""
    block, written, files, full = bytes(CHUNK), 0, [], False
    try:
        while not full and (limit is None or written < limit):
            path = pathlib.Path(directory) / f"{ZERO_FILE}{len(files)}"
            files.append(path)
            with open(path, "wb", buffering=0) as f:  # unbuffered: ENOSPC comes from write, not close
                try:
                    while limit is None or written < limit:
                        written += f.write(block[:CHUNK if limit is None else min(CHUNK, limit - written)])
                        progress("zero", written, limit)
                except OSError as e:
                    if e.errno == errno.ENOSPC:
                        full = True
                    elif e.errno != errno.EFBIG:  # EFBIG: the file is at FAT32's 4 GiB; start the next
                        raise
                os.fsync(f.fileno())
    finally:
        for p in files:
            p.unlink(missing_ok=True)
        os.sync()
    return written


def zero_plan(disk, zerofree):
    """(mountpoints to fill with zero files, devices for zerofree, partitions skipped) for BOOT and rootfs."""
    fill, free, skip = [], [], []
    for p in partitions(disk):
        if p.get("label") not in ZERO_LABELS or p.get("fstype") not in ZERO_FSTYPES:
            continue
        mounts = [m for m in p.get("mountpoints") or [] if m]
        if p["fstype"] == "ext4" and zerofree:
            free.append(p["path"])
        elif mounts:
            fill.append(mounts[0])
        else:
            skip.append(p["path"])
    return fill, free, skip


def progress(what, done, total, _last=[0.0]):
    now = time.monotonic()
    if now - _last[0] < 2 and (total is None or done < total):
        return
    _last[0] = now
    pct = f" {100 * done / total:5.1f}%" if total else ""
    print(f"\r{what}: {done / 1e9:7.2f} GB{pct}", end="", file=sys.stderr, flush=True)


def compressor(image, decompress=False):
    for suffix, cmds in COMPRESSORS.items():
        if str(image).endswith(suffix):
            return cmds[decompress]
    fail(f"{image}: suffix must be one of {', '.join(COMPRESSORS)}")


def copy_out(source, length, image):
    """Reads `length` bytes of source through the image's compressor into image; returns their SHA-256."""
    digest, done = hashlib.sha256(), 0
    with open(source, "rb", buffering=0) as src, open(image, "wb") as out:
        proc = subprocess.Popen(compressor(image), stdin=subprocess.PIPE, stdout=out)
        while done < length:
            chunk = src.read(min(CHUNK, length - done))
            if not chunk:
                fail(f"{source} ended at {done} bytes, expected {length}")
            digest.update(chunk)
            proc.stdin.write(chunk)
            done += len(chunk)
            progress("read", done, length)
        proc.stdin.close()
        if proc.wait():
            fail(f"{compressor(image)[0]} failed")
    print(file=sys.stderr)
    return digest.hexdigest()


def copy_in(image, target, length):
    """Decompresses image onto target; returns the SHA-256 of what was written."""
    digest, done = hashlib.sha256(), 0
    proc = subprocess.Popen(compressor(image, decompress=True) + [str(image)], stdout=subprocess.PIPE)
    with open(target, "r+b" if pathlib.Path(target).exists() else "wb", buffering=0) as dst:
        while chunk := proc.stdout.read(CHUNK):
            digest.update(chunk)
            dst.write(chunk)
            done += len(chunk)
            progress("write", done, length)
        os.fsync(dst.fileno())
    if proc.wait():
        fail(f"{compressor(image, True)[0]} failed")
    if done != length:
        fail(f"{image} held {done} bytes, its sidecar says {length}")
    print(file=sys.stderr)
    return digest.hexdigest()


def read_back(target, length):
    digest, done = hashlib.sha256(), 0
    with open(target, "rb", buffering=0) as src:
        while done < length:
            chunk = src.read(min(CHUNK, length - done))
            if not chunk:
                break
            digest.update(chunk)
            done += len(chunk)
            progress("verify", done, length)
    print(file=sys.stderr)
    return digest.hexdigest()


def sidecar(image):
    return pathlib.Path(f"{image}.json")


def give_back(*paths):
    """Files made under sudo belong to the user who ran it."""
    uid, gid = os.environ.get("SUDO_UID"), os.environ.get("SUDO_GID")
    if uid and gid:
        for p in paths:
            os.chown(p, int(uid), int(gid))


def unmount(disk):
    for part in partitions(disk):
        for m in part.get("mountpoints") or []:
            if m:
                subprocess.run(["umount", m], check=True)


def confirm(prompt, yes):
    if yes:
        return
    if input(f"{prompt} [y/N] ").strip().lower() != "y":
        fail("stopped")


def describe(disk):
    print(f"{disk['path']}: {disk.get('model') or ''} {disk['size'] / 1e9:.2f} GB")
    for p in partitions(disk):
        mounts = ", ".join(m for m in p.get("mountpoints") or [] if m)
        print(f"  {p['name']:8} {p['size'] / 1e9:8.3f} GB  {p.get('fstype') or '-':5} {p.get('label') or '':8} {mounts}")


def create(args):
    disk = lsblk(args.device)
    check_disk(disk)
    describe(disk)
    length = image_length(f"/sys/block/{disk['name']}")
    confirm(f"image {length / 1e9:.2f} GB of {disk['path']} to {args.image}"
            f"{'' if args.no_zero else ', zeroing the free space of BOOT and rootfs first'}?", args.yes)
    compressor(args.image)
    fill, free, skip = zero_plan(disk, shutil.which("zerofree")) if not args.no_zero else ([], [], [])
    for path in skip:
        print(f"{path} is not mounted: its free space stays as it is")
    for mount in fill:
        print(f"zeroing the free space of {mount}")
        print(f"\n  {zero_fill(mount) / 1e9:.2f} GB", file=sys.stderr)
    unmount(disk)
    for dev in free:
        print(f"zerofree {dev}")
        subprocess.run(["zerofree", "-v", dev], check=True)
    sha = copy_out(args.device, length, args.image)
    sidecar(args.image).write_text(json.dumps({"size": length, "sha256": sha, "source": disk["path"],
                                               "source_size": disk["size"], "model": disk.get("model")}, indent=2))
    give_back(args.image, sidecar(args.image))
    print(f"{args.image}: {pathlib.Path(args.image).stat().st_size / 1e9:.2f} GB, raw {length / 1e9:.2f} GB, sha256 {sha}")


def write(args):
    meta = json.loads(sidecar(args.image).read_text())
    disk = lsblk(args.device)
    check_disk(disk)
    describe(disk)
    if disk["size"] < meta["size"]:
        fail(f"{disk['path']} holds {disk['size']} bytes; the image needs {meta['size']}")
    confirm(f"ERASE {disk['path']} and write {args.image} ({meta['size'] / 1e9:.2f} GB)?", args.yes)
    unmount(disk)
    if copy_in(args.image, args.device, meta["size"]) != meta["sha256"]:
        fail(f"{args.image} does not match its sidecar's SHA-256")
    if read_back(args.device, meta["size"]) != meta["sha256"]:
        fail(f"{disk['path']} reads back different from the image")
    subprocess.run(["sgdisk", "-e", args.device], check=True)  # the backup GPT to the card's end
    subprocess.run(["partprobe", args.device], check=False)
    print(f"{disk['path']}: written and verified")


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    sub = parser.add_subparsers(dest="command", required=True)
    c = sub.add_parser("create", help="image a card")
    c.add_argument("device")
    c.add_argument("image")
    c.add_argument("--no-zero", action="store_true", help="do not zero the free space first")
    c.add_argument("--yes", action="store_true", help="do not ask")
    w = sub.add_parser("write", help="write an image to a card")
    w.add_argument("image")
    w.add_argument("device")
    w.add_argument("--yes", action="store_true", help="do not ask")
    args = parser.parse_args(argv)
    if os.geteuid() != 0:
        fail("run as root: sudo python3 scripts/ssp_card_image.py ...")
    (create if args.command == "create" else write)(args)


if __name__ == "__main__":
    main()
