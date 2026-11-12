"""Capture a real firmware-rendered RGB565 screen from a --screenshots build."""

import argparse
import array
import pathlib
import sys
import time

from PIL import Image
import serial


def command(port, text, prefix=None):
    port.write((text + "\n").encode("ascii"))
    deadline = time.monotonic() + (5 if prefix else 0.2)
    while time.monotonic() < deadline:
        line = port.readline().decode("utf-8", errors="replace").strip()
        if "ERROR" in line:
            raise RuntimeError(line)
        if prefix and line.startswith(prefix + " "):
            return dict(item.split("=", 1) for item in line.split()[1:])
    if prefix:
        raise RuntimeError(f"No {prefix} response to {text}")


def capture(port, destination):
    header = command(port, "screenshot", "SCREENSHOT_BEGIN")
    width, height, size = (int(header[key]) for key in ("width", "height", "bytes"))
    if (width, height, size) != (240, 284, 240 * 284 * 2):
        raise RuntimeError(f"Unexpected screen dimensions: {header}")
    data = bytearray()
    deadline = time.monotonic() + 15
    while len(data) < size and time.monotonic() < deadline:
        data.extend(port.read(size - len(data)))
    if len(data) != size:
        raise RuntimeError(f"Incomplete screen: {len(data)} / {size} bytes")
    hash_value = 2166136261
    for value in data:
        hash_value = ((hash_value ^ value) * 16777619) & 0xFFFFFFFF
    if hash_value != int(header["fnv"], 16):
        raise RuntimeError("Screenshot checksum mismatch")
    while time.monotonic() < deadline:
        line = port.readline().decode("utf-8", errors="replace").strip()
        if line == "SCREENSHOT_END":
            break
        if "ERROR" in line:
            raise RuntimeError(line)
    else:
        raise RuntimeError("Missing SCREENSHOT_END")
    pixels = array.array("H")
    pixels.frombytes(data)
    if sys.byteorder != "little":
        pixels.byteswap()
    rgb = bytes(component for pixel in pixels for component in (
        ((pixel >> 11) & 31) * 255 // 31,
        ((pixel >> 5) & 63) * 255 // 63,
        (pixel & 31) * 255 // 31))
    destination.parent.mkdir(parents=True, exist_ok=True)
    Image.frombytes("RGB", (width, height), rgb).save(destination)
    print(f"Captured {destination}", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True, help="USB serial port, e.g. COM3 or /dev/ttyACM0")
    parser.add_argument("output", type=pathlib.Path, help="Destination PNG")
    args = parser.parse_args()
    port = serial.Serial(baudrate=115200, timeout=0.1)
    port.port = args.port
    port.dtr = port.rts = False
    try:
        with port:
            time.sleep(1)
            capture(port, args.output)
    except (OSError, RuntimeError, serial.SerialException) as error:
        print(f"CAPTURE_ERROR: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
