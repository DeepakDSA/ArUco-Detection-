#pragma once

#include <opencv2/opencv.hpp>
#include <filesystem>
#include <fstream>
#include <string>
#include <iostream>

inline void save_frame_and_metrics(const cv::Mat& frame,
                                   const std::string& json_metrics,
                                   uint64_t ts_us,
                                   const std::string& out_dir = "/data/yash_project/frames")
{
    try {
        std::filesystem::create_directories(out_dir);
        std::string img = out_dir + "/frame_" + std::to_string(ts_us) + ".jpg";
        std::string meta = img + ".json";

        if (!cv::imwrite(img, frame))
            std::cerr << "FrameWriter: failed to write " << img << std::endl;

        std::ofstream(meta) << json_metrics;
    } catch (const std::exception& e) {
        std::cerr << "FrameWriter exception: " << e.what() << std::endl;
    }
}
