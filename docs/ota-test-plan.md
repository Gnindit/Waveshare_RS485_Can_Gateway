# Hardware acceptance test plan

Host tests and compilation do not verify radio reception, flash power-loss behavior,
mobile-browser authentication or bootloader rollback on a physical board. Run these
checks on the intended Waveshare ESP32-S3 RS485/CAN before deploying on the boat.
No physical uploads are performed by the development or release scripts.

1. Back up any application data and the private signing key. Set the boat router's
   **2.4 GHz channel to fixed 1**, disable auto-channel selection, and keep existing
   ESP-NOW receivers on channel 1. Copy `include/wifi_secrets.example.h` to
   `include/wifi_secrets.h` and configure STA credentials. Build first, then explicitly
   install the initial version via USB when ready. Do not erase flash or upload a filesystem.
2. Connect to the WPA2 AP. With generated credentials, open USB serial at 115200 before
   resetting the board to read the saved AP password and OTA login. Verify `/`,
   `/api/sensors` and `/api/system` at `http://192.168.4.1`. Compare all active sensor
   values and raw registers with the previous firmware; check reserved slots remain
   unchanged. Confirm 500 ms polling/broadcast and 1 s dashboard refresh.
3. Confirm every ESP-NOW receiver still decodes the same channel slots, including
   slots 12/13. Measure received packet cadence and loss, not just sender success:
   a successful broadcast callback does not prove receiver delivery. Record the serial
   WiFi channel, ESP-NOW errors and Modbus utilization before/during/after OTA.
4. Confirm STA connected, DHCP IP and RSSI. Open the dashboard from a second client
   using the STA IP; verify the AP stays available simultaneously. Disconnect WAN:
   Modbus and ESP-NOW must continue, and the internet probe should become unreachable
   within its next 60 s probe (plus TLS/DNS timeout). Restore WAN and check recovery.
5. Power off the router. AP and ESP-NOW must continue on channel 1. Power it back on:
   STA should reconnect at the next 30 s scan plus DHCP/association time. Repeat with
   bad credentials, unavailable SSID, and the router fixed on channel 6. Firmware must
   report router-not-on-channel-1 and preserve channel 1, never silently migrate the
   existing receivers. Also test a channel change while connected: STA must be dropped
   and channel 1 restored with a conflict log. Measure any interruption.
6. Publish a manually prepared signed **test** release. Open the dashboard on a phone,
   sign in for updates, confirm the checkbox and press Update Firmware. Watch manifest,
   download progress, verification, installation and reboot. Continue loading sensors
   from another client. Verify the expected version and `selfTest=passed` after at least
   30 healthy seconds. The test checks loop activity, AP IP, ESP-NOW initialization,
   polling cycles and free heap; it does not require sensors or internet to be present.
7. Verify requests without authentication get 401, wrong credentials fail, a GET to
   `/api/ota/start` cannot start an update, and authenticated POSTs with missing/wrong
   CSRF token, missing confirmation, foreign/missing Origin, or foreign Host get 403.
   Exercise mobile browser sign-in/cancel and repeated button presses. Concurrent
   starts must be refused. Test a POST from a different web origin: it must not update.
8. Try a missing release (404), HTTP URL, HTTPS-to-HTTP redirect, excessive redirects,
   untrusted/expired/hostname-mismatched certificate, no SNTP time, HTML instead of
   binary, wrong Content-Length, oversized image and a truncated transfer. Stop WAN
   midway. The error must be visible and the old firmware must still boot after reset.
9. Modify the manifest, use a different signing key, sign a different device ID, modify
   one firmware byte, and substitute ESP32 (non-S3), bootloader or merged binaries.
   All must fail before the new boot partition is selected. Repeat a failed update
   several times and compare free heap for leaks. Use a controlled HTTPS fixture with
   the necessary certificates/signing key; do not weaken TLS or signature checks.
10. With a known-good fallback slot, remove power during download/write and verify
    the previous firmware boots. Then install a deliberately failing *signed test*
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
