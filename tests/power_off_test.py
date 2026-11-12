"""Check USB power inhibits automatic power-off without changing the song."""

import argparse
import time

import serial


def command(port, text, prefix=None):
    port.write((text + "\n").encode("ascii"))
    deadline = time.monotonic() + (5 if prefix else 0.2)
    while time.monotonic() < deadline:
        line = port.readline().decode("utf-8", errors="replace").strip()
        if "ERROR" in line or "FATAL" in line:
            raise RuntimeError(line)
        if prefix and line.startswith(prefix + " "):
            return dict(item.split("=", 1) for item in line.split()[1:])
    if prefix:
        raise RuntimeError(f"No {prefix} response to {text}")


def check(port):
    status = lambda: command(port, "status", "ORBIT_STATUS")
    baseline = status()
    if any(baseline[key] != "0" for key in
           ("play", "rec", "armed", "capture", "fx_rec", "fx_armed", "fx_touch", "next_scene")):
        raise RuntimeError("Finish playback, recording and touching the board first")
    if baseline["usb"] != "1" or baseline["battery_valid"] != "1":
        raise RuntimeError("This check requires USB power and valid PMU readings")
    before = command(port, "fingerprint", "SESSION_FINGERPRINT")
    command(port, "wake")
    deadline = time.monotonic() + 47
    while time.monotonic() < deadline:
        current = status()
        assert current["usb"] == current["battery_valid"] == "1", current
        assert current["play"] == current["rec"] == current["armed"] == "0", current
        assert current["power_countdown"] == "0", current
        assert int(current["idle_ms"]) < 1000, current
        assert current["audio_errors"] == baseline["audio_errors"], current
        time.sleep(0.5)
    running = status()
    assert int(running["blocks"]) > int(baseline["blocks"]), running
    assert command(port, "fingerprint", "SESSION_FINGERPRINT") == before
    print("PASS: USB stayed awake beyond 45 seconds, no countdown or audio errors, song unchanged",
          flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    args = parser.parse_args()
    port = serial.Serial(baudrate=115200, timeout=0.1)
    port.port = args.port
    port.dtr = port.rts = False
    with port:
        check(port)


if __name__ == "__main__":
    main()
