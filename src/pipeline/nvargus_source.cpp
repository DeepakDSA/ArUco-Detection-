#include "nvargus_source.h"
#include <iostream>
#include <chrono>
#include <thread>

bool NvArgusSource::open() {
    std::ostringstream ss;
    // nvarguscamerasrc outputs NVMM buffers; convert to GRAY8 via nvvidconv
    ss << "nvarguscamerasrc ! video/x-raw(memory:NVMM), width=" << width_ << ", height=" << height_
       << ", framerate=" << fr_num_ << "/" << fr_den_ << " ! nvvidconv ! video/x-raw, format=GRAY8 ! "
       << "appsink name=sink max-buffers=" << max_buffers_ << " drop=" << (drop_?"true":"false")
       << " sync=" << (sync_?"true":"false");
    std::string pipe = ss.str();

    pipeline_ = gst_parse_launch(pipe.c_str(), nullptr);
    if (!pipeline_) {
        std::cerr << "GStreamer: failed to create nvargus pipeline" << std::endl;
        return false;
    }

    sink_ = gst_bin_get_by_name(GST_BIN(pipeline_), "sink");
    if (!sink_) {
        std::cerr << "GStreamer: failed to find appsink 'sink' in nvargus pipeline" << std::endl;
        gst_object_unref(pipeline_); pipeline_ = nullptr; return false;
    }

    GstStateChangeReturn ret = gst_element_set_state(pipeline_, GST_STATE_PLAYING);
    if (ret == GST_STATE_CHANGE_FAILURE) {
        std::cerr << "GStreamer: nvargus pipeline failed to PLAY" << std::endl;
        gst_object_unref(sink_); sink_ = nullptr;
        gst_object_unref(pipeline_); pipeline_ = nullptr;
        return false;
    }

    GstState state;
    ret = gst_element_get_state(pipeline_, &state, nullptr, GST_SECOND);
    if (ret == GST_STATE_CHANGE_FAILURE || state != GST_STATE_PLAYING) {
        std::cerr << "GStreamer: nvargus did not reach PLAYING" << std::endl;
        gst_object_unref(sink_); sink_ = nullptr;
        gst_object_unref(pipeline_); pipeline_ = nullptr;
        return false;
    }

    std::cerr << "GStreamer: nvargus pipeline started (" << pipe << ")" << std::endl;
    return true;
}

bool NvArgusSource::grab(cv::Mat& frame, uint64_t& ts_us) {
    GstSample* sample = gst_app_sink_pull_sample(GST_APP_SINK(sink_));
    if (!sample) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        return false;
    }
    GstBuffer* buffer = gst_sample_get_buffer(sample);
    GstMapInfo map;
    if (!gst_buffer_map(buffer, &map, GST_MAP_READ)) {
        gst_sample_unref(sample); return false;
    }
    // data is GRAY8
    gint w=width_, h=height_;
    frame = cv::Mat(h, w, CV_8UC1, (void*)map.data).clone();
    gst_buffer_unmap(buffer, &map);

    GstClockTime pts = GST_BUFFER_PTS(buffer);
    ts_us = (pts != GST_CLOCK_TIME_NONE) ? pts / 1000 : 0;

    gst_sample_unref(sample);
    return true;
}

void NvArgusSource::close() {
    if (pipeline_) {
        gst_element_set_state(pipeline_, GST_STATE_NULL);
        if (sink_) { gst_object_unref(sink_); sink_ = nullptr; }
        gst_object_unref(pipeline_); pipeline_ = nullptr;
        std::cerr << "GStreamer: nvargus pipeline closed" << std::endl;
    }
}
