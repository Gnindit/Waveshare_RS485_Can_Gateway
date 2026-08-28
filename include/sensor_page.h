#ifndef SENSOR_PAGE_H
#define SENSOR_PAGE_H

#include <Arduino.h>

static const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Tank and Temperature Monitor</title>
  <style>
    :root {
      color-scheme: light;
      --bg: #f5efe4;
      --panel: #fffaf2;
      --ink: #1d2a33;
      --accent: #0d8a86;
      --accent-2: #e07a2f;
      --muted: #61707a;
      --line: rgba(29, 42, 51, 0.12);
    }
    * { box-sizing: border-box; }
    body {
      margin: 0;
      min-height: 100vh;
      font-family: "Segoe UI", sans-serif;
      color: var(--ink);
      background:
        radial-gradient(circle at top left, rgba(13, 138, 134, 0.16), transparent 28%),
        radial-gradient(circle at bottom right, rgba(224, 122, 47, 0.16), transparent 24%),
        var(--bg);
      display: grid;
      place-items: center;
      padding: 24px;
    }
    .card {
      width: min(100%, 560px);
      background: var(--panel);
      border: 1px solid var(--line);
      border-radius: 24px;
      box-shadow: 0 20px 60px rgba(29, 42, 51, 0.12);
      padding: 28px;
    }
    h1 {
      margin: 0 0 8px;
      font-size: clamp(28px, 6vw, 42px);
      line-height: 1;
    }
    p {
      margin: 0;
      color: var(--muted);
    }
    .value {
      margin: 28px 0 20px;
      font-size: clamp(52px, 12vw, 86px);
      font-weight: 700;
      line-height: 0.95;
      letter-spacing: -0.04em;
    }
    .unit {
      font-size: 0.35em;
      color: var(--accent);
      margin-left: 8px;
      vertical-align: middle;
    }
    .grid {
      display: grid;
      grid-template-columns: repeat(2, minmax(0, 1fr));
      gap: 12px;
      margin-top: 18px;
    }
    .tile {
      padding: 16px;
      border-radius: 18px;
      background: rgba(255, 255, 255, 0.75);
      border: 1px solid var(--line);
    }
    .label {
      display: block;
      font-size: 12px;
      text-transform: uppercase;
      letter-spacing: 0.12em;
      color: var(--muted);
      margin-bottom: 6px;
    }
    .tile strong {
      font-size: 22px;
    }
    .status {
      margin-top: 18px;
      padding: 14px 16px;
      border-radius: 16px;
      background: rgba(13, 138, 134, 0.08);
      color: var(--ink);
    }
    .status.error {
      background: rgba(224, 122, 47, 0.12);
    }
  </style>
</head>
<body>
  <main class="card">
    <p>ESP32 access point tank and temperature monitor</p>
    <h1>Water Pressure, Fuel Pressure and Temperature Sensors</h1>
    <div id="sensors"></div>
    <div id="status" class="status">Waiting for sensor data...</div>
  </main>
  <script>
    function displayEngineeringValue(sensor) {
      if (!sensor.valid) {
        return '--.--';
      }

      const isPressure = sensor.unit === 'bar';
      const value = isPressure ? Math.max(sensor.engineering_value, 0) : sensor.engineering_value;
      return value.toFixed(3);
    }

    function sensorCard(sensor) {
      const tentative = sensor.tentative ? ' (tentative)' : '';
      const inactive = sensor.active ? '' : ' (reserved)';
      return `
        <div class="tile" style="margin-bottom: 12px;">
          <span class="label">${sensor.name}${tentative}${inactive}</span>
          <strong style="display:block; margin-bottom:6px;">${displayEngineeringValue(sensor)} ${sensor.unit}</strong>
          <div style="color: var(--muted); font-size: 14px; line-height: 1.5;">
            Raw: ${sensor.valid ? sensor.raw : '----'}<br>
            Current: ${sensor.valid && sensor.has_current ? `${sensor.current_ma.toFixed(3)} mA` : 'N/A'}<br>
            Address: 0x${sensor.address.toString(16).padStart(4, '0')}
          </div>
        </div>`;
    }

    async function refreshSensors() {
      try {
        const response = await fetch('/api/sensors', { cache: 'no-store' });
        const data = await response.json();
        document.getElementById('sensors').innerHTML = data.sensors.map(sensorCard).join('');

        const status = document.getElementById('status');
        status.textContent = data.sensors.map(sensor => sensor.status).join(' | ');
        const activeSensors = data.sensors.filter(sensor => sensor.active);
        status.className = activeSensors.every(sensor => sensor.valid) ? 'status' : 'status error';
      } catch (error) {
        const status = document.getElementById('status');
        status.textContent = 'Web update failed. Retrying...';
        status.className = 'status error';
      }
    }

    refreshSensors();
    setInterval(refreshSensors, 1000);
  </script>
</body>
</html>
)rawliteral";

#endif
