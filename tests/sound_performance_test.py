"""Exercise twelve sampled/synth voices with live FX, then restore the session."""
import json
import pathlib
import time

import serial

port = serial.Serial(baudrate=115200, timeout=0.1)
port.port = "COM3"
port.dtr = port.rts = False
with port:
    time.sleep(1)
    port.reset_input_buffer()

    def send(command, prefix=None):
        port.write((command + "\n").encode())
        deadline = time.monotonic() + (3 if prefix else 0.15)
        while time.monotonic() < deadline:
            line = port.readline().decode(errors="replace").strip()
            if "ERROR" in line:
                raise RuntimeError(line)
            if prefix and line.startswith(prefix):
                return dict(item.split("=", 1) for item in line.split()[1:])
        if prefix:
            raise RuntimeError(f"No {prefix} response")

    baseline = send("status", "ORBIT_STATUS")
    if any(baseline[key] != "0" for key in
           ("play", "rec", "armed", "capture", "fx_rec", "fx_armed", "fx_touch", "next_scene")):
        raise RuntimeError("Finish playback, recording and live touching first")
    before = send("fingerprint", "SESSION_FINGERPRINT")
    patches = [int(value) for value in baseline["patch"].split(",")]
    results = []
    try:
        send("volume 8")
        for track in (0, 1):
            send(f"track {track}")
            try:
                for sound in (0, 4, 10, 15, 17, 18, 19, 27, 28):
                    send(f"preview {sound + 1}")
                    send("use-sound")
                    time.sleep(0.55)
                    send("fx 25 80")
                    port.write(b"note 0\n" * 12)
                    time.sleep(0.25)
                    health = send("status", "ORBIT_STATUS")
                    results.append({"track": track, "sound": sound, "status": health})
                    if health["audio_errors"] != baseline["audio_errors"]:
                        raise RuntimeError(f"Audio errors: {health}")
                    if int(health["max_render_us"]) >= 4000:
                        raise RuntimeError(f"Missed audio deadline: {health}")
                    send("release")
                    send("stop")
                    time.sleep(0.25)
            finally:
                send(f"preview {patches[track] + 1}")
                send("use-sound")
                send("stop")
        print(f"PASS: twelve voices with FX; max render "
              f"{max(int(row['status']['max_render_us']) for row in results)} / 4000 us")
    finally:
        send("release")
        send("stop")
        send("fx-release")
        send(f"track {baseline['selected']}")
        send(f"volume {baseline['volume']}")
        send(f"page {baseline['page'] if baseline['page'] != '7' else '0'}")
        time.sleep(1.5)
        after = send("fingerprint", "SESSION_FINGERPRINT")
        pathlib.Path("device-sound-performance.json").write_text(
            json.dumps({"before": before, "after": after, "results": results}, indent=2))
        if after != before:
            raise RuntimeError(f"Music fingerprint changed: {before} -> {after}")
