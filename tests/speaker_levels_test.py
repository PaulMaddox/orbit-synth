"""Measure synth previews through the board microphone; preserve all music."""
import argparse
import json
import math
import pathlib
import time

import serial

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--port", default="COM3")
parser.add_argument("--output", type=pathlib.Path, required=True)
args = parser.parse_args()

port = serial.Serial(baudrate=115200, timeout=0.1)
port.port = args.port
port.dtr = port.rts = False
with port:
    time.sleep(1)
    port.reset_input_buffer()

    def send(command, prefix=None):
        if command is not None:
            port.write((command + "\n").encode())
        lines = []
        deadline = time.monotonic() + (3 if prefix else 0.18)
        while time.monotonic() < deadline:
            line = port.readline().decode(errors="replace").strip()
            if not line:
                continue
            lines.append(line)
            if "ERROR" in line:
                raise RuntimeError(line)
            if prefix and line.startswith(prefix):
                return dict(item.split("=", 1) for item in line.split()[1:])
        if prefix:
            raise RuntimeError(f"No {prefix} response: {lines}")

    def status():
        return send("status", "ORBIT_STATUS")

    baseline = status()
    if any(baseline[key] != "0" for key in ("play", "rec", "armed", "capture", "fx_rec", "fx_armed", "next_scene")):
        raise RuntimeError("Finish playback or recording before measuring speaker levels")
    before = send("fingerprint", "SESSION_FINGERPRINT")
    pitches = [int(value) for value in baseline["pitch"].split(",")]
    echoes = [int(value) for value in baseline["echo"].split(",")]
    results = {"baseline": baseline, "fingerprint": before, "measurements": [],
               "metric": "peak80ms-v1"}
    spreads = []
    try:
        send("wake")
        send("volume 20")
        floor = send("speaker-level", "SPEAKER_LEVEL")
        if int(floor["clips"]):
            raise RuntimeError("Microphone clips before playback")
        results["noise_rms"] = float(floor["note_rms"])
        print(f"Background note RMS {floor['note_rms']}", flush=True)
        for track, pitch in ((0, -12), (0, 0), (1, 0)):
            send(f"track {track}")
            send("echo 0")
            send(f"pitch {pitch}")
            readings = []
            for sound in range(30):
                port.write(f"preview {sound + 1}\nspeaker-level\n".encode())
                meter = send(None, "SPEAKER_LEVEL")
                if int(meter["clips"]):
                    raise RuntimeError(f"Microphone clips at track {track}, sound {sound}")
                energy = float(meter["note_rms"]) ** 2 - results["noise_rms"] ** 2
                if energy <= 0:
                    raise RuntimeError(f"No sound above background: track {track}, sound {sound}")
                reading = {"track": track, "pitch": pitch, "sound": sound, "rms": math.sqrt(energy)}
                readings.append(reading)
                results["measurements"].append(reading)
                time.sleep(0.65)
            quiet = min(readings, key=lambda row: row["rms"])
            loud = max(readings, key=lambda row: row["rms"])
            spreads.append(20 * math.log10(loud["rms"] / quiet["rms"]))
            print(f"track={track} pitch={pitch}: {quiet['rms']:.1f} (sound {quiet['sound']}) .. "
                  f"{loud['rms']:.1f} (sound {loud['sound']}), "
                  f"spread={20 * math.log10(loud['rms'] / quiet['rms']):.2f} dB", flush=True)
            send(f"pitch {pitches[track]}")
            send(f"echo {echoes[track]}")
        health = status()
        if health["audio_errors"] != baseline["audio_errors"]:
            raise RuntimeError(f"Audio errors increased: {health}")
        results["health"] = health
        if max(spreads) > 3:
            raise RuntimeError(f"Acoustic preset spread exceeds 3 dB: {spreads}")
    finally:
        send("release")
        send("stop")
        for track in (0, 1):
            send(f"track {track}")
            send(f"pitch {pitches[track]}")
            send(f"echo {echoes[track]}")
        send(f"volume {baseline['volume']}")
        send(f"track {baseline['selected']}")
        send(f"page {baseline['page'] if baseline['page'] != '7' else '0'}")
        time.sleep(1.5)
        after = send("fingerprint", "SESSION_FINGERPRINT")
        results["after"] = after
        args.output.write_text(json.dumps(results, indent=2), encoding="utf-8")
        if after != before:
            raise RuntimeError(f"Music fingerprint changed: {before} -> {after}")
