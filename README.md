# ESP32-S3 RS485 Sensor Gateway

PlatformIO firmware for ESP32-S3 that:

- polls Modbus RTU sensors over RS485
- serves a live sensor web page through the router, with an optional debugging AP
- exposes JSON at `/api/sensors`
- broadcasts sensor snapshots over ESP-NOW

## Features

- Modbus RTU master using `ModbusMaster`
- **Batched reads** — one Modbus transaction per slave group (faster, fewer bus round-trips)
- Multi-slave channel table with per-channel scaling
- Current-loop conversion (4-20 mA -> engineering units)
- Direct scaled values (`engineering = raw * scale`)
- Router STA on fixed channel 1, optional WPA2 AP, HTTP dashboard, HTTPS pull OTA without login
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

- Connect to the RUT240 and open the gateway STA IP shown in serial or the router DHCP client list.
- Live JSON endpoint: `http://<gateway-ip>/api/sensors`.
- The debugging AP is off by default; enable it with `FirmwareConfig::apEnabled` in `include/firmware_config.h`. Its SSID is `ESP32-S3-RS485` and its usual IP is `192.168.4.1`.

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


## Router and optional debugging AP

The pinned build uses PlatformIO Espressif32 7.0.1 / Arduino-ESP32 2.0.17 (IDF 4.4.7).
The N16R8 board definition is versioned in `boards/`, so clean checkouts and GitHub
runners use the same flash/PSRAM configuration as the development computer.
`include/firmware_config.h` contains the firmware version and this compile-time switch:

```cpp
constexpr bool apEnabled = false;
```

With `false`, the gateway starts only STA, and the webpage/OTA is reached through the
RUT240. Set it to `true` and rebuild to enable the WPA2 debugging AP alongside STA.
There is no automatic AP fallback when the router is unavailable. Modbus and ESP-NOW
continue without the router; webpage access then requires restoring the router or a
USB build with the AP enabled. Boot self-test does not require router connectivity.

Copy `include/wifi_secrets.example.h` to the ignored `include/wifi_secrets.h` and
configure `staSsid` and `staPassword`. The USB/main build provisions them in NVS;
public OTA builds reuse NVS and exclude local credentials. An empty STA SSID keeps
the saved network. To change networks, provision a new SSID/password with a USB/main
build. Keep credentials and serial logs private.

If the debugging AP is enabled, `apPassword` must contain 8–63 characters, or an empty
value generates a random password saved in NVS and printed over USB serial at boot.
The AP remains password protected. Old OTA username/password settings in local headers
or NVS are no longer used and may be removed from the header. No NVS erase is needed.

USB device paths belong in the ignored `platformio.local.ini`, which is loaded using
PlatformIO's [extra_configs](https://docs.platformio.org/en/stable/projectconf/sections/platformio/options/generic/extra_configs.html).
Copy `platformio.local.ini.example` for another machine, or let PlatformIO detect the
port. The committed configuration has no COM-port or machine-specific USB identifier.
Wi-Fi credentials remain in `include/wifi_secrets.h`, separate from port settings.

Use literal device IP addresses for Host/Origin validation. Reconnection scans only
channel 1 every 30 seconds and abandons an unsuccessful association after 12 seconds.
WiFi sleep and automatic roaming are disabled. A worker checks verified HTTPS access
to GitHub once per minute. SNTP time is required for TLS certificate verification;
blocked DNS/NTP or a captive portal can prevent OTA despite a connected STA.

### ESP-NOW channel requirement

**Lock the router to channel 1; turn off automatic channel selection.** STA,
ESP-NOW and the optional AP share one radio. Existing receivers must also remain
on channel 1.
The channel-specific asynchronous discovery scan avoids sweeping other channels.
STA is only started for a matching SSID/BSSID discovered on channel 1. If the radio
moves to a different channel, firmware logs the conflict, disconnects STA and
restores channel 1. It prioritizes ESP-NOW over connectivity to an incompatible
router. Do not point this device at a mesh that moves its channel automatically.
Association/channel recovery can still cause brief interruptions; verify actual
receiver delivery on the hardware, especially during reconnects and downloads.

The ESP-NOW wire format, magic, version, fixed sensor indices, broadcast address
and 500 ms send interval are unchanged. ESP-NOW uses the STA interface with the AP
disabled, or the AP interface with it enabled. The sender MAC therefore changes
between modes; any receiver filtering by source MAC must allow the selected
interface. Sender logs include actual WiFi channel and failures. A broadcast success does not acknowledge delivery
to every receiver. See [Espressif's channel/interface documentation](https://docs.espressif.com/projects/esp-idf/en/v4.4.7/esp32s3/api-reference/network/esp_now.html)
and [shared-radio behavior](https://docs.espressif.com/projects/esp-idf/en/v4.4.7/esp32s3/api-guides/wifi.html).

## HTTPS pull OTA without login

Default firmware URL:

```text
https://github.com/Gnindit/Waveshare_RS485_Can_Gateway/releases/latest/download/firmware.bin
```

Connect to the RUT240, open the gateway STA IP, leave the default firmware URL,
check the confirmation box and press **Update Firmware**. Confirm the restart and
watch progress. The webpage reconnects after reboot. No developer login or firmware
signing key is needed from version 1.2.0 onward.

Network access is the authorization boundary: anyone able to reach the gateway can
start updates. The RUT240 Wi-Fi password controls access through the router; the
optional debugging AP has its own password. Keep this HTTP interface on that trusted
network. Host/Origin checks, CSRF tokens, POST-only start, confirmation and concurrent
update protection remain in place.

The device fetches `firmware.manifest` and `firmware.bin` from the same release.
The unsigned manifest contains device ID, version, image size and SHA-256. Firmware
checks target, slot size, image integrity and ESP32-S3 application format before
activation. HTTPS certificates and hostnames are checked on every redirect; HTTP
downgrades are refused. These checks detect corruption and incompatible images;
they do not provide a separate publisher signature. Release/account access and the
trusted local network replace the previous signing/login requirements.

Metadata reads are bounded to 20 seconds and firmware streaming to 180 seconds.
Manual HTTPS URLs must end in `/firmware.bin` without a query or fragment and have
an adjacent manifest. GitHub redirects may contain long signed query strings; this
is independent of firmware signing. Public release assets are expected.

### GitHub builds and release assets

`.github/workflows/firmware.yml` follows ESP32-2: pushes to `main`, pull requests and
manual runs test/build public firmware and retain downloadable workflow artifacts.
Publishing a GitHub Release triggers the build and attaches `firmware.bin`,
`firmware.manifest` and `firmware.sha256` automatically. This uses GitHub's
[release published event](https://docs.github.com/en/actions/reference/workflows-and-actions/events-that-trigger-workflows#release).
No signing secret needs to be configured in GitHub.

Set `FIRMWARE_VERSION` before publishing; a release tag such as `v1.2.0` must match
the code version. Wait for the workflow and asset upload to succeed before updating
a device. Normal pushes do not publish a release. The workflow must be pushed before
GitHub can run it; no push or release publication is performed by local scripts.

Run these commands from the directory containing `platformio.ini`:

```bash
# USB provisioning build, including local credentials if configured:
platformio run -e esp32-s3-main
# Public OTA build and local packaging, no upload:
python3 scripts/release.py build
```

These are two environments for the same gateway application. `esp32-s3-main` can
provision local secrets over USB. `esp32-s3-release` excludes the credential header
and reuses NVS, making it suitable for public distribution. Both share the AP switch.
Only the release environment is published; never publish a USB/main binary that
may contain local passwords.

Public source binary: `.pio/build/esp32-s3-release/firmware.bin`.
Local packages: `dist/<version>/firmware.bin`, `firmware.manifest`, `firmware.sha256`.
`dist/` is ignored and disposable; the old `release/` directory has been removed.
The packager validates device/version identity, chip, application descriptor and
3 MiB slot limit. `python3 scripts/release.py package` packages an already built image.
Use a version-specific release URL for testing rather than `latest`.

### One-time transition from signed firmware

Devices running 1.1.3 or earlier still enforce their original signature and login.
For an OTA transition to 1.2.0, prepare one final signed package locally:

```bash
python3 scripts/release.py build --legacy-key .secrets/firmware-signing-key.pem
```

This adds `firmware.manifest.sig` and `firmware-public.pem` in `dist/1.2.0/`.
The key must match the old public anchor in `include/ota_public_key.h`; that header
is used only by the transition packager and is no longer embedded in firmware.
Keep the existing private key private and backed up until all old devices migrate.
Attach the signature alongside the three normal assets to the same release, then
perform the update using the old login once. Subsequent updates require neither.
The GitHub workflow does not generate this transition signature.

Alternatively, install `esp32-s3-main` over USB to transition without a signed OTA.
Neither method needs a full erase. Building/packaging does not upload firmware.

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

USB remains available for provisioning and recovery. For existing signed OTA
firmware, use the transition procedure above. The partition layout is compatible;
no full erase or data migration is needed.

The tested Arduino SDK has `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=1`. The application
uses `verifyRollbackLater()` to prevent early Arduino confirmation. A pending image
is confirmed after 30 continuous healthy seconds: initialized ESP-NOW, initialized Wi-Fi on channel 1 (and a ready AP only if enabled),
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
unsafe URLs, unsigned packaging, transition signing, credential exclusion and release image checks.
They do not simulate the WiFi driver or bootloader. Follow the detailed
[hardware acceptance plan](docs/ota-test-plan.md) for Modbus, ESP-NOW delivery, AP/STA,
reconnects, AP-disabled operation, browser CSRF protection, missing internet, bad URLs/images, interrupted
updates, power loss, restart and rollback. These physical tests remain unverified.
