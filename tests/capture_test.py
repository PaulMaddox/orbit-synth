"""Windows acoustic regression: PC speaker -> ORBIT microphone.

Requires pyserial and an audible PC speaker near the board. Replaces the
device's microphone sample with a test tone; never saves recorded audio here.
"""

import argparse
import io
import math
import struct
import threading
import time
import wave
import winsound

import serial


def line_until(port, prefix, timeout=5):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        line = port.readline()
        if line.startswith(prefix):
            return line
        if b"ERROR" in line:
            raise RuntimeError(line.decode(errors="replace").strip())
    raise TimeoutError(f"No {prefix!r} response")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--replace-sample", action="store_true", required=True,
                        help="Acknowledge replacing the device's recorded sample")
    args = parser.parse_args()
    tone = io.BytesIO()
    samples = [int(12000 * math.sin(2 * math.pi * i / 16)) for i in range(96000)]
    with wave.open(tone, "wb") as wav:
        wav.setnchannels(1)
        wav.setsampwidth(2)
        wav.setframerate(16000)
        wav.writeframes(struct.pack(f"<{len(samples)}h", *samples))

    player = threading.Thread(target=winsound.PlaySound,
                              args=(tone.getvalue(), winsound.SND_MEMORY))
    with serial.Serial(args.port, 115200, timeout=0.1) as port:
        time.sleep(1)
        port.reset_input_buffer()
        port.write(b"stop\npage 3\n")
        time.sleep(0.3)
        player.start()
        try:
            time.sleep(0.8)
            port.reset_input_buffer()
            port.write(b"input-probe\n")
            frames = [
                [int(value) for value in line_until(port, b"INPUT ").split()[1:]]
                for _ in range(64)
            ]
            zero_frames = sum(not any(frame) for frame in frames)
            assert zero_frames == 0, f"{zero_frames}/64 input frames were zero-filled"
            port.write(b"mic\n")
            time.sleep(2.7)
            port.reset_input_buffer()
            port.write(b"status\n")
            status = line_until(port, b"ORBIT_STATUS").decode().split()
            fields = dict(value.split("=", 1) for value in status[1:])
            assert fields["audio_errors"] == "0", fields
            assert fields["capture_drops"] == "0", fields
            port.write(b"sample-dump\n")
            header = line_until(port, b"SAMPLE_DATA ")
            count = int(header.split(b"=")[1])
            assert count == 32000, count
            port.timeout = 5
            data = port.read(count * 2)
            assert len(data) == count * 2, "Incomplete sample export"
            recorded = struct.unpack(f"<{count}h", data)
            purities = []
            # Use the central second, clear of capture-switching and edge fades.
            for start in range(8000, 22401, 1600):
                window = recorded[start:start + 1600]
                mean = sum(window) / len(window)
                window = [value - mean for value in window]
                power = sum(value * value for value in window)
                cosine = sum(value * math.cos(2 * math.pi * i / 16)
                             for i, value in enumerate(window))
                sine = sum(value * math.sin(2 * math.pi * i / 16)
                           for i, value in enumerate(window))
                purity = 200 * (cosine * cosine + sine * sine) / (len(window) * power) if power else 0
                print(f"sample={start} tone_energy={purity:.1f}%")
                purities.append(purity)
            assert min(purities) > 90, f"Distorted/missing 1 kHz tone: {purities}"
            time.sleep(1.5)
            print("PASS: continuous input, no capture drops, and clean 1 kHz recording")
        finally:
            player.join()


if __name__ == "__main__":
    main()
