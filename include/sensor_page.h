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
    .system-info { margin: 20px 0; overflow-wrap: anywhere; line-height: 1.6; }
    .firmware-update { margin-top: 28px; border-top: 1px solid var(--line); padding-top: 20px; }
    label { display: block; margin: 14px 0 6px; }
    input[type="url"] { width: 100%; min-width: 0; padding: 12px; border: 1px solid var(--line); border-radius: 8px; font: inherit; }
    button { display: inline-block; padding: 12px 16px; border-radius: 8px; background: var(--accent); color: white; border: 0; font: inherit; cursor: pointer; }
    button:disabled { opacity: 0.5; cursor: default; }
    progress { width: 100%; height: 22px; margin-top: 14px; }
    .ota-message { overflow-wrap: anywhere; white-space: pre-wrap; margin-top: 12px; }
    @media (max-width: 420px) { body { padding: 12px; } .card { padding: 18px; } }
  </style>
</head>
<body>
  <main class="card">
    <p>ESP32 access point tank and temperature monitor</p>
    <h1>Water Pressure, Fuel Pressure and Temperature Sensors</h1>
    <div id="sensors"></div>
    <div id="status" class="status">Waiting for sensor data...</div>
    <section class="system-info" aria-label="Network and system information">
      <h2>WiFi &amp; System</h2>
      <div id="systemInfo">Loading network status...</div>
    </section>
    <section class="firmware-update" aria-labelledby="firmwareTitle">
      <h2 id="firmwareTitle">Firmware Update</h2>
      <p>Current firmware: <strong id="firmwareVersion">Loading...</strong></p>
      <form id="otaForm">
        <label for="firmwareUrl">Firmware URL (HTTPS)</label>
        <input type="url" id="firmwareUrl" name="firmwareUrl" required maxlength="512" spellcheck="false" autocomplete="off">
        <label><input type="checkbox" id="otaConfirmation" required> I confirm the firmware update and restart.</label>
        <button type="submit" id="updateFirmware" disabled>Update Firmware</button>
      </form>
      <progress id="otaProgress" value="0" max="100" aria-label="Firmware download progress"></progress>
      <p id="otaMessage" class="ota-message" role="status" aria-live="polite">Loading update status...</p>
    </section>
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
      if (pageLeaving || sensorsLoading) return;
      sensorsLoading = true;
      try {
        const response = await fetchWithTimeout('/api/sensors');
        const data = await response.json();
        document.getElementById('sensors').innerHTML = data.sensors.map(sensorCard).join('');

        const status = document.getElementById('status');
        status.textContent = data.sensors.map(sensor => sensor.status).join(' | ');
        const activeSensors = data.sensors.filter(sensor => sensor.active);
        status.className = activeSensors.every(sensor => sensor.valid) ? 'status' : 'status error';
      } catch (error) {
        const status = document.getElementById('status');
        if (!pageLeaving) {
          status.textContent = 'Web update failed. Retrying...';
          status.className = 'status error';
        }
      } finally { sensorsLoading = false; }
    }

    let pageLeaving = false;
    let sensorsLoading = false;
    const dashboardTimers = [];
    const pendingRequests = new Set();
    let csrfToken = '';
    let otaActive = false;
    let systemReady = false;
    let systemLoading = false;
    let otaLoading = false;
    let staConnected = false;
    let otaConfigured = false;

    function updateOtaButton() {
      document.getElementById('updateFirmware').disabled = !csrfToken || otaActive || !staConnected || !otaConfigured;
    }

    async function fetchWithTimeout(url, options = {}) {
      const controller = new AbortController();
      const timer = setTimeout(() => controller.abort(), 8000);
      pendingRequests.add(controller);
      try { return await fetch(url, { ...options, signal: controller.signal, cache: 'no-store', credentials: 'same-origin' }); }
      finally { clearTimeout(timer); pendingRequests.delete(controller); }
    }

    async function refreshSystem() {
      if (pageLeaving || systemLoading) return;
      systemLoading = true;
      try {
        const response = await fetchWithTimeout('/api/system');
        if (!response.ok) throw new Error('System status unavailable');
        const data = await response.json();
        const network = data.network;
        staConnected = network.staConnected;
        otaConfigured = data.ota.configured;
        document.getElementById('firmwareVersion').textContent = data.firmwareVersion;
        document.getElementById('systemInfo').textContent =
          `STA: ${network.staConnected ? 'Connected' : 'Disconnected'} (${network.staState}) | ` +
          `STA IP: ${network.staIp} | AP: ${network.apEnabled ? network.apIp : 'disabled'} | ` +
          `RSSI: ${network.rssi === null ? 'N/A' : network.rssi + ' dBm'} | ` +
          `WiFi channel: ${network.wifiChannel} (required: ${network.requiredChannel}) | ` +
          `Internet: ${network.internetStatus} | Boot self-test: ${data.ota.selfTest}`;
        if (!systemReady) document.getElementById('firmwareUrl').value = data.defaultFirmwareUrl;
        systemReady = true;
        updateOtaButton();
      } catch (error) {
        document.getElementById('systemInfo').textContent = 'Device unavailable. Reconnecting...';
        staConnected = false;
        updateOtaButton();
      } finally { systemLoading = false; }
    }

    async function refreshOta() {
      if (pageLeaving || otaLoading) return;
      otaLoading = true;
      try {
        const response = await fetchWithTimeout('/api/ota/status');
        if (!response.ok) throw new Error('Update status unavailable. Retrying...');
        const data = await response.json();
        csrfToken = data.csrfToken;
        otaActive = data.active;
        const percent = data.total ? Math.floor(data.downloaded * 100 / data.total) : 0;
        document.getElementById('otaProgress').value = percent;
        const labels = {
          idle: 'Ready. Select the firmware URL and confirm the update.',
          manifest: 'Checking firmware manifest...',
          downloading: `Downloading firmware: ${percent}% (${data.downloaded} / ${data.total} bytes)`,
          verifying: 'Verifying firmware integrity...',
          installing: 'Installing verified firmware...',
          rebooting: 'Rebooting. The page will reconnect automatically.',
          error: `Update failed: ${data.error}`
        };
        document.getElementById('otaMessage').textContent = labels[data.stage] || data.stage;
      } catch (error) {
        document.getElementById('otaMessage').textContent = otaActive ? 'Device restarting or unavailable. Reconnecting...' : error.message;
      } finally { otaLoading = false; updateOtaButton(); }
    }

    document.getElementById('otaForm').addEventListener('submit', async event => {
      event.preventDefault();
      if (!csrfToken || otaActive || !document.getElementById('otaConfirmation').checked) return;
      if (!window.confirm('Install firmware and restart this gateway? Sensor data will pause during restart.')) return;
      otaActive = true;
      updateOtaButton();
      try {
        const body = new URLSearchParams({ firmwareUrl: document.getElementById('firmwareUrl').value.trim(), confirmation: 'UPDATE' });
        const response = await fetchWithTimeout('/api/ota/start', {
          method: 'POST', headers: { 'X-CSRF-Token': csrfToken, 'Content-Type': 'application/x-www-form-urlencoded' }, body
        });
        const data = await response.json();
        if (!response.ok) throw new Error(data.error || 'Update rejected');
        await refreshOta();
      } catch (error) {
        otaActive = false;
        document.getElementById('otaMessage').textContent = error.message;
        updateOtaButton();
      }
    });

    function stopDashboardPolling() {
      pageLeaving = true;
      dashboardTimers.splice(0).forEach(clearInterval);
      pendingRequests.forEach(controller => controller.abort());
    }

    function startDashboardPolling() {
      if (dashboardTimers.length) return;
      pageLeaving = false;
      refreshSensors();
      refreshSystem();
      refreshOta();
      dashboardTimers.push(setInterval(refreshSensors, 1000));
      dashboardTimers.push(setInterval(refreshSystem, 5000));
      dashboardTimers.push(setInterval(refreshOta, 1000));
    }

    window.addEventListener('pagehide', stopDashboardPolling);
    window.addEventListener('pageshow', startDashboardPolling);
    startDashboardPolling();
  </script>
</body>
</html>
)rawliteral";

#endif
