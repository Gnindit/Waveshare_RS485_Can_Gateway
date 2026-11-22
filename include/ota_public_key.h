#pragma once
// Legacy <=1.1.3 trust anchor, used ONLY to package the one-time transition.
// Not included in firmware 1.2.0 or newer. Safe to commit.
constexpr const char otaPublicKey[] = R"key(
-----BEGIN PUBLIC KEY-----
MFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDQgAELj/Lm9j0RlvmKiz06dYqgDqr+eZ4
t23IzQiVHYJG9AzSWChskfWlpaxxgDo6kFojqWzhRT1nITNr80vdZViJ4w==
-----END PUBLIC KEY-----
)key";
