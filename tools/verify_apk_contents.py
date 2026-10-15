#!/usr/bin/env python3
"""Check the shipped Android libraries, not just Gradle's requested ABI list."""
import struct
import sys
import zipfile

# ELF class and e_machine for each Android ABI (little-endian ELF).
APK_ABIS = {"arm64-v8a": (2, 183), "armeabi-v7a": (1, 40), "x86": (1, 3), "x86_64": (2, 62)}
# The project uses CMake/NDK's default static C++ runtime.
LIBRARIES = ("libnova.so",)


def verify_apk(path):
    with zipfile.ZipFile(path) as apk:
        bad_entry = apk.testzip()
        if bad_entry:
            raise ValueError(f"APK CRC mismatch: {bad_entry}")
        for abi, (elf_class, machine) in APK_ABIS.items():
            libraries = list(LIBRARIES)
            if f"lib/{abi}/libc++_shared.so" in apk.namelist():
                libraries.append("libc++_shared.so")
            for library in libraries:
                name = f"lib/{abi}/{library}"
                try:
                    with apk.open(name) as entry:
                        header = entry.read(64)
                except KeyError as exc:
                    raise ValueError(f"Missing required native library: {name}") from exc
                if (len(header) < 52 or header[:4] != b"\x7fELF"
                        or header[4] != elf_class or header[5] != 1
                        or struct.unpack_from("<H", header, 18)[0] != machine):
                    raise ValueError(f"Wrong or invalid ELF architecture: {name}")
            print(f"Verified native libraries: {abi}")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit("Usage: verify_apk_contents.py path/to/app.apk")
    try:
        verify_apk(sys.argv[1])
    except (ValueError, OSError, zipfile.BadZipFile) as error:
        sys.exit(f"::error title=APK contents::APK verification failed: {error}")
