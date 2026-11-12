"""Build the flash-resident multisample bank from pinned, licensed recordings."""
import argparse
import concurrent.futures
import functools
import hashlib
import json
import math
import pathlib
import re
import urllib.parse
import urllib.request

import numpy as np
import soundfile as sf
from scipy import signal

ROOT = pathlib.Path(__file__).resolve().parents[1]
ASSETS = ROOT / "assets"
RATE = 16000
SOURCES = {
    "vsco": ("sgossner/VSCO-2-CE", "440300901dfe9275fd84e0b7763af1f8443ae62e", "CC0-1.0"),
    "guitar": ("sfzinstruments/karoryfer.shinyguitar",
               "57243cca85277dbcc120ce17c6178032f93c80f3", "CC0-1.0"),
    "sax": ("sfzinstruments/MTG.SoloSax",
            "b494d256549b3d088fdec176ce82867f8a1f58b2", "CC-BY-4.0"),
}
TARGETS = (48, 60, 72, 84)
# patch, source, directory, octave convention, sustained, velocity layers, roots
SPECS = [
    (0, "guitar", "Samples/acoustic", 1, False, True, TARGETS),
    (2, "vsco", "Percussion/Glock", 2, False, False, (79, 84, 91, 96)),
    (4, "guitar", "Samples/electric", 1, False, False, TARGETS),
    (5, "vsco", "Strings/Harp", 1, False, False, TARGETS),
    (6, "vsco", "Percussion/Marimba", 2, False, False, TARGETS),
    (7, "vsco", "Keys/Upright Nr1", 1, False, True, TARGETS),
    (8, "guitar", "Samples/electric", 1, False, False, TARGETS),
    (9, "vsco", "Keys/Organ/Loud", 1, True, False, TARGETS),
    (10, "sax", "", 1, True, True, (49, 60, 72, 81)),
    (11, "vsco", "Brass/Trumpet/susvib", 2, True, False, (55, 64, 73, 82)),
    (12, "vsco", "Brass/F Horn/sus", 2, True, False, (43, 52, 61, 70)),
    (13, "vsco", "Brass/Tenor Trombone/sus", 2, True, False, (34, 43, 52, 61)),
    (14, "vsco", "Strings/Cello Section/susvib", 2, True, False, TARGETS),
    (15, "vsco", "Strings/Violin Section/susVib", 2, True, False, (55, 64, 73, 82)),
    (19, "vsco", "Woodwinds/Flute/susvib", 2, True, False, (60, 69, 78, 87)),
    (20, "vsco", "Strings/Solo Contrabass/SusNV", 2, True, False, (28, 40, 52, 64)),
    (22, "vsco", "Strings/Solo Contrabass/Pizz", 2, False, False, (28, 40, 52, 64)),
    (24, "vsco", "Woodwinds/Clarinet/susLong", 2, True, False, (50, 60, 70, 80)),
    (25, "vsco", "Woodwinds/Oboe/Sus", 2, True, False, (58, 67, 76, 85)),
    (28, "vsco", "Brass/Tuba/sus", 2, True, False, (34, 41, 48, 55)),
    (29, "vsco", "Strings/Solo Violin/Arco Vib", 1, True, False, (57, 66, 75, 84)),
]
NOTE = re.compile(r"(?:_|/)([a-gA-G])([#b]?)([0-7])(?:_|[.])")


def fetch(source, path, sha=None):
    repo, revision, _ = SOURCES[source]
    url = f"https://raw.githubusercontent.com/{repo}/{revision}/{urllib.parse.quote(path)}"
    destination = ASSETS / "cache" / source / pathlib.Path(path)
    if not destination.exists():
        destination.parent.mkdir(parents=True, exist_ok=True)
        with urllib.request.urlopen(url, timeout=60) as response:
            data = response.read()
        if sha and hashlib.sha1(b"blob " + str(len(data)).encode() + b"\0" + data).hexdigest() != sha:
            raise ValueError(f"Source integrity failure: {url}")
        destination.write_bytes(data)
    data = destination.read_bytes()
    if sha and hashlib.sha1(b"blob " + str(len(data)).encode() + b"\0" + data).hexdigest() != sha:
        raise ValueError(f"Cached source integrity failure: {destination}")
    return destination


def dynamics(path):
    match = re.search(r"_v(?:l)?([1-4])(?:_|[.])", path, re.I)
    if match:
        return int(match[1])
    match = re.search(r"_(pp|p|mf|f)_", path)
    return {"pp": 0, "p": 1, "mf": 2, "f": 3}.get(match[1], 2) if match else 2


def select_manifest():
    trees = {}
    for source in SOURCES:
        tree = json.loads((ROOT / f"{source}-source-tree.json").read_text(encoding="utf-8-sig"))
        trees[source] = [row for row in tree["tree"] if row["type"] == "blob"]
    manifest = []
    for patch, source, prefix, octave, sustained, dual, targets in SPECS:
        candidates = []
        if source == "sax":
            mappings = {}
            for dynamic in ("p", "f"):
                path = fetch("sax", f"MTG Solo Saxophones/Data/alt_{dynamic}_rr1.txt")
                for line in path.read_text().splitlines():
                    match = re.search(r"key=(\d+) sample=(\S+)[.]\\?\$EXT", line)
                    if not match:
                        continue
                    tune = re.search(r"\btune=(-?\d+)", line)
                    mappings[f"MTG Solo Saxophones/Samples/{match[2]}.flac"] = (
                        int(match[1]), int(tune[1]) if tune else 0, dynamic == "f")
            for row in trees[source]:
                if row["path"] in mappings:
                    midi, cents, layer = mappings[row["path"]]
                    candidates.append((midi, layer, row, cents))
        else:
            for row in trees[source]:
                path = row["path"]
                if not path.startswith(prefix + "/") or not path.endswith(".wav"):
                    continue
                if "release" in path or "noise" in path:
                    continue
                if patch == 9:
                    midi = 35 + int(re.search(r"_(\d+)[.]wav$", path)[1])
                else:
                    match = NOTE.search(path)
                    if not match:
                        continue
                    name, accidental, number = match.groups()
                    midi = 12 * (int(number) + octave) + "c d ef g a b".index(name.lower())
                    midi += 1 if accidental == "#" else -1 if accidental == "b" else 0
                candidates.append((midi, dynamics(path), row, 0))
        if not candidates:
            raise ValueError(f"No recorded samples for patch {patch}: {prefix}")
        roots = sorted({min(candidates, key=lambda row: abs(row[0] - target))[0]
                        for target in targets})
        for midi in roots:
            available = [row for row in candidates if row[0] == midi]
            for layer in range(2 if dual else 1):
                desired = min(row[1] for row in available) if dual and not layer else max(row[1] for row in available)
                options = [row for row in available if row[1] == desired]
                chosen = min(options, key=lambda row: ("rr2" in row[2]["path"].lower(),
                                                     "rr3" in row[2]["path"].lower(), row[2]["path"]))
                manifest.append({"patch": patch, "source": source, "path": chosen[2]["path"],
                                 "blob": chosen[2]["sha"], "root": midi, "layer": layer,
                                 "sustained": sustained, "tune": chosen[3]})
    ASSETS.mkdir(exist_ok=True)
    (ASSETS / "instruments.json").write_text(json.dumps(manifest, indent=2) + "\n")
    return manifest


@functools.cache
def speaker_impulse():
    header = (ROOT / "tests" / "sound_loudness.h").read_text()
    table = re.search(r"SpeakerCurve\[\]\[2\] = \{(.*?)\};", header, re.S)
    if not table:
        raise ValueError("Missing measured speaker response")
    points = np.array(re.findall(r"\{([0-9.]+),\s*([0-9.]+)\}", table[1]), dtype=float)
    hz = np.fft.rfftfreq(2048, 1 / RATE)
    response = np.exp(np.interp(np.log(np.maximum(1, hz)),
                               np.log(points[:, 0]), np.log(points[:, 1]))) / points[:, 1].max()
    low, high = hz < 80, hz > 6000
    response[low] *= (hz[low] / 80) ** 2
    response[high] *= np.maximum(0, (8000 - hz[high]) / 2000)
    cepstrum = np.fft.irfft(np.log(np.maximum(1e-6, response)))
    cepstrum[1:1024] *= 2
    cepstrum[1025:] = 0
    return np.fft.irfft(np.exp(np.fft.rfft(cepstrum)))[:1024]


def prepare(row):
    path = fetch(row["source"], row["path"], row["blob"])
    audio, rate = sf.read(path, dtype="float64", always_2d=True)
    audio = audio.mean(axis=1)
    if len(audio) < rate * 0.3 or not np.isfinite(audio).all():
        raise ValueError(f"Invalid source recording: {path}")
    divisor = math.gcd(rate, RATE)
    audio = signal.resample_poly(audio, RATE // divisor, rate // divisor)
    if row["tune"]:
        ratio = 2 ** (row["tune"] / 1200)
        audio = signal.resample_poly(audio, 10000, round(10000 * ratio))
    audio = signal.sosfilt(signal.butter(2, 40, "highpass", fs=RATE, output="sos"), audio)
    peak = np.max(np.abs(audio))
    audible = np.flatnonzero(np.abs(audio) > peak * 0.008)
    if not len(audible):
        raise ValueError(f"Silent source: {path}")
    trim = max(0, int(audible[0]) - 32)
    audio = audio[trim:]
    if row["sustained"]:
        # Retain a little natural attack, not a long, barely audible pre-roll.
        window = RATE // 20
        # Use the same causal measured-speaker model as calibration, not raw
        # bass/breath energy that the board cannot reproduce.
        weighted = signal.fftconvolve(audio[:RATE * 3 // 5], speaker_impulse())[:RATE * 3 // 5]
        power = np.r_[0.0, np.cumsum(weighted ** 2)]
        energy = (power[window:] - power[:-window])[::RATE // 200] / window
        onset = int(np.flatnonzero(energy >= energy.max() * 0.81)[0]) * (RATE // 200)
        start = max(0, onset - RATE // 50)
        audio = audio[start:]
        trim += start
    if row["patch"] == 22:
        # Leave headroom for the recorded string detail the small speaker can reproduce.
        cutoff = 440 * 2 ** ((row["root"] - 69) / 12) * 1.5
        audio = signal.sosfilt(signal.butter(2, cutoff, "highpass", fs=RATE, output="sos"), audio)
    if row["patch"] == 4:
        # Oversampled soft clipping and a cabinet roll-off, not aliasing a hard clip.
        guitar = signal.resample_poly(audio, 4, 1)
        guitar = np.tanh(guitar * 5 / max(0.02, np.percentile(np.abs(guitar), 95)))
        guitar = signal.sosfilt(signal.butter(3, 4800, fs=RATE * 4, output="sos"), guitar)
        audio = signal.resample_poly(guitar, 1, 4)
    if row["sustained"]:
        loop_start = 4096
        loop_end = 14336
        fade = 256
        if len(audio) < loop_end + 4096:
            raise ValueError(f"Recording too short for a natural sustain: {path}")
        # Match a whole waveform segment, then overlap it into the original body.
        start = audio[loop_start:loop_start + fade]
        ends = range(11264, 14337, 256)
        loop_end = min(ends, key=lambda end: np.mean((audio[end - fade:end] - start) ** 2))
        blend = np.linspace(0, 1, fade, endpoint=False)
        body = np.concatenate((audio[:loop_end - fade],
                               audio[loop_end - fade:loop_end] * (1 - blend) + start * blend,
                               audio[loop_start + fade:loop_start + fade + 4096]))
        loop_start += fade
        audio = body
    else:
        loop_start = loop_end = 0
        audio = audio[:int(RATE * 1.8)]
    attack = min(64, len(audio))
    audio[:attack] *= np.linspace(0, 1, attack)
    tail = min(3200, len(audio))
    audio[-tail:] *= np.linspace(1, 0, tail)
    audio *= 0.9 / max(1e-8, np.max(np.abs(audio)))
    compressed = np.rint(np.sign(audio) * np.log1p(255 * np.abs(audio)) /
                         np.log(256) * 127).astype(np.int16)
    encoded = (compressed + 128).astype(np.uint8)
    decoded = np.sign(compressed) * np.expm1(np.abs(compressed) / 127 * np.log(256)) / 255
    snr = 10 * np.log10(np.sum(audio * audio) / np.sum((audio - decoded) ** 2))
    if snr < 34:
        raise ValueError(f"Insufficient sample fidelity ({snr:.1f} dB): {path}")
    return encoded.tobytes(), loop_start, loop_end, float(snr), trim


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--select", action="store_true", help="Select from pinned GitHub tree snapshots")
    args = parser.parse_args()
    manifest = select_manifest() if args.select else json.loads((ASSETS / "instruments.json").read_text())
    with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
        prepared = list(pool.map(prepare, manifest))
    bank = bytearray()
    descriptors = []
    for row, (encoded, start, end, snr, trim) in zip(manifest, prepared):
        descriptors.append((len(bank), len(encoded), start, end, row["patch"], row["root"], row["layer"]))
        bank.extend(encoded)
        row["frames"], row["loop_start"], row["loop_end"], row["snr_db"] = len(encoded), start, end, round(snr, 2)
        row["attack_trim_frames"] = trim
    if len(bank) > 2200000:
        raise ValueError(f"Instrument bank exceeds flash budget: {len(bank)} bytes")
    cpp = ['// Generated from licensed recordings by tools/build_instruments.py.',
           '#include "sample_bank.h"', 'namespace orbit {', 'const uint8_t InstrumentBank[] = {']
    cpp.extend("    " + ",".join(str(value) for value in bank[i:i + 32]) + ","
               for i in range(0, len(bank), 32))
    cpp.extend(["};", "const size_t InstrumentBankBytes = sizeof(InstrumentBank);",
                "const InstrumentSample InstrumentSamples[] = {"])
    cpp.extend("    {InstrumentBank + %d, %d, %d, %d, %d, %d, %d}," % descriptor
               for descriptor in descriptors)
    cpp.extend(["};", "const size_t InstrumentSampleCount = sizeof(InstrumentSamples) / sizeof(*InstrumentSamples);",
                "const int16_t InstrumentDecode[256] = {"])
    values = np.arange(256) - 128
    decode = np.clip(np.rint(np.sign(values) * np.expm1(np.abs(values) / 127 * np.log(256)) /
                            255 * 32767), -32768, 32767).astype(int)
    cpp.extend("    " + ",".join(str(value) for value in decode[i:i + 16]) + ","
               for i in range(0, 256, 16))
    cpp.extend(["};", "const int16_t InstrumentResampler[8][32][16] = {"])
    for cutoff in (61604, 49152, 39322, 30802, 22938, 15401, 7700, 3850):
        cpp.append("    {")
        for phase in range(32):
            taps = np.arange(16) - 7 - phase / 32
            coefficients = np.sinc(taps * cutoff / 65536) * np.hamming(16)
            coefficients = np.rint(coefficients / coefficients.sum() * 32768).astype(int)
            coefficients[7] += 32768 - coefficients.sum()
            if np.abs(coefficients).sum() > 65536 or np.abs(coefficients).max() > 32767:
                raise ValueError("Resampler would overflow its fixed-point accumulator")
            cpp.append("        {" + ",".join(map(str, coefficients)) + "},")
        cpp.append("    },")
    cpp.extend(["};", "}", ""])
    (ROOT / "Orbit" / "sample_bank.cpp").write_text("\n".join(cpp), encoding="ascii")
    (ASSETS / "instruments.json").write_text(json.dumps(manifest, indent=2) + "\n")
    licenses = ASSETS / "licenses"
    licenses.mkdir(exist_ok=True)
    for source in SOURCES:
        licenses.joinpath(source + "-LICENSE.txt").write_bytes(fetch(source, "LICENSE").read_bytes())
    print(f"Built {len(manifest)} multisamples for {len(SPECS)} instruments; "
          f"{len(bank):,} flash bytes; minimum companding SNR {min(p[3] for p in prepared):.1f} dB")


if __name__ == "__main__":
    main()
