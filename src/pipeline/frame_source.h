#pragma once
#include <opencv2/opencv.hpp>
#include <cstdint>

class FrameSource {
public:
    virtual ~FrameSource() = default;
    virtual bool open() = 0;
    virtual bool grab(cv::Mat& frame, uint64_t& ts_us) = 0;
    virtual void close() = 0;
};
