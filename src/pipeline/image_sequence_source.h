#pragma once
#include "frame_source.h"
#include <opencv2/opencv.hpp>
#include <string>
#include <vector>

class ImageSequenceSource : public FrameSource {
public:
    explicit ImageSequenceSource(const std::string& dir);
    bool open() override;
    bool grab(cv::Mat& frame, uint64_t& timestamp_us) override;
    void close() override;

private:
    std::string dir_;
    std::vector<std::string> files_;
    size_t idx_ = 0;
    bool started_ = false;
};
