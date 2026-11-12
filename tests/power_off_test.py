"""Check idle countdown/cancellation; --shutdown also powers off the real board."""

import argparse
import time

import serial
from serial.tools import list_ports


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


def check(port, shutdown):
    status = lambda: command(port, "status", "ORBIT_STATUS")
    baseline = status()
    if any(baseline[key] != "0" for key in
           ("play", "rec", "armed", "capture", "fx_rec", "fx_armed", "fx_touch", "next_scene")):
        raise RuntimeError("Finish playback, recording and touching the board first")
    before = command(port, "fingerprint", "SESSION_FINGERPRINT")
    command(port, "wake")
    warning_draws = None
    seen = set()
    deadline = time.monotonic() + 46
    try:
        while time.monotonic() < deadline:
            current = status()
            idle = int(current["idle_ms"])
            seconds = int(current["power_countdown"])
            if idle < 30000:
                assert seconds == 0, current
            elif seconds:
                assert seconds == (45000 - idle + 999) // 1000, current
                seen.add(seconds)
                if warning_draws is None:
                    # The first warning draw may still be pending on the UI tick.
                    if seconds < 15:
                        warning_draws = current["full_draws"]
                else:
                    assert current["full_draws"] == warning_draws, current
                if seconds == 1:
                    break
            time.sleep(0.1)
        else:
            raise RuntimeError("No complete 15-second warning within 45 idle seconds")
        assert seen == set(range(1, 16)), seen
    finally:
        command(port, "wake")
    cancelled = status()
    assert cancelled["power_countdown"] == "0", cancelled
    assert int(cancelled["idle_ms"]) < 2000, cancelled
    assert cancelled["audio_errors"] == baseline["audio_errors"], cancelled
    assert command(port, "fingerprint", "SESSION_FINGERPRINT") == before
    print("PASS: 15..1 countdown, incremental redraws, cancellation, session unchanged",
          flush=True)
    if not shutdown:
        return
    command(port, "wake")
    deadline = time.monotonic() + 48
    while time.monotonic() < deadline:
        line = port.readline().decode("utf-8", errors="replace").strip()
        if "ERROR" in line or "FATAL" in line:
            raise RuntimeError(line)
        if line.startswith("POWER_OFF "):
            break
    else:
        raise RuntimeError("No PMU shutdown announcement")
    port.close()
    deadline = time.monotonic() + 5
    while time.monotonic() < deadline:
        if port.port not in [item.device for item in list_ports.comports()]:
            print("PASS: shutdown announced and USB disconnected. Verify the screen is off, "
                  "then press PWR and check the saved session returns.", flush=True)
            return
        time.sleep(0.1)
    raise RuntimeError("USB remained connected after PMU shutdown")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--shutdown", action="store_true",
                        help="Also power off; requires the physical PWR button to restart")
    args = parser.parse_args()
    port = serial.Serial(baudrate=115200, timeout=0.1)
    port.port = args.port
    port.dtr = port.rts = False
    with port:
        check(port, args.shutdown)


if __name__ == "__main__":
    main()
