#!/usr/bin/env python3
"""Simple MJPEG streamer that serves /stream by repeatedly reading /tmp/live.jpg."""
from flask import Flask, Response, send_file, render_template_string
import time
import os

FRAME_PATH = '/tmp/live.jpg'
FPS = 10.0

app = Flask(__name__)

INDEX_HTML = '''
<html><head><title>Live Stream</title></head>
<body>
<h3>Jetson Live Stream</h3>
<img src="/stream" />
</body></html>
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
            if os.path.exists(FRAME_PATH):
                try:
                    with open(FRAME_PATH, 'rb') as f:
                        img = f.read()
                    yield boundary + b"\r\nContent-Type: image/jpeg\r\nContent-Length: " + str(len(img)).encode() + b"\r\n\r\n" + img + b"\r\n"
                except Exception:
                    # skip and continue
                    pass
            time.sleep(delay)
    return Response(gen(), mimetype='multipart/x-mixed-replace; boundary=frame')

if __name__ == '__main__':
    import argparse
    parser = argparse.ArgumentParser()
    parser.add_argument('--host', default='0.0.0.0')
    parser.add_argument('--port', type=int, default=8080)
    parser.add_argument('--fps', type=float, default=10.0)
    args = parser.parse_args()
    FPS = args.fps
    print(f"Starting MJPEG streamer on {args.host}:{args.port} serving {FRAME_PATH} at {FPS} FPS")
    app.run(host=args.host, port=args.port, threaded=True)
