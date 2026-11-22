#!/usr/bin/env python3
"""Build/package public OTA artifacts locally; never upload or publish."""
import argparse
import hashlib
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess

root = Path(__file__).resolve().parents[1]
deviceId = "waveshare-esp32-s3-rs485-can"
slotSize = 0x300000


def validateImage(image, version):
    if not 288 <= len(image) <= slotSize:
        raise ValueError("Firmware does not fit the existing 3 MiB OTA partition")
    if image[0] != 0xE9 or struct.unpack_from("<H", image, 12)[0] != 9:
        raise ValueError("Expected a raw ESP32-S3 application image")
    if struct.unpack_from("<I", image, 32)[0] != 0xABCD5432:
        raise ValueError("Missing application descriptor (bootloader/merged image rejected)")
    marker = f"RS485FW|{deviceId}|{version}|publicRelease|".encode()
    if marker not in image:
        raise ValueError("Wrong device/version identity or not a public release build")


def prepareArtifacts(imagePath, output, version, legacyKey=None,
                     legacyHeader=root / "include/ota_public_key.h"):
    image = imagePath.read_bytes()
    validateImage(image, version)
    if legacyKey:
        public = subprocess.check_output(["openssl", "pkey", "-in", str(legacyKey), "-pubout"])
        if public.decode().strip() not in legacyHeader.read_text():
            raise ValueError("Key does not match the legacy device trust anchor")
    digest = hashlib.sha256(image).hexdigest()
    manifest = (f"format=1\ndevice={deviceId}\nversion={version}\nsize={len(image)}\n"
                f"sha256={digest}\nimage=firmware.bin\n").encode()
    output.mkdir(parents=True, exist_ok=True)
    # Remove obsolete signature/public-key files when reusing a staging directory.
    for name in ("firmware.manifest.sig", "firmware-public.pem"):
        (output / name).unlink(missing_ok=True)
    (output / "firmware.bin").write_bytes(image)
    (output / "firmware.sha256").write_text(f"{digest}  firmware.bin\n")
    manifestPath = output / "firmware.manifest"
    manifestPath.write_bytes(manifest)
    if legacyKey:
        signature = output / "firmware.manifest.sig"
        subprocess.run(["openssl", "dgst", "-sha256", "-sign", str(legacyKey),
                        "-out", str(signature), str(manifestPath)], check=True)
        (output / "firmware-public.pem").write_bytes(public)
        subprocess.run(["openssl", "dgst", "-sha256", "-verify", str(output / "firmware-public.pem"),
                        "-signature", str(signature), str(manifestPath)], check=True)
        print("Legacy transition signature added for devices running <=1.1.3.")
    print(f"Firmware: {len(image):,} / {slotSize:,} bytes; {slotSize - len(image):,} bytes free")
    print(f"Local release ready: {output}\nNo firmware uploaded; no GitHub Release published.")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=["build", "package"])
    parser.add_argument("--output", type=Path, default=root / "dist")
    parser.add_argument("--release-tag", default=os.environ.get("RELEASE_TAG", ""))
    parser.add_argument("--legacy-key", type=Path, help="One-time transition only: sign for old installed firmware")
    managedPio = Path.home() / ".platformio/penv/bin/platformio"
    parser.add_argument("--pio", default=str(managedPio) if managedPio.is_file() else shutil.which("platformio") or "platformio")
    args = parser.parse_args()
    config = (root / "include/firmware_config.h").read_text()
    version = re.search(r'^#define FIRMWARE_VERSION "([A-Za-z0-9._-]+)"$', config, re.M).group(1)
    if args.release_tag and args.release_tag not in (version, "v" + version):
        parser.error(f"Release tag must match firmware version {version}")
    if args.command == "build":
        print(f"Using PlatformIO: {args.pio}", flush=True)
        subprocess.run([args.pio, "run", "-e", "esp32-s3-release"], cwd=root, check=True)
    prepareArtifacts(root / ".pio/build/esp32-s3-release/firmware.bin", args.output / version,
                     version, args.legacy_key)


if __name__ == "__main__":
    main()
