#pragma once

#include <memory>
#include <string>

#include "ffmpeg_include.h"

class Mp4Encoder {
public:
    explicit Mp4Encoder(const std::string &saveFilePath);
    ~Mp4Encoder();

    bool start();

    void stop();

    void addTrack(AVStream *stream);

    void writePacket(const std::shared_ptr<AVPacket> &pkt, bool isVideo);

    int videoIndex = -1;
    int audioIndex = -1;

    std::string saveFilePath_;

private:
    bool isOpen_ = false;

    std::shared_ptr<AVFormatContext> formatCtx_;

    AVRational originVideoTimeBase_{};

    AVRational originAudioTimeBase_{};

    bool writtenKeyFrame_ = false;
};
