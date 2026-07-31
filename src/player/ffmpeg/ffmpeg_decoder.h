#pragma once

#include <atomic>
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

struct SdpReadState {
    const uint8_t *ptr;
    size_t sizeLeft;
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

    float GetFramerate() const {
        return videoFramerate;
    }

    bool HasAudio() const {
        return hasAudioStream;
    }

    bool HasVideo() const {
        return hasVideoStream;
    }

    bool ReadAudioBuff(uint8_t *aSample, size_t aSize);

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

    // 每帧音频样本数估算:按 25fps 视频每帧时长预分配,*2 为安全余量
    int GetAudioFrameSamples() const {
        return pAudioCodecCtx->sample_rate * 2 / 25;
    }

    void ResetHeaderState() {
        hasSps = false;
        hasPps = false;
        isWaitingForKeyframe = true;
    }

private:
    bool OpenVideo();

    bool OpenAudio();

    void CloseVideo();

    void CloseAudio();

    size_t DecodeAudio(const AVPacket *av_pkt, uint8_t *pOutBuffer, size_t nOutBufferSize);

    void writeAudioBuff(const uint8_t *aSample, size_t aSize);

    /// NALU callback (video/audio)
    std::function<void(const std::shared_ptr<AVPacket> &packet)> gotPktCallback;

    std::function<void(const std::shared_ptr<AVFrame> &frame)> gotVideoFrameCallback;

    bool createHwCtx(AVCodecContext *ctx, enum AVHWDeviceType type);

    void emitBitrateUpdate(uint64_t pBitrate) {
        bitrateUpdateCallback(pBitrate);
    }

    std::function<void(int width, int height, AVPixelFormat format)> videoConfigChangedCallback;

    std::chrono::time_point<std::chrono::steady_clock> startTime;

    AVFormatContext *pFormatCtx = nullptr;

    AVCodecContext *pVideoCodecCtx = nullptr;

    AVCodecContext *pAudioCodecCtx = nullptr;

    // ffmpeg 音频样本格式转换
    std::shared_ptr<SwrContext> swrCtx;
    AVSampleFormat lastAudioFrameFormat = AV_SAMPLE_FMT_NONE;

    // 音频解码中间缓冲区(复用,避免每包重新分配)
    std::vector<uint8_t> audioDecodeBuffer;

    int videoStreamIndex = -1;

    int audioStreamIndex = -1;

    std::atomic<bool> sourceIsOpened = false;

    float videoFramerate = 0;

    double videoBaseTime = 0;

    double audioBaseTime = 0;

    std::mutex _releaseLock;
    std::mutex _readMtx;

    bool hasVideoStream{};

    bool hasAudioStream{};

    int width{};

    int height{};

    std::atomic<uint64_t> bytesSecond = 0;
    std::atomic<uint64_t> bitrate = 0;
    std::chrono::steady_clock::time_point lastCountBitrateTime;
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
    std::atomic<bool> dropCurrentVideoFrame = false;
    std::shared_ptr<AVFrame> hwFrame;

    // Custom I/O for in-memory SDP
    AVIOContext *pAvioCtx = nullptr;
    std::vector<uint8_t> sdpBuffer;
    SdpReadState sdpReadState{};

    // NALU State machine for stability
    bool hasSps = false;
    bool hasPps = false;
    bool isWaitingForKeyframe = true;

    /**
     * @brief Parse NAL units in the packet to detect SPS/PPS/IDR
     * @return true if the packet should be fed to the decoder, false if it should be dropped.
     */
    bool parseNalUnits(const AVPacket *pkt);
};
