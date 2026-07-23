# ESP32-S3 RS485 (Modbus RTU)

This is a beginner-friendly PlatformIO project for reading Modbus RTU data over RS485 using an ESP32-S3.

## Windows ME31 simulator

If you want to simulate the ME31 modules directly from a Windows 10 PC over a USB-to-RS485 adapter, use [windows_me31_simulator.py](windows_me31_simulator.py).

Requirements:

- Python 3
- `pyserial` installed with `pip install pyserial`

Example:

```bash
python windows_me31_simulator.py --port COM11 --baud 57600
```

Use `--verbose` to print raw Modbus frames while testing.

## Hardware pins used

- `GPIO 17` -> RS485 TX (DI)
- `GPIO 18` -> RS485 RX (RO)
- `GPIO 21` -> RS485 direction (DE/RE)
- `GPIO 47` -> CH1 control (kept HIGH)

## Active protocol settings

- Modbus RTU over RS-485
- Baud rate: `57600`
- Framing: `8N1`
- Slave 1 reads use Input Registers (`FC04`)
- Slave 2 reads use Holding Registers (`FC03`)

## Active channel map on this ESP

- Slave 1, `0x0064` (FC04): Water Pressure, 4-20 mA mapped to -1.0..2.0 bar
- Slave 1, `0x0065` (FC04): Fuel Pressure, 4-20 mA mapped to 0.0..0.5 bar
- Slave 1, `0x0066` (FC04): Reserved (Fuel Level is implemented on another ESP)
- Slave 1, `0x0067` (FC04): Reserved / unused
- Slave 2, `0x0000` (FC03): Left Cylinder Bank Temp, `value = raw * 0.1` C
- Slave 2, `0x0001` (FC03): Right Cylinder Bank Temp, `value = raw * 0.1` C
- Slave 2, `0x0003` (FC03): Motor Ambient Temp, `value = raw * 0.1` C
- Slave 2, `0x0004` (FC03): Air Ambient Temp, `value = raw * 0.1` C

## Before first test

1. Verify your ME31 Modbus settings:
   - Slave IDs `1` and `2`
   - Baud rate
   - Parity / stop bits
   - Register addresses to read
2. If needed, update communication constants in `src/main.cpp`:
   - `MODBUS_BAUDRATE`
   - `MODBUS_READ_RETRIES`
   - `MODBUS_RETRY_DELAY_MS`
   - `MODBUS_SLAVE_SWITCH_SETTLE_MS`
3. If register mapping changes, edit the `SENSOR_CHANNELS` table in `src/main.cpp`.

## Build and upload in VS Code

1. Open this project folder in VS Code.
2. Make sure PlatformIO extension is installed.
3. In PlatformIO sidebar:
   - Build
   - Upload
   - Monitor
How to use:
Press Ctrl+Shift+P
Choose Tasks: Run Task
Select the compound task you want

Serial monitor speed is `115200`.
