#pragma once

#include <common/any_callable.h>

#include <memory>
#include <queue>
#include <thread>

#include "ffmpeg/ffmpeg_decoder.h"
#include "ffmpeg/gif_encoder.h"
#include "ffmpeg/mp4_encoder.h"
#include "yuv_renderer.h"

struct SDL_AudioStream;

class VideoPlayer {
public:
    VideoPlayer(std::shared_ptr<Pathfinder::Device> device, std::shared_ptr<Pathfinder::Queue> queue);
    virtual ~VideoPlayer() = default;

    bool video_info_dirty() const {
        return video_info_changed_;
    }

    void make_video_info_dirty(const bool dirty) {
        video_info_changed_ = dirty;
    }

    int video_width() const {
        return video_width_;
    }

    int video_height() const {
        return video_height_;
    }

    int video_format() const {
        return video_format_;
    }

    bool getMuted() const {
        return isMuted;
    }

    virtual void play(const std::string &playUrl, bool forceSoftwareDecoding) = 0;

    virtual void update(float dt) = 0;

    virtual void render(std::shared_ptr<Pathfinder::Texture> target) = 0;

    virtual void stop() = 0;

    virtual void set_muted(bool muted) = 0;

    virtual std::string capture_jpeg() = 0;

    virtual bool start_mp4_recording() = 0;
    virtual std::string stop_mp4_recording() const = 0;

    virtual bool start_gif_recording() = 0;
    virtual std::string stop_gif_recording() const = 0;

    void force_sw_decoder(bool force);

    // Signals

    std::vector<revector::AnyCallable<void>> connectionLostCallbacks;
    void emit_connection_lost();

    // void gotRecordVol(double vol);
    revector::AnyCallable<void> gotRecordVolume;

    // void onBitrate(long bitrate);
    revector::AnyCallable<void> onBitrateUpdate;

    // void onMutedChanged(bool muted);
    revector::AnyCallable<void> onMutedChanged;

    // void onHasAudio(bool has);
    revector::AnyCallable<void> onHasAudio;

    std::shared_ptr<YuvRenderer> yuvRenderer_;

protected:
    // Play file URL
    std::string url;

    bool should_stop_playing_ = true;

    bool isMuted = false;

    std::mutex mtx;

    std::thread decodeThread;
    std::mutex decodeResMtx; // Resource mutex

    std::thread analysisThread;
    std::mutex analysisResMtx; // Resource mutex

    void update_video_info(int width, int height, int format);

    // bool hasAudio() const;

    bool force_sw_decoder_ = false;

    int video_width_{};
    int video_height_{};
    int video_format_{};

    bool video_info_changed_ = false;
};
