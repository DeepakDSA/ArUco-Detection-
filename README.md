# Jetson ArUco Motion Tracker

High-FPS ArUco detection + CUDA LK optical flow, live web UI, per-frame CSV logging, and Drive uploads.

- V4L2 + GStreamer capture (target 110–120 FPS)
- ArUco detection + 4-quadrant ROI
- CUDA SparsePyrLK tracking
- Velocity/acceleration per frame
- Async CSV logging
- Periodic frame + JSON snapshots
- UDP metrics to Flask (web UI)
- Drive uploads (rclone or Google API)

## Build (C++)

```bash
cd /home/robo/Desktop/Aruco-DSA
mkdir -p build && cd build
cmake ..
make -j$(nproc)
```

Binary: `build/jetson_motion_tracker`

## Run (tracker)

```bash
# Set output directory for frames/csv (optional)
export ARUCO_OUT_DIR=/data/yash_project/frames

# Camera source, grayscale 640x480 @120fps
./build/jetson_motion_tracker --source camera --device /dev/video0 --width 640 --height 480 --framerate 120 --reuse-buffer
```

### DMK37BUX273 (fixed pipeline)

Before running, force the camera mode (one-time per boot/session):

```bash
v4l2-ctl --device=/dev/video0 \
   --set-fmt-video=width=640,height=480,pixelformat=GREY \
   --set-parm=120 \
   --set-ctrl=exposure_auto=1 --set-ctrl=exposure_absolute=50
```

Then run the tracker (uses a single mmap GRAY8 pipeline, no fallbacks):

```bash
cd /home/robo/Desktop/Aruco-DSA/build
export ARUCO_OUT_DIR=/data/yash_project/frames
./jetson_motion_tracker --source camera --device /dev/video0 --width 640 --height 480 --framerate 120 --reuse-buffer
```

- Use `--display` to show overlay UI.
- Use `--source video --source-path /path/video.mp4` for file input.
- Use `--source sequence --source-path /path/images` for image folder.

## Web UI (Flask)

```bash
python3 streamer/streamer.py --host 0.0.0.0 --port 5000 --fps 10
```
- Opens MJPEG stream at `http://<host>:5000/`.
- Receives UDP metrics on port 5001 and displays live JSON.

## Google Drive Upload

1. Create `config/drive_config.json` from example and set:
   - `service_account`: absolute path to your credentials json
   - `folder_id`: Google Drive folder ID to upload into

2. Install Python deps:
```bash
python3 -m pip install google-api-python-client google-auth-httplib2 google-auth-oauthlib google-auth
```

3a. Run uploader (Google API):
```bash
python3 tools/drive_uploader.py --out-dir ${ARUCO_OUT_DIR} --config config/drive_config.json --poll-interval 2 --extensions ".jpg,.json,.csv"
```
It uploads `.jpg`, `.json`, and `.csv` files to the configured Drive folder. State is tracked in `.upload_state.json` to avoid duplicates.

3b. Or use rclone (recommended for continuous sync):
```bash
/data/yash_project/upload_to_drive.sh
```
Syncs `/data/yash_project/frames` to `gdrive:/aruco-data` and keeps `metrics.csv` up to date.

## Notes on Performance
- Capture uses GStreamer `appsink` with drop=true, sync=false.
- Processing offloads LK to CUDA and minimizes copies.
- Displaying a window may reduce FPS; run headless for maximum throughput.
- CSV logging runs asynchronously in a background thread; large spikes are bounded by a ring buffer.

## Controls
- In the display window, press `q` to quit, `o` to toggle overlay.

## Output Files
- Frames: `${ARUCO_OUT_DIR}/frame_<ts_us>.jpg`
- JSON: `${ARUCO_OUT_DIR}/frame_<ts_us>.jpg.json`
- CSV: `${ARUCO_OUT_DIR}/metrics.csv`

## Quick Start (3 terminals)

1) Tracker
```bash
cd /home/robo/Desktop/Aruco-DSA
export ARUCO_OUT_DIR=/data/yash_project/frames
mkdir -p "$ARUCO_OUT_DIR"
./build/jetson_motion_tracker --source camera --device /dev/video0 --width 640 --height 480 --framerate 120 --reuse-buffer
```

2) Web UI
```bash
cd /home/robo/Desktop/Aruco-DSA
python3 streamer/streamer.py --host 0.0.0.0 --port 5000 --fps 10
```

3) Drive Sync
```bash
/data/yash_project/upload_to_drive.sh
```

## Publish to GitHub

Credentials are protected via `.gitignore` (secrets/). Verify before pushing:
```bash
cd /home/robo/Desktop/Aruco-DSA
git status
git add .
git commit -m "ArUco tracker: high-FPS pipeline, UI, uploads"
git remote add origin https://github.com/DeepakDSA/ArUco-Detection-.git  # if not set
git push -u origin main
```

Ensure the folder is shared with the service account (for Google API uploads), or use `rclone` which uses your `gdrive` remote.
