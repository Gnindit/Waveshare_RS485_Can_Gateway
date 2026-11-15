#include "ota_policy.h"
#include <cassert>
#include <iostream>

int main() {
  assert(OtaPolicy::trustedHost("192.168.4.1", "192.168.4.1", "192.168.1.22", true));
  assert(OtaPolicy::trustedHost("192.168.1.22:80", "192.168.4.1", "192.168.1.22", true));
  assert(!OtaPolicy::trustedHost("192.168.1.22", "192.168.4.1", "192.168.1.22", false));
  assert(!OtaPolicy::trustedHost("evil.example", "192.168.4.1", "192.168.1.22", true));
  assert(!OtaPolicy::trustedHost("192.168.4.1.evil.example", "192.168.4.1", "192.168.1.22", true));
  assert(OtaPolicy::validStartRequest("192.168.4.1", "http://192.168.4.1", "same-origin", "token", "token", "UPDATE"));
  assert(OtaPolicy::validStartRequest("192.168.4.1", "http://192.168.4.1", "", "token", "token", "UPDATE"));
  assert(!OtaPolicy::validStartRequest("192.168.4.1", "http://evil.example", "same-origin", "token", "token", "UPDATE"));
  assert(!OtaPolicy::validStartRequest("192.168.4.1", "", "", "token", "token", "UPDATE"));
  assert(!OtaPolicy::validStartRequest("192.168.4.1", "http://192.168.4.1", "cross-site", "token", "token", "UPDATE"));
  assert(!OtaPolicy::validStartRequest("192.168.4.1", "http://192.168.4.1", "same-site", "token", "token", "UPDATE"));
  assert(!OtaPolicy::validStartRequest("192.168.4.1", "http://192.168.4.1", "same-origin", "wrong", "token", "UPDATE"));
  assert(!OtaPolicy::validStartRequest("192.168.4.1", "http://192.168.4.1", "same-origin", "", "", "UPDATE"));
  assert(!OtaPolicy::validStartRequest("192.168.4.1", "http://192.168.4.1", "same-origin", "token", "token", ""));
  const char *deviceId = "waveshare-esp32-s3-rs485-can";
  const std::string good = std::string("format=1\ndevice=") + deviceId +
      "\nversion=1.1.0\nsize=1048576\nsha256=" + std::string(64, 'a') + "\nimage=firmware.bin\n";
  OtaPolicy::Manifest manifest;
  assert(OtaPolicy::parseManifest(good, deviceId, 0x300000, manifest));
  assert(manifest.size == 1048576 && manifest.version == "1.1.0");
  assert(!OtaPolicy::parseManifest(good, "other-device", 0x300000, manifest));
  assert(!OtaPolicy::parseManifest(good, deviceId, 1000000, manifest));
  assert(!OtaPolicy::parseManifest(good + "extra=1\n", deviceId, 0x300000, manifest));
  assert(!OtaPolicy::parseManifest(good.substr(0, good.size()-1), deviceId, 0x300000, manifest));
  const auto rejectReplacement = [&](const std::string &from, const std::string &to) {
    auto input = good;
    input.replace(input.find(from), from.size(), to);
    assert(!OtaPolicy::parseManifest(input, deviceId, 0x300000, manifest));
  };
  rejectReplacement("format=1", "format=2");
  rejectReplacement("size=1048576", "size=0");
  rejectReplacement("size=1048576", "size=-1");
  rejectReplacement("size=1048576", "size=9999999999999999999");
  rejectReplacement("size=1048576", "size=1048576junk");
  rejectReplacement("size=1048576", "size=3145729");
  rejectReplacement("version=1.1.0", "version=bad\rvalue");
  rejectReplacement("image=firmware.bin", "image=bootloader.bin");
  rejectReplacement(std::string(64, 'a'), std::string(63, 'a'));
  rejectReplacement(std::string(64, 'a'), std::string(64, 'g'));
  assert(OtaPolicy::secureUrl("https://github.com/a/firmware.bin"));
  assert(OtaPolicy::secureUrl("https://release-assets.githubusercontent.com/a?signature=abc%2F123"));
  assert(OtaPolicy::secureUrl("https://release-assets.githubusercontent.com/a?jwt=" + std::string(1400, 'a')));
  for (const auto *bad : {"http://github.com/a", "//github.com/a", "https:///a", "https://user:pass@host/a",
                          "https://host:80/a", "https://host/a#fragment", "https://host\\evil/a", "https://host/a\r\nX: y"}) {
    assert(!OtaPolicy::secureUrl(bad));
  }
  assert(!OtaPolicy::secureUrl("https://host/" + std::string(4096, 'a')));
  std::cout << "OTA policy: Host/Origin/CSRF/confirmation, manifests, bounds and HTTPS URLs tested\n";
}
