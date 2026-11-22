# Hardware acceptance test plan

Host tests and compilation do not verify radio reception, flash power-loss behavior,
mobile-browser behavior or bootloader rollback on a physical board. Run these
checks on the intended Waveshare ESP32-S3 RS485/CAN before deploying on the boat.
No physical uploads are performed by the development or release scripts.

1. Back up any application data and the private signing key. Set the boat router's
   **2.4 GHz channel to fixed 1**, disable auto-channel selection, and keep existing
   ESP-NOW receivers on channel 1. Copy `include/wifi_secrets.example.h` to
   `include/wifi_secrets.h` and configure STA credentials. Build first, then explicitly
   install the initial version via USB when ready. Do not erase flash or upload a filesystem.
2. First test the default `FirmwareConfig::apEnabled = false`: verify no gateway AP
   is advertised and open the gateway STA IP through the RUT240. Then test a build
   with `apEnabled = true` and connect to the WPA2 debugging AP. With generated credentials, open USB serial at 115200 before
   resetting the board to read the saved AP password. Verify `/`,
   `/api/sensors` and `/api/system` at `http://192.168.4.1`. Compare all active sensor
   values and raw registers with the previous firmware; check reserved slots remain
   unchanged. Confirm 500 ms polling/broadcast and 1 s dashboard refresh.
   With AP disabled, ESP-NOW uses STA and thus a different sender MAC; verify receivers
   accept it. Disconnect the router and verify Modbus/ESP-NOW continue and boot self-test
   passes without router connectivity; no debugging AP should start automatically.
3. Confirm every ESP-NOW receiver still decodes the same channel slots, including
   slots 12/13. Measure received packet cadence and loss, not just sender success:
   a successful broadcast callback does not prove receiver delivery. Record the serial
   WiFi channel, ESP-NOW errors and Modbus utilization before/during/after OTA.
4. With the debugging AP enabled, confirm STA connected, DHCP IP and RSSI. Open the dashboard from a second client
   using the STA IP; verify the AP stays available simultaneously. Disconnect WAN:
   Modbus and ESP-NOW must continue, and the internet probe should become unreachable
   within its next 60 s probe (plus TLS/DNS timeout). Restore WAN and check recovery.
5. Power off the router. ESP-NOW must continue on channel 1; AP stays available only
   in an AP-enabled build. Power it back on:
   STA should reconnect at the next 30 s scan plus DHCP/association time. Repeat with
   bad credentials, unavailable SSID, and the router fixed on channel 6. Firmware must
   report router-not-on-channel-1 and preserve channel 1, never silently migrate the
   existing receivers. Also test a channel change while connected: STA must be dropped
   and channel 1 restored with a conflict log. Measure any interruption.
6. Use a GitHub-built unsigned **test** release after installing 1.2.0 (or use the
   documented signed transition for an older device). Open the dashboard on a phone
   without signing in, confirm the checkbox and press Update Firmware. Watch manifest,
   download progress, verification, installation and reboot. Continue loading sensors
   from another client. Verify the expected version and `selfTest=passed` after at least
   30 healthy seconds. The test checks loop activity, initialized radio on channel 1, AP only if enabled, ESP-NOW initialization,
   polling cycles and free heap; it does not require sensors or internet to be present.
7. Verify `/api/ota/status` succeeds without authentication and exposes a CSRF token.
   A GET to `/api/ota/start` must not update; POSTs with missing/wrong CSRF token,
   missing confirmation, foreign/missing Origin, or foreign Host must get 403.
   Test repeated presses and concurrent starts. A foreign-origin POST must not update.
8. Try a missing release (404), HTTP URL, HTTPS-to-HTTP redirect, excessive redirects,
   untrusted/expired/hostname-mismatched certificate, no SNTP time, HTML instead of
   binary, wrong Content-Length, oversized image and a truncated transfer. Stop WAN
   midway. The error must be visible and the old firmware must still boot after reset.
9. Use malformed metadata or a wrong device ID in the manifest, modify
   one firmware byte, and substitute ESP32 (non-S3), bootloader or merged binaries.
   All must fail before the new boot partition is selected. Repeat a failed update
   several times and compare free heap for leaks. Use a controlled HTTPS fixture with
   the necessary certificates; do not weaken TLS checks. Separately verify the legacy
   transition signature rejects a wrong key or a modified manifest.
10. With a known-good fallback slot, remove power during download/write and verify
    the previous firmware boots. Then install a deliberately failing *test*
    build that resets before confirmation, and verify bootloader rollback. Also test
    a build whose self-test stays unhealthy for 90 s. Do not call it rollback-tested
    until these physical tests pass. Never test first on the live installation.

USB remains the recovery route. For public application OTA use `.pio/build/esp32-s3-release/firmware.bin`, as packaged
by the release script. Do not publish a USB/main binary containing local secrets. Neither release preparation nor pull OTA touches the FAT partition,
partition table, bootloader or existing sensor data.

## USB monitor / standalone regression

Keep the USB cable and power connected. Open the monitor, verify polling, then close
only the monitor while loading the dashboard on the phone. Modbus LEDs, changing
sensor values and receiver packet delivery must continue. Repeat several times,
including while a serial summary is being printed, and reopen the monitor to inspect
counters. Repeat with the board powered without a computer. A WiFi association alone
does not demonstrate a working main loop. This physical test has not been performed
by the build/host tests.

Firmware 1.1.2 uses a positive 1 ms USB TX timeout to avoid the Arduino 2.0.17 HWCDC
unsigned retry-counter wraparound at zero. ESP-NOW callbacks only update atomic
statistics; their serial output is deferred to the main loop. Logs can be dropped
under USB backpressure, and are not allowed to control sensor polling or delivery.

## Dashboard polling regression

Open the page through the RUT240 with the debugging AP disabled; verify OTA status
loads without a login. Also test with the debugging AP enabled. Try browser back,
forward, pagehide/pageshow and slow/unavailable HTTP responses. Requests must not
accumulate and polling must resume after navigation. Host tests exercise delayed
mocked responses; actual browser and radio behavior still needs physical tests.

`/api/system` reports `webLastHandleMs` and `webMaxHandleMs`. Slow WebServer calls
(250 ms or longer) are logged as `Web handling slow: ...`. The firmware-size lookup
runs once at boot rather than on each system-status request.
