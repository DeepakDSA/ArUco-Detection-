#include "video_file_source.h"
#include <chrono>

VideoFileSource::VideoFileSource(const std::string& path) : path_(path) {}

bool VideoFileSource::open() {
    cap_.open(path_);
    return cap_.isOpened();
}

bool VideoFileSource::grab(cv::Mat& frame, uint64_t& timestamp_us) {
    if (!cap_.isOpened()) return false;
    cv::Mat f;
    if (!cap_.read(f)) return false;
    // ensure grayscale if needed
    if (f.channels() == 3) cv::cvtColor(f, frame, cv::COLOR_BGR2GRAY);
    else frame = f;

    auto now = std::chrono::high_resolution_clock::now();
    timestamp_us = std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count();
    return true;
}

void VideoFileSource::close() {
    if (cap_.isOpened()) cap_.release();
}
