#include "aruco_tracker.h"
#include "motion_update.h"
#include "frame_writer.h"
#include "../network/udp_sender.h"
#include "../util/csv_logger.h"

#include <sstream>
#include <iomanip>

using namespace cv;

ArucoTracker::ArucoTracker() {
    dict_ = aruco::getPredefinedDictionary(aruco::DICT_4X4_50);
    lk_ = cuda::SparsePyrLKOpticalFlow::create();
    // Tune LK for speed: fewer pyramid levels and smaller window
    lk_->setMaxLevel(2);            // default 3
    lk_->setWinSize({15,15});       // default 21x21
    lk_->setNumIters(10);           // keep iterations moderate
}

void ArucoTracker::process(const Mat& frame, uint64_t ts_us) {
    d_curr_.upload(frame);
    frame_count_++;

    if (!state_.tracking || frame_count_ % 20 == 0)
        detect_marker(frame);

    if (state_.tracking && have_prev_)
        track(frame, ts_us);

    // Append CSV metrics for each processed frame (asynchronous logger)
    if (options_.enable_csv) {
        CsvLogger::instance().log(ts_us, state_);
    }

    // Optionally save frame + metrics once per second
    if (options_.enable_save && state_.tracking && ts_us - state_.last_saved_us > 1000000ULL) {
        // build JSON metrics with per-quadrant "valid" flag and safer values
        std::ostringstream os;
        os << std::fixed << std::setprecision(2);
        os << "{\"marker_id\":" << state_.marker_id << ",\"ts_us\":" << ts_us << ",\"quadrants\":[";
        for (int i=0;i<4;i++) {
            auto& q = state_.q[i];
            if (i) os << ",";
            if (!q.valid) {
                os << "{\"valid\":false,\"cx\":null,\"cy\":null,\"vx\":null,\"vy\":null,\"ax\":null,\"ay\":null}";
            } else {
                auto& m = q.motion;
                os << "{\"valid\":true,\"cx\":" << m.pos.x << ",\"cy\":" << m.pos.y
                   << ",\"vx\":" << m.vel.x << ",\"vy\":" << m.vel.y
                   << ",\"ax\":" << m.acc.x << ",\"ay\":" << m.acc.y << "}";
            }
        }
        os << "]}";
        std::string json = os.str();

        // Save frame and metrics (background work)
        save_frame_and_metrics(frame, json, ts_us);

        state_.last_saved_us = ts_us;
    }

    // Send UDP metrics at 10Hz independently of saving
    const uint64_t METRIC_PERIOD_US = 100000ULL; // 100ms
    if (options_.enable_metrics && state_.tracking && ts_us - state_.last_metrics_us > METRIC_PERIOD_US) {
        // Reuse latest JSON; if none, build a compact one
        std::ostringstream os;
        os << std::fixed << std::setprecision(2);
        os << "{\"marker_id\":" << state_.marker_id << ",\"ts_us\":" << ts_us << ",\"quadrants\":[";
        for (int i=0;i<4;i++) {
            auto& q = state_.q[i];
            if (i) os << ",";
            if (!q.valid) {
                os << "{\"valid\":false,\"vx\":null,\"vy\":null,\"ax\":null,\"ay\":null}";
            } else {
                auto& m = q.motion;
                os << "{\"valid\":true,\"vx\":" << m.vel.x << ",\"vy\":" << m.vel.y
                   << ",\"ax\":" << m.acc.x << ",\"ay\":" << m.acc.y << "}";
            }
        }
        os << "]}";
        send_metrics_udp(os.str());
        state_.last_metrics_us = ts_us;
    }

    // Write a low-rate live jpg snapshot for MJPEG streaming (default 10 FPS)
    const uint64_t LIVE_PERIOD_US = 100000ULL; // 100ms -> 10FPS
    if (options_.enable_live && ts_us - state_.last_live_us > LIVE_PERIOD_US) {
        try {
            Mat vis;
            if (frame.channels() == 1) cvtColor(frame, vis, COLOR_GRAY2BGR);
            else vis = frame.clone();

            // draw bbox, marker id, quadrant markers, and velocities
            if (state_.tracking) {
                rectangle(vis, state_.marker_bbox, Scalar(0,255,0), 2);
                std::string idtxt = "ID: " + std::to_string(state_.marker_id);
                putText(vis, idtxt, {state_.marker_bbox.x, std::max(0, state_.marker_bbox.y-6)}, FONT_HERSHEY_SIMPLEX, 0.5, Scalar(0,255,0), 1);

                for (int i=0;i<4;i++) {
                    if (state_.q[i].valid) {
                        auto p = state_.q[i].motion.pos;
                        int ix = cv::saturate_cast<int>(p.x);
                        int iy = cv::saturate_cast<int>(p.y);
                        circle(vis, Point(ix, iy), 4, Scalar(0,0,255), -1);

                        // velocity arrow and text
                        float vx = state_.q[i].motion.vel.x;
                        float vy = state_.q[i].motion.vel.y;
                        float scale = 0.05f;
                        Point dst(cv::saturate_cast<int>(ix + vx*scale), cv::saturate_cast<int>(iy + vy*scale));
                        arrowedLine(vis, Point(ix,iy), dst, Scalar(255,0,0), 1, LINE_AA, 0, 0.3);
                        std::ostringstream os; os << std::fixed << std::setprecision(1) << "v=" << vx << "," << vy;
                        putText(vis, os.str(), {ix+6, iy-6}, FONT_HERSHEY_SIMPLEX, 0.4, Scalar(255,255,255), 1);
                    } else {
                        cv::Point2f c;
                        c.x = state_.marker_bbox.x + state_.marker_bbox.width * (i%2 ? 0.75f : 0.25f);
                        c.y = state_.marker_bbox.y + state_.marker_bbox.height * (i<2 ? 0.25f : 0.75f);
                        int ix = cv::saturate_cast<int>(c.x), iy = cv::saturate_cast<int>(c.y);
                        cv::line(vis, {ix-3,iy-3},{ix+3,iy+3}, cv::Scalar(0,128,255), 1);
                        cv::line(vis, {ix+3,iy-3},{ix-3,iy+3}, cv::Scalar(0,128,255), 1);
                    }
                }
            }

            std::vector<int> jpg_params = {cv::IMWRITE_JPEG_QUALITY, 75};
            std::vector<uchar> enc;
            cv::imencode(".jpg", vis, enc, jpg_params);
            // write to /tmp/live.jpg without re-encoding
            {
                std::ofstream f("/tmp/live.jpg", std::ios::binary);
                if (f.good()) f.write(reinterpret_cast<const char*>(enc.data()), static_cast<std::streamsize>(enc.size()));
            }
            // also send via UDP for Flask receiver
            send_jpeg_udp(enc, "127.0.0.1", 5002);
            state_.last_live_us = ts_us;
        } catch (const std::exception& e) {
            std::cerr << "Live snapshot write failed: " << e.what() << std::endl;
        }
    }

    d_prev_ = d_curr_;
    have_prev_ = true;
}

void ArucoTracker::detect_marker(const Mat& frame) {
    std::vector<int> ids;
    std::vector<std::vector<Point2f>> corners;
    aruco::detectMarkers(frame, dict_, corners, ids);

    if (ids.empty()) {
        state_.tracking = false;
        state_.marker_id = -1;
        return;
    }

    state_.marker_bbox = boundingRect(corners[0]);
    state_.tracking = true;
    state_.marker_id = ids[0];

    Mat pts(1, 4, CV_32FC2);
    for (int i = 0; i < 4; i++)
        pts.at<Point2f>(0, i) = quadrant_center(i);

    d_prev_pts_.upload(pts);
    have_prev_ = false;
}

void ArucoTracker::track(const Mat&, uint64_t ts_us) {
    lk_->calc(d_prev_, d_curr_, d_prev_pts_, d_curr_pts_, d_status_);

    Mat h_pts, h_status;
    d_curr_pts_.download(h_pts);
    d_status_.download(h_status);

    for (int i = 0; i < 4; i++) {
        if (!h_status.at<uchar>(0, i)) continue;
        update_motion(state_.q[i].motion,
                      h_pts.at<Point2f>(0, i),
                      ts_us);
        state_.q[i].valid = true;
    }

    d_prev_pts_ = d_curr_pts_.clone();
}

Point2f ArucoTracker::quadrant_center(int i) const {
    auto& b = state_.marker_bbox;
    float cx = b.x + b.width * 0.5f;
    float cy = b.y + b.height * 0.5f;
    float dx = b.width * 0.25f;
    float dy = b.height * 0.25f;

    static int sx[4] = {-1, 1, -1, 1};
    static int sy[4] = {-1, -1, 1, 1};
    return {cx + sx[i]*dx, cy + sy[i]*dy};
}
