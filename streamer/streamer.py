#!/usr/bin/env python3
"""Simple MJPEG streamer that serves /stream by repeatedly reading /tmp/live.jpg."""
from flask import Flask, Response, render_template_string, jsonify
import time
import os
import threading
import socket
import json

FRAME_PATH = '/tmp/live.jpg'
FPS = 10.0
latest_frame_bytes = None

app = Flask(__name__)

INDEX_HTML = '''
<!doctype html>
<html lang="en">
<head>
    <meta charset="utf-8">
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <title>Jetson Motion Tracker</title>
    <style>
        body { font-family: 'Inter', system-ui, -apple-system, sans-serif; margin: 0; background: #f5f7fb; color: #1a1a1a; }
        header { padding: 18px 24px; background: #0f172a; color: #e2e8f0; box-shadow: 0 2px 12px rgba(0,0,0,0.15); }
        h1 { margin: 0; font-size: 22px; display: flex; align-items: center; gap: 8px; }
        .container { padding: 24px; display: flex; gap: 18px; flex-wrap: wrap; }
        .card { background: #fff; border-radius: 12px; box-shadow: 0 6px 20px rgba(0,0,0,0.08); padding: 16px; }
        .stream-card { flex: 1 1 640px; max-width: 820px; }
        .metrics-card { width: 320px; min-width: 280px; }
        img.stream { width: 100%; border-radius: 10px; border: 3px solid #16a34a; background: #111; }
        .tabs { display: flex; gap: 8px; margin-bottom: 12px; }
        .tab { padding: 8px 12px; border-radius: 8px; border: 1px solid #cbd5e1; background: #fff; cursor: pointer; font-weight: 600; }
        .tab.active { background: #0f172a; color: #e2e8f0; border-color: #0f172a; }
        .section { margin-top: 12px; }
        .section h3 { margin: 0 0 8px 0; font-size: 14px; color: #0f172a; }
        .stat { padding: 8px 10px; border: 1px solid #e2e8f0; border-radius: 8px; margin-bottom: 6px; display: flex; justify-content: space-between; font-size: 14px; }
        .stat strong { color: #0f172a; }
        .q-item { padding: 8px 10px; border-radius: 8px; border: 1px solid #e2e8f0; margin-bottom: 6px; }
        .q-item .label { font-weight: 700; font-size: 13px; }
        .q-item .vals { font-size: 13px; color: #111; margin-top: 4px; }
        .pill { display: inline-block; padding: 2px 8px; border-radius: 999px; font-size: 12px; font-weight: 700; }
        .pill.ok { background: #dcfce7; color: #166534; }
        .pill.warn { background: #fee2e2; color: #991b1b; }
    </style>
</head>
<body>
    <header>
        <h1>🎯 Jetson Motion Tracker</h1>
    </header>
    <div class="container">
        <div class="card stream-card">
            <div class="tabs">
                <div class="tab active">Live Stream</div>
            </div>
            <img id="stream" class="stream" src="/stream" alt="Live stream" />
        </div>
        <div class="card metrics-card">
            <div style="display:flex;justify-content:space-between;align-items:center;">
                <div style="font-weight:700;">Tracking</div>
                <span id="tracking-pill" class="pill warn">NO TRACK</span>
            </div>
            <div class="section">
                <h3>Summary</h3>
                <div class="stat"><span>ID</span><strong id="marker-id">-</strong></div>
                <div class="stat"><span>RMS vel</span><strong id="rms">-</strong></div>
                <div class="stat"><span>Peak vel</span><strong id="peak">-</strong></div>
            </div>
            <div class="section">
                <h3>Quadrants</h3>
                <div id="quadrants"></div>
            </div>
        </div>
    </div>

    <script>
        const qColors = ["#ef4444","#f59e0b","#a855f7","#0ea5e9"];
        let peakVel = 0;

        function mag(vx, vy) { return Math.sqrt((vx||0)*(vx||0) + (vy||0)*(vy||0)); }

        function renderQuadrants(qs) {
                const wrap = document.getElementById('quadrants');
                wrap.innerHTML = '';
                qs.forEach((q, i) => {
                        const div = document.createElement('div');
                        div.className = 'q-item';
                        div.style.borderLeft = '4px solid ' + qColors[i];
                        const label = document.createElement('div');
                        label.className = 'label';
                        label.textContent = `Q${i}`;
                        const vel = mag(q.vx, q.vy).toFixed(1);
                        const acc = mag(q.ax, q.ay).toFixed(1);
                        const vals = document.createElement('div');
                        vals.className = 'vals';
                        vals.textContent = `Vel: ${vel} | Acc: ${acc}`;
                        div.appendChild(label);
                        div.appendChild(vals);
                        wrap.appendChild(div);
                });
        }

        async function poll() {
            try {
                const r = await fetch('/metrics');
                const j = await r.json();
                const tracking = j.tracking !== false && j.marker_id !== undefined && j.marker_id !== -1;
                document.getElementById('tracking-pill').textContent = tracking ? 'TRACKING' : 'NO TRACK';
                document.getElementById('tracking-pill').className = 'pill ' + (tracking ? 'ok' : 'warn');
                document.getElementById('marker-id').textContent = tracking ? j.marker_id : '-';

                if (j.quadrants) {
                        const mags = j.quadrants.filter(q=>q.valid).map(q=>mag(q.vx, q.vy));
                        const rms = mags.length ? Math.sqrt(mags.reduce((s,x)=>s+x*x,0)/mags.length) : 0;
                        peakVel = Math.max(peakVel, ...(mags.length?mags:[0]));
                        document.getElementById('rms').textContent = rms.toFixed(2);
                        document.getElementById('peak').textContent = peakVel.toFixed(2);

                        renderQuadrants(j.quadrants.map(q=>({
                                vx: q.valid ? q.vx : 0,
                                vy: q.valid ? q.vy : 0,
                                ax: q.valid ? q.ax : 0,
                                ay: q.valid ? q.ay : 0,
                        })));
                }
            } catch(e) {
                console.error(e);
            }
            setTimeout(poll, 200);
        }
        poll();
    </script>
</body>
</html>
'''

@app.route('/')
def index():
    return render_template_string(INDEX_HTML)

@app.route('/stream')
def stream():
    def gen():
        boundary = b'--frame'
        delay = 1.0 / FPS
        while True:
            if latest_frame_bytes:
                img = latest_frame_bytes
                yield boundary + b"\r\nContent-Type: image/jpeg\r\nContent-Length: " + str(len(img)).encode() + b"\r\n\r\n" + img + b"\r\n"
            elif os.path.exists(FRAME_PATH):
                try:
                    with open(FRAME_PATH, 'rb') as f:
                        img = f.read()
                    yield boundary + b"\r\nContent-Type: image/jpeg\r\nContent-Length: " + str(len(img)).encode() + b"\r\n\r\n" + img + b"\r\n"
                except Exception:
                    # skip and continue
                    pass
            time.sleep(delay)
    return Response(gen(), mimetype='multipart/x-mixed-replace; boundary=frame')

# UDP metrics receiver (port 5001) and endpoint
latest_metrics = {}

def udp_receiver(host='0.0.0.0', port=5001):
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind((host, port))
    sock.settimeout(1.0)
    print(f"UDP metrics receiver on {host}:{port}")
    while True:
        try:
            data, _ = sock.recvfrom(8192)
            # try parse as JSON
            try:
                import json
                latest_metrics.update(json.loads(data.decode('utf-8')))
            except Exception:
                pass
        except socket.timeout:
            continue
        except Exception:
            break

@app.route('/metrics')
def metrics():
    return jsonify(latest_metrics or {"status":"no data yet"})

# UDP JPEG frame receiver using the custom chunking protocol
def udp_frame_receiver(host='0.0.0.0', port=5002):
    global latest_frame_bytes
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind((host, port))
    sock.settimeout(1.0)
    print(f"UDP frame receiver on {host}:{port}")
    partial = {}
    last_cleanup = time.time()
    while True:
        try:
            data, _ = sock.recvfrom(65536)
            if len(data) < 12 or data[:4] != b'IMG0':
                continue
            fid = int.from_bytes(data[4:8], 'big')
            total = int.from_bytes(data[8:10], 'big')
            idx = int.from_bytes(data[10:12], 'big')
            payload = data[12:]
            entry = partial.setdefault(fid, {"total": total, "chunks": {}, "bytes": 0})
            if idx not in entry["chunks"]:
                entry["chunks"][idx] = payload
                entry["bytes"] += len(payload)
            if len(entry["chunks"]) == entry["total"]:
                # reassemble
                buf = bytearray()
                for i in range(entry["total"]):
                    buf.extend(entry["chunks"][i])
                latest_frame_bytes = bytes(buf)
                # cleanup old frame id
                keys = [k for k in partial.keys() if k <= fid]
                for k in keys:
                    partial.pop(k, None)
        except socket.timeout:
            pass
        except Exception:
            break
        # periodic cleanup in case of dropped frames
        if time.time() - last_cleanup > 5:
            # drop partials with too many missing chunks
            drop = [k for k, v in partial.items() if len(v.get("chunks", {})) < max(1, v.get("total", 1)//2)]
            for k in drop:
                partial.pop(k, None)
            last_cleanup = time.time()

if __name__ == '__main__':
    import argparse
    parser = argparse.ArgumentParser()
    parser.add_argument('--host', default='0.0.0.0')
    parser.add_argument('--port', type=int, default=8080)
    parser.add_argument('--fps', type=float, default=10.0)
    args = parser.parse_args()
    FPS = args.fps
    print(f"Starting MJPEG streamer on {args.host}:{args.port} serving {FRAME_PATH} at {FPS} FPS")
    t = threading.Thread(target=udp_receiver, daemon=True)
    t.start()
    t2 = threading.Thread(target=udp_frame_receiver, daemon=True)
    t2.start()
    app.run(host=args.host, port=args.port, threaded=True)
