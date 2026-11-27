#pragma once

#include <vecgui/common/any_callable.h>

#include <memory>
#include <queue>
#include <thread>

#include "../video_player.h"
#include "../yuv_renderer.h"
#include "ffmpeg_decoder.h"
#include "gif_encoder.h"
#include "mp4_encoder.h"

struct SDL_AudioStream;

class VideoPlayerFfmpeg final : public VideoPlayer {
public:
    VideoPlayerFfmpeg(const std::shared_ptr<Pathfinder::Device> &device,
                      const std::shared_ptr<Pathfinder::Queue> &queue);

    ~VideoPlayerFfmpeg() override;

    std::shared_ptr<AVFrame> getFrame();

    void play(const std::string &playUrl, bool forceSoftwareDecoding) override;

    void update(float dt) override;

    void render(std::shared_ptr<Pathfinder::Texture> target) override;

    void stop() override;

    void set_muted(bool muted) override;

    std::string capture_jpeg() override;

    bool start_mp4_recording() override;
    std::string stop_mp4_recording() const override;

    void handleRecordingSegmentChange();

    bool start_gif_recording() override;
    std::string stop_gif_recording() const override;

    std::shared_ptr<FfmpegDecoder> getDecoder() const;

protected:
    std::shared_ptr<FfmpegDecoder> decoder;

    std::queue<std::shared_ptr<AVFrame>> videoFrameQueue;

    SDL_AudioStream *stream{};

    std::shared_ptr<AVFrame> lastFrame_;

    bool enableAudio();

    void disableAudio();

    mutable std::shared_ptr<Mp4Encoder> mp4Encoder_;

    mutable std::shared_ptr<GifEncoder> gifEncoder_;

    mutable std::mutex mp4RecordingMtx_;

    bool hasAudio() const;

    std::atomic<bool> has_emitted_ready_ = false;
    std::atomic<bool> restartRecordingAfterReconnect = false;
    std::string current_decoder_name;
};
