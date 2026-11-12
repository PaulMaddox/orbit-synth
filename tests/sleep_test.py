"""Check real USB standby/resume without playing music or changing the session."""

import argparse
import time

import serial


def command(port, text, prefix=None):
    port.write((text + "\n").encode("ascii"))
    deadline = time.monotonic() + (5 if prefix else 0.5)
    while time.monotonic() < deadline:
        line = port.readline().decode("utf-8", errors="replace").strip()
        if "ERROR" in line or "FATAL" in line:
            raise RuntimeError(line)
        if prefix and line.startswith(prefix + " "):
            return dict(item.split("=", 1) for item in line.split()[1:])
    if prefix:
        raise RuntimeError(f"No {prefix} response to {text}")


def check(port, cycles):
    status = lambda: command(port, "status", "ORBIT_STATUS")
    baseline = status()
    if baseline["usb"] != "1" or baseline["battery_valid"] != "1":
        raise RuntimeError("This check requires USB power and valid PMU readings")
    if any(baseline[key] != "0" for key in
           ("play", "rec", "armed", "capture", "fx_rec", "fx_armed", "fx_touch", "next_scene")):
        raise RuntimeError("Finish playback, recording and touching the board first")
    before = command(port, "fingerprint", "SESSION_FINGERPRINT")
    try:
        for cycle in range(cycles):
            command(port, "wake")
            deadline = time.monotonic() + 70
            while time.monotonic() < deadline:
                sleeping = status()
                if sleeping["screen_sleep"] == "1":
                    break
                time.sleep(1)
            else:
                raise RuntimeError("Device did not sleep after 60 seconds")
            assert sleeping["audio_sleep"] == "1", sleeping
            assert sleeping["sleep_error"] == "0", sleeping
            time.sleep(0.5)
            still = status()
            for key in ("blocks", "rx_drops", "light_sleeps", "light_sleep_us"):
                assert still[key] == sleeping[key], (key, sleeping, still)
            command(port, "wake")
            awake = status()
            assert awake["screen_sleep"] == awake["audio_sleep"] == "0", awake
            assert awake["sleep_error"] == "0", awake
            time.sleep(0.5)
            running = status()
            assert int(running["blocks"]) > int(awake["blocks"]), running
            assert running["audio_errors"] == baseline["audio_errors"], running
            assert running["capture_drops"] == baseline["capture_drops"], running
            assert command(port, "fingerprint", "SESSION_FINGERPRINT") == before
            print(f"PASS cycle {cycle + 1}: I2S stopped, USB responsive, audio resumed, session intact",
                  flush=True)
    finally:
        command(port, "wake")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--cycles", type=int, default=2)
    args = parser.parse_args()
    if args.cycles < 1:
        parser.error("--cycles must be positive")
    port = serial.Serial(baudrate=115200, timeout=0.1)
    port.port = args.port
    port.dtr = port.rts = False
    with port:
        time.sleep(1)
        check(port, args.cycles)


if __name__ == "__main__":
    main()
