import hashlib
import importlib.util
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest

root = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("release", root / "scripts/release.py")
release = importlib.util.module_from_spec(spec)
spec.loader.exec_module(release)


class ReleaseTest(unittest.TestCase):
    def test_public_build_excludes_local_credentials(self):
        with tempfile.TemporaryDirectory() as directory:
            directory = Path(directory)
            for name in ["Arduino.h", "esp_http_client.h", "Preferences.h", "WiFi.h", "esp_wifi.h", "esp_system.h"]:
                (directory / name).write_text("")
            (directory / "wifi_secrets.h").write_text("#error LOCAL_CREDENTIAL_HEADER_INCLUDED\n")
            command = ["g++", "-E", "-I" + str(directory), "-I" + str(root / "include"), str(root / "src/network_manager.cpp")]
            public = subprocess.run(command + ["-DGATEWAY_PUBLIC_RELEASE=1"], capture_output=True)
            self.assertEqual(public.returncode, 0, public.stderr.decode())
            usb = subprocess.run(command, capture_output=True)
            self.assertNotEqual(usb.returncode, 0)
            self.assertIn(b"LOCAL_CREDENTIAL_HEADER_INCLUDED", usb.stderr)

    def makeImage(self):
        image = bytearray(2048)
        image[0] = 0xE9
        struct.pack_into("<H", image, 12, 9)
        struct.pack_into("<I", image, 32, 0xABCD5432)
        identity = f"RS485FW|{release.deviceId}|1.2.0|publicRelease|".encode()
        image[512:512+len(identity)] = identity
        return image

    def test_unsigned_artifacts_and_invalid_images(self):
        with tempfile.TemporaryDirectory() as directory:
            directory = Path(directory)
            image = self.makeImage()
            firmware = directory / "firmware.bin"
            firmware.write_bytes(image)
            output = directory / "release"
            output.mkdir()
            (output / "firmware.manifest.sig").write_text("stale signature")
            release.prepareArtifacts(firmware, output, "1.2.0")
            self.assertEqual({p.name for p in output.iterdir()},
                             {"firmware.bin", "firmware.sha256", "firmware.manifest"})
            self.assertEqual((output / "firmware.bin").read_bytes(), image)
            digest = hashlib.sha256(image).hexdigest()
            self.assertEqual((output / "firmware.sha256").read_text(), f"{digest}  firmware.bin\n")
            self.assertIn(f"sha256={digest}\n", (output / "firmware.manifest").read_text())
            for offset in [0, 12, 32, 512]:
                broken = image.copy()
                broken[offset] = 0
                with self.assertRaises(ValueError):
                    release.validateImage(broken, "1.2.0")
            with self.assertRaises(ValueError):
                release.validateImage(image, "wrong-version")
            with self.assertRaises(ValueError):
                release.validateImage(bytes(release.slotSize + 1), "1.2.0")

    def test_legacy_transition_signature(self):
        with tempfile.TemporaryDirectory() as directory:
            directory = Path(directory)
            key = directory / "key.pem"
            subprocess.run(["openssl", "genpkey", "-algorithm", "EC", "-pkeyopt",
                            "ec_paramgen_curve:P-256", "-out", str(key)], check=True)
            public = subprocess.check_output(["openssl", "pkey", "-in", str(key), "-pubout"])
            header = directory / "legacy.h"
            header.write_bytes(public)
            firmware = directory / "firmware.bin"
            firmware.write_bytes(self.makeImage())
            output = directory / "release"
            release.prepareArtifacts(firmware, output, "1.2.0", key, header)
            manifest = output / "firmware.manifest"
            manifest.write_bytes(manifest.read_bytes().replace(b"version=1.2.0", b"version=9.9.9"))
            verification = subprocess.run(["openssl", "dgst", "-sha256", "-verify", str(output / "firmware-public.pem"),
                                           "-signature", str(output / "firmware.manifest.sig"), str(manifest)], capture_output=True)
            self.assertNotEqual(verification.returncode, 0)
            header.write_text("wrong public key")
            with self.assertRaises(ValueError):
                release.prepareArtifacts(firmware, output, "1.2.0", key, header)

    def test_mismatched_release_tag(self):
        result = subprocess.run(["python3", str(root / "scripts/release.py"), "package",
                                 "--release-tag", "v0.0.0"], capture_output=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn(b"Release tag must match", result.stderr)


if __name__ == "__main__":
    unittest.main()
