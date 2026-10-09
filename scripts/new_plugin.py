#!/usr/bin/env python3
"""Create plugins/<dir>/ from a guide example: examples/svca (C++) or, with --faust, examples/tremolo.

The copy builds as it is: a stereo VCA, or a stereo tremolo whose kernel header is the example's,
renamed. Adds the folder to plugins/CMakeLists.txt and, for Faust, its kernel to `make faust-kernels`.
See docs/CPP_PLUGINS.md and docs/FAUST_PLUGINS.md.
"""

import argparse
import pathlib
import re
import shutil
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import release  # noqa: E402

DIR_RE = re.compile(r"[a-z][a-z0-9_]*")
NAME_RE = re.compile(r"[a-z][a-z0-9]{3}")  # JUCE wants a four-character code, and Synthor the code to spell the name

COMMON_EXAMPLE = 'set(COMMON "${PROJECT_SOURCE_DIR}/../../plugins/common")'
COMMON_PLUGIN = 'set(COMMON "${PROJECT_SOURCE_DIR}/../common")'
COMMON_COMMENT = "# a plugin in plugins/<name> uses ../common; this one lives in examples/\n"


def camel(d: str) -> str:
    return "".join(part.capitalize() for part in d.split("_"))


def replace(text: str, pairs: list[tuple[str, str]], where: str) -> str:
    """Every template token must be present, so a changed example fails here, not in the build."""
    for old, new in pairs:
        if old not in text:
            raise SystemExit(f"new_plugin: {where} no longer contains {old!r}; update scripts/new_plugin.py")
        text = text.replace(old, new)
    return text


def cmake(text: str, project: str, code: str, name: str, description: str, old: tuple[str, str, str, str]) -> str:
    o_project, o_code, o_name, o_description = old
    text = replace(text, [(COMMON_COMMENT, ""), (COMMON_EXAMPLE, COMMON_PLUGIN),
                          (f"PLUGIN_CODE {o_code}", f"PLUGIN_CODE {code}"),
                          (f'PRODUCT_NAME "{o_name}"', f'PRODUCT_NAME "{name}"'),
                          (f'DESCRIPTION "{o_description}"', f'DESCRIPTION "{description}"')], "CMakeLists.txt")
    # after PLUGIN_CODE, which can equal the project name
    return re.sub(rf"\b{o_project}\b", project, text)


def scaffold_cpp(src: pathlib.Path, dest: pathlib.Path, d: str, name: str, description: str) -> None:
    eng = f"{camel(d)}Engine"
    (dest / "Source").mkdir(parents=True)
    tokens = [("SvcaEngine", eng), ("namespace svca", f"namespace {d}"), ("svca::", f"{d}::")]
    (dest / "Source" / f"{eng}.h").write_text(replace((src / "Source/SvcaEngine.h").read_text(), tokens[:2], "SvcaEngine.h"))
    (dest / "Source/PluginProcessor.h").write_text(
        replace((src / "Source/PluginProcessor.h").read_text(), [tokens[0], tokens[2]], "PluginProcessor.h"))
    (dest / "Source/PluginProcessor.cpp").write_text(
        replace((src / "Source/PluginProcessor.cpp").read_text(), [tokens[0], tokens[2], ('{ "vca",', f'{{ "{name}",')],
                "PluginProcessor.cpp"))
    shutil.copy2(src / "Source/SSPApi.cpp", dest / "Source/SSPApi.cpp")
    (dest / "CMakeLists.txt").write_text(cmake((src / "CMakeLists.txt").read_text(), d.upper(), name.upper(), name,
                                               description, ("SVCA", "SVCA", "svca", "stereo VCA")))


def scaffold_faust(src: pathlib.Path, dest: pathlib.Path, d: str, name: str, description: str) -> None:
    kernel = f"{camel(d)}Kernel.h"
    (dest / "Source").mkdir(parents=True)
    (dest / "Source" / f"{d}.dsp").write_text(
        replace((src / "Source/tremolo.dsp").read_text(), [('declare name "tremolo";', f'declare name "{d}";')], "tremolo.dsp"))
    (dest / "Source" / kernel).write_text(
        replace((src / "Source/TremoloKernel.h").read_text(),
                [("from tremolo.dsp", f"from {d}.dsp"), ("namespace ssp::faust::tremolo", f"namespace ssp::faust::{d}"),
                 ('"tremolo"', f'"{d}"')],  # metadata and box label, as a regenerated kernel has them
                "TremoloKernel.h"))
    (dest / "Source/PluginProcessor.h").write_text(
        replace((src / "Source/PluginProcessor.h").read_text(),
                [("TremoloKernel.h", kernel), ("ssp::faust::tremolo::", f"ssp::faust::{d}::")], "PluginProcessor.h"))
    shutil.copy2(src / "Source/SSPApi.cpp", dest / "Source/SSPApi.cpp")
    (dest / "CMakeLists.txt").write_text(cmake((src / "CMakeLists.txt").read_text(), d.upper(), name.upper(), name,
                                               description,
                                               ("TREMOLO", "TREM", "trem", "stereo tremolo, compiled from Faust")))


def docs(dest: pathlib.Path, d: str, name: str, description: str) -> None:
    (dest / "README.md").write_text(f"# {d}\n\n`{d}` is a {description}.\n\n## Install\n\n"
                                    f"Copy `{name}.so` to the `plugins` folder on the SD card. The module is `{name}`.\n")
    (dest / "CHANGELOG.md").write_text(
        f"# Changelog\n\n{d} follows [semantic versioning](https://semver.org). Until 1.0.0, a minor version may change "
        "parameters, so presets saved with an earlier version can restore differently.\n\n"
        f"The version is set in `CMakeLists.txt` (`project({d.upper()} VERSION ...)`) and shown at the top right of "
        "the screen.\n\n## [Unreleased]\n\n- First version.\n")


def register(root: pathlib.Path, d: str, faust: bool) -> None:
    """add_subdirectory after the last unconditional one; for Faust, a line in make faust-kernels."""
    lists = root / "plugins/CMakeLists.txt"
    lines = lists.read_text().splitlines(keepends=True)
    last = max(i for i, line in enumerate(lines) if line.startswith("add_subdirectory("))
    lines.insert(last + 1, f"add_subdirectory({d})\n")
    lists.write_text("".join(lines))
    if faust:
        mk = root / "Makefile"
        text = mk.read_text()
        target = "faust-kernels:\n"
        if target not in text:
            raise SystemExit("new_plugin: no faust-kernels target in the Makefile")
        start = text.index(target) + len(target)
        end = start
        while text.startswith("\t", end):
            end = text.index("\n", end) + 1
        rule = f"\tscripts/faust_kernel.sh plugins/{d}/Source/{d}.dsp plugins/{d}/Source/{camel(d)}Kernel.h {d}\n"
        mk.write_text(text[:end] + rule + text[end:])


def main(argv: list[str] | None = None, root: pathlib.Path = ROOT) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("dir", help="folder under plugins/, and the C++ namespace: lower case, [a-z0-9_]")
    ap.add_argument("name", help="module name: four characters, [a-z0-9], starting with a letter")
    ap.add_argument("--faust", action="store_true", help="a compiled Faust module, from examples/tremolo")
    ap.add_argument("--description", help="shown on the editor's title line")
    args = ap.parse_args(argv)

    d, name = args.dir, args.name
    if not DIR_RE.fullmatch(d):
        raise SystemExit(f"new_plugin: dir {d!r} must be lower case [a-z0-9_], starting with a letter")
    if not NAME_RE.fullmatch(name):
        raise SystemExit(f"new_plugin: name {name!r} must be four characters, [a-z0-9], starting with a letter")
    dest = root / "plugins" / d
    if dest.exists():
        raise SystemExit(f"new_plugin: {dest} exists")
    taken = {p.product.lower(): p.dir.name for p in release.find_plugins(root)}
    if name in taken:
        raise SystemExit(f"new_plugin: plugins/{taken[name]} is already {name}")

    description = args.description or ("stereo tremolo, compiled from Faust" if args.faust else "stereo VCA")
    try:
        if args.faust:
            scaffold_faust(root / "examples/tremolo", dest, d, name, description)
        else:
            scaffold_cpp(root / "examples/svca", dest, d, name, description)
        docs(dest, d, name, description)
    except BaseException:
        shutil.rmtree(dest, ignore_errors=True)
        raise
    register(root, d, args.faust)

    print(f"created plugins/{d} ({name}); added to plugins/CMakeLists.txt" + (" and make faust-kernels" if args.faust else ""))
    print("next:")
    if args.faust:
        print(f"  edit plugins/{d}/Source/{d}.dsp, then make faust-kernels")
    else:
        print(f"  edit plugins/{d}/Source/{camel(d)}Engine.h and PluginProcessor.cpp")
    print(f'  add "{d}": "{name}" to PRODUCTS in plugins/common/tests/test_engine_plugins.py, and a test')
    print("  UPDATE_MANIFEST=1 uv run --with pytest pytest plugins/common/tests -k manifest")
    print(f"  make && make install MOD={name}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
