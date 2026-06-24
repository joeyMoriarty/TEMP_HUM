"""
Room Climate Logger & Dashboard
================================
1. Polls ESP32 every 60 s → appends to readings.csv
2. Serves /history.json  → ESP32 OLED graph (last 24 h, 96 points)
3. Serves /              → Browser dashboard with live + Chart.js graphs
4. Serves /readings.csv  → Direct CSV download

Usage
-----
  pip install flask requests
  python server.py

  On first run the script prints your PC's local IP.
  Copy that IP into TEMP_HUM.ino → PC_HOST, then re-upload the sketch.

  Access the dashboard at http://<your-PC-IP>:5000
"""

import csv
import json
import socket
import threading
import time
from datetime import datetime, timedelta
from pathlib import Path

import requests
from flask import Flask, jsonify, Response, send_file

# ── Configuration ─────────────────────────────────────────────────────────────
ESP32_HOST    = "climate.local"   # or paste the ESP32's IP from Serial Monitor
CSV_FILE      = Path("readings.csv")
PORT          = 5000
POLL_INTERVAL = 60    # seconds between ESP32 polls
HIST_POINTS   = 96    # points returned to OLED (1 per 15 min = 24 h)

# ─────────────────────────────────────────────────────────────────────────────
app      = Flask(__name__)
csv_lock = threading.Lock()

# ── Helpers ───────────────────────────────────────────────────────────────────

def get_local_ip() -> str:
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.connect(("8.8.8.8", 80))
        return s.getsockname()[0]
    except Exception:
        return "127.0.0.1"


def append_reading(temp: float, hum: float, feels: float) -> None:
    with csv_lock:
        exists = CSV_FILE.exists()
        with open(CSV_FILE, "a", newline="") as f:
            w = csv.writer(f)
            if not exists:
                w.writerow(["timestamp", "temp_c", "humidity_pct", "feels_like_c"])
            w.writerow([datetime.now().isoformat(timespec="seconds"),
                        f"{temp:.2f}", f"{hum:.2f}", f"{feels:.2f}"])


def read_last_24h() -> list:
    """Return rows from the last 24 hours as list of dicts."""
    if not CSV_FILE.exists():
        return []
    cutoff = datetime.now() - timedelta(hours=24)
    rows = []
    with csv_lock:
        with open(CSV_FILE, newline="") as f:
            for row in csv.DictReader(f):
                try:
                    ts = datetime.fromisoformat(row["timestamp"])
                    if ts >= cutoff:
                        rows.append({
                            "ts":    ts.isoformat(),
                            "temp":  float(row["temp_c"]),
                            "hum":   float(row["humidity_pct"]),
                            "feels": float(row["feels_like_c"]),
                        })
                except (ValueError, KeyError):
                    pass
    return rows


def downsample(rows: list, n: int) -> list:
    """Average rows into n evenly-spaced buckets."""
    if not rows:
        return []
    if len(rows) <= n:
        return rows
    bucket = len(rows) / n
    out = []
    for i in range(n):
        s, e = int(i * bucket), int((i + 1) * bucket)
        chunk = rows[s:e]
        if chunk:
            out.append({
                "ts":    chunk[-1]["ts"],
                "temp":  sum(r["temp"]  for r in chunk) / len(chunk),
                "hum":   sum(r["hum"]   for r in chunk) / len(chunk),
                "feels": sum(r["feels"] for r in chunk) / len(chunk),
            })
    return out


# ── Background polling thread ─────────────────────────────────────────────────

def poll_loop() -> None:
    time.sleep(3)  # let server start first
    while True:
        try:
            resp = requests.get(f"http://{ESP32_HOST}/data.json", timeout=5)
            if resp.status_code == 200:
                d = resp.json()
                if d.get("valid"):
                    append_reading(d["temp"], d["humidity"], d["feelsLike"])
                    print(f"[{datetime.now():%H:%M:%S}]  "
                          f"T={d['temp']:.1f}°C  "
                          f"RH={d['humidity']:.1f}%  "
                          f"FL={d['feelsLike']:.1f}°C  → logged")
                else:
                    print(f"[{datetime.now():%H:%M:%S}]  ESP32: sensor not detected")
            else:
                print(f"[{datetime.now():%H:%M:%S}]  ESP32 HTTP {resp.status_code}")
        except Exception as exc:
            print(f"[{datetime.now():%H:%M:%S}]  Poll error: {exc}")
        time.sleep(POLL_INTERVAL)


# ── Flask routes ──────────────────────────────────────────────────────────────

@app.route("/history.json")
def history_json():
    """Compact history for the OLED graph — temps/hums arrays only."""
    rows    = read_last_24h()
    sampled = downsample(rows, HIST_POINTS)
    return jsonify({
        "temps":      [round(r["temp"],  2) for r in sampled],
        "hums":       [round(r["hum"],   2) for r in sampled],
        "timestamps": [r["ts"]              for r in sampled],
        "count":      len(sampled),
    })


@app.route("/live.json")
def live_json():
    """Most recent logged reading (for the dashboard page)."""
    if not CSV_FILE.exists():
        return jsonify({"valid": False})
    with csv_lock:
        with open(CSV_FILE, newline="") as f:
            rows = list(csv.DictReader(f))
    if not rows:
        return jsonify({"valid": False})
    last = rows[-1]
    return jsonify({
        "valid":     True,
        "temp":      float(last["temp_c"]),
        "humidity":  float(last["humidity_pct"]),
        "feelsLike": float(last["feels_like_c"]),
        "timestamp": last["timestamp"],
    })


@app.route("/readings.csv")
def download_csv():
    if CSV_FILE.exists():
        return send_file(str(CSV_FILE.resolve()),
                         as_attachment=True,
                         download_name="readings.csv")
    return "No data yet", 404


@app.route("/")
def dashboard():
    html = """<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Room Climate — 24 h Dashboard</title>
<script src="https://cdn.jsdelivr.net/npm/chart.js@4.4.0/dist/chart.umd.min.js"></script>
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{background:#0d1117;color:#e6edf3;font-family:-apple-system,BlinkMacSystemFont,sans-serif;padding:20px}
h1{color:#58a6ff;text-align:center;margin:16px 0 4px;font-size:1.4em}
.sub{text-align:center;color:#8b949e;font-size:.8em;margin-bottom:20px}
.cards{display:flex;justify-content:center;gap:14px;flex-wrap:wrap;margin-bottom:20px}
.card{background:#161b22;border:1px solid #30363d;border-radius:10px;padding:16px 24px;text-align:center;min-width:100px}
.val{font-size:2em;font-weight:700;color:#3fb950}
.lbl{color:#8b949e;font-size:.8em;margin-top:4px}
.chart-box{background:#161b22;border:1px solid #30363d;border-radius:10px;padding:16px;margin:0 auto 16px;max-width:860px}
.chart-title{color:#8b949e;font-size:.85em;margin-bottom:8px}
footer{text-align:center;color:#484f58;font-size:.75em;margin-top:10px}
a{color:#58a6ff}
.dot{display:inline-block;width:8px;height:8px;border-radius:50%;background:#f85149;margin-right:4px;animation:blink 2s infinite}
@keyframes blink{0%,100%{opacity:1}50%{opacity:.3}}
</style>
</head>
<body>
<h1>Room Climate Monitor</h1>
<p class="sub"><span class="dot"></span><span id="ts">--</span></p>

<div class="cards">
  <div class="card"><div class="val" id="vT">--</div><div class="lbl">Temperature</div></div>
  <div class="card"><div class="val" id="vH">--</div><div class="lbl">Humidity</div></div>
  <div class="card"><div class="val" id="vF">--</div><div class="lbl">Feels Like</div></div>
</div>

<div class="chart-box">
  <div class="chart-title">Temperature (°C) — last 24 h</div>
  <canvas id="chartT" height="120"></canvas>
</div>
<div class="chart-box">
  <div class="chart-title">Humidity (%) — last 24 h</div>
  <canvas id="chartH" height="120"></canvas>
</div>

<footer>
  Logs every 60 s &nbsp;·&nbsp;
  <a href="/readings.csv" download>Download CSV</a> &nbsp;·&nbsp;
  <a href="/history.json">history.json</a>
</footer>

<script>
const cfg = (label, color) => ({
  type: 'line',
  data: { labels: [], datasets: [{ label, data: [],
    borderColor: color, backgroundColor: color + '22',
    borderWidth: 1.5, pointRadius: 0, fill: true, tension: 0.3 }] },
  options: {
    responsive: true, animation: false,
    plugins: { legend: { display: false } },
    scales: {
      x: { ticks: { color: '#8b949e', maxTicksLimit: 8,
                    maxRotation: 0, font: { size: 10 } },
           grid: { color: '#21262d' } },
      y: { ticks: { color: '#8b949e', font: { size: 10 } },
           grid: { color: '#21262d' } }
    }
  }
});

const chartT = new Chart(document.getElementById('chartT'), cfg('Temp °C',   '#58a6ff'));
const chartH = new Chart(document.getElementById('chartH'), cfg('Humidity %', '#3fb950'));

function setChart(chart, labels, data) {
  chart.data.labels               = labels;
  chart.data.datasets[0].data     = data;
  chart.update('none');
}

function fmt(iso) {
  const d = new Date(iso);
  return d.toLocaleTimeString([], { hour: '2-digit', minute: '2-digit' });
}

async function updateLive() {
  try {
    const d = await (await fetch('/live.json')).json();
    if (d.valid) {
      document.getElementById('vT').textContent  = d.temp.toFixed(1)      + ' °C';
      document.getElementById('vH').textContent  = d.humidity.toFixed(1)  + ' %';
      document.getElementById('vF').textContent  = d.feelsLike.toFixed(1) + ' °C';
      document.getElementById('ts').textContent  = 'Last reading: ' + d.timestamp.replace('T', ' ');
    }
  } catch(e) {}
}

async function updateCharts() {
  try {
    const d = await (await fetch('/history.json')).json();
    const labels = d.timestamps.map(fmt);
    setChart(chartT, labels, d.temps);
    setChart(chartH, labels, d.hums);
  } catch(e) {}
}

updateLive();   setInterval(updateLive,   60000);
updateCharts(); setInterval(updateCharts, 60000);
</script>
</body>
</html>"""
    return Response(html, mimetype="text/html")


# ── Entry point ───────────────────────────────────────────────────────────────

if __name__ == "__main__":
    local_ip = get_local_ip()
    bar = "=" * 54
    print(f"\n{bar}")
    print(f"  Room Climate Logger")
    print(f"  Dashboard : http://{local_ip}:{PORT}")
    print(f"  ESP32 host: {ESP32_HOST}")
    print(f"")
    print(f"  In TEMP_HUM.ino set:")
    print(f"    #define PC_HOST  \"{local_ip}\"")
    print(f"    #define PC_PORT  {PORT}")
    print(f"{bar}\n")

    threading.Thread(target=poll_loop, daemon=True).start()
    app.run(host="0.0.0.0", port=PORT, use_reloader=False)
