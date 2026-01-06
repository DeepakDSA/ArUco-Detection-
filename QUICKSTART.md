# ArUco Motion Tracker - Complete Setup & Run Guide

## 📋 Overview
High-FPS ArUco marker detection + CUDA optical flow tracking → CSV logging → Google Drive uploads

**Performance:** ~117-120 FPS capture | ~117-120 FPS processing | 10 FPS live UI

---

## 🔨 Step 1: Build the C++ Project

```bash
cd /home/robo/Desktop/Aruco-DSA
mkdir -p build
cd build
cmake ..
make -j$(nproc)
```

Binary location: `/home/robo/Desktop/Aruco-DSA/build/jetson_motion_tracker`

---

## 📷 Step 2: Setup Camera (DMK37BUX273)

Run **once per boot**:

```bash
v4l2-ctl --device=/dev/video0 \
   --set-fmt-video=width=640,height=480,pixelformat=GREY \
   --set-parm=120 \
   --set-ctrl=exposure_auto=1 \
   --set-ctrl=exposure_absolute=50
```

Verify:
```bash
v4l2-ctl --device=/dev/video0 --get-fmt-video
v4l2-ctl --device=/dev/video0 --get-parm
```

---

## 🎯 Step 3: Run the Motion Tracker

**Terminal 1** - Start tracker (generates frames + JSON + CSV):

```bash
cd /home/robo/Desktop/Aruco-DSA
export ARUCO_OUT_DIR=/data/yash_project/frames
mkdir -p $ARUCO_OUT_DIR

./build/jetson_motion_tracker \
    --source camera \
    --device /dev/video0 \
    --width 640 \
    --height 480 \
    --framerate 120 \
    --reuse-buffer
```

**Output files created:**
- `$ARUCO_OUT_DIR/frame_<timestamp>.jpg` — captured frames
- `$ARUCO_OUT_DIR/frame_<timestamp>.jpg.json` — marker metadata (ID, position, velocity, acceleration)
- `$ARUCO_OUT_DIR/metrics.csv` — per-frame tracking log

**Optional flags:**
```bash
--display              # Show OpenCV overlay window
--no-save              # Don't save frames/JSON (faster processing)
--no-csv               # Don't write metrics.csv
--no-metrics           # Don't send UDP metrics
--no-live              # Don't send live MJPEG stream
```

---

## 🌐 Step 4: Run Web UI (Flask)

**Terminal 2** - Start streamer:

```bash
cd /home/robo/Desktop/Aruco-DSA
python3 streamer/streamer.py --host 0.0.0.0 --port 5000 --fps 10
```

**Open in browser:**
```
http://<your-jetson-ip>:5000
```

Shows:
- Live MJPEG stream
- Real-time tracking status
- Per-quadrant velocity & acceleration
- RMS/peak metrics

---

## 📤 Step 5: Upload to Google Drive

**Configure (one-time):**

The project already has:
- ✅ Service account credentials at `secrets/service_account.json`
- ✅ Config file at `config/drive_config.json` (folder ID set)

**Terminal 3** - Start rclone sync:

```bash
/data/yash_project/upload_to_drive.sh
```

Or run manually:
```bash
cd /home/robo/Desktop/Aruco-DSA
export ARUCO_OUT_DIR=/data/yash_project/frames
rclone sync "$ARUCO_OUT_DIR" gdrive:/aruco-data \
    --include="*.jpg" \
    --include="*.json" \
    -v
```

**Syncs to:** `Google Drive / aruco-data /` folder every 5 seconds

---

## ✅ Step 6: Validate Outputs

Check what's been generated:

```bash
/data/yash_project/validate_outputs.sh
```

Shows:
- File counts (JPG, JSON, CSV)
- Latest outputs with sizes
- Sample CSV structure
- Sample JSON metadata

---

## 🚀 Quick Start (All-in-One)

Open **3 terminal tabs** and run:

**Tab 1 (Tracker):**
```bash
cd /home/robo/Desktop/Aruco-CSA
export ARUCO_OUT_DIR=/data/yash_project/frames
mkdir -p $ARUCO_OUT_DIR
./build/jetson_motion_tracker --source camera --device /dev/video0 --width 640 --height 480 --framerate 120 --reuse-buffer
```

**Tab 2 (Web UI):**
```bash
cd /home/robo/Desktop/Aruco-DSA
python3 streamer/streamer.py --host 0.0.0.0 --port 5000 --fps 10
```

**Tab 3 (Google Drive Sync):**
```bash
/data/yash_project/upload_to_drive.sh
```

Then open: `http://<jetson-ip>:5000`

---

## 📊 Output File Structure

```
/data/yash_project/frames/
├── frame_1704704400000000.jpg         (captured frame)
├── frame_1704704400000000.jpg.json    (metadata)
├── frame_1704704401000000.jpg
├── frame_1704704401000000.jpg.json
└── metrics.csv                        (per-frame log)
```

### Sample JSON Structure:
```json
{
  "marker_id": 0,
  "ts_us": 1704704400000000,
  "quadrants": [
    {
      "valid": true,
      "cx": 320.5,
      "cy": 240.2,
      "vx": 5.3,
      "vy": -2.1,
      "ax": 0.8,
      "ay": -0.3
    },
    ...
  ]
}
```

### Sample CSV Rows:
```
ts_us,marker_id,q0_valid,q0_cx,q0_cy,q0_vx,q0_vy,q0_ax,q0_ay,...
1704704400000000,0,1,320.5,240.2,5.3,-2.1,0.8,-0.3,...
1704704400100000,0,1,321.8,239.1,6.1,-1.9,0.9,-0.2,...
```

---

## 🐛 Troubleshooting

| Issue | Solution |
|-------|----------|
| `Camera: no source` | Check camera: `ls /dev/video*` |
| Low FPS | Use `--no-display`, `--no-save`, `--no-csv`, `--no-metrics` |
| No frames saved | Ensure `ARUCO_OUT_DIR` is writable: `mkdir -p /data/yash_project/frames` |
| Drive sync fails | Verify folder exists: `rclone lsd gdrive:/` |
| Web UI not loading | Check port 5000 is free: `lsof -i :5000` |

---

## 📝 Config Files

**Camera config:** None needed (hardcoded for DMK37BUX273)

**Drive config:** `config/drive_config.json`
```json
{
  "service_account": "/home/robo/Desktop/Aruco-DSA/secrets/service_account.json",
  "folder_id": "1w50Yn5mBstCUTemPej9LrjPRcZPWpxB1",
  "mime_overrides": {
    ".json": "application/json",
    ".jpg": "image/jpeg"
  }
}
```

---

## 🎮 Controls

In the OpenCV display window (if `--display` used):
- **`q`** — Quit
- **`o`** — Toggle overlay

---

## 📈 Expected Performance

| Metric | Value |
|--------|-------|
| Capture FPS | ~120 |
| Processing FPS | ~117-120 |
| Live Stream FPS | 10 |
| Marker Detection | Every 20 frames (~6 Hz) |
| CSV Log Rate | Per-frame |
| Drive Sync Interval | 5s |

---

## 🔒 Security Notes

- Service account JSON is in `.gitignore` (protected from git)
- Credentials only used locally or on trusted networks
- Folder ID is stored in plaintext (safe to share)

---

## ✨ Summary

```
1. Build:     cmake && make
2. Setup:     v4l2-ctl (camera config)
3. Tracker:   ./build/jetson_motion_tracker
4. UI:        python3 streamer/streamer.py
5. Upload:    /data/yash_project/upload_to_drive.sh
6. Validate:  /data/yash_project/validate_outputs.sh
```

Done! 🎉
