#pragma once
#include <stdint.h>
#include <stdlib.h>
#include <string>

namespace OtaPolicy {
inline size_t requestBufferSize(size_t urlLength) {
  // ESP-IDF 4.4 must fit the complete HTTP request line in its TX buffer.
  // GitHub release redirects include long signed query strings.
  return urlLength + 256 < 1024 ? 1024 : urlLength + 256;
}

inline bool trustedHost(const std::string &host, const std::string &apIp,
                        const std::string &staIp, bool connected) {
  return host == apIp || host == apIp + ":80" ||
      (connected && (host == staIp || host == staIp + ":80"));
}

inline bool validStartRequest(const std::string &host, const std::string &origin,
                             const std::string &fetchSite, const std::string &token,
                             const std::string &expectedToken, const std::string &confirmation) {
  return origin == "http://" + host && (fetchSite.empty() || fetchSite == "same-origin") &&
      !expectedToken.empty() && token == expectedToken && confirmation == "UPDATE";
}

inline bool secureUrl(const std::string &url) {
  // GitHub's signed release-asset redirect URLs are much longer than the input URL.
  if (url.compare(0, 8, "https://") != 0 || url.size() > 4096) return false;
  const size_t end = url.find('/', 8);
  const std::string authority = url.substr(8, end == std::string::npos ? end : end - 8);
  if (authority.empty() || authority.find('@') != std::string::npos || authority.find(':') != std::string::npos) return false;
  for (unsigned char ch : url) if (ch <= 32 || ch >= 127 || ch == '\\' || ch == '#') return false;
  return true;
}

struct Manifest {
  std::string version;
  std::string sha256;
  uint32_t size = 0;
};

inline bool parseManifest(const std::string &text, const char *deviceId,
                          uint32_t maxSize, Manifest &manifest) {
  const std::string keys[] = {"format=", "device=", "version=", "size=", "sha256=", "image="};
  std::string values[6];
  size_t offset = 0;
  for (size_t i = 0; i < 6; ++i) {
    const size_t end = text.find('\n', offset);
    if (end == std::string::npos || text.compare(offset, keys[i].size(), keys[i]) != 0) return false;
    values[i] = text.substr(offset + keys[i].size(), end - offset - keys[i].size());
    offset = end + 1;
  }
  if (offset != text.size() || values[0] != "1" || values[1] != deviceId || values[5] != "firmware.bin") return false;
  if (values[2].empty() || values[2].size() > 31) return false;
  for (char ch : values[2]) {
    if (!((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || ch == '.' || ch == '-' || ch == '_')) return false;
  }
  if (values[3].empty() || values[3].size() > 8) return false;
  for (char ch : values[3]) if (ch < '0' || ch > '9') return false;
  const unsigned long size = strtoul(values[3].c_str(), nullptr, 10);
  if (size < 288 || size > maxSize) return false;
  if (values[4].size() != 64) return false;
  for (char ch : values[4]) if (!((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f'))) return false;
  manifest.version = values[2];
  manifest.sha256 = values[4];
  manifest.size = size;
  return true;
}
}
