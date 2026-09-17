import json
import pathlib
import re
import sys
import xml.etree.ElementTree as ET

import pytest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))

import py2trax

REPO = pathlib.Path(__file__).resolve().parents[3]
FIXTURE = REPO / "resources" / "test" / "trax.filtergraph"
EXAMPLE = pathlib.Path(__file__).resolve().parents[1] / "examples" / "two_track" / "two_track.json"


def manifest() -> dict:
    return py2trax.scan_modules(REPO / "technobear")


def fixture_state() -> bytes:
    text = FIXTURE.read_text()
    blocks = re.findall(r"<STATE>(.*?)</STATE>", text, re.S)
    return py2trax.b64_decode(blocks[-1].strip())


# --- JUCE encodings ---


def test_base64_round_trips():
    data = bytes(range(256)) * 3
    assert py2trax.b64_decode(py2trax.b64_encode(data)) == data


def test_base64_matches_juce_output():
    text = FIXTURE.read_text()
    block = re.findall(r"<STATE>(.*?)</STATE>", text, re.S)[-1].strip()
    assert py2trax.b64_encode(py2trax.b64_decode(block)) == block


def test_envelope_round_trips():
    xml = "<TRAX><Tracks/></TRAX>"
    assert py2trax.unwrap_xml(py2trax.wrap_xml(xml)) == py2trax.XML_HEADER + xml


def test_envelope_length_field_matches_juce():
    # copyXmlToBinary writes size-9 after magic, header and trailing NUL.
    blob = py2trax.wrap_xml("<TRAX/>")
    assert int.from_bytes(blob[4:8], "little") == len(blob) - 9


# --- decoding a preset trax itself wrote ---


def test_decodes_real_preset():
    doc = py2trax.decode(fixture_state())
    assert len(doc["tracks"]) == 4
    assert doc["tracks"][0]["modules"] == {"1": "omod", "2": "drum", "3": "plts"}
    assert doc["tracks"][1]["modules"] == {"1": "mmx4", "2": "plts"}
    assert doc["tracks"][2]["modules"] == {}


def test_decodes_real_wires():
    track = py2trax.decode(fixture_state())["tracks"][0]
    assert track["wires"] == [
        "2:2 -> out:0",
        "1:0 -> 2:4",
        "3:0 -> out:0",
        "3:0 -> out:1",
        "1:0 -> 3:1",
    ]


def test_decodes_real_performance_params():
    doc = py2trax.decode(fixture_state())
    assert doc["performance"] == [
        {"track": 2, "slot": "2", "param": 2},
        {"track": 2, "slot": "2", "param": 0},
    ]


def test_decodes_real_parameters_by_id():
    params = py2trax.decode(fixture_state())["tracks"][0]["params"]
    assert params["1"]["freq"] == "50.0"
    assert params["1"]["slaveosc:3:ratio"] == "0.3299999833106995"


# --- encoding ---


def test_encode_decode_round_trip():
    doc = json.loads(EXAMPLE.read_text())
    built = py2trax.decode(py2trax.encode(doc, manifest()))
    assert built["tracks"][0]["modules"] == doc["tracks"]["1"]["modules"]


def test_round_trip_is_stable():
    once = py2trax.decode(py2trax.encode(json.loads(EXAMPLE.read_text()), manifest()))
    assert py2trax.decode(py2trax.encode(once)) == once


def test_decoded_real_preset_re_encodes():
    doc = py2trax.decode(fixture_state())
    assert py2trax.decode(py2trax.encode(doc)) == doc


def test_occupied_slots_get_nonzero_datasz():
    # Track::setStateInformation skips any module whose dataSz is 0.
    xml = ET.fromstring(py2trax.unwrap_xml(py2trax.encode(json.loads(EXAMPLE.read_text()), manifest())))
    modules = xml.findall("./Tracks/Track")[0].findall("./Modules/Module")
    assert [m.get("pluginName") for m in modules][:3] == ["IN", "omod", "drum"]
    assert all(int(m.get("dataSz")) > 0 for m in modules[1:3])
    assert all(int(m.get("dataSz")) == 0 for m in modules[3:])
    assert modules[0].get("dataSz") == "0" and modules[9].get("dataSz") == "0"


def test_every_track_emits_ten_slots():
    xml = ET.fromstring(py2trax.unwrap_xml(py2trax.encode({"tracks": {"1": {"modules": {"3": "clds"}}}})))
    for track in xml.findall("./Tracks/Track"):
        assert len(track.findall("./Modules/Module")) == py2trax.M_MAX
    assert len(xml.findall("./Tracks/Track")) == py2trax.MAX_TRACKS


def test_empty_preset_is_valid():
    assert py2trax.decode(py2trax.encode({"trax": 1}))["tracks"][0]["modules"] == {}


def test_parameters_are_written_as_denormalised_values():
    doc = {"tracks": {"1": {"modules": {"1": "clds"}, "params": {"1": {"position": 25.5}}}}}
    xml = ET.fromstring(py2trax.unwrap_xml(py2trax.encode(doc)))
    param = xml.find("./Tracks/Track/Modules/Module/data/VST/state/PARAM")
    assert param.get("id") == "position" and param.get("value") == "25.5"


def test_wire_with_gain_survives_round_trip():
    doc = {"tracks": [{"modules": {"1": "clds"}, "wires": [{"from": "in:0", "to": "1:0", "gain": 0.5}]}]}
    wire = py2trax.decode(py2trax.encode(doc))["tracks"][0]["wires"][0]
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
    with pytest.raises(py2trax.PresetError):
        py2trax.encode(doc)


def test_decode_rejects_foreign_payload():
    with pytest.raises(py2trax.PresetError):
        py2trax.decode(py2trax.wrap_xml("<VST><state/></VST>"))


# --- parameter manifest ---


def test_scan_reads_simple_parameters():
    manifest = py2trax.scan_modules(REPO / "technobear")
    assert manifest["clds"]["params"]["Position"] == "position"
    assert manifest["clds"]["skipped"] == 0


def test_scan_counts_unrecoverable_parameters():
    manifest = py2trax.scan_modules(REPO / "technobear")
    assert manifest["omod"]["skipped"] > 0


def test_names_resolve_through_manifest():
    manifest = py2trax.scan_modules(REPO / "technobear")
    doc = {"tracks": [{"modules": {"1": "clds"}, "params": {"1": {"Position": 25.0, "size": 10.0}}}]}
    params = py2trax.decode(py2trax.encode(doc, manifest))["tracks"][0]["params"]["1"]
    assert params == {"position": "25.0", "size": "10.0"}


def test_unknown_parameter_name_is_rejected():
    manifest = py2trax.scan_modules(REPO / "technobear")
    doc = {"tracks": [{"modules": {"1": "clds"}, "params": {"1": {"Nonesuch": 1.0}}}]}
    with pytest.raises(py2trax.PresetError, match="Nonesuch"):
        py2trax.encode(doc, manifest)


def test_ids_pass_through_without_a_manifest():
    doc = {"tracks": [{"modules": {"1": "omod"}, "params": {"1": {"slaveosc:3:ratio": 0.33}}}]}
    params = py2trax.decode(py2trax.encode(doc))["tracks"][0]["params"]["1"]
    assert params == {"slaveosc:3:ratio": "0.33"}


def test_incompletely_scanned_module_accepts_ids():
    # omod builds its sub-oscillator ids at runtime, so the manifest cannot list them.
    manifest = py2trax.scan_modules(REPO / "technobear")
    doc = {"tracks": [{"modules": {"1": "omod"}, "params": {"1": {"Freq": 800.0, "slaveosc:0:ratio": 0.5}}}]}
    params = py2trax.decode(py2trax.encode(doc, manifest))["tracks"][0]["params"]["1"]
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
    assert py2trax.decode(py2trax.encode(doc, manifest()))["tracks"][0]["wires"] == [
        "1:0 -> 2:4",
        "2:2 -> out:0",
    ]


def test_channel_names_are_case_insensitive():
    doc = {"tracks": [{"modules": {"1": "clds"}, "wires": ["in:0 -> 1:in l"]}]}
    assert py2trax.decode(py2trax.encode(doc, manifest()))["tracks"][0]["wires"] == ["in:0 -> 1:0"]


def test_channel_out_of_range_is_rejected():
    doc = {"tracks": [{"modules": {"1": "srvb"}, "wires": ["in:0 -> 1:9"]}]}
    with pytest.raises(py2trax.PresetError, match="does not exist"):
        py2trax.encode(doc, manifest())


def test_unknown_channel_name_is_rejected():
    doc = {"tracks": [{"modules": {"1": "drum"}, "wires": ["in:0 -> 1:Cowbell"]}]}
    with pytest.raises(py2trax.PresetError, match="Cowbell"):
        py2trax.encode(doc, manifest())


def test_track_output_has_two_channels():
    doc = {"tracks": [{"modules": {"1": "clds"}, "wires": ["1:0 -> out:2"]}]}
    with pytest.raises(py2trax.PresetError, match="does not exist"):
        py2trax.encode(doc, manifest())


def test_module_without_known_channels_still_takes_indices():
    # pmix builds its channel names from the index, so the scan cannot list them.
    doc = {"tracks": [{"modules": {"1": "pmix"}, "wires": ["in:0 -> 1:3"]}]}
    assert py2trax.decode(py2trax.encode(doc, manifest()))["tracks"][0]["wires"] == ["in:0 -> 1:3"]


def test_module_without_known_channels_rejects_names():
    doc = {"tracks": [{"modules": {"1": "pmix"}, "wires": ["in:0 -> 1:IN 4"]}]}
    with pytest.raises(py2trax.PresetError, match="not known"):
        py2trax.encode(doc, manifest())


def test_channels_are_unchecked_without_a_manifest():
    doc = {"tracks": [{"modules": {"1": "srvb"}, "wires": ["in:0 -> 1:9"]}]}
    assert py2trax.decode(py2trax.encode(doc))["tracks"][0]["wires"] == ["in:0 -> 1:9"]


# --- state a module keeps outside its parameters ---


def test_real_preset_carries_non_parameter_state():
    doc = py2trax.decode(fixture_state())
    assert sorted(doc["tracks"][0]["state"]) == ["1", "2", "3"]


def test_non_parameter_state_round_trips():
    doc = py2trax.decode(fixture_state())
    assert py2trax.decode(py2trax.encode(doc))["tracks"][0]["state"] == doc["tracks"][0]["state"]


def test_state_holds_no_parameters():
    import base64

    doc = py2trax.decode(fixture_state())
    vst = ET.fromstring(py2trax.unwrap_xml(base64.b64decode(doc["tracks"][0]["state"]["1"])))
    assert vst.find("state") is None and vst.find("MIDI") is not None


def test_state_for_an_empty_slot_is_rejected():
    doc = py2trax.decode(fixture_state())
    doc["tracks"][0]["state"]["7"] = doc["tracks"][0]["state"]["1"]
    with pytest.raises(py2trax.PresetError, match="slot 7"):
        py2trax.encode(doc)


def test_parameters_win_over_state():
    doc = py2trax.decode(fixture_state())
    doc["tracks"][0]["params"]["1"]["freq"] = 999.0
    rebuilt = py2trax.decode(py2trax.encode(doc))
    assert rebuilt["tracks"][0]["params"]["1"]["freq"] == "999.0"
