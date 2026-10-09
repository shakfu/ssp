#!/usr/bin/env python3
"""Package the SSP plugins as releases/shakfu-ssp-plugins-<VERSION>/ and a zip of it.

BOOT/ mirrors the SD card's BOOT partition: copying its contents onto the card installs every
module and the data it reads. docs/<module>/ holds each plugin's README and CHANGELOG, with links
into the repo pointing at the release tag. With --pdf, those docs are PDFs rendered by quarto.
Run by `make release` and `make release-pdf`, after `make` and `make deps`.
"""

import argparse
import concurrent.futures
import dataclasses
import os
import pathlib
import re
import shutil
import subprocess
import sys
import zipfile
from typing import Callable

ROOT = pathlib.Path(__file__).resolve().parents[1]
REPO_URL = "https://github.com/shakfu/ssp"

# what a module reads from BOOT: (source in the repo, folder under BOOT, files to take)
DATA = {
    "fstr": [("plugins/faust/examples", "faust", "*.dsp"), ("{deps}/share/faust", "faust/libraries", "**")],
    "csnd": [("plugins/csound/examples", "csound", "*")],
    "chuk": [("plugins/chuck/examples", "chuck", "*")],
    "rack": [("presets", "rack_presets", "*")],
}

# folders a module reads that the user fills
USER_DATA = {
    "rdio": ("radio", "a Radio Music library: banks of WAV or .raw files"),
    "bard": ("bard", "spoken-word recordings, one folder per shelf"),
    "strc": ("pstretch", "clips to stretch"),
    "sfct": ("samples", "WAV and AIFF files to load"),
}

LICENSES = {"gltc": "GPL-3.0", "sfct": "GPL-3.0, for softcut"}

SK_ENGINES = "https://github.com/shakfu/sk-engines"

# where each module comes from; most are sk-engines engines
ORIGINS = {
    "bard": f"[sk-engines]({SK_ENGINES})",
    "chrs": f"[sk-engines]({SK_ENGINES}) (DSP)",
    "chuk": f"[sk-engines]({SK_ENGINES})",
    "csnd": f"[sk-engines]({SK_ENGINES})",
    "edrm": f"[sk-engines]({SK_ENGINES})",
    "gltc": f"[sk-engines]({SK_ENGINES})",
    "rdio": f"[sk-engines]({SK_ENGINES})",
    "strc": f"[sk-engines]({SK_ENGINES})",
    "rack": "[TheTechnobear's trax](https://github.com/TheTechnobear/SSP)",
    "sfct": "[monome softcut](https://github.com/monome/softcut-lib)",
    "fstr": "new",
}


PACKAGE = "shakfu-ssp-plugins"


def tag(version: str) -> str:
    """A release's git tag: the bare version, as 0.1.0's was. The repo already says whose it is."""
    return version


def package_name(version: str) -> str:
    """The release folder, zip and notes; these leave GitHub, so the name says whose they are."""
    return f"{PACKAGE}-{version}"


@dataclasses.dataclass
class Plugin:
    dir: pathlib.Path
    product: str
    version: str
    description: str


def find_plugins(root: pathlib.Path) -> list[Plugin]:
    """Every plugin with a PRODUCT_NAME, from its CMakeLists.txt."""
    plugins = []
    for cmake in sorted((root / "plugins").glob("*/CMakeLists.txt")):
        text = cmake.read_text()
        product = re.search(r'PRODUCT_NAME "([^"]+)"', text)
        if product is None:
            continue
        version = re.search(r"project\(\w+ VERSION ([\d.]+)", text)
        description = re.search(r'DESCRIPTION "([^"]*)"', text)
        plugins.append(Plugin(cmake.parent, product.group(1), version.group(1) if version else "",
                              description.group(1) if description else ""))
    return plugins


LINK = re.compile(r"\]\(([^)\s#]+)(#[^)\s]*)?\)")


def rewrite_links(text: str, src_dir: pathlib.Path, kept: set[str], tag: str, root: pathlib.Path) -> str:
    """Links to files copied alongside stay; other repo links point at the tag on GitHub."""
    def fix(m: re.Match) -> str:
        target, anchor = m.group(1), m.group(2) or ""
        if re.match(r"[a-z][a-z0-9+.-]*:", target) or target in kept:
            return m.group(0)
        path = (src_dir / target).resolve()
        try:
            rel = path.relative_to(root.resolve()).as_posix()
        except ValueError:
            return m.group(0)
        kind = "tree" if path.is_dir() else "blob"
        return f"]({REPO_URL}/{kind}/{tag}/{rel}{anchor})"
    return LINK.sub(fix, text)


def copy_docs(src_dir: pathlib.Path, dest_dir: pathlib.Path, names: list[str], tag: str, root: pathlib.Path) -> None:
    """Copies the named files that exist; Markdown gets its links rewritten."""
    dest_dir.mkdir(parents=True, exist_ok=True)
    present = [n for n in names if (src_dir / n).is_file()]
    for name in present:
        src = src_dir / name
        if src.suffix == ".md":
            (dest_dir / name).write_text(rewrite_links(src.read_text(), src_dir, set(present), tag, root))
        else:
            shutil.copy2(src, dest_dir / name)


def built_so(build_dir: pathlib.Path, plugin: Plugin) -> pathlib.Path | None:
    found = sorted(build_dir.glob(f"plugins/{plugin.dir.name}/*_artefacts/Release/VST3/"
                                  f"{plugin.product}.vst3/Contents/*/{plugin.product}.so"))
    return found[0] if found else None


def readme(version: str, tag: str, plugins: list[Plugin]) -> str:
    rows = "\n".join(f"| `{p.product}` | {p.version} | {p.description} | {ORIGINS.get(p.product, '')} "
                     f"| [docs/{p.product}](docs/{p.product}/README.md) |" for p in plugins)
    ported = ", ".join(f"`{m}`" for m, o in ORIGINS.items() if o == f"[sk-engines]({SK_ENGINES})")
    data_rows = [("plugins", "all", "the modules")]
    for product, sources in DATA.items():
        for _, folder, _ in sources:
            if "/" not in folder:
                data_rows.append((folder, product, {"faust": "example programs; `libraries/` holds the Faust "
                                                             "libraries that `import(\"stdfaust.lib\")` needs",
                                                    "csound": "example orchestras", "chuck": "example programs",
                                                    "rack_presets": "presets"}[folder]))
    data = "\n".join(f"| `{f}` | {m if m == 'all' else f'`{m}`'} | {w} |" for f, m, w in data_rows)
    user = "\n".join(f"| `{f}` | `{m}` | {w} |" for m, (f, w) in USER_DATA.items())
    licences = "; ".join(f"`{m}` is {l}" for m, l in LICENSES.items())
    return f"""# {PACKAGE} {version}

Modules for the [Percussa SSP](https://www.percussa.com/), built from [shakfu/ssp]({REPO_URL}) at tag `{tag}`.

Most are ports of engines from [sk-engines]({SK_ENGINES}), a platform fork of the Synthux Spotykach firmware that swaps DSP engines on the same hardware: {ported}, and the DSP of `chrs`.

| Module | Version | What | From | Docs |
|-|-|-|-|-|
{rows}

## Install

1. Turn off the SSP and mount its SD card on a computer.
2. Copy the contents of `BOOT/` onto the card's BOOT partition, the one holding `plugins/`. Modules with the same names are replaced.
3. Put the card back and turn the SSP on. Synthor lists each module by its four-letter name.

## What BOOT holds

| Folder | For | Contents |
|-|-|-|
{data}

Folders a module reads that you fill:

| Folder | For | Contents |
|-|-|-|
{user}

## Requirements

- `fstr` uses the SSP's own `/usr/lib/libLLVM-9.so`, part of its system.

## Licence

AGPL-3.0; see [LICENSE](LICENSE). {licences}; see each one's docs. Changes: [CHANGELOG.md](CHANGELOG.md).
"""


MD_LINK = re.compile(r"\]\(([^)\s:#]+)\.md(#[^)\s]*)?\)")


def md_links_to_pdf(text: str) -> str:
    """Relative links to Markdown files point at their PDFs; a PDF has no anchors to keep."""
    return MD_LINK.sub(lambda m: f"]({m.group(1)}.pdf)", text)


def quarto_pdf(md: pathlib.Path) -> None:
    """Renders md to a PDF beside it; quarto and a LaTeX engine must be installed."""
    r = subprocess.run(["quarto", "render", md.name, "--to", "pdf"], cwd=md.parent, capture_output=True, text=True)
    if r.returncode != 0 or not md.with_suffix(".pdf").is_file():
        raise SystemExit(f"quarto could not render {md}:\n{r.stderr[-2000:]}")


def to_pdf(dest: pathlib.Path, skip: list[pathlib.Path], render: Callable[[pathlib.Path], None]) -> None:
    """Replaces each Markdown file under dest, outside the folders in skip, with a PDF."""
    docs = [md for md in sorted(dest.rglob("*.md")) if not any(md.is_relative_to(s) for s in skip)]
    for md in docs:
        md.write_text(md_links_to_pdf(md.read_text()))
    with concurrent.futures.ThreadPoolExecutor(max_workers=os.cpu_count() or 1) as pool:
        list(pool.map(render, docs))
    for md in docs:
        md.unlink()


def package(root: pathlib.Path, build_dir: pathlib.Path, deps_dir: pathlib.Path, out_dir: pathlib.Path,
            version: str, strip: str | None, pdf: Callable[[pathlib.Path], None] | None = None) -> pathlib.Path:
    """Builds out_dir/shakfu-ssp-plugins-<version>/ and its zip; returns the zip's path. With pdf, the
    package's Markdown is rendered to PDF by that function and only the PDFs are kept."""
    release_tag = tag(version)
    plugins = find_plugins(root)
    missing = [p.product for p in plugins if built_so(build_dir, p) is None]
    if missing:
        raise SystemExit(f"not built in {build_dir}: {', '.join(missing)} (run make, and make deps for csnd and chuk)")

    dest = out_dir / package_name(version)
    if dest.exists():
        shutil.rmtree(dest)
    boot = dest / "BOOT"
    (boot / "plugins").mkdir(parents=True)
    copied_whole = []  # third-party trees, such as the Faust libraries: their Markdown is theirs

    for p in plugins:
        so = boot / "plugins" / f"{p.product}.so"
        shutil.copy2(built_so(build_dir, p), so)
        if strip:
            subprocess.run([strip, "--strip-unneeded", str(so)], check=True)
        # what a user reads; TODO.md and design notes stay in the repo
        names = sorted(f.name for f in p.dir.iterdir()
                       if f.is_file() and f.name.startswith(("README", "CHANGELOG", "LICENSE", "NOTICE")))
        copy_docs(p.dir, dest / "docs" / p.product, names, release_tag, root)

    for sources in DATA.values():
        for src, folder, pattern in sources:
            src_dir = pathlib.Path(src.format(deps=deps_dir))
            src_dir = src_dir if src_dir.is_absolute() else root / src_dir
            if not src_dir.is_dir():
                raise SystemExit(f"missing {src_dir}")
            target = boot / folder
            if pattern == "**":
                shutil.copytree(src_dir, target)
                copied_whole.append(target)
                continue
            names = sorted(f.name for f in src_dir.glob(pattern) if f.is_file())
            copy_docs(src_dir, target, names, release_tag, root)

    shutil.copy2(root / "LICENSE", dest / "LICENSE")
    softcut = root / "external" / "softcut-lib" / "LICENSE.txt"
    if softcut.is_file():
        shutil.copy2(softcut, dest / "docs" / "sfct" / "softcut-LICENSE.txt")
    copy_docs(root, dest, ["CHANGELOG.md"], release_tag, root)
    (dest / "README.md").write_text(readme(version, release_tag, plugins))
    if pdf:
        to_pdf(dest, copied_whole, pdf)

    archive = out_dir / f"{package_name(version)}.zip"
    with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED) as z:
        for path in sorted(dest.rglob("*")):
            z.write(path, path.relative_to(out_dir))
    return archive


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--build", default=os.environ.get("BUILD_DIR", str(ROOT / "build.cmake.ssp")))
    parser.add_argument("--deps", default=str(ROOT / "build" / "deps" / "ssp"))
    parser.add_argument("--out", default=str(ROOT / "releases"))
    parser.add_argument("--version", default=(ROOT / "VERSION").read_text().strip())
    parser.add_argument("--pdf", action="store_true", help="package the docs as PDFs, rendered by quarto")
    args = parser.parse_args()
    if args.pdf and not shutil.which("quarto"):
        print("--pdf needs quarto, with a LaTeX engine (quarto install tinytex)", file=sys.stderr)
        return 1
    strip = os.environ.get("STRIP") or shutil.which("llvm-strip") or shutil.which("arm-linux-gnueabihf-strip")
    if not strip:
        print("no llvm-strip or arm-linux-gnueabihf-strip found; set STRIP", file=sys.stderr)
        return 1
    archive = package(ROOT, pathlib.Path(args.build), pathlib.Path(args.deps), pathlib.Path(args.out), args.version, strip,
                      quarto_pdf if args.pdf else None)
    print(archive)
    return 0


if __name__ == "__main__":
    sys.exit(main())
