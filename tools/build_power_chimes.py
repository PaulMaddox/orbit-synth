"""Encode the approved original startup/shutdown WAVs without changing their sound."""

import argparse
import array
import hashlib
import pathlib
import sys
import wave

ROOT = pathlib.Path(__file__).resolve().parents[1]
SOURCES = (
    ("Startup", 15920, "af26d8fa97d3fca468ee837dc1cb79291e6b6edef6ea9b10933fd96c17f32be5"),
    ("Shutdown", 16000, "deda229d9cff92ba4bebc38c84bf8ca6f0b8a6065d38bd3f03905152ba7a7d45"),
)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="Check the generated bank without writing")
    args = parser.parse_args()
    cpp = ['// Approved original power chimes; tools/build_power_chimes.py.',
           '#include "power_chime.h"', 'namespace orbit {']
    for name, frames, digest in SOURCES:
        source = ROOT / "tools" / "power-chimes" / f"{name.lower()}.wav"
        if hashlib.sha256(source.read_bytes()).hexdigest() != digest:
            raise ValueError(f"Approved chime source changed: {source}")
        with wave.open(str(source), "rb") as audio:
            if (audio.getnchannels(), audio.getsampwidth(), audio.getframerate(),
                    audio.getnframes(), audio.getcomptype()) != (1, 2, 16000, frames, "NONE"):
                raise ValueError(f"Incorrect chime format: {source}")
            samples = array.array("h", audio.readframes(frames))
        if sys.byteorder != "little":
            samples.byteswap()
        if len(samples) != frames:
            raise ValueError(f"Short chime source: {source}")
        cpp.append(f"static const int16_t {name}Pcm[] = {{")
        cpp.extend("    " + ",".join(map(str, samples[i:i + 16])) + ","
                   for i in range(0, frames, 16))
        cpp.extend(["};", f"const PowerChimeAudio {name}Chime = "
                    f"{{{name}Pcm, sizeof({name}Pcm) / sizeof({name}Pcm[0])}};"])
    cpp.extend(["}", ""])
    content = "\n".join(cpp)
    target = ROOT / "Orbit" / "power_chime.cpp"
    if args.check:
        if target.read_text(encoding="ascii") != content:
            raise ValueError("Power chime bank differs; run python tools/build_power_chimes.py")
        print("PASS: power chimes match the approved WAVs")
    else:
        target.write_text(content, encoding="ascii")


if __name__ == "__main__":
    main()
