# ESP32-S3 RS485 Sensor Gateway

PlatformIO firmware for ESP32-S3 that:

- polls Modbus RTU sensors over RS485
- serves a live sensor web page over Wi-Fi AP
- exposes JSON at `/api/sensors`
- broadcasts sensor snapshots over ESP-NOW

## Features

- Modbus RTU master using `ModbusMaster`
- **Batched reads** — one Modbus transaction per slave group (faster, fewer bus round-trips)
- Multi-slave channel table with per-channel scaling
- Current-loop conversion (4-20 mA -> engineering units)
- Direct scaled values (`engineering = raw * scale`)
- Built-in access point + HTTP dashboard
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
