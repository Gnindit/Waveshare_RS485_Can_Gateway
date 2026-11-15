#!/usr/bin/env python3
"""Prepare signed local OTA release artifacts; never upload or publish."""
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


def run(*args, **kwargs):
    return subprocess.check_output(args, **kwargs)


def publicPem(key):
    return run("openssl", "pkey", "-in", str(key), "-pubout")


def initKey(key, publicHeader):
    if key.exists():
        raise SystemExit("Signing key already exists; refusing to replace it")
    if publicHeader.exists() and 'otaPublicKey[] = ""' not in publicHeader.read_text():
        raise SystemExit("Public trust anchor already exists; refusing to rotate it")
    key.parent.mkdir(parents=True, exist_ok=True)
    oldMask = os.umask(0o077)
    try:
        subprocess.run(["openssl", "genpkey", "-algorithm", "EC", "-pkeyopt",
                        "ec_paramgen_curve:P-256", "-out", str(key)], check=True)
    finally:
        os.umask(oldMask)
    publicHeader.parent.mkdir(parents=True, exist_ok=True)
    publicHeader.write_text('#pragma once\n// Public ECDSA P-256 OTA trust anchor; safe to commit.\n'
                           'constexpr const char otaPublicKey[] = R"key(\n' + publicPem(key).decode() + ')key";\n')
    print(f"Created private key: {key}\nPublic trust anchor: {publicHeader}")
    print("Back up the private key securely. Losing it prevents signing updates for this trust anchor.")


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


def prepareArtifacts(imagePath, output, key, version):
    image = imagePath.read_bytes()
    validateImage(image, version)
    # A release must itself trust the key used to sign it, preserving the next OTA.
    if publicPem(key).strip() not in image:
        raise ValueError("Firmware does not embed this signing key's public trust anchor")
    digest = hashlib.sha256(image).hexdigest()
    manifest = (f"format=1\ndevice={deviceId}\nversion={version}\nsize={len(image)}\n"
                f"sha256={digest}\nimage=firmware.bin\n").encode()
    output.mkdir(parents=True, exist_ok=True)
    (output / "firmware.bin").write_bytes(image)
    (output / "firmware.sha256").write_text(f"{digest}  firmware.bin\n")
    manifestPath = output / "firmware.manifest"
    manifestPath.write_bytes(manifest)
    subprocess.run(["openssl", "dgst", "-sha256", "-sign", str(key), "-out",
                    str(output / "firmware.manifest.sig"), str(manifestPath)], check=True)
    publicPath = output / "firmware-public.pem"
    publicPath.write_bytes(publicPem(key))
    subprocess.run(["openssl", "dgst", "-sha256", "-verify", str(publicPath), "-signature",
                    str(output / "firmware.manifest.sig"), str(manifestPath)], check=True)
    print(f"Firmware: {len(image):,} / {slotSize:,} bytes; {slotSize - len(image):,} bytes free")
    print(f"Local release ready: {output}\nNo firmware uploaded; no GitHub Release published.")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=["init-key", "build"])
    parser.add_argument("--key", type=Path, default=root / ".secrets/firmware-signing-key.pem")
    parser.add_argument("--public-header", type=Path, default=root / "include/ota_public_key.h")
    parser.add_argument("--output", type=Path, default=root / "release")
    managedPio = Path.home() / ".platformio/penv" / ("Scripts/platformio.exe" if os.name == "nt" else "bin/pio")
    parser.add_argument("--pio", default=str(managedPio) if managedPio.is_file() else shutil.which("pio") or "pio")
    args = parser.parse_args()
    if args.command == "init-key":
        initKey(args.key, args.public_header)
        return
    if not args.key.is_file():
        parser.error("Missing private signing key. Run init-key once, or restore your existing key.")
    if publicPem(args.key).decode().strip() not in args.public_header.read_text():
        parser.error("Signing key does not match include/ota_public_key.h")
    print(f"Using PlatformIO: {args.pio}", flush=True)
    subprocess.run([args.pio, "run", "-e", "esp32-s3-release"], cwd=root, check=True)
    config = (root / "include/firmware_config.h").read_text()
    version = re.search(r'^#define FIRMWARE_VERSION "([A-Za-z0-9._-]+)"$', config, re.M).group(1)
    prepareArtifacts(root / ".pio/build/esp32-s3-release/firmware.bin", args.output / version, args.key, version)


if __name__ == "__main__":
    main()
