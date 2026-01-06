#pragma once

#include <opencv2/opencv.hpp>
#include <opencv2/aruco.hpp>
#include <opencv2/cudaoptflow.hpp>
#include <opencv2/cudaarithm.hpp>

#include "motion_types.h"

class ArucoTracker {
public:
    struct Options {
        bool enable_save = true;   // frame+json snapshots once per second
        bool enable_live = true;   // live JPEG/UDP snapshots (~10 FPS)
        bool enable_csv = true;    // per-frame CSV logging
        bool enable_metrics = true;// UDP metrics output
    };

    ArucoTracker();
    void process(const cv::Mat& frame, uint64_t ts_us);
    void setOptions(const Options& opt) { options_ = opt; }
    bool isTracking() const { return state_.tracking; }
    const TrackerState& state() const { return state_; }

private:
    void detect_marker(const cv::Mat& frame);
    void track(const cv::Mat& frame, uint64_t ts_us);
    cv::Point2f quadrant_center(int idx) const;

private:
    TrackerState state_;

    cv::Ptr<cv::aruco::Dictionary> dict_;
    cv::Ptr<cv::cuda::SparsePyrLKOpticalFlow> lk_;

    cv::cuda::GpuMat d_prev_, d_curr_;
    cv::cuda::GpuMat d_prev_pts_, d_curr_pts_, d_status_;

    bool have_prev_ = false;
    int frame_count_ = 0;

    Options options_{};
};
