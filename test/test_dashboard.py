from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


class DashboardTest(unittest.TestCase):
    @unittest.skipUnless(shutil.which("gjs"), "GJS JavaScript runtime is not installed")
    def test_slow_requests_and_login_navigation(self):
        page = (Path(__file__).resolve().parents[1] / "include/sensor_page.h").read_text()
        source = page.split("<script>", 1)[1].split("</script>", 1)[0]
        harness = r'''
const GLib = imports.gi.GLib;
const System = imports.system;
const calls = [];
const intervals = new Map();
const timers = new Map();
const elements = new Map();
let nextId = 1;
function assert(value, message) { if (!value) throw new Error(message); }
const document = { getElementById(id) {
  if (!elements.has(id)) elements.set(id, { handlers: {}, addEventListener(name, fn) { this.handlers[name] = fn; } });
  return elements.get(id);
}};
const window = { location: { search: '' }, addEventListener() {} };
class AbortController {
  constructor() { this.signal = { aborted: false, listeners: [] }; }
  abort() {
    if (this.signal.aborted) return;
    this.signal.aborted = true;
    this.signal.listeners.forEach(fn => fn());
  }
}
class URLSearchParams { get() { return null; } }
function fetch(url, options) {
  calls.push({ url, signal: options.signal });
  return new Promise((resolve, reject) => options.signal.listeners.push(() => reject(new Error('aborted'))));
}
function setTimeout(fn, ms) { const id = nextId++; timers.set(id, fn); return id; }
function clearTimeout(id) { timers.delete(id); }
function setInterval(fn, ms) { const id = nextId++; intervals.set(id, fn); return id; }
function clearInterval(id) { intervals.delete(id); }
'''
        checks = r'''
assert(calls.length === 2, 'Initial sensor and system requests expected');
for (let i = 0; i < 10; i++) { refreshSensors(); refreshSystem(); }
assert(calls.length === 2, 'Slow responses must not queue overlapping polls');
assert(intervals.size === 3, 'Expected one timer per dashboard poll');
elements.get('otaLogin').handlers.click();
assert(intervals.size === 0, 'Login navigation must stop polling timers');
assert(calls.every(call => call.signal.aborted), 'Login navigation must abort pending HTTP requests');
for (let i = 0; i < 10; i++) await Promise.resolve();
assert(pendingRequests.size === 0 && timers.size === 0, 'Aborted requests must release controllers and timers');
assert(!sensorsLoading && !systemLoading, 'Aborted requests must release in-flight guards');
await refreshSensors();
await refreshSystem();
assert(calls.length === 2, 'No polling while navigating to authentication');
startDashboardPolling();
assert(calls.length === 4 && intervals.size === 3, 'Polling must resume on browser back/pageshow');
startDashboardPolling();
assert(calls.length === 4 && intervals.size === 3, 'Repeated pageshow must not duplicate polling');
stopDashboardPolling();
for (let i = 0; i < 10; i++) await Promise.resolve();
print('Dashboard slow-response, login cancellation and polling-resume checks passed');
'''
        program = harness + "\nasync function run() {\n" + source + checks + "\n}\n"
        program += "const loop = new GLib.MainLoop(null, false);\n"
        program += "run().then(() => loop.quit()).catch(error => { printerr(error.stack); System.exit(1); });\nloop.run();\n"
        with tempfile.TemporaryDirectory() as directory:
            script = Path(directory) / "dashboard-test.js"
            script.write_text(program)
            result = subprocess.run(["gjs", str(script)], capture_output=True, text=True, timeout=15)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
