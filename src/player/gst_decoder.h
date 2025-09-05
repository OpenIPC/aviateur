#pragma once

#ifdef AVIATEUR_USE_GSTREAMER

    #include <gst/gst.h>

    #include <functional>
    #include <memory>
    #include <string>

class BitrateCalculator {
public:
    BitrateCalculator();

    void feed_bytes(guint64 bytes);

    std::function<void(uint64_t bitrate)> bitrate_cb;

private:
    guint64 total_bytes = 0;
    GstClockTime start_time = 0;
    GMutex mutex;
};

class GstDecoder {
public:
    GstDecoder() = default;

    void init();

    void create_pipeline(const std::string& codec);

    void play_pipeline(const std::string& uri);

    void stop_pipeline();

    std::string decoder_name_;

private:
    GstElement* pipeline_{};

    bool initialized_ = false;

    std::shared_ptr<BitrateCalculator> bitrate_calculator_;
};

#endif
