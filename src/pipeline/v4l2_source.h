#pragma once

#include "frame_source.h"

#include <gst/gst.h>
#include <gst/app/gstappsink.h>

class V4L2CameraSource : public FrameSource {
public:
    V4L2CameraSource(int width=640, int height=480,
                     int fr_num=110, int fr_den=1,
                     int io_mode=0, int queue_buffers=8, int max_buffers=8,
                     bool drop=true, bool sync=false,
                     bool reuse_buffer=false);
    ~V4L2CameraSource() override;

    bool open() override;
    bool grab(cv::Mat& frame, uint64_t& ts_us) override;
    void close() override;

private:
    GstElement* pipeline_ = nullptr;
    GstElement* sink_ = nullptr;

    // configuration
    int width_ = 640;
    int height_ = 480;
    int fr_num_ = 110;
    int fr_den_ = 1;
    int io_mode_ = 0; // 0 = not set
    int queue_buffers_ = 8;
    int max_buffers_ = 8;
    bool drop_ = true;
    bool sync_ = false;

    // optional reuse buffer to avoid per-frame allocations
    bool reuse_buffer_ = false;
    cv::Mat scratch_;
};
