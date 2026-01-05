#include "v4l2_source.h"

#include <opencv2/opencv.hpp>
#include <string>
#include <chrono>
#include <thread>
#include <iostream>

V4L2CameraSource::V4L2CameraSource(int width, int height, int fr_num, int fr_den, int io_mode, int queue_buffers, int max_buffers, bool drop, bool sync, bool reuse_buffer)
    : width_(width), height_(height), fr_num_(fr_num), fr_den_(fr_den), io_mode_(io_mode), queue_buffers_(queue_buffers), max_buffers_(max_buffers), drop_(drop), sync_(sync), reuse_buffer_(reuse_buffer) {}

V4L2CameraSource::~V4L2CameraSource() {
    close();
}

bool V4L2CameraSource::open() {
    std::ostringstream ss;
    ss << "v4l2src device=/dev/video0 ";
    if (io_mode_ > 0) ss << "io-mode=" << io_mode_ << " ";
    ss << "! queue max-size-buffers=" << queue_buffers_ << " leaky=2 ! ";
    ss << "video/x-raw,format=GRAY8,width=" << width_ << ",height=" << height_ << ",framerate=" << fr_num_ << "/" << fr_den_ << " ! ";
    ss << "appsink name=sink max-buffers=" << max_buffers_ << " drop=" << (drop_?"true":"false") << " sync=" << (sync_?"true":"false");

    std::string pipe = ss.str();

    pipeline_ = gst_parse_launch(pipe.c_str(), nullptr);

    // allocate scratch buffer if reusing
    if (reuse_buffer_) {
        scratch_ = cv::Mat(height_, width_, CV_8UC1);
        std::cerr << "V4L2CameraSource: reusing host buffer for frames (no per-frame alloc)" << std::endl;
    }
    if (!pipeline_) {
        std::cerr << "GStreamer: failed to create pipeline" << std::endl;
        return false;
    }

    sink_ = gst_bin_get_by_name(GST_BIN(pipeline_), "sink");
    if (!sink_) {
        std::cerr << "GStreamer: failed to find appsink 'sink' in pipeline" << std::endl;
        gst_object_unref(pipeline_);
        pipeline_ = nullptr;
        return false;
    }

    GstStateChangeReturn ret = gst_element_set_state(pipeline_, GST_STATE_PLAYING);
    if (ret == GST_STATE_CHANGE_FAILURE) {
        std::cerr << "GStreamer: failed to set pipeline PLAYING" << std::endl;
        gst_object_unref(sink_); sink_ = nullptr;
        gst_object_unref(pipeline_); pipeline_ = nullptr;
        return false;
    }

    GstState state;
    ret = gst_element_get_state(pipeline_, &state, nullptr, GST_SECOND);
    if (ret == GST_STATE_CHANGE_FAILURE || state != GST_STATE_PLAYING) {
        std::cerr << "GStreamer: pipeline did not reach PLAYING state (ret=" << ret << ", state=" << state << ")" << std::endl;
        gst_object_unref(sink_); sink_ = nullptr;
        gst_object_unref(pipeline_); pipeline_ = nullptr;
        return false;
    }

    std::cerr << "GStreamer: pipeline started (" << pipe << ")" << std::endl;
    return true;
}

bool V4L2CameraSource::grab(cv::Mat& frame, uint64_t& ts_us) {
    GstSample* sample = gst_app_sink_pull_sample(GST_APP_SINK(sink_));
    if (!sample) {
        // Inspect bus for errors/warnings briefly
        if (pipeline_) {
            GstBus* bus = gst_element_get_bus(pipeline_);
            GstMessage* msg = gst_bus_timed_pop_filtered(bus, 0, static_cast<GstMessageType>(GST_MESSAGE_ERROR | GST_MESSAGE_WARNING | GST_MESSAGE_EOS));
            if (msg) {
                if (GST_MESSAGE_TYPE(msg) == GST_MESSAGE_ERROR) {
                    GError* err = nullptr;
                    gst_message_parse_error(msg, &err, nullptr);
                    std::cerr << "GStreamer ERROR: " << (err ? err->message : "unknown") << std::endl;
                    if (err) g_error_free(err);
                } else if (GST_MESSAGE_TYPE(msg) == GST_MESSAGE_WARNING) {
                    GError* warn = nullptr;
                    gst_message_parse_warning(msg, &warn, nullptr);
                    std::cerr << "GStreamer WARNING: " << (warn ? warn->message : "unknown") << std::endl;
                    if (warn) g_error_free(warn);
                } else if (GST_MESSAGE_TYPE(msg) == GST_MESSAGE_EOS) {
                    std::cerr << "GStreamer: EOS received on pipeline" << std::endl;
                }
                gst_message_unref(msg);
            }
            gst_object_unref(bus);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        return false;
    }

    GstBuffer* buffer = gst_sample_get_buffer(sample);
    GstMapInfo map;
    if (!gst_buffer_map(buffer, &map, GST_MAP_READ)) {
        std::cerr << "GStreamer: failed to map buffer" << std::endl;
        gst_sample_unref(sample);
        return false;
    }

    size_t sz = (size_t)height_ * (size_t)width_;
    if (reuse_buffer_) {
        // copy into persistent scratch buffer (avoid repeated allocations)
        memcpy(scratch_.data, map.data, sz);
        frame = scratch_; // header copy only; scratch_ will be overwritten next frame
    } else {
        // safe clone for independent lifetime
        frame = cv::Mat(height_, width_, CV_8UC1, (void*)map.data).clone();
    }

    gst_buffer_unmap(buffer, &map);

    GstClockTime pts = GST_BUFFER_PTS(buffer);
    ts_us = (pts != GST_CLOCK_TIME_NONE) ? pts / 1000 : 0;

    gst_sample_unref(sample);
    return true;
}

void V4L2CameraSource::close() {
    if (pipeline_) {
        gst_element_set_state(pipeline_, GST_STATE_NULL);
        if (sink_) {
            gst_object_unref(sink_);
            sink_ = nullptr;
        }
        gst_object_unref(pipeline_);
        pipeline_ = nullptr;
        std::cerr << "GStreamer: pipeline closed" << std::endl;
    }
}
