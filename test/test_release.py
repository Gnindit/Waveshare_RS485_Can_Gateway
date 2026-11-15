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

    def test_signed_artifacts_and_tampering(self):
        with tempfile.TemporaryDirectory() as directory:
            directory = Path(directory)
            key = directory / "key.pem"
            header = directory / "public.h"
            release.initKey(key, header)
            self.assertEqual(key.stat().st_mode & 0o777, 0o600)
            with self.assertRaises(SystemExit):
                release.initKey(key, header)
            image = bytearray(2048)
            image[0] = 0xE9
            struct.pack_into("<H", image, 12, 9)
            struct.pack_into("<I", image, 32, 0xABCD5432)
            identity = f"RS485FW|{release.deviceId}|1.1.0|publicRelease|".encode()
            image[512:512+len(identity)] = identity
            public = release.publicPem(key)
            image[1024:1024+len(public)] = public
            firmware = directory / "firmware.bin"
            firmware.write_bytes(image)
            output = directory / "release"
            release.prepareArtifacts(firmware, output, key, "1.1.0")
            self.assertEqual((output / "firmware.bin").read_bytes(), image)
            self.assertTrue((output / "firmware.sha256").read_text().startswith(hashlib.sha256(image).hexdigest()))
            manifest = output / "firmware.manifest"
            manifest.write_bytes(manifest.read_bytes().replace(b"version=1.1.0", b"version=9.9.9"))
            verification = subprocess.run(["openssl", "dgst", "-sha256", "-verify", str(output / "firmware-public.pem"),
                                           "-signature", str(output / "firmware.manifest.sig"), str(manifest)], capture_output=True)
            self.assertNotEqual(verification.returncode, 0)
            image[12] = 0
            with self.assertRaises(ValueError):
                release.validateImage(image, "1.1.0")
            image[12] = 9
            with self.assertRaises(ValueError):
                release.validateImage(image, "wrong-version")
            with self.assertRaises(ValueError):
                release.validateImage(bytes(release.slotSize + 1), "1.1.0")


if __name__ == "__main__":
    unittest.main()
