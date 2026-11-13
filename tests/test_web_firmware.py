import hashlib
import json
import pathlib
import struct
import tempfile
import unittest
from unittest.mock import patch

from tools.package_web_firmware import PARTITIONS, package_firmware


def partition_table():
    entries = b"".join(struct.pack("<HBBII16sI", 0x50AA, kind, subtype, offset, size, label, 0)
                       for kind, subtype, offset, size, label in PARTITIONS)
    return (entries + b"\xeb\xeb" + b"\xff" * 14 + hashlib.md5(entries).digest()).ljust(3072, b"\xff")


class WebFirmwareTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = pathlib.Path(self.temporary.name)
        self.destination = self.root / "web"
        image = bytearray(128)
        image[0] = 0xE9
        struct.pack_into("<H", image, 12, 13)
        (self.root / "Orbit.ino.bin").write_bytes(image)
        (self.root / "Orbit.ino.bootloader.bin").write_bytes(image)
        (self.root / "Orbit.ino.partitions.bin").write_bytes(partition_table())
        self.boot_app0 = self.root / "boot_app0.bin"
        self.boot_app0.write_bytes(b"\xff" * 8192)
        self.git = patch("tools.package_web_firmware.subprocess.check_output",
                         side_effect=["a" * 40, "2026-11-13T00:00:00+00:00"])
        self.git_mock = self.git.start()
        self.addCleanup(self.git.stop)

    def package(self):
        package_firmware(self.root, self.boot_app0, self.destination, "build-42")

    def test_packages_the_exact_images_and_hashes_at_safe_offsets(self):
        self.package()
        manifest = json.loads((self.destination / "manifest.json").read_text())
        self.assertEqual(manifest["version"], "build-42")
        self.assertEqual(manifest["chip"], "ESP32-C6")
        self.assertEqual([part["offset"] for part in manifest["parts"]], [0, 0x8000, 0xE000, 0x10000])
        for part in manifest["parts"]:
            packaged = (self.destination / part["path"]).read_bytes()
            self.assertEqual(part["size"], len(packaged))
            self.assertEqual(part["sha256"], hashlib.sha256(packaged).hexdigest())
        self.assertEqual((self.destination / ("a" * 40) / "application.bin").read_bytes(),
                         (self.root / "Orbit.ino.bin").read_bytes())

    def test_rejects_an_image_for_a_different_chip_before_publishing(self):
        image = bytearray((self.root / "Orbit.ino.bin").read_bytes())
        struct.pack_into("<H", image, 12, 9)
        (self.root / "Orbit.ino.bin").write_bytes(image)
        with self.assertRaisesRegex(ValueError, "ESP32-C6"):
            self.package()
        self.assertFalse(self.destination.exists())

    def test_rejects_an_oversized_application_before_publishing(self):
        (self.root / "Orbit.ino.bin").write_bytes(b"x" * (0x2F0000 + 1))
        with self.assertRaisesRegex(ValueError, "flash region"):
            self.package()
        self.assertFalse(self.destination.exists())

    def test_rejects_corrupt_or_different_partition_layouts(self):
        for corruption in ("checksum", "layout"):
            with self.subTest(corruption=corruption):
                table = bytearray(partition_table())
                table[4] ^= 0x10
                if corruption == "layout":
                    table[176:192] = hashlib.md5(table[:160]).digest()
                (self.root / "Orbit.ino.partitions.bin").write_bytes(table)
                with self.assertRaisesRegex(ValueError, "checksum|16 MB layout"):
                    self.package()
                self.assertFalse(self.destination.exists())
                self.git_mock.side_effect = ["a" * 40, "2026-11-13T00:00:00+00:00"]


if __name__ == "__main__":
    unittest.main()
