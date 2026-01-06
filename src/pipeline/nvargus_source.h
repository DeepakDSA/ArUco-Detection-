#pragma once

#include "frame_source.h"
#include <gst/gst.h>
#include <gst/app/gstappsink.h>
#include <opencv2/opencv.hpp>
#include <string>

class NvArgusSource : public FrameSource {
public:
    NvArgusSource(int width=640, int height=480, int fr_num=110, int fr_den=1,
                  int max_buffers=8, bool drop=true, bool sync=false)
        : width_(width), height_(height), fr_num_(fr_num), fr_den_(fr_den),
          max_buffers_(max_buffers), drop_(drop), sync_(sync) {}
    ~NvArgusSource() override { close(); }

    bool open() override;
    bool grab(cv::Mat& frame, uint64_t& ts_us) override;
    void close() override;

private:
    GstElement* pipeline_ = nullptr;
    GstElement* sink_ = nullptr;

    int width_ = 640;
    int height_ = 480;
    int fr_num_ = 110;
    int fr_den_ = 1;
    int max_buffers_ = 8;
    bool drop_ = true;
    bool sync_ = false;
};
