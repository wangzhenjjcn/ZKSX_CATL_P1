#!/usr/bin/env python3
"""Host-side checks for mux packing / CRC / sensor checksum."""

from __future__ import annotations

import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from usb_mux_dump import MuxDecoder, crc16_ccitt, sensor_checksum_ok  # noqa: E402


def make_sensor_frame(cnt: int, fill: int = 0x11) -> bytes:
    frame = bytearray(70)
    frame[0] = 0xFF
    frame[1] = 0x84
    frame[2] = (cnt >> 8) & 0xFF
    frame[3] = cnt & 0xFF
    for i in range(4, 68):
        frame[i] = fill
    total = sum(frame[2:68]) & 0xFFFF
    frame[68] = (total >> 8) & 0xFF
    frame[69] = total & 0xFF
    return bytes(frame)


def mux_pack(ptype: int, payload: bytes) -> bytes:
    body = bytes([ptype, len(payload) & 0xFF, (len(payload) >> 8) & 0xFF]) + payload
    crc = crc16_ccitt(body)
    return b"\xA5\x5A" + body + struct.pack("<H", crc)


def main() -> int:
    frame = make_sensor_frame(0x1234)
    assert len(frame) == 70
    assert sensor_checksum_ok(frame)
    bad = bytearray(frame)
    bad[10] ^= 0x01
    assert not sensor_checksum_ok(bytes(bad))

    decoder = MuxDecoder()
    pkt = mux_pack(0x01, frame)
    got = decoder.feed(pkt)
    assert len(got) == 1 and got[0][0] == 0x01 and got[0][1] == frame

    split = mux_pack(0x03, b"\x03")
    mid = len(split) // 2
    assert decoder.feed(split[:mid]) == []
    got = decoder.feed(split[mid:])
    assert len(got) == 1 and got[0] == (0x03, b"\x03")

    status = struct.pack("<IIII", 10, 1, 2, 3)
    got = decoder.feed(mux_pack(0x04, status))
    assert got == [(0x04, status)]

    print("protocol self-test ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
