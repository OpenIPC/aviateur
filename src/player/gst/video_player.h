#pragma once

#include <common/any_callable.h>

#include <memory>
#include <queue>
#include <thread>

#include "../video_player.h"
#include "gst_decoder.h"

struct SDL_AudioStream;

class VideoPlayerGst final : public VideoPlayer {
public:
    VideoPlayerGst(std::shared_ptr<Pathfinder::Device> device, std::shared_ptr<Pathfinder::Queue> queue);

    ~VideoPlayerGst() override;

    void play(const std::string &playUrl, bool forceSoftwareDecoding) override;

    void update(float dt) override;

    void render(std::shared_ptr<Pathfinder::Texture> target) override;

    void stop() override;

    void set_muted(bool muted) override;

    std::string capture_jpeg() override {
        return "";
    }

    bool start_mp4_recording() override {
        return false;
    }

    std::string stop_mp4_recording() const override {
        return "";
    }

    bool start_gif_recording() override {
        return false;
    }

    std::string stop_gif_recording() const override {
        return "";
    }

protected:
    std::shared_ptr<GstDecoder> gst_decoder_;
};
