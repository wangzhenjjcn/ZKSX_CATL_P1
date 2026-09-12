#!/usr/bin/env python3
"""Decode XIAO ESP32-S3 USB CDC mux frames.

Packet: A5 5A | type | len_le | payload | crc16_ccitt_le
CRC-16-CCITT-FALSE over type + len + payload (poly 0x1021, init 0xFFFF).

Types:
  0x01 sensor  70-byte raw frame (FF 84 ...)
  0x02 audio   int16 LE PCM, 48 kHz mono, 960 samples / 20 ms
  0x03 switch  bit0=SW1, bit1=SW2, 1=closed
  0x04 status  sensor_ok, checksum_fail, audio_dropped, usb_incomplete (u32 LE)

Example:
  python tools/usb_mux_dump.py COM5
  python tools/usb_mux_dump.py COM5 --wav capture.wav
"""

from __future__ import annotations

import argparse
import struct
import sys
import time
import wave
from pathlib import Path

try:
    import serial
    from serial.tools import list_ports
except ImportError:
    print("Install pyserial: pip install pyserial", file=sys.stderr)
    raise


MUX_MAGIC = b"\xA5\x5A"
SENSOR_HEAD = b"\xFF\x84"
TYPE_SENSOR = 0x01
TYPE_AUDIO = 0x02
TYPE_SWITCH = 0x03
TYPE_STATUS = 0x04
AUDIO_RATE = 48000


def crc16_ccitt(data: bytes, init: int = 0xFFFF) -> int:
    crc = init
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            if crc & 0x8000:
                crc = ((crc << 1) ^ 0x1021) & 0xFFFF
            else:
                crc = (crc << 1) & 0xFFFF
    return crc


def sensor_checksum_ok(frame: bytes) -> bool:
    if len(frame) != 70 or frame[:2] != SENSOR_HEAD:
        return False
    total = sum(frame[2:68]) & 0xFFFF
    return frame[68] == ((total >> 8) & 0xFF) and frame[69] == (total & 0xFF)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Dump USB mux packets from XIAO ESP32-S3")
    parser.add_argument(
        "port",
        nargs="?",
        help="Serial port (COMx on Windows, /dev/ttyACM0 on Linux). Omit to list ports.",
    )
    parser.add_argument("--baud", type=int, default=115200, help="Ignored by USB CDC; kept for Serial API")
    parser.add_argument("--wav", type=Path, help="Write decoded PCM to this WAV file")
    parser.add_argument("--quiet-audio", action="store_true", help="Do not print every audio chunk")
    parser.add_argument("--seconds", type=float, default=0.0, help="Stop after N seconds (0 = run until Ctrl+C)")
    return parser.parse_args()


def list_serial_ports() -> None:
    ports = list(list_ports.comports())
    if not ports:
        print("No serial ports found.")
        return
    print("Available ports:")
    for item in ports:
        print(f"  {item.device:12} {item.description}")


class MuxDecoder:
    def __init__(self) -> None:
        self.buf = bytearray()

    def feed(self, data: bytes) -> list[tuple[int, bytes]]:
        self.buf.extend(data)
        packets: list[tuple[int, bytes]] = []
        while True:
            start = self.buf.find(MUX_MAGIC)
            if start < 0:
                if self.buf and self.buf[-1] == MUX_MAGIC[0]:
                    self.buf[:] = self.buf[-1:]
                else:
                    self.buf.clear()
                break
            if start > 0:
                del self.buf[:start]
            if len(self.buf) < 5:
                break
            ptype = self.buf[2]
            length = self.buf[3] | (self.buf[4] << 8)
            total = 5 + length + 2
            if length > 2048:
                del self.buf[:2]
                continue
            if len(self.buf) < total:
                break
            raw = bytes(self.buf[:total])
            del self.buf[:total]
            payload = raw[5 : 5 + length]
            crc_got = raw[5 + length] | (raw[5 + length + 1] << 8)
            crc_exp = crc16_ccitt(raw[2 : 5 + length])
            if crc_got != crc_exp:
                continue
            packets.append((ptype, payload))
        return packets


def format_switch(bits: int) -> str:
    sw1 = "closed" if bits & 0x01 else "open"
    sw2 = "closed" if bits & 0x02 else "open"
    return f"SW1={sw1} SW2={sw2} (0x{bits:02X})"


def main() -> int:
    args = parse_args()
    if not args.port:
        list_serial_ports()
        return 0

    wav: wave.Wave_write | None = None
    if args.wav:
        wav = wave.open(str(args.wav), "wb")
        wav.setnchannels(1)
        wav.setsampwidth(2)
        wav.setframerate(AUDIO_RATE)

    decoder = MuxDecoder()
    audio_chunks = 0
    sensor_count = 0
    last_cnt: int | None = None
    t0 = time.time()

    try:
        with serial.Serial(args.port, args.baud, timeout=0.1) as ser:
            print(f"Opened {args.port}. Waiting for mux packets... (Ctrl+C to stop)")
            while True:
                if args.seconds and (time.time() - t0) >= args.seconds:
                    break
                chunk = ser.read(4096)
                if not chunk:
                    continue
                for ptype, payload in decoder.feed(chunk):
                    if ptype == TYPE_SENSOR:
                        sensor_count += 1
                        if len(payload) != 70:
                            print(f"sensor: bad length {len(payload)}")
                            continue
                        cnt = (payload[2] << 8) | payload[3]
                        gap = ""
                        if last_cnt is not None:
                            expected = (last_cnt + 1) & 0xFFFF
                            if cnt != expected:
                                gap = f" gap(expected {expected})"
                        last_cnt = cnt
                        ok = "ok" if sensor_checksum_ok(payload) else "bad-csum"
                        print(f"sensor n={sensor_count} CNT={cnt} {ok}{gap}")
                    elif ptype == TYPE_AUDIO:
                        audio_chunks += 1
                        if wav is not None:
                            wav.writeframes(payload)
                        if not args.quiet_audio:
                            if len(payload) >= 2:
                                sample = struct.unpack_from("<h", payload, 0)[0]
                                peak = max(abs(struct.unpack_from("<h", payload, i)[0]) for i in range(0, len(payload), 2))
                            else:
                                sample = 0
                                peak = 0
                            print(f"audio chunk={audio_chunks} bytes={len(payload)} s0={sample} peak={peak}")
                    elif ptype == TYPE_SWITCH:
                        bits = payload[0] if payload else 0
                        print(f"switch {format_switch(bits)}")
                    elif ptype == TYPE_STATUS:
                        if len(payload) >= 16:
                            ok, bad, drop, usb = struct.unpack_from("<IIII", payload, 0)
                            print(f"status sensor_ok={ok} csum_fail={bad} audio_drop={drop} usb_fail={usb}")
                    else:
                        print(f"unknown type=0x{ptype:02X} len={len(payload)}")
    except KeyboardInterrupt:
        print("\nStopped.")
    finally:
        if wav is not None:
            wav.close()
            print(f"Wrote WAV to {args.wav}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
