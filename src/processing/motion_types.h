#pragma once
#include <opencv2/core.hpp>
#include <cstdint>

struct MotionState {
    cv::Point2f pos{0,0};
    cv::Point2f vel{0,0};
    cv::Point2f acc{0,0};
    uint64_t last_ts_us = 0;
};

struct QuadrantState {
    MotionState motion;
    bool valid = false;
};

struct TrackerState {
    QuadrantState q[4];
    cv::Rect marker_bbox;
    int marker_id = -1;
    uint64_t last_saved_us = 0;
    uint64_t last_live_us = 0; // last time a live snapshot was written
    bool tracking = false;
};
