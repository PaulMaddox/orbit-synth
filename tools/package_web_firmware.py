"""Package checked firmware images for the ORBIT browser installer."""

import hashlib
import json
import pathlib
import re
import shutil
import struct
import subprocess


PARTITIONS = [
    (1, 2, 0x9000, 0x5000, b"nvs"),
    (1, 0, 0xE000, 0x2000, b"otadata"),
    (0, 0x10, 0x10000, 0x2F0000, b"app0"),
    (1, 3, 0x300000, 0x10000, b"coredump"),
    (1, 0x82, 0x310000, 0xCF0000, b"spiffs"),
]
IMAGES = [
    ("bootloader", "Orbit.ino.bootloader.bin", 0, 0x8000),
    ("partitions", "Orbit.ino.partitions.bin", 0x8000, 0x1000),
    ("boot-support", "boot_app0.bin", 0xE000, 0x2000),
    ("application", "Orbit.ino.bin", 0x10000, 0x2F0000),
]


def validate_partitions(data):
    entries = []
    for offset in range(0, len(data) - 31, 32):
        magic, kind, subtype, start, size, label, flags = struct.unpack_from(
            "<HBBII16sI", data, offset)
        if magic == 0xEBEB:
            if hashlib.md5(data[:offset]).digest() != data[offset + 16:offset + 32]:
                raise ValueError("Partition table checksum does not match.")
            if entries != PARTITIONS:
                raise ValueError("Partition table is not the supported ORBIT 16 MB layout.")
            return
        if magic != 0x50AA or flags:
            raise ValueError("Invalid partition table entry.")
        entries.append((kind, subtype, start, size, label.rstrip(b"\0")))
    raise ValueError("Partition table has no checksum.")


def package_firmware(build_path, boot_app0, destination, version=None, libraries=None):
    root = pathlib.Path(__file__).resolve().parents[1]
    revision = subprocess.check_output(
        ["git", "rev-parse", "HEAD"], cwd=root, text=True).strip()
    timestamp = subprocess.check_output(
        ["git", "show", "-s", "--format=%cI", "HEAD"], cwd=root, text=True).strip()
    version = version or revision[:12]
    if not re.fullmatch(r"[a-zA-Z0-9._-]{1,80}", version):
        raise ValueError("Web firmware version must be a short filename-safe identifier.")
    parts = []
    sources = []
    for name, filename, offset, maximum in IMAGES:
        source = boot_app0 if name == "boot-support" else build_path / filename
        data = source.read_bytes()
        if not 0 < len(data) <= maximum:
            raise ValueError(f"{filename} is empty or exceeds its flash region.")
        if name in ("application", "bootloader"):
            if len(data) < 24 or data[0] != 0xE9 or struct.unpack_from("<H", data, 12)[0] != 13:
                raise ValueError(f"{filename} is not an ESP32-C6 firmware image.")
        if name == "partitions":
            validate_partitions(data)
        parts.append({
            "name": name, "path": f"{revision}/{name}.bin", "offset": offset,
            "size": len(data), "sha256": hashlib.sha256(data).hexdigest(),
        })
        sources.append(source)
    source_url = f"https://github.com/PaulMaddox/orbit-synth/commit/{revision}"
    manifest = {
        "schema": 1, "name": "ORBIT", "version": version, "revision": revision,
        "source_url": source_url, "commit_date": timestamp,
        "release_url": (
            f"https://github.com/PaulMaddox/orbit-synth/releases/tag/{version}"
            if re.fullmatch(r"build-[0-9]+", version) else source_url),
        "chip": "ESP32-C6", "flash_size": 0x1000000,
        "layout": "orbit-16mb-v1", "parts": parts,
    }
    # Validate everything before publishing any files; write the manifest last.
    for part, source in zip(parts, sources):
        target = destination / part["path"]
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, target)
    destination.mkdir(parents=True, exist_ok=True)
    for relative in (
            "THIRD_PARTY_NOTICES.md", "assets/ATTRIBUTION.txt", "assets/licenses",
            "Orbit/LICENSE-APACHE-2.0.txt", "docs/licenses"):
        source = root / relative
        target = destination / relative
        if source.is_dir():
            shutil.copytree(source, target, dirs_exist_ok=True)
        else:
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(source, target)
    for name, folder in (libraries or {}).items():
        for license_file in folder.glob("LICENSE*"):
            target = destination / "licenses" / name / license_file.name
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(license_file, target)
    (destination / "SHA256SUMS").write_text(
        "".join(f'{part["sha256"]}  {part["path"]}\n' for part in parts), encoding="utf-8")
    (destination / "manifest.json").write_text(
        json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(f"Packaged ORBIT {version} for the web installer: {destination}", flush=True)
