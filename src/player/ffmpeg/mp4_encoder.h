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
    // 是否已经初始化
    bool isOpen_ = false;
    // 编码上下文
    std::shared_ptr<AVFormatContext> formatCtx_;
    // 原始视频流时间基
    AVRational originVideoTimeBase_ {};
    // 原始音频流时间基
    AVRational originAudioTimeBase_ {};
    // 已经写入关键帧
    bool writtenKeyFrame_ = false;
};
