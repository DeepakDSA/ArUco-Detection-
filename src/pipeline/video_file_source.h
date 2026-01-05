#pragma once
#include "frame_source.h"
#include <opencv2/opencv.hpp>
#include <string>

class VideoFileSource : public FrameSource {
public:
    explicit VideoFileSource(const std::string& path);
    bool open() override;
    bool grab(cv::Mat& frame, uint64_t& timestamp_us) override;
    void close() override;

private:
    std::string path_;
    cv::VideoCapture cap_;
};
