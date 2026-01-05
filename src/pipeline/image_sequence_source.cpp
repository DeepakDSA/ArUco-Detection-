#include "image_sequence_source.h"
#include <chrono>
#include <filesystem>
#include <algorithm>

namespace fs = std::filesystem;

ImageSequenceSource::ImageSequenceSource(const std::string& dir) : dir_(dir) {}

bool ImageSequenceSource::open() {
    files_.clear(); idx_ = 0; started_ = true;
    try {
        for (auto &p : fs::directory_iterator(dir_)) {
            if (!p.is_regular_file()) continue;
            auto ext = p.path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            if (ext == ".jpg" || ext == ".jpeg" || ext == ".png" || ext == ".bmp" || ext == ".tif") {
                files_.push_back(p.path().string());
            }
        }
        std::sort(files_.begin(), files_.end());
    } catch (...) {
        return false;
    }
    return !files_.empty();
}

bool ImageSequenceSource::grab(cv::Mat& frame, uint64_t& timestamp_us) {
    if (!started_ || idx_ >= files_.size()) return false;
    frame = cv::imread(files_[idx_], cv::IMREAD_GRAYSCALE);
    if (frame.empty()) return false;
    auto now = std::chrono::high_resolution_clock::now();
    timestamp_us = std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count();
    idx_++;
    return true;
}

void ImageSequenceSource::close() {
    files_.clear(); idx_ = 0; started_ = false;
}
