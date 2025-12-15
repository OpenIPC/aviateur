#pragma once

#include <memory>

#include "../video_player.h"
#include "gst_decoder.h"

class VideoPlayerGst final : public VideoPlayer {
public:
    VideoPlayerGst(std::shared_ptr<Pathfinder::Device> device, std::shared_ptr<Pathfinder::Queue> queue);

    ~VideoPlayerGst() override;

    void play(const std::string &playUrl, bool forceSoftwareDecoding) override;

    void update(float dt) override;

    void render(std::shared_ptr<Pathfinder::Texture> target) override;

    void stop() override;

    void set_muted(bool muted) override;

    std::string capture_jpeg() override;

    bool start_mp4_recording() override;

    std::string stop_mp4_recording() const override;

    bool start_gif_recording() override {
        return false;
    }

    std::string stop_gif_recording() const override;

protected:
    std::shared_ptr<GstDecoder> gst_decoder_;

    std::shared_ptr<Pathfinder::Device> device_;

    std::shared_ptr<Pathfinder::Queue> queue_;

    GstSample *prev_sample_ = nullptr;
    std::mutex prev_sample_mutex_;

    std::string record_filename_;
};
