"""Synthetic ZIP fixtures test packaging checks without an Android SDK."""
import io
from pathlib import Path
import struct
import sys
import unittest
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from verify_apk_contents import ARM_ABIS, LIBRARIES, verify_apk


def libraries():
    result = {}
    for abi, (elf_class, machine) in ARM_ABIS.items():
        header = bytearray(64)
        header[:6] = b"\x7fELF" + bytes([elf_class, 1])
        struct.pack_into("<H", header, 18, machine)
        for lib in LIBRARIES:
            result[f"lib/{abi}/{lib}"] = bytes(header)
    return result


def archive(entries):
    stream = io.BytesIO()
    with zipfile.ZipFile(stream, "w") as apk:
        for name, content in entries.items():
            apk.writestr(name, content)
    stream.seek(0)
    return stream


class ApkContentsTest(unittest.TestCase):
    def test_both_arm_abis(self):
        verify_apk(archive(libraries()))

    def test_missing_library(self):
        for name in libraries():
            with self.subTest(name=name):
                entries = libraries()
                del entries[name]
                with self.assertRaisesRegex(ValueError, "Missing required"):
                    verify_apk(archive(entries))

    def test_arm64_library_in_arm32_directory(self):
        entries = libraries()
        entries["lib/armeabi-v7a/libnova.so"] = entries["lib/arm64-v8a/libnova.so"]
        with self.assertRaisesRegex(ValueError, "Wrong or invalid ELF"):
            verify_apk(archive(entries))

    def test_wrong_machine(self):
        entries = libraries()
        header = bytearray(entries["lib/armeabi-v7a/libnova.so"])
        struct.pack_into("<H", header, 18, 3)  # x86, not ARM
        entries["lib/armeabi-v7a/libnova.so"] = header
        with self.assertRaisesRegex(ValueError, "Wrong or invalid ELF"):
            verify_apk(archive(entries))

    def test_truncated_library(self):
        entries = libraries()
        entries["lib/armeabi-v7a/libnova.so"] = b"\x7fELF"
        with self.assertRaisesRegex(ValueError, "Wrong or invalid ELF"):
            verify_apk(archive(entries))

    def test_not_a_zip(self):
        with self.assertRaises(zipfile.BadZipFile):
            verify_apk(io.BytesIO(b"not an APK"))


if __name__ == "__main__":
    unittest.main()
