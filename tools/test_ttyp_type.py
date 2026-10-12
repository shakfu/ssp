"""ttyp_type.py's key records."""

import pytest

import ttyp_type


def test_records():
    assert ttyp_type.records("Cv 1:\n") == [
        bytes([2, 0x06, 0, 0]), bytes([2, 0x19, 0, 0]), bytes([2, 0x2C, 0, 0]),
        bytes([2, 0x1E, 0, 0]), bytes([2, 0x33, 0x02, 0]), bytes([2, 0x28, 0, 0]),
    ]
    assert ttyp_type.records("a\r\nb") == ttyp_type.records("a\nb")


def test_unknown_character():
    with pytest.raises(ValueError):
        ttyp_type.records("é")
