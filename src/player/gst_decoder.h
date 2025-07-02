#pragma once

#ifdef AVIATEUR_ENABLE_GSTREAMER

    #include <gst/gl/gl.h>
    #include <gst/gst.h>

    #include <string>

    #include "hb-ot-layout-gsubgpos.hh"

namespace Pathfinder {
class Texture;
}

class GstDecoder {
public:
    GstDecoder() = default;

    ~GstDecoder();

    void init();

    void create_pipeline();

    void play_pipeline(const std::string &uri);

    void stop_pipeline();

    std::shared_ptr<Pathfinder::Texture> pull_texture();

    GMutex sample_mutex;
    GstSample *sample;
    struct timespec sample_decode_end_ts;
    bool pipeline_is_running_ = false;
    bool received_first_frame;
    GstElement *appsink_;
    GstGLContext *context_;
    GstGLDisplay *display_;

private:
    GstElement *pipeline_{};

    bool initialized_ = false;

    struct MySample *prev_sample_;
};

#endif
