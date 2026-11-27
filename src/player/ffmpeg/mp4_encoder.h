#pragma once

#include <memory>
#include <mutex>
#include <string>

#include "ffmpeg_include.h"

/**
 * @brief MP4 encoder.
 *
 * Usage restrictions:
 * 1. Each Mp4Encoder instance is intended for recording a single MP4 file.
 * 2. Recording cannot be restarted after stop() has been called.
 * 3. Create a new Mp4Encoder instance to record another MP4 file.
 * 4. formatCtx_ is managed automatically by std::shared_ptr.
 */
class Mp4Encoder {
public:
    explicit Mp4Encoder(const std::string &saveFilePath);
    ~Mp4Encoder();

    bool start();

    void stop();

    void addTrack(AVStream *stream, const AVCodecContext *codecCtx = nullptr);

    void writePacket(const std::shared_ptr<AVPacket> &pkt, bool isVideo);

    int videoIndex = -1;
    int audioIndex = -1;

    std::string saveFilePath_;

private:
    bool isOpen_ = false;

    bool startedOnce_ = false;

    std::shared_ptr<AVFormatContext> formatCtx_;

    AVRational originVideoTimeBase_{};

    AVRational originAudioTimeBase_{};

    bool writtenKeyFrame_ = false;

    std::mutex writeMtx_;
};
