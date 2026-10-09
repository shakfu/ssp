import json
import pathlib
import re
import sys
import xml.etree.ElementTree as ET

import pytest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))

import py2rack

REPO = pathlib.Path(__file__).resolve().parents[3]
FIXTURE = REPO / "resources" / "test" / "trax.filtergraph"
EXAMPLE = pathlib.Path(__file__).resolve().parents[1] / "examples" / "two_track" / "two_track.json"
MODULES = pathlib.Path(__file__).resolve().parents[1] / "modules.json"
LOCAL = pathlib.Path(__file__).resolve().parents[1] / "modules-local.json"  # this repo's modules
SOURCES = pathlib.Path(__file__).resolve().parent / "fixtures"


def load_manifest() -> dict:
    return json.loads(MODULES.read_text())


def fixture_state() -> bytes:
    text = FIXTURE.read_text()
    blocks = re.findall(r"<STATE>(.*?)</STATE>", text, re.S)
    return py2rack.b64_decode(blocks[-1].strip())


# --- JUCE encodings ---


def test_base64_round_trips():
    data = bytes(range(256)) * 3
    assert py2rack.b64_decode(py2rack.b64_encode(data)) == data


def test_base64_matches_juce_output():
    text = FIXTURE.read_text()
    block = re.findall(r"<STATE>(.*?)</STATE>", text, re.S)[-1].strip()
    assert py2rack.b64_encode(py2rack.b64_decode(block)) == block


def test_envelope_round_trips():
    xml = "<TRAX><Tracks/></TRAX>"
    assert py2rack.unwrap_xml(py2rack.wrap_xml(xml)) == py2rack.XML_HEADER + xml


def test_envelope_length_field_matches_juce():
    # copyXmlToBinary writes size-9 after magic, header and trailing NUL.
    blob = py2rack.wrap_xml("<TRAX/>")
    assert int.from_bytes(blob[4:8], "little") == len(blob) - 9


# --- decoding a preset upstream trax wrote ---


def test_decodes_real_preset():
    doc = py2rack.decode(fixture_state())
    assert len(doc["tracks"]) == 4
    assert doc["tracks"][0]["modules"] == {"1": "omod", "2": "drum", "3": "plts"}
    assert doc["tracks"][1]["modules"] == {"1": "mmx4", "2": "plts"}
    assert doc["tracks"][2]["modules"] == {}


def test_decodes_real_wires():
    track = py2rack.decode(fixture_state())["tracks"][0]
    assert track["wires"] == [
        "2:2 -> out:0",
        "1:0 -> 2:4",
        "3:0 -> out:0",
        "3:0 -> out:1",
        "1:0 -> 3:1",
    ]


def test_decodes_real_performance_params():
    doc = py2rack.decode(fixture_state())
    assert doc["performance"] == [
        {"track": 2, "slot": "2", "param": 2},
        {"track": 2, "slot": "2", "param": 0},
    ]


def test_decodes_real_parameters_by_id():
    params = py2rack.decode(fixture_state())["tracks"][0]["params"]
    assert params["1"]["freq"] == "50.0"
    assert params["1"]["slaveosc:3:ratio"] == "0.3299999833106995"


# --- encoding ---


def test_encode_decode_round_trip():
    doc = json.loads(EXAMPLE.read_text())
    built = py2rack.decode(py2rack.encode(doc, load_manifest()))
    assert built["tracks"][0]["modules"] == doc["tracks"]["1"]["modules"]


def test_round_trip_is_stable():
    once = py2rack.decode(py2rack.encode(json.loads(EXAMPLE.read_text()), load_manifest()))
    assert py2rack.decode(py2rack.encode(once)) == once


def test_decoded_real_preset_re_encodes():
    doc = py2rack.decode(fixture_state())
    assert py2rack.decode(py2rack.encode(doc)) == doc


def test_occupied_slots_get_nonzero_datasz():
    # Track::setStateInformation skips any module whose dataSz is 0.
    xml = ET.fromstring(py2rack.unwrap_xml(py2rack.encode(json.loads(EXAMPLE.read_text()), load_manifest())))
    modules = xml.findall("./Tracks/Track")[0].findall("./Modules/Module")
    assert [m.get("pluginName") for m in modules][:3] == ["IN", "omod", "drum"]
    assert all(int(m.get("dataSz")) > 0 for m in modules[1:3])
    assert all(int(m.get("dataSz")) == 0 for m in modules[3:])
    assert modules[0].get("dataSz") == "0" and modules[9].get("dataSz") == "0"


def test_every_track_emits_ten_slots():
    xml = ET.fromstring(py2rack.unwrap_xml(py2rack.encode({"tracks": {"1": {"modules": {"3": "clds"}}}})))
    for track in xml.findall("./Tracks/Track"):
        assert len(track.findall("./Modules/Module")) == py2rack.M_MAX
    assert len(xml.findall("./Tracks/Track")) == py2rack.MAX_TRACKS


def test_empty_preset_is_valid():
    assert py2rack.decode(py2rack.encode({"rack": 1}))["tracks"][0]["modules"] == {}


def test_parameters_are_written_as_denormalised_values():
    doc = {"tracks": {"1": {"modules": {"1": "clds"}, "params": {"1": {"position": 25.5}}}}}
    xml = ET.fromstring(py2rack.unwrap_xml(py2rack.encode(doc)))
    param = xml.find("./Tracks/Track/Modules/Module/data/VST/state/PARAM")
    assert param.get("id") == "position" and param.get("value") == "25.5"


def test_wire_with_gain_survives_round_trip():
    doc = {"tracks": [{"modules": {"1": "clds"}, "wires": [{"from": "in:0", "to": "1:0", "gain": 0.5}]}]}
    wire = py2rack.decode(py2rack.encode(doc))["tracks"][0]["wires"][0]
    assert wire["from"] == "in:0" and wire["gain"] == "0.5"


# --- input validation ---


@pytest.mark.parametrize(
    "doc",
    [
        {"tracks": {"1": {"modules": {"9": "clds"}}}},
        {"tracks": {"5": {"modules": {"1": "clds"}}}},
        {"tracks": [{"modules": {"1": "clds"}, "wires": ["1:0 -> 4:0"]}]},
        {"tracks": [{"modules": {"1": "clds"}, "wires": ["1:0"]}]},
        {"tracks": [{"modules": {"1": "clds"}, "params": {"2": {"x": 1}}}]},
    ],
)
def test_bad_input_is_rejected(doc):
    with pytest.raises(py2rack.PresetError):
        py2rack.encode(doc)


def test_decode_rejects_foreign_payload():
    with pytest.raises(py2rack.PresetError):
        py2rack.decode(py2rack.wrap_xml("<VST><state/></VST>"))


# --- parameter manifest ---


def test_scan_reads_simple_parameters():
    manifest = py2rack.scan_modules(SOURCES)
    assert manifest["clds"]["params"]["Position"] == "position"
    assert manifest["clds"]["skipped"] == 0


def test_scan_counts_unrecoverable_parameters():
    manifest = py2rack.scan_modules(SOURCES)
    assert manifest["omod"]["skipped"] > 0


def test_vendored_manifest_matches_scanner():
    scanned = py2rack.scan_modules(SOURCES)
    vendored = load_manifest()
    assert {name: vendored[name] for name in scanned} == scanned


def test_names_resolve_through_manifest():
    manifest = load_manifest()
    doc = {"tracks": [{"modules": {"1": "clds"}, "params": {"1": {"Position": 25.0, "size": 10.0}}}]}
    params = py2rack.decode(py2rack.encode(doc, manifest))["tracks"][0]["params"]["1"]
    assert params == {"position": "25.0", "size": "10.0"}


def test_unknown_parameter_name_is_rejected():
    manifest = load_manifest()
    doc = {"tracks": [{"modules": {"1": "clds"}, "params": {"1": {"Nonesuch": 1.0}}}]}
    with pytest.raises(py2rack.PresetError, match="Nonesuch"):
        py2rack.encode(doc, manifest)


def test_ids_pass_through_without_a_manifest():
    doc = {"tracks": [{"modules": {"1": "omod"}, "params": {"1": {"slaveosc:3:ratio": 0.33}}}]}
    params = py2rack.decode(py2rack.encode(doc))["tracks"][0]["params"]["1"]
    assert params == {"slaveosc:3:ratio": "0.33"}


def test_incompletely_scanned_module_accepts_ids():
    # omod builds its sub-oscillator ids at runtime, so the manifest cannot list them.
    manifest = load_manifest()
    doc = {"tracks": [{"modules": {"1": "omod"}, "params": {"1": {"Freq": 800.0, "slaveosc:0:ratio": 0.5}}}]}
    params = py2rack.decode(py2rack.encode(doc, manifest))["tracks"][0]["params"]["1"]
    assert params == {"freq": "800.0", "slaveosc:0:ratio": "0.5"}


# --- channel names and range checking ---


def test_channel_names_resolve_to_indices():
    doc = {
        "tracks": [
            {
                "modules": {"1": "omod", "2": "drum"},
                "wires": ["1:Main -> 2:AS Trig", "2:A Snare -> out:Out 0"],
            }
        ]
    }
    assert py2rack.decode(py2rack.encode(doc, load_manifest()))["tracks"][0]["wires"] == [
        "1:0 -> 2:4",
        "2:2 -> out:0",
    ]


def test_channel_names_are_case_insensitive():
    doc = {"tracks": [{"modules": {"1": "clds"}, "wires": ["in:0 -> 1:in l"]}]}
    assert py2rack.decode(py2rack.encode(doc, load_manifest()))["tracks"][0]["wires"] == ["in:0 -> 1:0"]


def test_channel_out_of_range_is_rejected():
    doc = {"tracks": [{"modules": {"1": "srvb"}, "wires": ["in:0 -> 1:9"]}]}
    with pytest.raises(py2rack.PresetError, match="does not exist"):
        py2rack.encode(doc, load_manifest())


def test_unknown_channel_name_is_rejected():
    doc = {"tracks": [{"modules": {"1": "drum"}, "wires": ["in:0 -> 1:Cowbell"]}]}
    with pytest.raises(py2rack.PresetError, match="Cowbell"):
        py2rack.encode(doc, load_manifest())


def test_track_output_has_two_channels():
    doc = {"tracks": [{"modules": {"1": "clds"}, "wires": ["1:0 -> out:2"]}]}
    with pytest.raises(py2rack.PresetError, match="does not exist"):
        py2rack.encode(doc, load_manifest())


def test_module_without_known_channels_still_takes_indices():
    # pmix builds its channel names from the index, so the scan cannot list them.
    doc = {"tracks": [{"modules": {"1": "pmix"}, "wires": ["in:0 -> 1:3"]}]}
    assert py2rack.decode(py2rack.encode(doc, load_manifest()))["tracks"][0]["wires"] == ["in:0 -> 1:3"]


def test_module_without_known_channels_rejects_names():
    doc = {"tracks": [{"modules": {"1": "pmix"}, "wires": ["in:0 -> 1:IN 4"]}]}
    with pytest.raises(py2rack.PresetError, match="not known"):
        py2rack.encode(doc, load_manifest())


def test_channels_are_unchecked_without_a_manifest():
    doc = {"tracks": [{"modules": {"1": "srvb"}, "wires": ["in:0 -> 1:9"]}]}
    assert py2rack.decode(py2rack.encode(doc))["tracks"][0]["wires"] == ["in:0 -> 1:9"]


# --- state a module keeps outside its parameters ---


def test_real_preset_carries_non_parameter_state():
    doc = py2rack.decode(fixture_state())
    assert sorted(doc["tracks"][0]["state"]) == ["1", "2", "3"]


def test_non_parameter_state_round_trips():
    doc = py2rack.decode(fixture_state())
    assert py2rack.decode(py2rack.encode(doc))["tracks"][0]["state"] == doc["tracks"][0]["state"]


def test_state_holds_no_parameters():
    import base64

    doc = py2rack.decode(fixture_state())
    vst = ET.fromstring(py2rack.unwrap_xml(base64.b64decode(doc["tracks"][0]["state"]["1"])))
    assert vst.find("state") is None and vst.find("MIDI") is not None


def test_state_for_an_empty_slot_is_rejected():
    doc = py2rack.decode(fixture_state())
    doc["tracks"][0]["state"]["7"] = doc["tracks"][0]["state"]["1"]
    with pytest.raises(py2rack.PresetError, match="slot 7"):
        py2rack.encode(doc)


def test_parameters_win_over_state():
    doc = py2rack.decode(fixture_state())
    doc["tracks"][0]["params"]["1"]["freq"] = 999.0
    rebuilt = py2rack.decode(py2rack.encode(doc))
    assert rebuilt["tracks"][0]["params"]["1"]["freq"] == "999.0"


# --- matrix authoring ---


def test_matrix_cells_become_wires():
    wires = py2rack.matrix_wires(
        rows=["in:0", "in:1", "1:Out L"],
        cols=["1:In L", "1:In R", "out:0"],
        gain=[[1, 0, 0], [0, 1, 0], [0, 0, 0.5]],
    )
    assert wires == ["in:0 -> 1:In L", "in:1 -> 1:In R", {"from": "1:Out L", "to": "out:0", "gain": 0.5}]


def test_matrix_dc_offsets_the_first_wire_into_its_column():
    wires = py2rack.matrix_wires(
        rows=["in:0", "in:1", "dc"],
        cols=["1:0", "1:1"],
        gain=[[1, 0], [0.5, 1], [0.25, 0]],
    )
    assert wires == [
        {"from": "in:0", "to": "1:0", "gain": 1.0, "offset": 0.25},
        {"from": "in:1", "to": "1:0", "gain": 0.5},
        "in:1 -> 1:1",
    ]


def test_matrix_accepts_any_2d_sequence():
    assert py2rack.matrix_wires(["in:0"], ["out:0"], ((2,),)) == [{"from": "in:0", "to": "out:0", "gain": 2.0}]


@pytest.mark.parametrize(
    "rows, cols, gain, match",
    [
        (["in:0"], ["out:0"], [[1], [1]], "2 rows of gain"),
        (["in:0"], ["out:0", "out:1"], [[1]], "1 values for 2 columns"),
        (["in:0", "IN:0"], ["out:0"], [[1], [1]], "repeated row"),
        (["in:0"], ["out:0", "dc"], [[1, 1]], "only label a row"),
        (["in0"], ["out:0"], [[1]], "bad jack"),
        (["in:0"], ["out:0"], [[float("nan")]], "finite"),
        (["in:0", "dc"], ["out:0", "out:1"], [[1, 0], [0, 0.5]], "needs a wire"),
    ],
)
def test_matrix_rejects(rows, cols, gain, match):
    with pytest.raises(py2rack.PresetError, match=match):
        py2rack.matrix_wires(rows, cols, gain)


def test_matrix_wires_encode_with_names_checked():
    wires = py2rack.matrix_wires(
        rows=["in:0", "1:Out L", "dc"],
        cols=["1:In L", "2:In L"],
        gain=[[1, 0], [0, 0.5], [0, 0.1]],
    )
    doc = {"tracks": [{"modules": {"1": "clds", "2": "srvb"}, "wires": wires}]}
    assert py2rack.decode(py2rack.encode(doc, load_manifest()))["tracks"][0]["wires"] == [
        "in:0 -> 1:0",
        {"from": "1:0", "to": "2:0", "gain": "0.5", "offset": "0.1"},
    ]


def test_matrix_field_encodes_like_wires():
    matrix = {"rows": ["in:0", "1:Out L", "dc"], "cols": ["1:In L", "2:In L"], "gain": [[1, 0], [0, 0.5], [0, 0.1]]}
    modules = {"1": "clds", "2": "srvb"}
    from_matrix = py2rack.encode({"tracks": [{"modules": modules, "matrix": matrix}]}, load_manifest())
    from_wires = py2rack.encode(
        {"tracks": [{"modules": modules, "wires": py2rack.matrix_wires(**matrix)}]}, load_manifest()
    )
    assert from_matrix == from_wires


def test_matrix_field_names_are_checked():
    doc = {"tracks": [{"modules": {"1": "clds"}, "matrix": {"rows": ["in:0"], "cols": ["1:Nope"], "gain": [[1]]}}]}
    with pytest.raises(py2rack.PresetError, match="Nope"):
        py2rack.encode(doc, load_manifest())


def test_matrix_field_needs_all_keys():
    with pytest.raises(py2rack.PresetError, match="needs"):
        py2rack.encode({"tracks": [{"matrix": {"rows": [], "cols": []}}]})


# --- presets shipped in presets/ ---


@pytest.mark.parametrize("path", sorted((REPO / "presets").glob("*.json")), ids=lambda p: p.name)
def test_shipped_preset_encodes_with_names_checked(path):
    py2rack.encode(json.loads(path.read_text()), {**load_manifest(), **json.loads(LOCAL.read_text())})
