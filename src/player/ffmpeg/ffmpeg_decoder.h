#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

#include "ffmpeg_include.h"

class ReadFrameException : public std::runtime_error {
public:
    ReadFrameException(const std::string &msg) : runtime_error(msg.c_str()) {}
};

class SendPacketException : public std::runtime_error {
public:
    SendPacketException(const std::string &msg) : runtime_error(msg.c_str()) {}
};

class FfmpegDecoder {
    friend class VideoPlayerFfmpeg;

public:
    FfmpegDecoder() = default;

    ~FfmpegDecoder() {
        CloseInput();

        swrCtx.reset();
        hwFrame.reset();
    }

    bool OpenInput(std::string &inputFile, bool forceSoftwareDecoding);

    bool CloseInput();

    std::shared_ptr<AVFrame> GetNextFrame();

    int GetWidth() const {
        return width;
    }

    int GetHeight() const {
        return height;
    }

    float GetFps() const {
        return videoFps;
    }

    bool HasAudio() const {
        return hasAudioStream;
    }

    bool HasVideo() const {
        return hasVideoStream;
    }

    int ReadAudioBuff(uint8_t *aSample, size_t aSize);

    void ClearAudioBuff();

    int GetAudioSampleRate() const {
        return pAudioCodecCtx->sample_rate;
    }

    int GetAudioChannelCount() const {
        return pAudioCodecCtx->ch_layout.nb_channels;
    }

    AVSampleFormat GetAudioSampleFormat() const {
        return AV_SAMPLE_FMT_S16;
    }

    AVPixelFormat GetVideoFrameFormat() const {
        if (hwDecoderEnabled) {
            return AV_PIX_FMT_NV12;
        }
        return pVideoCodecCtx->pix_fmt;
    }

    int GetAudioFrameSamples() const {
        return pAudioCodecCtx->sample_rate * 2 / 25;
    }

private:
    bool OpenVideo();

    bool OpenAudio();

    void CloseVideo();

    void CloseAudio();

    size_t DecodeAudio(const AVPacket *av_pkt, uint8_t *pOutBuffer, size_t nOutBufferSize);

    bool DecodeVideo(const AVPacket *av_pkt, std::shared_ptr<AVFrame> &pOutFrame);

    void writeAudioBuff(const uint8_t *aSample, size_t aSize);

    /// NALU callback (video/audio)
    std::function<void(const std::shared_ptr<AVPacket> &packet)> gotPktCallback;

    std::function<void(const std::shared_ptr<AVFrame> &frame)> gotVideoFrameCallback;

    bool createHwCtx(AVCodecContext *ctx, enum AVHWDeviceType type);

    void emitBitrateUpdate(uint64_t pBitrate) {
        bitrateUpdateCallback(pBitrate);
    }

    std::chrono::time_point<std::chrono::steady_clock> startTime;

    AVFormatContext *pFormatCtx = nullptr;

    AVCodecContext *pVideoCodecCtx = nullptr;

    AVCodecContext *pAudioCodecCtx = nullptr;

    // ffmpeg 音频样本格式转换
    std::shared_ptr<SwrContext> swrCtx;

    int videoStreamIndex = -1;

    int audioStreamIndex = -1;

    volatile bool sourceIsOpened = false;

    float videoFps = 0;

    double videoBaseTime = 0;

    double audioBaseTime = 0;

    std::mutex _releaseLock;

    bool hasVideoStream{};

    bool hasAudioStream{};

    int width{};

    int height{};

    volatile uint64_t bytesSecond = 0;
    uint64_t bitrate = 0;
    uint64_t lastCountBitrateTime = 0;
    std::function<void(uint64_t bitrate)> bitrateUpdateCallback;

    // Audio buffer
    std::mutex abBuffMtx;
    AVFifo *audioFifoBuffer{};

    // Hardware decoding
    AVHWDeviceType hwDecoderType = AV_HWDEVICE_TYPE_NONE;
    bool hwDecoderEnabled = false;
    std::optional<std::string> hwDecoderName;
    bool forceSwDecoder = false;
    AVPixelFormat hwPixFmt;
    AVBufferRef *hwDeviceCtx = nullptr;
    volatile bool dropCurrentVideoFrame = false;
    std::shared_ptr<AVFrame> hwFrame;
};
