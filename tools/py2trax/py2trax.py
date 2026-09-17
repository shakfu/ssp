#!/usr/bin/env python3
"""Offline generator and inspector for trax preset files.

trax stores its presets as JUCE binary-wrapped XML (see PluginProcessor::savePreset).
This turns a hand-written or generated JSON description into that file, so a patch can
be authored on a desktop instead of built encoder-by-encoder on the SSP.

Schema, commands and limits: see README.md.
"""

from __future__ import annotations

import argparse
import json
import pathlib
import re
import struct
import sys
import xml.etree.ElementTree as ET

# Track::ModuleIdx - slot 0 is the track input, slot 9 the track output.
M_IN, M_OUT, M_MAX = 0, 9, 10
MAX_TRACKS = 4
MAX_SLOT = M_OUT - 1

MAGIC = b"VC2!"  # AudioProcessor::copyXmlToBinary magicXmlNumber, little-endian
XML_HEADER = '<?xml version="1.0" encoding="UTF-8"?> '

# MemoryBlock::toBase64Encoding - JUCE's own table, not RFC 4648.
B64_TABLE = ".ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+"
B64_DECODE = {c: i for i, c in enumerate(B64_TABLE)}


class PresetError(Exception):
    pass


# --- JUCE MemoryBlock base64 -------------------------------------------------


def b64_decode(text: str) -> bytes:
    """Inverse of MemoryBlock::toBase64Encoding: "<size>.<chars>", 6 bits per char, LSB first."""
    size_text, dot, body = text.partition(".")
    if not dot:
        raise PresetError("not a JUCE base64 block: missing '.' separator")
    out = bytearray(int(size_text))
    pos = 0
    for char in body:
        value = B64_DECODE.get(char)
        if value is None:
            continue
        _set_bits(out, pos, value)
        pos += 6
    return bytes(out)


def b64_encode(data: bytes) -> str:
    n_chars = ((len(data) << 3) + 5) // 6
    chars = [B64_TABLE[_get_bits(data, i * 6)] for i in range(n_chars)]
    return f"{len(data)}." + "".join(chars)


def _set_bits(buf: bytearray, start: int, value: int) -> None:
    byte, offset, remaining = start >> 3, start & 7, 6
    while remaining > 0 and byte < len(buf):
        count = min(remaining, 8 - offset)
        mask = ((0xFF >> (8 - count)) << offset) & 0xFF
        buf[byte] = (buf[byte] & ~mask & 0xFF) | ((value << offset) & mask)
        byte += 1
        remaining -= count
        value >>= count
        offset = 0


def _get_bits(data: bytes, start: int) -> int:
    result, byte, offset, remaining, shift = 0, start >> 3, start & 7, 6, 0
    while remaining > 0 and byte < len(data):
        count = min(remaining, 8 - offset)
        mask = ((0xFF >> (8 - count)) << offset) & 0xFF
        result |= ((data[byte] & mask) >> offset) << shift
        shift += count
        remaining -= count
        byte += 1
        offset = 0
    return result


# --- copyXmlToBinary envelope ------------------------------------------------


def wrap_xml(xml: str) -> bytes:
    payload = (XML_HEADER + xml).encode("utf8")
    return MAGIC + struct.pack("<I", len(payload)) + payload + b"\0"


def unwrap_xml(blob: bytes) -> str:
    start = blob.find(MAGIC)
    if start < 0:
        raise PresetError("no JUCE XML envelope found (magic 'VC2!' absent)")
    length = struct.unpack_from("<I", blob, start + 4)[0]
    return blob[start + 8 : start + 8 + length].decode("utf8")


def extract_state(blob: bytes) -> bytes:
    """Peel a VST3 wrapper if present, so a .filtergraph payload decodes like a raw preset.

    Both layers carry the same envelope, so the wrapper is recognised by its content.
    """
    inner = re.search(r"<IComponent>(.*?)</IComponent>", unwrap_xml(blob), re.S)
    return extract_state(b64_decode(inner.group(1).strip())) if inner else blob


# --- slots, wires, values ----------------------------------------------------


def slot_index(name) -> int:
    key = str(name).strip().lower()
    if key == "in":
        return M_IN
    if key == "out":
        return M_OUT
    if not key.isdigit() or not 1 <= int(key) <= MAX_SLOT:
        raise PresetError(f"bad slot {name!r}: expected 'in', 'out' or 1-{MAX_SLOT}")
    return int(key)


def slot_name(index: int) -> str:
    return {M_IN: "in", M_OUT: "out"}.get(index, str(index))


def parse_jack(text: str) -> tuple[int, int]:
    slot, sep, channel = str(text).partition(":")
    if not sep or not channel.strip().isdigit():
        raise PresetError(f"bad jack {text!r}: expected '<slot>:<channel>', e.g. '1:0'")
    return slot_index(slot), int(channel)


def format_jack(slot: int, channel: int) -> str:
    return f"{slot_name(slot)}:{channel}"


WIRE_RE = re.compile(r"^\s*(.+?)\s*(?:->|=>)\s*(.+?)\s*$")


def parse_wire(wire) -> dict:
    if isinstance(wire, str):
        match = WIRE_RE.match(wire)
        if not match:
            raise PresetError(f"bad wire {wire!r}: expected '<slot>:<ch> -> <slot>:<ch>'")
        wire = {"from": match.group(1), "to": match.group(2)}
    if not isinstance(wire, dict) or "from" not in wire or "to" not in wire:
        raise PresetError(f"bad wire {wire!r}: need 'from' and 'to'")
    src_mod, src_ch = parse_jack(wire["from"])
    dest_mod, dest_ch = parse_jack(wire["to"])
    return {
        "srcMod": src_mod,
        "srcCh": src_ch,
        "destMod": dest_mod,
        "destCh": dest_ch,
        "gain": wire.get("gain", 1.0),
        "offset": wire.get("offset", 0.0),
    }


def format_number(value) -> str:
    """Values pass through as strings so a decoded preset re-encodes unchanged."""
    if isinstance(value, str):
        return value
    if isinstance(value, bool):
        return "1.0" if value else "0.0"
    return repr(float(value))


# --- parameter manifest ------------------------------------------------------

PARAM_RE = re.compile(
    r"make_unique<\s*ssp::Base(Float|Bool|Choice|Int)Parameter\s*>\s*\(\s*ID::(\w+)\s*,\s*\"([^\"]*)\""
)
PARAM_ID_RE = re.compile(r"PARAMETER_ID\s*\(\s*(\w+)\s*\)")
COMPOSED_RE = re.compile(r"make_unique<\s*ssp::Base\w+Parameter\s*>\s*\(\s*(?!ID::\w+\s*,\s*\")")


def scan_modules(root: pathlib.Path) -> dict:
    """Map parameter name -> id per module, by reading the plugin sources.

    Only the literal `ID::x, "Name"` form is recoverable this way. Parameters whose id is
    built at runtime (getPID(), per-layer groups) are counted as skipped, not guessed.
    """
    manifest = {}
    for source in sorted(root.glob("*/Source/PluginProcessor.cpp")):
        module = source.parent.parent.name
        text = source.read_text(errors="replace")
        header = source.with_suffix(".h")
        declared = set(PARAM_ID_RE.findall(header.read_text(errors="replace"))) if header.exists() else set()

        params, unresolved = {}, []
        for _kind, symbol, name in PARAM_RE.findall(text):
            if declared and symbol not in declared:
                unresolved.append(symbol)
                continue
            params[name] = symbol
        skipped = len(COMPOSED_RE.findall(text)) + len(unresolved)
        manifest[module] = {"params": params, "skipped": skipped}
    return manifest


def resolve_param(module: str, key: str, manifest: dict | None) -> str:
    if manifest is None:
        return key
    entry = manifest.get(module)
    if entry is None:
        raise PresetError(f"module {module!r} is not in the manifest")
    params = entry["params"]
    if key in params:
        return params[key]
    ids = set(params.values())
    if key in ids:
        return key
    lowered = {name.lower(): pid for name, pid in params.items()}
    if key.lower() in lowered:
        return lowered[key.lower()]
    hint = f" ({entry['skipped']} parameters could not be read from source)" if entry["skipped"] else ""
    raise PresetError(f"module {module!r} has no parameter named or id'd {key!r}{hint}")


# --- encode ------------------------------------------------------------------


def encode(doc: dict, manifest: dict | None = None) -> bytes:
    trax = ET.Element("TRAX")
    tracks = ET.SubElement(trax, "Tracks")
    for track in _tracks_of(doc):
        _encode_track(ET.SubElement(tracks, "Track"), track, manifest)

    performance = ET.SubElement(trax, "PerformParams")
    for entry in doc.get("performance", []):
        ET.SubElement(
            performance,
            "Param",
            {
                "track": str(int(entry["track"]) - 1),
                "module": str(slot_index(entry["slot"])),
                "param": str(int(entry["param"])),
            },
        )
    return wrap_xml(ET.tostring(trax, encoding="unicode"))


def _tracks_of(doc: dict) -> list:
    """Accept a list of tracks, or a map keyed by 1-based track number with gaps."""
    tracks = doc.get("tracks", [])
    if isinstance(tracks, dict):
        indexed = {}
        for key, track in tracks.items():
            number = int(key)
            if not 1 <= number <= MAX_TRACKS:
                raise PresetError(f"bad track {key!r}: expected 1-{MAX_TRACKS}")
            indexed[number] = track
        tracks = [indexed.get(n, {}) for n in range(1, MAX_TRACKS + 1)]
    if len(tracks) > MAX_TRACKS:
        raise PresetError(f"{len(tracks)} tracks: trax has {MAX_TRACKS}")
    return list(tracks) + [{}] * (MAX_TRACKS - len(tracks))


def _encode_track(element: ET.Element, track: dict, manifest: dict | None) -> None:
    element.set("mute", "1" if track.get("mute", False) else "0")
    element.set("level", format_number(track.get("level", 1.0)))

    modules = {slot_index(k): v for k, v in track.get("modules", {}).items()}
    params = {slot_index(k): v for k, v in track.get("params", {}).items()}
    for slot in params:
        if slot not in modules:
            raise PresetError(f"params for slot {slot_name(slot)} but no module loaded there")

    xml_modules = ET.SubElement(element, "Modules")
    for slot in range(M_MAX):
        if slot in (M_IN, M_OUT):
            ET.SubElement(xml_modules, "Module", {"pluginName": "IN" if slot == M_IN else "OUT", "dataSz": "0"})
            continue
        name = modules.get(slot, "")
        if not name:
            ET.SubElement(xml_modules, "Module", {"pluginName": "", "dataSz": "0"})
            continue
        # Track::setStateInformation only loads a module when dataSz > 0, so every
        # occupied slot carries a <data> block even when no parameters are set.
        state = _module_state(name, params.get(slot, {}), manifest)
        blob = wrap_xml(ET.tostring(state, encoding="unicode"))
        xml_module = ET.SubElement(xml_modules, "Module", {"pluginName": name, "dataSz": str(len(blob))})
        ET.SubElement(xml_module, "data").append(state)

    xml_matrix = ET.SubElement(element, "Matrix")
    for wire in track.get("wires", []):
        parsed = parse_wire(wire)
        for jack in ("src", "dest"):
            if parsed[jack + "Mod"] not in modules and parsed[jack + "Mod"] not in (M_IN, M_OUT):
                raise PresetError(f"wire {wire!r} touches empty slot {slot_name(parsed[jack + 'Mod'])}")
        ET.SubElement(
            xml_matrix,
            "Wire",
            {
                "srcMod": str(parsed["srcMod"]),
                "srcCh": str(parsed["srcCh"]),
                "destMod": str(parsed["destMod"]),
                "destCh": str(parsed["destCh"]),
                "gain": format_number(parsed["gain"]),
                "offset": format_number(parsed["offset"]),
            },
        )


def _module_state(module: str, params: dict, manifest: dict | None) -> ET.Element:
    """A BaseProcessor state tree. Omitted parameters keep the plugin's own defaults."""
    vst = ET.Element("VST")
    state = ET.SubElement(vst, "state")
    for key, value in params.items():
        ET.SubElement(state, "PARAM", {"id": resolve_param(module, key, manifest), "value": format_number(value)})
    return vst


# --- decode ------------------------------------------------------------------


def decode(blob: bytes) -> dict:
    root = ET.fromstring(unwrap_xml(extract_state(blob)))
    if root.tag != "TRAX":
        raise PresetError(f"expected a TRAX preset, got <{root.tag}>")

    tracks = []
    for xml_track in root.findall("./Tracks/Track"):
        modules, params = {}, {}
        for slot, xml_module in enumerate(xml_track.findall("./Modules/Module")):
            name = xml_module.get("pluginName", "")
            if slot in (M_IN, M_OUT) or not name:
                continue
            modules[slot_name(slot)] = name
            values = {
                p.get("id"): p.get("value")
                for p in xml_module.findall("./data/VST/state/PARAM")
            }
            if values:
                params[slot_name(slot)] = values

        wires = [
            f"{format_jack(int(w.get('srcMod')), int(w.get('srcCh')))}"
            f" -> {format_jack(int(w.get('destMod')), int(w.get('destCh')))}"
            if w.get("gain", "1.0") in ("1.0", "1") and w.get("offset", "0.0") in ("0.0", "0")
            else {
                "from": format_jack(int(w.get("srcMod")), int(w.get("srcCh"))),
                "to": format_jack(int(w.get("destMod")), int(w.get("destCh"))),
                "gain": w.get("gain", "1.0"),
                "offset": w.get("offset", "0.0"),
            }
            for w in xml_track.findall("./Matrix/Wire")
        ]

        track = {"level": xml_track.get("level", "1.0")}
        if xml_track.get("mute", "0") not in ("0", "false"):
            track["mute"] = True
        track.update({"modules": modules, "wires": wires})
        if params:
            track["params"] = params
        tracks.append(track)

    doc = {"trax": 1, "tracks": tracks}
    performance = [
        {
            "track": int(p.get("track")) + 1,
            "slot": slot_name(int(p.get("module"))),
            "param": int(p.get("param")),
        }
        for p in root.findall("./PerformParams/Param")
    ]
    if performance:
        doc["performance"] = performance
    return doc


# --- cli ---------------------------------------------------------------------


def _load_manifest(path: str | None) -> dict | None:
    return json.loads(pathlib.Path(path).read_text()) if path else None


def _emit(text: str, path: str | None) -> None:
    if path:
        pathlib.Path(path).write_text(text + "\n")
    else:
        print(text)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    sub = parser.add_subparsers(dest="command", required=True)

    p_encode = sub.add_parser("encode", help="JSON preset -> trax preset file")
    p_encode.add_argument("input")
    p_encode.add_argument("-o", "--output", required=True)
    p_encode.add_argument("-m", "--modules", help="manifest from 'scan'; enables parameters by name")

    p_decode = sub.add_parser("decode", help="trax preset file (or .filtergraph state) -> JSON")
    p_decode.add_argument("input")
    p_decode.add_argument("-o", "--output")

    p_scan = sub.add_parser("scan", help="build a parameter manifest from the plugin sources")
    p_scan.add_argument("root", help="directory holding the module sources, e.g. technobear")
    p_scan.add_argument("-o", "--output")

    args = parser.parse_args(argv)
    try:
        if args.command == "encode":
            doc = json.loads(pathlib.Path(args.input).read_text())
            pathlib.Path(args.output).write_bytes(encode(doc, _load_manifest(args.modules)))
        elif args.command == "decode":
            _emit(json.dumps(decode(pathlib.Path(args.input).read_bytes()), indent=2), args.output)
        else:
            manifest = scan_modules(pathlib.Path(args.root))
            _emit(json.dumps(manifest, indent=2, sort_keys=True), args.output)
            named = sum(len(m["params"]) for m in manifest.values())
            skipped = sum(m["skipped"] for m in manifest.values())
            print(f"{len(manifest)} modules, {named} parameters named, {skipped} skipped", file=sys.stderr)
    except (PresetError, OSError, ValueError) as error:
        print(f"py2trax: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
