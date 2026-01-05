#include "pipeline/v4l2_source.h"
#include "pipeline/video_file_source.h"
#include "pipeline/image_sequence_source.h"
#include "processing/aruco_tracker.h"
#include "util/ring_buffer.h"

#include <gst/gst.h>
#include <atomic>
#include <csignal>
#include <iostream>
#include <memory>
#include <chrono>
#include <thread>
#include <sstream>
#include <iomanip>

static std::atomic<bool> running(true);

void sigint(int) { running = false; }

int main(int argc, char** argv) {
    signal(SIGINT, sigint);

    // parse args
    bool display = false;
    int width = 640, height = 480;
    int framerate = 110; // fps
    int io_mode = 0;
    int queue_sz = 8, max_buffers = 8;
    int process_every = 1;
    bool reuse_buffer = false;
    int ring_size = 8;
    bool ring_drop_oldest = true;

    for (int i=1;i<argc;i++) {
        std::string a(argv[i]);
        if (a == "--display" || a == "-d") display = true;
        else if (a == "--width" && i+1<argc) { width = atoi(argv[++i]); }
        else if (a == "--height" && i+1<argc) { height = atoi(argv[++i]); }
        else if (a == "--framerate" && i+1<argc) { framerate = atoi(argv[++i]); }
        else if (a == "--io-mode" && i+1<argc) { io_mode = atoi(argv[++i]); }
        else if (a == "--queue" && i+1<argc) { queue_sz = atoi(argv[++i]); }
        else if (a == "--max-buffers" && i+1<argc) { max_buffers = atoi(argv[++i]); }
        else if (a == "--process-every" && i+1<argc) { process_every = atoi(argv[++i]); }
        else if (a == "--reuse-buffer") { reuse_buffer = true; }
        else if (a == "--ring-size" && i+1<argc) { ring_size = atoi(argv[++i]); }
        else if (a == "--ring-drop-oldest") { ring_drop_oldest = true; }
        else if (a == "--ring-drop-new") { ring_drop_oldest = false; }
    }

    // REQUIRED for GStreamer
    gst_init(&argc, &argv);

    // create FrameSource based on --source
    std::string source = "camera";
    std::string source_path;
    for (int i=1;i<argc;i++) {
        std::string a(argv[i]);
        if (a == "--source" && i+1<argc) source = argv[++i];
        else if (a == "--source-path" && i+1<argc) source_path = argv[++i];
    }

    std::unique_ptr<FrameSource> camp;
    if (source == "camera") {
        std::unique_ptr<V4L2CameraSource> cam =
            std::make_unique<V4L2CameraSource>(width, height, framerate, 1, io_mode, queue_sz, max_buffers, true, false, reuse_buffer);
        if (!cam->open()) { std::cerr << "Camera open failed\n"; return -1; }
        camp = std::move(cam);
    } else if (source == "video") {
        if (source_path.empty()) { std::cerr << "--source-path required for video\n"; return -1; }
        auto vf = std::make_unique<VideoFileSource>(source_path);
        if (!vf->open()) { std::cerr << "Video file open failed\n"; return -1; }
        camp = std::move(vf);
    } else if (source == "sequence") {
        if (source_path.empty()) { std::cerr << "--source-path required for sequence\n"; return -1; }
        auto seq = std::make_unique<ImageSequenceSource>(source_path);
        if (!seq->open()) { std::cerr << "Image sequence open failed\n"; return -1; }
        camp = std::move(seq);
    } else {
        std::cerr << "Unknown --source: " << source << std::endl;
        return -1;
    }

    ArucoTracker tracker;

    if (display) {
        cv::namedWindow("Live", cv::WINDOW_AUTOSIZE);
        // Quick check: is display usable? If not, warn the user.
        double prop = cv::getWindowProperty("Live", cv::WND_PROP_AUTOSIZE);
        if (prop < 0) {
            std::cerr << "Warning: display window could not be created; are you running headless or without X?\n";
        }
    }

    // FPS counters
    std::atomic<int> proc_fps_cnt{0};
    std::atomic<int> cap_fps_cnt{0};
    std::atomic<int> dropped_cnt{0};
    auto t0_report = std::chrono::high_resolution_clock::now();
    std::atomic<int> total_frames{0};

    // Define a simple frame item for the ring buffer
    struct FrameItem { cv::Mat frame; uint64_t ts; };

    RingBuffer<FrameItem> ring(ring_size, ring_drop_oldest);

    // Capture thread: pushes frames into ring buffer
    std::thread capture_thread([&](){
        while (running) {
            FrameItem it;
            if (!camp->grab(it.frame, it.ts)) continue;
            bool ok = ring.push(it);
            if (ok) {
                cap_fps_cnt++;
            } else {
                dropped_cnt++;
            }
        }
        // on exit, ensure consumers wake up
        ring.close();
    });

    // Processing loop: pop frames from ring and process
    bool overlay_on = true;
    while (running) {
        FrameItem it;
        if (!ring.pop(it)) break; // closed and empty

        int tf = ++total_frames;
        if (tf % process_every == 0) {
            tracker.process(it.frame, it.ts);
            proc_fps_cnt++;
        }

        auto t1_report = std::chrono::high_resolution_clock::now();
        if (std::chrono::duration<double>(t1_report - t0_report).count() >= 1.0) {
            std::cout << "Processing FPS: " << proc_fps_cnt.load() << " | Capture FPS: " << cap_fps_cnt.load()
                      << " | Dropped: " << dropped_cnt.load() << " | Ring size: " << ring.size() << std::endl;
            proc_fps_cnt = 0;
            cap_fps_cnt = 0;
            dropped_cnt = 0;
            t0_report = t1_report;
        }

        if (display) {
            cv::Mat vis;
            if (it.frame.channels() == 1) cv::cvtColor(it.frame, vis, cv::COLOR_GRAY2BGR);
            else vis = it.frame.clone();

            auto& s = tracker.state();

            if (s.tracking) {
                // draw bbox and marker id
                if (overlay_on) {
                    cv::rectangle(vis, s.marker_bbox, cv::Scalar(0,255,0), 2);
                    std::string idtxt = "ID: " + std::to_string(s.marker_id);
                    cv::putText(vis, idtxt, {s.marker_bbox.x, std::max(0, s.marker_bbox.y-6)}, cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(0,255,0), 1);
                }

                // Draw each quadrant
                for (int i=0;i<4;i++) {
                    cv::Scalar col = s.q[i].valid ? cv::Scalar(0,0,255) : cv::Scalar(0,128,255);
                    cv::Point2f center;
                    if (s.q[i].valid) center = s.q[i].motion.pos;
                    else center = cv::Point2f(s.marker_bbox.x + s.marker_bbox.width * (i%2 ? 0.75f : 0.25f),
                                              s.marker_bbox.y + s.marker_bbox.height * (i<2 ? 0.25f : 0.75f));

                    int ix = cv::saturate_cast<int>(center.x);
                    int iy = cv::saturate_cast<int>(center.y);

                    if (s.q[i].valid) {
                        cv::circle(vis, {ix, iy}, 4, col, -1);
                        if (overlay_on) {
                            // draw velocity arrow (scaled for visibility)
                            float vx = s.q[i].motion.vel.x;
                            float vy = s.q[i].motion.vel.y;
                            float scale = 0.05f; // px per (velocity unit)
                            cv::Point dst(cv::saturate_cast<int>(ix + vx*scale), cv::saturate_cast<int>(iy + vy*scale));
                            cv::arrowedLine(vis, {ix,iy}, dst, cv::Scalar(255,0,0), 1, cv::LINE_AA, 0, 0.3);
                            // put small text for vx,vy
                            std::ostringstream os; os << std::fixed << std::setprecision(1) << "v=" << vx << "," << vy;
                            cv::putText(vis, os.str(), {ix+6, iy-6}, cv::FONT_HERSHEY_SIMPLEX, 0.4, cv::Scalar(255,255,255), 1);
                        }
                    } else {
                        // draw small X
                        cv::line(vis, {ix-3,iy-3},{ix+3,iy+3}, col, 1);
                        cv::line(vis, {ix+3,iy-3},{ix-3,iy+3}, col, 1);
                    }
                }
            }

            cv::imshow("Live", vis);
            int k = cv::waitKey(1);
            if (k == 'q' || k == 'Q') {
                running = false;
                break;
            } else if (k == 'o' || k == 'O') {
                overlay_on = !overlay_on;
            }
        }
    }

    // shutdown
    ring.close();
    if (capture_thread.joinable()) capture_thread.join();

    camp->close();
    return 0;
}
