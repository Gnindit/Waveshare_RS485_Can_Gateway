# ESP32-S3 RS485 Sensor Gateway

PlatformIO firmware for ESP32-S3 that:

- polls Modbus RTU sensors over RS485
- serves a live sensor web page over WPA2 Wi-Fi AP and router STA
- exposes JSON at `/api/sensors`
- broadcasts sensor snapshots over ESP-NOW

## Features

- Modbus RTU master using `ModbusMaster`
- **Batched reads** — one Modbus transaction per slave group (faster, fewer bus round-trips)
- Multi-slave channel table with per-channel scaling
- Current-loop conversion (4-20 mA -> engineering units)
- Direct scaled values (`engineering = raw * scale`)
- WPA2 AP + STA on fixed channel 1, HTTP dashboard, authenticated signed HTTPS pull OTA
- ESP-NOW broadcast packet for downstream nodes
- Serial summary with poll timing and utilization % for easy interval tuning

## Hardware
- Slave 1: ME31-XAAX0440
- Slave 2: ME31-XDXX0800-485
- Slave 3: ME31-XEXX0800-485

## Hardware pins

- GPIO 17 -> RS485 TX (DI)
- GPIO 18 -> RS485 RX (RO)
- GPIO 21 -> RS485 direction (DE/RE)
- GPIO 47 -> CH1 control (set HIGH)

## Active communication settings

- Modbus RTU over RS485
- Baud rate: 57600
- Framing: 8N1
- USB serial monitor: 115200
- Poll interval: 500 ms (configurable — see tuning notes below)
- ESP-NOW send interval: 500 ms

## Measured performance (3 batch reads, 8 active channels)

| Metric | Value |
|---|---|
| Modbus read time (avg) | ~84 ms |
| Poll interval utilization | ~16% at 500 ms |
| Minimum safe poll interval | ~100–150 ms |

Reads are batched per slave (one Modbus transaction per group), so adding more
consecutive registers within a slave's range does not proportionally increase bus time.

## Sensor channel map (current firmware)

Active channels are read in three batched Modbus transactions per poll cycle.
Inactive/reserved channels are kept in the table as placeholders for future use.

### Slave 1 (Input Registers / FC04) — batch: 0x0064–0x0065

| Address | Name | Scaling |
|---|---|---|
| 0x0064 | Water pressure | 4–20 mA → −1.0 to 2.0 bar |
| 0x0065 | Fuel pressure | 4–20 mA → 0.0 to 0.5 bar |
| 0x0066 | Reserved / unused | — |
| 0x0067 | Reserved / unused | — |

### Slave 2 (Input Registers / FC04) — batch: 0x0190–0x0193

| Address | Name | Scaling |
|---|---|---|
| 0x0190 | Cylinder left temp | raw × 0.1 °C |
| 0x0191 | Cylinder right temp | raw × 0.1 °C |
| 0x0192 | Motor ambient temp | raw × 0.1 °C |
| 0x0193 | Air ambient temp | raw × 0.1 °C |
| 0x0194–0x0197 | Reserved / unused | — |

### Slave 3 (Holding Registers / FC03) — batch: 0x0000–0x0001

| Address | Name | Scaling |
|---|---|---|
| 0x0000 | Exhaust gas left temp | raw × 0.1 °C |
| 0x0001 | Exhaust gas right temp | raw × 0.1 °C |
| 0x0002–0x0007 | Reserved / unused | — |

## Web interface

- AP SSID: `ESP32-S3-RS485`
- Open browser to the AP IP shown in serial log (typically `http://192.168.4.1`)
- Live JSON endpoint: `http://192.168.4.1/api/sensors`

Each sensor object includes:

- `name`, `unit`, `address`
- `raw`, `has_current`, `current_ma`
- `engineering_value`, `valid`, `active`, `tentative`
- `status`

## Build, upload, and monitor (VS Code)

Use the predefined tasks:

1. `RS485: Build (esp32-s3-main)`
2. `RS485: Upload (esp32-s3-main)`
3. `RS485: Monitor (esp32-s3-main)`
4. `RS485: Build + Upload (esp32-s3-main)`

Quick run path:

1. Press Ctrl+Shift+P
2. Run `Tasks: Run Task`
3. Choose one of the tasks above

## Configuration notes

If your Modbus setup differs, update constants and channel table in `src/main.cpp`:

- communication timing/baud
- enabled slaves (`ENABLE_SLAVE_1/2/3`)
- register type (Input vs Holding)
- per-channel scaling and units

### Poll interval tuning

The serial summary prints utilization every 5 seconds:

```
Poll interval=500ms | Modbus read ms min/avg/max=82/84/89 (16% of interval) | cycles=68 overruns=0
```

- Reduce `POLL_INTERVAL_MS` until utilization approaches 80–90% — that is your practical floor.
- If `overruns` increments, the interval is too short.
- When adding all 20 channels (4+8+8), estimated read time is ~250 ms; use at least 350 ms interval.

### Batch read layout

Batches are defined explicitly in `pollModbus()` in `src/main.cpp`. If you activate
previously reserved registers, extend the batch count and add the corresponding
`applyModbusResult()` calls — the registers must be consecutive within the same slave
and register type for batching to work.


## AP + STA configuration (firmware 1.1.0)

The tested build uses PlatformIO Espressif32 7.0.1 / Arduino-ESP32 2.0.17 (IDF 4.4.7).
Set `include/firmware_config.h` to change the central firmware version.

Copy the versioned `include/wifi_secrets.example.h` to `include/wifi_secrets.h` and
set `staSsid` and `staPassword` to the boat's 2.4 GHz router. The local header is
ignored by Git. Optionally set `apPassword` (8–63 characters) and `otaPassword`
(at least 16 characters); the OTA user defaults to `admin`. Do not put actual
credentials in the example header or commit them.

The USB/main build provisions these settings in NVS. Later OTA builds reuse NVS settings.
Without that local header or stored settings, STA is unconfigured but the AP still works. Empty AP
and OTA passwords generate independent random device passwords, saved in the
`gateway-net` NVS namespace and printed over USB serial at boot. Configured AP/OTA credentials are also printed;
keep serial output private. Open the 115200
monitor **before resetting** to read them. The AP never intentionally starts open.
NVS write/configuration errors disable network startup rather than use an open AP.
Keep these serial logs private. Explicit passwords in the ignored header override
stored passwords during a USB/main build. An empty STA SSID retains the stored network;
use a USB provisioning build with a new SSID/password to change networks. An NVS erase would remove generated passwords.

AP and STA share one web server on port 80. Use the AP IP (normally
`http://192.168.4.1`) or the STA IP shown in serial / WiFi & System. OTA uses
**literal device IP addresses**, not a custom hostname, for Host/Origin validation.
Sensor readings continue without STA or internet. Reconnection is asynchronous:
scan channel 1 every 30 seconds, associate with the discovered BSSID, and abandon
an unsuccessful association after 12 seconds. WiFi sleep and automatic roaming
are disabled. A separate worker checks verified HTTPS reachability to GitHub once
per minute; this status is an observation, not a guarantee that every URL works.
SNTP time is required for certificate verification. Blocked DNS/NTP or a captive
portal can prevent OTA even when STA is connected.

### ESP-NOW channel requirement

**Lock the router to channel 1; turn off automatic channel selection.** AP, STA and
ESP-NOW share a single radio. Existing receivers must also remain on channel 1.
The channel-specific asynchronous discovery scan avoids sweeping other channels.
STA is only started for a matching SSID/BSSID discovered on channel 1. If the radio
moves to a different channel, firmware logs the conflict, disconnects STA and
restores channel 1. It prioritizes AP/ESP-NOW over connectivity to an incompatible
router. Do not point this device at a mesh that moves its channel automatically.
Association/channel recovery can still cause brief interruptions; verify actual
receiver delivery on the hardware, especially during reconnects and downloads.

The ESP-NOW wire format, magic, version, fixed sensor indices, AP interface,
broadcast address and 500 ms send interval are unchanged. Sender logs include
actual WiFi channel and failures. A broadcast success does not acknowledge delivery
to every receiver. See [Espressif's channel/interface documentation](https://docs.espressif.com/projects/esp-idf/en/v4.4.7/esp32s3/api-reference/network/esp_now.html)
and [shared-radio behavior](https://docs.espressif.com/projects/esp-idf/en/v4.4.7/esp32s3/api-guides/wifi.html).

## Signed HTTPS pull OTA

Default firmware URL:

```text
https://github.com/Gnindit/Waveshare_RS485_Can_Gateway/releases/latest/download/firmware.bin
```

The device downloads three adjacent assets: `firmware.manifest`,
`firmware.manifest.sig` and `firmware.bin`. `firmware.sha256` is also published for
operator verification. The manifest contains the device ID, version, image size,
filename and SHA-256. Its detached signature is DER ECDSA P-256 / SHA-256, checked
against the public key embedded in `include/ota_public_key.h`. Only after signature
verification does firmware accept the signed size/target/hash. The streamed image
must also be an ESP32-S3 application and pass ESP-IDF image validation. An unsigned
checksum alone is not an authorization mechanism.

The private signing key created for this checkout is
`.secrets/firmware-signing-key.pem` (mode 0600, ignored by Git). **Back it up securely.**
The public header is safe and necessary to version-control. Other developers should
receive the public header, not a private key. On a new project without an existing
trust anchor, initialize once with `python3 scripts/release.py init-key`; the command
refuses to overwrite existing keys/trust anchors. To build releases later, restore
this same private key. Do not generate a replacement key for devices already trusting
the current key; key rotation needs a signed transition build or USB provisioning.
There is no insecure or unsigned fallback when the key is missing.

Each HTTPS hop validates certificates and hostnames with ESP-IDF's built-in full
CA bundle. Only HTTPS redirects are followed, at most five. There is no `setInsecure`
or HTTP downgrade. The bundle comes from the pinned framework and must be maintained
as roots change. Network calls have a 5 s timeout, metadata reads are bounded to
20 s, and image streaming to 180 s. Content-Length must be known; chunked/compressed
assets are rejected. Manual firmware URLs must end in `/firmware.bin` with no query
or fragment; their adjacent manifest/signature must exist. Redirect targets may have
GitHub's signed query parameters. Public GitHub Release assets are expected; private
release authentication tokens are deliberately not stored or forwarded.

### Build and prepare a release

Compile only (never uploads):

```bash
pio run -e esp32-s3-main
```

Build, check the 3 MiB slot limit, copy the correct application image, calculate
SHA-256, sign the manifest and verify that signature with one command:

```bash
python3 scripts/release.py build
```

The command builds the dedicated `esp32-s3-release` environment. That build completely
excludes `wifi_secrets.h` and reads credentials from the device NVS at runtime. Its
source binary is `.pio/build/esp32-s3-release/firmware.bin`. **Never publish a main/USB
build compiled with local credentials:** ignored headers can still leave passwords
inside compiled binaries. The release command avoids that leak.

The output is `release/<version>/`:

- `firmware.bin`
- `firmware.sha256`
- `firmware.manifest`
- `firmware.manifest.sig`
- `firmware-public.pem` (optional public verification key for operators)

The script rejects wrong chip/application/version identity, oversize images and a
signing key that differs from the public key in the build. Generated files stay
local and ignored. To publish manually, open GitHub → Releases → Draft a new release,
select an appropriate version tag (for example `v1.1.0`), attach the first four files,
and publish when ready. Attach them to **one release**; the device detects mismatched
assets and refuses activation. For testing use that release's version-specific URL
instead of `latest`. Neither the script nor firmware creates a GitHub Release.

### Update from a phone

1. Connect the phone to the device AP or the boat router. The ESP32 STA needs internet.
2. Open the dashboard using the AP or STA IP and check channel 1 and STA status.
3. Select **Sign in for updates** and enter the configured/generated OTA credentials.
4. Keep the default firmware URL or enter a version-specific HTTPS `firmware.bin` URL.
5. Check the confirmation box, press **Update Firmware** and confirm the restart.
6. Watch download progress, verification, installation and reboot. The sensor panel
   continues refreshing once per second while the device is available. After reboot,
   check the new version and wait for the boot self-test to pass.

Internet status checks only `https://github.com/robots.txt`, not every release asset
or redirect host. `online` therefore does not guarantee OTA connectivity. Firmware
1.1.1 sizes the HTTP transmit buffer for GitHub's long signed asset URLs (the IDF
4.4 default of 512 bytes is too short). Connection/request errors now identify the
failing hostname, ESP error code and TLS/certificate flags without logging signed
query strings. Certificate and signature checks remain enforced.

The existing `WebServer server(80)` and `/api/sensors` are retained. New endpoints:

| Endpoint | Access | Purpose |
|---|---|---|
| `GET /api/system` | Local HTTP | Version, WiFi/channel/internet, slot and rollback status |
| `GET /api/ota/status` | HTTP Digest authentication | Progress/error and per-boot CSRF token |
| `POST /api/ota/start` | Digest + same Origin/Host + CSRF + confirmation | Start one worker task |

The POST takes URL-encoded `firmwareUrl` and `confirmation=UPDATE`, with the CSRF
value in `X-CSRF-Token`. No CORS permission is granted. Foreign origins/hosts and
cross-site requests are rejected; the page cannot be framed. Progress uses a locked
snapshot, not shared mutable Strings. The worker leaves the server/sensor loop free;
flash writes and radio traffic can still briefly affect physical timing. Failed
attempts abort the inactive slot. Only a fully verified image is selected for boot.

The local web server remains HTTP as requested. Digest avoids sending the password
as Basic plaintext, but does not encrypt the page, sensors or session. Use a trusted
AP/LAN, strong credentials and no internet port-forwarding. Signatures authorize the
image, not browser traffic. This does not enable Secure Boot, flash encryption or
anti-downgrade eFuses: someone controlling the signing key can authorize older images,
and physical USB access remains a recovery/provisioning path.

## Partitions, first install and rollback

The original framework table was found at
`~/.platformio/packages/framework-arduinoespressif32/tools/partitions/app3M_fat9M_16MB.csv`.
An identical, version-controlled copy lives in `partitions/` and is selected by
`platformio.ini`. **No offsets, sizes or data formats have changed.**

| Partition | Offset | Size |
|---|---:|---:|
| NVS | `0x9000` | `0x5000` (20 KiB) |
| OTA data | `0xe000` | `0x2000` (8 KiB) |
| app0 / OTA_0 | `0x10000` | `0x300000` (3 MiB) |
| app1 / OTA_1 | `0x310000` | `0x300000` (3 MiB) |
| FAT / ffat | `0x610000` | `0x9e0000` (9.875 MiB) |
| Coredump | `0xff0000` | `0x10000` (64 KiB) |

Pull OTA writes **only** the inactive app slot and OTA selection metadata. It never
formats FAT, erases NVS or uploads a filesystem. Existing NVS credentials survive OTA. The build/release script reports
actual image size and remaining slot space. Do not use merged images, bootloader,
partition images or filesystem binaries for application OTA.

The old firmware has no pull OTA endpoint, so install the first version via USB
when you explicitly choose to do so. The layout is compatible with the old table;
no full erase or data migration is needed. USB recovery remains available. No USB
installation has been performed as part of this implementation.

The tested Arduino SDK has `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=1`. The application
uses `verifyRollbackLater()` to prevent early Arduino confirmation. A pending image
is confirmed after 30 continuous healthy seconds: initialized ESP-NOW, an AP IP,
at least one complete poll cycle, a responsive main loop and more than 30 KB free heap.
No internet or valid sensor reading is required. Failure to meet those checks within
90 s requests rollback; an independent timer also resets a stalled main loop. A reset
before confirmation is handled by the bootloader.
The fallback must be a valid previous app. Actual reboot/power-loss behavior and
bootloader compatibility require the physical tests below. See
[Espressif OTA rollback](https://docs.espressif.com/projects/esp-idf/en/v4.4.7/esp32s3/api-reference/system/ota.html).

## Verification

Run host policy/release tests with `bash test/run_host_tests.sh`, then compile with
`pio run -e esp32-s3-main`. The tests cover malformed/wrong-device manifests, bounds,
unsafe URLs, key creation protection, signing, tampering and release image checks.
They do not simulate the WiFi driver or bootloader. Follow the detailed
[hardware acceptance plan](docs/ota-test-plan.md) for Modbus, ESP-NOW delivery, AP/STA,
reconnects, browser authentication/CSRF, missing internet, bad URLs/images, interrupted
updates, power loss, restart and rollback. These physical tests remain unverified.
