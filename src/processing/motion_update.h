#pragma once
#include "motion_types.h"

inline void update_motion(MotionState& s,
                          const cv::Point2f& new_pos,
                          uint64_t ts_us) {
    // Parameters: simple exponential moving average (EMA) for velocity
    constexpr double VEL_ALPHA = 0.5;        // smoothing factor (0..1)
    constexpr double MAX_ACCEL = 10000.0;    // clamp acceleration (px/s^2)

    if (s.last_ts_us == 0) {
        s.pos = new_pos;
        s.vel = {0.0f, 0.0f};
        s.acc = {0.0f, 0.0f};
        s.last_ts_us = ts_us;
        return;
    }

    double dt = (ts_us - s.last_ts_us) * 1e-6;
    if (dt <= 0) return;

    cv::Point2f v_raw = (new_pos - s.pos) * (1.0 / dt);
    cv::Point2f v_smoothed = static_cast<float>(VEL_ALPHA) * v_raw + static_cast<float>(1.0 - VEL_ALPHA) * s.vel;

    cv::Point2f acc = (v_smoothed - s.vel) * (1.0 / dt);

    auto clamp = [](float x, double M) {
        if (x > M) return static_cast<float>(M);
        if (x < -M) return static_cast<float>(-M);
        return x;
    };

    acc.x = clamp(acc.x, MAX_ACCEL);
    acc.y = clamp(acc.y, MAX_ACCEL);

    s.acc = acc;
    s.vel = v_smoothed;
    s.pos = new_pos;
    s.last_ts_us = ts_us;
}
