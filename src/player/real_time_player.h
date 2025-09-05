#pragma once

#include <common/any_callable.h>

#include <memory>
#include <queue>
#include <thread>

#include "ffmpeg_decoder.h"
#include "gif_encoder.h"
#include "mp4_encoder.h"
#include "yuv_renderer.h"

struct SDL_AudioStream;

class RealTimePlayer {
public:
    RealTimePlayer(std::shared_ptr<Pathfinder::Device> device, std::shared_ptr<Pathfinder::Queue> queue);
    ~RealTimePlayer();
    void update(float dt);

    std::shared_ptr<AVFrame> getFrame();

    bool infoDirty() const {
        return infoChanged_;
    }
    void makeInfoDirty(bool dirty) {
        infoChanged_ = dirty;
    }
    int videoWidth() const {
        return videoWidth_;
    }
    int videoHeight() const {
        return videoHeight_;
    }
    int videoFormat() const {
        return videoFormat_;
    }
    bool getMuted() const {
        return isMuted;
    }

    void play(const std::string &playUrl, bool forceSoftwareDecoding);

    void stop();

    void setMuted(bool muted = false);

    std::string captureJpeg();

    // Record MP4
    bool startRecord();
    std::string stopRecord() const;

    // Record GIF
    bool startGifRecord();
    std::string stopGifRecord() const;

    int getVideoWidth() const;

    int getVideoHeight() const;

    void forceSoftwareDecoding(bool force);

    std::shared_ptr<FfmpegDecoder> getDecoder() const;

    // Signals

    std::vector<revector::AnyCallable<void>> connectionLostCallbacks;
    void emitConnectionLost();

    // void gotRecordVol(double vol);
    revector::AnyCallable<void> gotRecordVolume;

    // void onBitrate(long bitrate);
    revector::AnyCallable<void> onBitrateUpdate;

    // void onMutedChanged(bool muted);
    revector::AnyCallable<void> onMutedChanged;

    // void onHasAudio(bool has);
    revector::AnyCallable<void> onHasAudio;

protected:
    std::shared_ptr<FfmpegDecoder> decoder;

    // Play file URL
    std::string url;

    volatile bool playStop = true;

    volatile bool isMuted = false;

    SDL_AudioStream *stream{};

    std::queue<std::shared_ptr<AVFrame>> videoFrameQueue;

    std::mutex mtx;

    std::thread decodeThread;
    std::mutex decodeResMtx; // Resource mutex

    std::thread analysisThread;
    std::mutex analysisResMtx; // Resource mutex

    std::shared_ptr<AVFrame> lastFrame_;

    void onVideoInfoReady(int width, int height, int format);

    bool enableAudio();

    void disableAudio();

    std::shared_ptr<Mp4Encoder> mp4Encoder_;

    std::shared_ptr<GifEncoder> gifEncoder_;

    bool hasAudio() const;

    bool forceSoftwareDecoding_ = false;

public:
    std::shared_ptr<YuvRenderer> yuvRenderer_;
    int videoWidth_{};
    int videoHeight_{};
    int videoFormat_{};
    bool infoChanged_ = false;
};
